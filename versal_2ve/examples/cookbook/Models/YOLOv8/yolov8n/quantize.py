#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
AMD Quark ONNX quantization for YOLOv8n (FP32 -> VINT8 / NPU CNN).

Reproduces the quantized output shown in the reference QUARK log:
  - get_default_config("VINT8"), enable_npu_cnn = True
  - PowerOfTwoMethod.MinMSE calibration on 100 COCO images
  - Symmetric INT8 activations + INT8 weights + INT8 bias (Int32Bias=False)
  - QuantizeAllOpTypes=True (shared QDQ pairs; DedicatedQDQPair not set)
  - The detection tail (/model.22 DFL/decode block) left in float via
    subgraphs_to_exclude, producing the "*_tail_non_quant.onnx" model.

The resulting op-type / precision tables match the log:
  Conv     INT8 (act/weight/bias)
  Sigmoid  INT8 (act)      Mul     INT8 (act)
  Slice    INT8 (act)      Add     INT8 (act)
  Concat   INT8 (act)      MaxPool INT8 (act)
  Resize   INT8 (act)      Reshape INT8 (act)

Usage:
  python quantize.py \
      --input        yolov8n.onnx \
      --output       yolov8n_int8_fp32.onnx \
      --calib-dir    /path/to/coco/calib_images \
      --max-images   100
"""

import os
import glob
import argparse

import cv2
import numpy as np
import onnxruntime
from onnxruntime.quantization import CalibrationDataReader

# ---- AMD Quark (quark.onnx) imports -----------------------------------------
from quark.onnx import ModelQuantizer
from quark.onnx.quantization.config.config import Config
from quark.onnx.quantization.config.custom_config import get_default_config


# =============================================================================
# Calibration data reader  (COCO-style preprocessing, letterbox 640x640)
# =============================================================================
class CalibDataReader(CalibrationDataReader):
    """
    Feeds preprocessed COCO images to the calibrator.

    Preprocessing matches the standard YOLOv8 input pipeline:
        letterbox resize -> 640x640 (pad value 114, aspect ratio preserved)
        BGR -> RGB
        HWC -> CHW
        uint8 [0,255] -> float32 [0.0, 1.0]
        add batch dim  -> NCHW  [1, 3, 640, 640]
    """

    def __init__(self, model_path, calib_dir, input_name,
                 num_samples=100, input_size=640):
        self.input_name = input_name
        self.input_size = input_size

        # Discover the input tensor name from the model if not provided.
        if self.input_name is None:
            sess = onnxruntime.InferenceSession(
                model_path, providers=["CPUExecutionProvider"])
            self.input_name = sess.get_inputs()[0].name

        print(os.path.splitext(os.path.basename(model_path))[0])
        print(os.path.basename(model_path))
        print("-------------------  Calibrating the model----------------- ")

        # Gather image paths.
        exts = ("*.jpg", "*.jpeg", "*.png", "*.bmp")
        files = []
        for e in exts:
            files.extend(glob.glob(os.path.join(calib_dir, e)))
            files.extend(glob.glob(os.path.join(calib_dir, "**", e),
                                    recursive=True))
        files = sorted(set(files))[:num_samples]
        print(f"Found {len(files)} images")

        print("Processing images with COCO preprocessing...")
        self.samples = []
        for idx, fp in enumerate(files, 1):
            arr = self._preprocess(fp)
            if arr is not None:
                self.samples.append({self.input_name: arr})
            if idx % 25 == 0 or idx == len(files):
                print(f"Processed {idx}/{len(files)} images")

        print(f"Successfully loaded {len(self.samples)} calibration samples")
        self._iter = iter(self.samples)

    # ---- YOLOv8 letterbox preprocessing -------------------------------------
    def _preprocess(self, image_path):
        img = cv2.imread(image_path)
        if img is None:
            return None

        h0, w0 = img.shape[:2]
        s = self.input_size
        r = min(s / h0, s / w0)                       # scale (no upscaling cap not applied here)
        nw, nh = int(round(w0 * r)), int(round(h0 * r))

        resized = cv2.resize(img, (nw, nh), interpolation=cv2.INTER_LINEAR)

        # Center pad to a square canvas of 114 (standard YOLO padding).
        canvas = np.full((s, s, 3), 114, dtype=np.uint8)
        top = (s - nh) // 2
        left = (s - nw) // 2
        canvas[top:top + nh, left:left + nw] = resized

        canvas = cv2.cvtColor(canvas, cv2.COLOR_BGR2RGB)   # BGR -> RGB
        canvas = canvas.transpose(2, 0, 1)                 # HWC -> CHW
        canvas = np.ascontiguousarray(canvas, dtype=np.float32) / 255.0
        canvas = np.expand_dims(canvas, axis=0)            # NCHW [1,3,640,640]
        return canvas

    def get_next(self):
        return next(self._iter, None)

    def rewind(self):
        self._iter = iter(self.samples)


# =============================================================================
# Quantization
# =============================================================================
def build_quant_config():
    """
    VINT8 / NPU-CNN config matching the reference log.
    """
    # Detection tail to keep in float. The subgraph is bounded by the
    # /model.22 Concat inputs (start nodes) and the final decode Concat
    # (end node), so the DFL / box-decode block is excluded from quantization.
    subgraphs_to_exclude = [
        (["/model.22/Concat_1", "/model.22/Concat"], ["/model.22/Concat_3"])
    ]

    quant_config = get_default_config("VINT8")
    quant_config.enable_npu_cnn = True
    quant_config.subgraphs_to_exclude = subgraphs_to_exclude
    quant_config.extra_options["Int32Bias"] = False
    quant_config.extra_options["QuantizeAllOpTypes"] = True

    return quant_config


def main():
    ap = argparse.ArgumentParser(
        description="Quantize YOLOv8n FP32 ONNX to VINT8 with AMD Quark.")
    ap.add_argument("--input", default="yolov8n.onnx",
                    help="Path to the FP32 yolov8n.onnx model.")
    ap.add_argument("--output", default="yolov8n_int8_fp32.onnx",
                    help="Path to write the quantized ONNX model.")
    ap.add_argument("--calib-dir", required=True,
                    help="Directory of COCO calibration images.")
    ap.add_argument("--max-images", type=int, default=100,
                    help="Number of calibration images to use.")
    ap.add_argument("--input-name", default=None,
                    help="Model input tensor name (auto-detected if omitted).")
    args = ap.parse_args()

    if not os.path.isfile(args.input):
        raise FileNotFoundError(f"Input model not found: {args.input}")

    # 1) Calibration data reader.
    calib_reader = CalibDataReader(
        model_path=args.input,
        calib_dir=getattr(args, 'calib_dir'),
        input_name=getattr(args, 'input_name'),
        num_samples=getattr(args, 'max_images'),
    )

    # 2) Quantization configuration (VINT8, NPU CNN).
    quant_config = build_quant_config()
    config = Config(global_quant_config=quant_config)

    # 3) Quantize.
    quantizer = ModelQuantizer(config)
    quantizer.quantize_model(
        model_input=args.input,
        model_output=args.output,
        calibration_data_reader=calib_reader,
    )

    print(f"\nQuantized model written to: {args.output}")
if __name__ == "__main__":
    main()
