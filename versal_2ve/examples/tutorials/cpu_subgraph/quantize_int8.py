#!/usr/bin/env python3
# Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.
# pyright: reportMissingImports=false
"""Quantize YOLOv7 ONNX to VINT8 while keeping decode/NMS in FP32."""

from __future__ import annotations

import argparse
import os
import shutil
import tempfile
import subprocess
from collections import Counter
from copy import deepcopy
from pathlib import Path
from urllib.request import urlretrieve
from zipfile import ZipFile

import cv2
import numpy as np
import onnx
from onnx import TensorProto, checker, helper, shape_inference
from onnxruntime.quantization.calibrate import CalibrationDataReader
from quark.onnx import ModelQuantizer
from quark.onnx.quantization.config import Config, get_default_config


YOLOV7_QUANTIZATION_BOUNDARY_NODES = [
    "/model/model.105/Transpose",
    "/model/model.105/Transpose_1",
    "/model/model.105/Transpose_2",
]

# In this process, the backbone/head portion is quantized to INT8 while the
# later tail remains in FP32. These named nodes mark the handoff point between
# those two sections.

QDQ_OPS = {"QuantizeLinear", "DequantizeLinear"}

CONV_ACT_BLOCKS_TO_QUANTIZE = [
    *range(0, 10),
    11,
    *range(13, 16),
    *range(17, 23),
    24,
    *range(26, 29),
    *range(30, 36),
    37,
    *range(39, 42),
    *range(43, 49),
    50,
    52,
    54,
    *range(56, 62),
    63,
    64,
    66,
    *range(68, 74),
    75,
    *range(77, 80),
    *range(81, 87),
    88,
    *range(90, 93),
    94,
]
CONCAT_NODES_TO_QUANTIZE = [10, 16, 23, 29, 36, 42, 49, 55, 62, 67, 74, 80, 87, 93]
MAXPOOL_NODES_TO_QUANTIZE = [12, 25, 38, 76, 89]
MODEL_51_CV_BLOCKS_TO_QUANTIZE = ["cv1", "cv3", "cv4", "cv5", "cv6", "cv2", "cv7"]


def hardcoded_quantized_node_names() -> set[str]:
    """Return the YOLOv7 node names selected for INT8 quantization."""
    nodes = set()
    for block_index in CONV_ACT_BLOCKS_TO_QUANTIZE:
        nodes.update(
            {
                f"/model.{block_index}/conv/Conv",
                f"/model.{block_index}/act/Sigmoid",
                f"/model.{block_index}/act/Mul",
            }
        )
    for node_index in CONCAT_NODES_TO_QUANTIZE:
        nodes.add(f"/model.{node_index}/Concat")
    for node_index in MAXPOOL_NODES_TO_QUANTIZE:
        nodes.add(f"/model.{node_index}/m/MaxPool")

    for cv_name in MODEL_51_CV_BLOCKS_TO_QUANTIZE:
        nodes.update(
            {
                f"/model.51/{cv_name}/conv/Conv",
                f"/model.51/{cv_name}/act/Sigmoid",
                f"/model.51/{cv_name}/act/Mul",
            }
        )
    nodes.update(
        {
            "/model.51/m.0/MaxPool",
            "/model.51/m.1/MaxPool",
            "/model.51/m.2/MaxPool",
            "/model.51/Concat",
            "/model.51/Concat_1",
            "/model.53/Resize",
            "/model.65/Resize",
            "/model.102/rbr_reparam/Conv",
            "/model.102/act/Sigmoid",
            "/model.102/act/Mul",
            "/model.103/rbr_reparam/Conv",
            "/model.103/act/Sigmoid",
            "/model.103/act/Mul",
            "/model.105/m.0/Conv",
            "/model.105/Reshape",
            "/model.105/Transpose",
            "/model.105/m.1/Conv",
            "/model.105/Reshape_2",
            "/model.105/Transpose_1",
        }
    )
    return nodes


class CocoCalibrationDataReader(CalibrationDataReader):
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
        return int(shape[-1].dim_value or 640)

    @staticmethod
    def _preprocess_image(image: np.ndarray, image_size: int) -> np.ndarray:
        # Calibration samples should follow the same input normalization path as
        # deployment inputs so collected activation ranges stay representative.
        image = cv2.resize(image, (image_size, image_size), interpolation=cv2.INTER_LINEAR)
        image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
        image = image.astype(np.float32) / 255.0
        return np.ascontiguousarray(image.transpose(2, 0, 1), dtype=np.float32)

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
            samples.append(self._preprocess_image(image, self.input_size))

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


def canonical_model_name(name: str) -> str:
    return name.replace("/model/model.", "/model.").replace("model.model.", "model.")


def producer_by_output(model: onnx.ModelProto) -> dict[str, int]:
    producers = {}
    for index, node in enumerate(model.graph.node):
        for output in node.output:
            producers[output] = index
    return producers


def dependency_node_indices(
    model: onnx.ModelProto,
    output_names: list[str],
    stop_at: set[str] | None = None,
) -> set[int]:
    # Starting from a set of outputs, walk backward through the graph to find
    # every node required to produce them. The same helper is used when creating
    # the INT8 backbone/head subgraph and when recovering the FP32 tail.
    stop_at = stop_at or set()
    producers = producer_by_output(model)
    needed_values = list(output_names)
    needed_nodes: set[int] = set()
    seen_values = set(stop_at)

    while needed_values:
        value_name = needed_values.pop()
        if value_name in seen_values:
            continue
        seen_values.add(value_name)
        node_index = producers.get(value_name)
        if node_index is None or node_index in needed_nodes:
            continue
        needed_nodes.add(node_index)
        needed_values.extend(
            input_name for input_name in model.graph.node[node_index].input if input_name
        )

    return needed_nodes


def value_info_by_name(model: onnx.ModelProto) -> dict[str, onnx.ValueInfoProto]:
    values = {}
    for collection in (model.graph.input, model.graph.output, model.graph.value_info):
        for value_info in collection:
            values[value_info.name] = value_info
    return values


def output_names_for_nodes(model: onnx.ModelProto, node_names: list[str]) -> list[str]:
    nodes_by_name = {node.name: node for node in model.graph.node}
    missing_nodes = [node_name for node_name in node_names if node_name not in nodes_by_name]
    if missing_nodes:
        raise ValueError(
            "Could not find YOLOv7 quantization boundary nodes. Missing: "
            + ", ".join(missing_nodes)
        )
    output_names = []
    for node_name in node_names:
        node_outputs = [output_name for output_name in nodes_by_name[node_name].output if output_name]
        if len(node_outputs) != 1:
            raise ValueError(
                f"Expected quantization boundary node {node_name} to have exactly one output, "
                f"found {len(node_outputs)}."
            )
        output_names.append(node_outputs[0])
    return output_names


def infer_value_info(model: onnx.ModelProto, output_name: str) -> onnx.ValueInfoProto:
    try:
        inferred = shape_inference.infer_shapes(model)
        values = value_info_by_name(inferred)
        if output_name in values:
            return deepcopy(values[output_name])
    except Exception as exc:
        print(f"Warning: shape inference failed while resolving {output_name}: {exc}")
    return helper.make_tensor_value_info(output_name, TensorProto.FLOAT, None)


def make_raw_detection_model(input_model: Path, output_model: Path) -> list[str]:
    """Write an intermediate YOLOv7 model that ends at the selected split nodes."""
    model = onnx.load(str(input_model))
    original_model = deepcopy(model)
    raw_outputs = output_names_for_nodes(model, YOLOV7_QUANTIZATION_BOUNDARY_NODES)

    # This intermediate model contains only the backbone/head portion that will
    # become INT8. By quantizing a smaller graph first, the FP32 tail can be
    # left unchanged and restored afterward as part of a mixed-precision model.
    keep_nodes = dependency_node_indices(model, raw_outputs)
    model.graph.ClearField("node")
    model.graph.node.extend(
        deepcopy(node) for index, node in enumerate(original_model.graph.node) if index in keep_nodes
    )
    model.graph.ClearField("output")
    model.graph.output.extend(infer_value_info(original_model, name) for name in raw_outputs)

    checker.check_model(model)
    onnx.save(model, str(output_model))
    return raw_outputs


def quantized_node_names_from_hardcoded_allowlist(candidate_model: Path) -> list[str]:
    """Restrict Quark to the hardcoded YOLOv7 INT8 node allowlist."""
    # This fallback path uses a fixed list of node names that matches the
    # expected YOLOv7 export naming.
    expected_nodes = hardcoded_quantized_node_names()
    candidate = onnx.load(str(candidate_model), load_external_data=False)
    nodes_to_quantize = [
        node.name
        for node in candidate.graph.node
        if canonical_model_name(node.name) in expected_nodes
    ]
    matched_nodes = {canonical_model_name(node_name) for node_name in nodes_to_quantize}
    missing_nodes = sorted(expected_nodes - matched_nodes)
    if missing_nodes:
        raise ValueError(
            "Hardcoded quantization allowlist did not match candidate model nodes. Missing: "
            + ", ".join(missing_nodes)
        )
    return nodes_to_quantize


def append_fp32_tail(
    quantized_model_path: Path,
    fp32_model_path: Path,
    output_model_path: Path,
    raw_outputs: list[str],
) -> None:
    """Append the remaining FP32 YOLOv7 nodes after the quantized outputs."""
    # After the INT8 backbone/head portion is generated, restore the FP32 tail
    # so the final graph becomes a mixed-precision model with the same public
    # outputs as the original export.
    quantized = onnx.load(str(quantized_model_path))
    fp32 = onnx.load(str(fp32_model_path))

    tail_node_indices = dependency_node_indices(
        fp32,
        [output.name for output in fp32.graph.output],
        stop_at=set(raw_outputs),
    )
    produced_by_quantized = {output for node in quantized.graph.node for output in node.output}
    tail_nodes = []
    for index, node in enumerate(fp32.graph.node):
        if index not in tail_node_indices:
            continue
        if all(output in produced_by_quantized for output in node.output):
            continue
        tail_nodes.append(deepcopy(node))

    tail_inputs = {input_name for node in tail_nodes for input_name in node.input if input_name}
    available_values = (
        produced_by_quantized
        | {input_value.name for input_value in quantized.graph.input}
        | {initializer.name for initializer in quantized.graph.initializer}
        | {output for node in tail_nodes for output in node.output}
    )
    needed_initializers = tail_inputs - available_values
    existing_initializers = {initializer.name for initializer in quantized.graph.initializer}
    for initializer in fp32.graph.initializer:
        if initializer.name in needed_initializers and initializer.name not in existing_initializers:
            quantized.graph.initializer.append(deepcopy(initializer))

    quantized.graph.node.extend(tail_nodes)
    quantized.graph.ClearField("output")
    quantized.graph.output.extend(deepcopy(output) for output in fp32.graph.output)

    existing_opsets = {(opset.domain, opset.version) for opset in quantized.opset_import}
    for opset in fp32.opset_import:
        if (opset.domain, opset.version) not in existing_opsets:
            quantized.opset_import.append(deepcopy(opset))

    quantized.producer_name = "yolov7_vint8_body_fp32_nms"
    quantized.producer_version = "1.0"
    checker.check_model(quantized)
    onnx.save(quantized, str(output_model_path))


def build_quantizer(use_calibration: bool, nodes_to_quantize: list[str] | None = None) -> ModelQuantizer:
    # Configure Quark for the mixed-precision path where only the selected
    # section is quantized instead of converting the full graph.
    quant_config = get_default_config("VINT8")
    quant_config.enable_npu_cnn = True
    if nodes_to_quantize:
        quant_config.nodes_to_quantize = nodes_to_quantize
    quant_config.extra_options["Int32Bias"] = False
    quant_config.extra_options["TmpDir"] = str((Path.cwd() / "quark_tmp").resolve())
    quant_config.extra_options["UseRandomData"] = not use_calibration
    return ModelQuantizer(Config(global_quant_config=quant_config))


def run_cmd(cmd: list[str], cwd: Path | None = None) -> None:
    print("+", " ".join(cmd))
    subprocess.run(cmd, cwd=str(cwd) if cwd else None, check=True)


def download_file(url: str, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)

    wget_bin = shutil.which("wget")
    if wget_bin:
        run_cmd([wget_bin, "-O", str(dst), url])
        return

    curl_bin = shutil.which("curl")
    if curl_bin:
        run_cmd([curl_bin, "-L", "-o", str(dst), url])
        return

    print("Neither wget nor curl found. Falling back to Python downloader.")
    urlretrieve(url, str(dst))


def ensure_coco_val2017(calib_dir: Path, val2017_url: str) -> Path:
    resolved_calib_dir = calib_dir.resolve()
    if resolved_calib_dir.is_dir() and any(resolved_calib_dir.rglob("*.jpg")):
        print(f"Using existing calibration directory: {resolved_calib_dir}")
        return resolved_calib_dir

    if resolved_calib_dir.name.lower() != "val2017":
        raise ValueError(
            "Auto-download expects --calib-dir to point to a val2017 directory. "
            f"Received: {resolved_calib_dir}"
        )

    zip_path = resolved_calib_dir.parent / "val2017.zip"
    if not zip_path.is_file():
        print(f"Downloading COCO val2017 from {val2017_url}")
        download_file(val2017_url, zip_path)
    else:
        print(f"Using existing archive: {zip_path}")

    print(f"Extracting {zip_path} into {resolved_calib_dir.parent}")
    with ZipFile(zip_path, "r") as archive:
        archive.extractall(resolved_calib_dir.parent)

    if not resolved_calib_dir.is_dir() or not any(resolved_calib_dir.rglob("*.jpg")):
        raise RuntimeError(
            "COCO val2017 extraction did not produce readable JPG files under "
            f"{resolved_calib_dir}"
        )

    print(f"Prepared calibration directory: {resolved_calib_dir}")
    return resolved_calib_dir


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
    for key in ("inputs", "outputs", "node_count"):
        status = "MATCH" if generated[key] == reference[key] else "DIFF"
        print(f"{key}: {status}")
        if status == "DIFF":
            print(f"  generated: {generated[key]}")
            print(f"  reference: {reference[key]}")

    generated_ops = generated["op_counts"]
    reference_ops = reference["op_counts"]
    status = "MATCH" if generated_ops == reference_ops else "DIFF"
    print(f"op_counts: {status}")
    if status == "DIFF":
        all_ops = sorted(set(generated_ops) | set(reference_ops))
        for op_type in all_ops:
            gen_count = generated_ops.get(op_type, 0)
            ref_count = reference_ops.get(op_type, 0)
            if gen_count != ref_count:
                print(f"  {op_type}: generated={gen_count}, reference={ref_count}")


def parse_args() -> argparse.Namespace:
    repo_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=repo_dir / "yolov7.onnx", help="Input float32 YOLOv7 ONNX model.")
    parser.add_argument("--output", type=Path, default=repo_dir / "yolov7_NMS_MP_Calibrated.onnx", help="Output mixed-precision ONNX model.")
    parser.add_argument("--raw-output", type=Path, default=None, help="Optional path for the intermediate quantized raw-detection model.")
    parser.add_argument("--full-graph", action="store_true", help="Quantize the input model directly instead of cutting and re-appending the FP32 decode/NMS tail.")
    parser.add_argument("--calib-dir", type=Path, default=None, help="Directory containing COCO calibration images.")
    parser.add_argument(
        "--download-val2017",
        action="store_true",
        help=(
            "Download and extract COCO val2017.zip when calibration data is required. "
            "If --calib-dir is omitted, defaults to ./coco/val2017."
        ),
    )
    parser.add_argument(
        "--val2017-url",
        default="http://images.cocodataset.org/zips/val2017.zip",
        help="Source URL for COCO val2017.zip.",
    )
    parser.add_argument("--max-images", type=int, default=100, help="Maximum calibration images to use. Use 0 for all images.")
    parser.add_argument("--no-calib", action="store_true", help="Use Quark random data instead of a calibration dataset.")
    parser.add_argument("--compare-model", type=Path, default=None, help="Optional reference ONNX model to compare graph structure against after quantization.")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    input_model = args.input.resolve()
    output_model = args.output.resolve()
    if not input_model.is_file():
        raise FileNotFoundError(f"Input model not found: {input_model}")

    use_calibration = not args.no_calib
    calib_dir = args.calib_dir.resolve() if args.calib_dir else None
    if use_calibration and calib_dir is None:
        if args.download_val2017:
            calib_dir = (Path(__file__).resolve().parent / "coco" / "val2017").resolve()
        else:
            raise ValueError(
                "Calibration is expected for this model. Pass --calib-dir, "
                "enable --download-val2017, or use --no-calib for smoke testing."
            )

    if use_calibration and calib_dir is not None and args.download_val2017:
        calib_dir = ensure_coco_val2017(calib_dir, args.val2017_url)

    print("=" * 80)
    print("YOLOv7 VINT8 quantization")
    print(f"Input:       {input_model}")
    print(f"Output:      {output_model}")
    print(f"Mode:        {'full graph' if args.full_graph else 'mixed precision, FP32 decode/NMS tail'}")
    print(f"Calibration: {'enabled' if use_calibration else 'disabled (random data)'}")
    if use_calibration and calib_dir is not None:
        print(f"Calib dir:   {calib_dir}")
    print("INT8 nodes:  built-in YOLOv7 allowlist")
    print("=" * 80)

    output_model.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="yolov7_quant_") as temp_dir:
        temp_path = Path(temp_dir)
        quantizer_input = input_model
        final_quantized_output = output_model
        raw_outputs: list[str] = []

        if not args.full_graph:
            quantizer_input = temp_path / "yolov7_raw_detection.onnx"
            raw_outputs = make_raw_detection_model(input_model, quantizer_input)
            final_quantized_output = args.raw_output.resolve() if args.raw_output else temp_path / "yolov7_raw_detection_int8.onnx"
            # In mixed-precision mode, this temporary model is the INT8 section.
            # The downstream FP32 section is appended after quantization.
            print("Raw detection outputs:")
            for raw_output in raw_outputs:
                print(f"  {raw_output}")

        reader = None
        if use_calibration:
            if calib_dir is None:
                raise RuntimeError("Calibration directory is not set.")
            reader = CocoCalibrationDataReader(quantizer_input, calib_dir, args.max_images)

        nodes_to_quantize = quantized_node_names_from_hardcoded_allowlist(quantizer_input)
        print(f"Built-in INT8 node matches: {len(nodes_to_quantize)}")

        quantizer = build_quantizer(use_calibration, nodes_to_quantize)
        if reader is None:
            quantizer.quantize_model(str(quantizer_input), str(final_quantized_output))
        else:
            quantizer.quantize_model(str(quantizer_input), str(final_quantized_output), reader)

        if args.full_graph:
            checker.check_model(onnx.load(str(output_model)))
        else:
            append_fp32_tail(final_quantized_output, input_model, output_model, raw_outputs)

    print(f"Wrote {output_model}")
    if args.compare_model is not None:
        compare_graphs(output_model, args.compare_model.resolve())


if __name__ == "__main__":
    main()
