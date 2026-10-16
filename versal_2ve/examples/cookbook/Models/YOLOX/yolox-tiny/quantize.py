#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

#!/usr/bin/env python3
# pyright: reportMissingImports=false
"""Quantize YOLOX ONNX models to VINT8 with AMD Quark and COCO calibration images."""

from __future__ import annotations

import argparse
import os
from collections import Counter
from pathlib import Path

import cv2
import numpy as np
import onnx
from onnxruntime.quantization.calibrate import CalibrationDataReader
from quark.onnx import ModelQuantizer
from quark.onnx.quantization.config import Config, get_default_config


class YoloXCalibrationDataReader(CalibrationDataReader):
    def __init__(self, model_path: Path, images_dir: Path, max_images: int) -> None:
        self.input_name = self._get_input_name(model_path)
        self.input_size = self._get_input_size(model_path)
        self.samples = self._load_samples(images_dir, max_images)
        self._iterator = None

    @staticmethod
    def _get_input_name(model_path: Path) -> str:
        model = onnx.load(str(model_path))
        return model.graph.input[0].name

    @staticmethod
    def _get_input_size(model_path: Path) -> int:
        model = onnx.load(str(model_path))
        shape = model.graph.input[0].type.tensor_type.shape.dim
        return int(shape[-1].dim_value or 416)

    @staticmethod
    def _preprocess(image: np.ndarray, image_size: int) -> np.ndarray:
        padded = np.full((image_size, image_size, 3), 114, dtype=np.uint8)
        scale = min(image_size / image.shape[0], image_size / image.shape[1])
        resized = cv2.resize(
            image,
            (int(image.shape[1] * scale), int(image.shape[0] * scale)),
            interpolation=cv2.INTER_LINEAR,
        )
        padded[: resized.shape[0], : resized.shape[1]] = resized
        return np.ascontiguousarray(padded.transpose(2, 0, 1), dtype=np.float32)

    def _load_samples(self, images_dir: Path, max_images: int) -> list[np.ndarray]:
        image_paths = sorted(
            path
            for pattern in ("*.jpg", "*.jpeg", "*.png", "*.bmp")
            for path in images_dir.rglob(pattern)
        )
        if max_images > 0:
            image_paths = image_paths[:max_images]
        if not image_paths:
            raise FileNotFoundError(f"No calibration images found under {images_dir}")

        samples = []
        for image_path in image_paths:
            image = cv2.imread(str(image_path))
            if image is None:
                print(f"Skipping unreadable image: {image_path}")
                continue
            samples.append(self._preprocess(image, self.input_size))

        if not samples:
            raise ValueError(f"No readable calibration images found under {images_dir}")
        print(f"Loaded {len(samples)} calibration images from {images_dir}")
        return samples

    def get_next(self) -> dict[str, np.ndarray] | None:
        if self._iterator is None:
            self._iterator = iter(
                [{self.input_name: np.expand_dims(sample, axis=0)} for sample in self.samples]
            )
        return next(self._iterator, None)

    def rewind(self) -> None:
        self._iterator = None


def build_quantizer(use_calibration: bool) -> ModelQuantizer:
    quant_config = get_default_config("VINT8")
    quant_config.enable_npu_cnn = True
    quant_config.extra_options["Int32Bias"] = False
    quant_config.extra_options["TmpDir"] = f"$PWD"
    # Reference YOLOX quantization logs keep this option true even with a reader.
    quant_config.extra_options["UseRandomData"] = True
    return ModelQuantizer(Config(global_quant_config=quant_config))


def graph_summary(model_path: Path) -> dict[str, object]:
    model = onnx.load(str(model_path), load_external_data=False)
    return {
        "inputs": [
            (value.name, [dim.dim_value or dim.dim_param for dim in value.type.tensor_type.shape.dim])
            for value in model.graph.input
        ],
        "outputs": [
            (value.name, [dim.dim_value or dim.dim_param for dim in value.type.tensor_type.shape.dim])
            for value in model.graph.output
        ],
        "node_count": len(model.graph.node),
        "op_counts": dict(sorted(Counter(node.op_type for node in model.graph.node).items())),
    }


def compare_graphs(generated_model: Path, reference_model: Path) -> None:
    generated = graph_summary(generated_model)
    reference = graph_summary(reference_model)
    print("\nGraph comparison against reference INT8 model")
    print("=" * 80)
    for key in ("inputs", "outputs", "node_count", "op_counts"):
        status = "MATCH" if generated[key] == reference[key] else "DIFF"
        print(f"{key}: {status}")
        if status == "DIFF":
            print(f"  generated: {generated[key]}")
            print(f"  reference: {reference[key]}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True, help="Input float32 ONNX model.")
    parser.add_argument("--output", type=Path, required=True, help="Output INT8 ONNX model.")
    parser.add_argument("--calib-dir", type=Path, default=None, help="Directory containing COCO calibration images.")
    parser.add_argument("--max-images", type=int, default=100, help="Maximum calibration images to use. Use 0 for all images.")
    parser.add_argument("--no-calib", action="store_true", help="Use Quark random data instead of calibration images.")
    parser.add_argument("--compare-model", type=Path, default=None, help="Optional reference INT8 ONNX to compare graph structure against.")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    input_model = args.input.resolve()
    output_model = args.output.resolve()
    if not input_model.is_file():
        raise FileNotFoundError(f"Input model not found: {input_model}")

    use_calibration = not args.no_calib
    if use_calibration and args.calib_dir is None:
        raise ValueError("Calibration is expected for INT8 YOLOX models. Pass --calib-dir or --no-calib.")

    reader = None
    if use_calibration:
        reader = YoloXCalibrationDataReader(input_model, args.calib_dir.resolve(), args.max_images)

    output_model.parent.mkdir(parents=True, exist_ok=True)
    quantizer = build_quantizer(use_calibration)
    if reader is None:
        quantizer.quantize_model(str(input_model), str(output_model))
    else:
        quantizer.quantize_model(str(input_model), str(output_model), reader)

    print(f"Wrote {output_model}")
    if args.compare_model is not None:
        compare_graphs(output_model, args.compare_model.resolve())


if __name__ == "__main__":
    main()
