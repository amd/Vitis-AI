#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

import argparse
import os
import sys

import numpy as np
import onnx

HERE = os.path.dirname(os.path.abspath(__file__))
MODEL = os.path.join(HERE, "yolov8m_optimized_fp32.onnx")
OUT = os.path.join(HERE, "yolov8m_int8.onnx")
TMP = os.path.join(HERE, ".quantize_work")

DECODE_OPS = {"Concat", "Mul", "Add", "Sub", "Div"}


def find_decode_nodes(graph):
    prod = {o: n for n in graph.node for o in n.output}
    seen, names = set(), []
    stack = [o.name for o in graph.output]
    while stack:
        t = stack.pop()
        n = prod.get(t)
        if n is None or n.name in seen or n.op_type not in DECODE_OPS:
            continue
        seen.add(n.name)
        names.append(n.name)
        stack.extend(n.input)
    return sorted(names)


def make_calib_reader(calib_dir, model_path, max_images=None):
    import cv2
    from onnxruntime.quantization.calibrate import CalibrationDataReader

    m = onnx.load(model_path, load_external_data=False)
    iname = m.graph.input[0].name
    dims = [d.dim_value if d.HasField("dim_value") else 1
            for d in m.graph.input[0].type.tensor_type.shape.dim]
    size = dims[-1]

    if size < 32:
        sys.exit("[quantize] model input is %s -- expected a static HxW" % dims)

    files = sorted(os.path.join(r, f)
                   for r, _, fs in os.walk(calib_dir) for f in fs
                   if f.lower().endswith((".jpg", ".jpeg", ".png", ".bmp")))
    if not files:
        sys.exit("[quantize] no images found under %s" % calib_dir)

    # Limit number of calibration images if specified
    if max_images is not None and max_images > 0:
        files = files[:max_images]

    print("[quantize] calibration: %d image(s) @ %dx%d" % (len(files), size, size))

    class Reader(CalibrationDataReader):
        def __init__(self):
            self.it = iter(self._load())

        def _load(self):
            for p in files:
                img = cv2.imread(p)
                if img is None:
                    continue
                img = cv2.resize(img, (size, size))
                img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
                img = img.astype(np.float32) / 255.0
                yield {iname: np.expand_dims(np.transpose(img, (2, 0, 1)), 0)}

        def get_next(self):
            return next(self.it, None)

    return Reader()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--calib-dir", required=True,
                    help="folder of calibration images (recursed); 100 COCO images")
    ap.add_argument("--max-calib-images", type=int, default=0,
                    help="maximum number of calibration images to use (0 = use all images in calib-dir)")
    ap.add_argument("--input-model", type=str, default=None,
                    help="path to input FP32 ONNX model (default: yolov8m_optimized_fp32.onnx)")
    ap.add_argument("--output-model", type=str, default=None,
                    help="path to output INT8 ONNX model (default: yolov8m_int8.onnx)")
    a = ap.parse_args()

    # Use command-line args if provided, otherwise use defaults
    input_model = a.input_model if a.input_model else MODEL
    output_model = a.output_model if a.output_model else OUT
    max_calib_images = a.max_calib_images if a.max_calib_images > 0 else None

    if not os.path.exists(input_model):
        sys.exit("[quantize] input not found: %s -- run export.py then optimize.py "
                 "first." % input_model)
    try:
        from quark.onnx import ModelQuantizer, QConfig
    except ImportError:
        sys.exit("[quantize] amd-quark is not importable by this interpreter -- run "
                 "this with a quark-enabled python (pip install amd-quark), which is "
                 "usually NOT the venv used by export.py/optimize.py.")

    cfg = QConfig.get_default_config("VINT8")
    cfg.global_quant_config.extra_options["Int32Bias"] = False
    cfg.global_quant_config.enable_npu_cnn = True
    os.makedirs(TMP, exist_ok=True)
    cfg.global_quant_config.extra_options["TmpDir"] = TMP

    decode = find_decode_nodes(onnx.load(input_model, load_external_data=False).graph)
    if not decode:
        sys.exit("[quantize] could not identify the box decode -- refusing to "
                 "quantize it silently.")
    cfg.global_quant_config.nodes_to_exclude = decode
    print("[quantize] box decode kept in FLOAT (%d nodes): %s"
          % (len(decode), " ".join(decode)))

    print("[quantize] input model: %s" % input_model)
    print("[quantize] output model: %s" % output_model)
    if max_calib_images:
        print("[quantize] using max %d calibration images" % max_calib_images)

    ModelQuantizer(cfg).quantize_model(
        input_model, output_model,
        make_calib_reader(a.calib_dir, input_model, max_calib_images)
    )
    if not os.path.exists(output_model):
        sys.exit("[quantize] quark produced no model at %s" % output_model)
    print("[quantize] quantized model saved: %s" % output_model)
    return 0


if __name__ == "__main__":
    sys.exit(main())
