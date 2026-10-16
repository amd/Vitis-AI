#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

#!/usr/bin/env python3
"""
RTMDet ONNX Quantization Script using Quark

Quantizes RTMDet ONNX model to INT8 using AMD Quark with proper calibration.
Uses the same preprocessing as the official mmdetection pipeline:
- BGR format (no RGB conversion)
- Resize with keep_ratio=True
- Pad to 640x640 with bottom-right (114, 114, 114) — matches evaluate_coco.py
- Normalize with mean=[103.53, 116.28, 123.675], std=[57.375, 57.12, 58.395]
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

import cv2
import numpy as np
import onnx
from quark.onnx import ModelQuantizer, QConfig

DEFAULT_MODEL_PATH = "rtmdet_m_8xb32-300e_coco_1x3x640x640_without_smaller_tail.onnx"
DEFAULT_CALIB_DATA_FOLDER = "coco_100_calib"
DEFAULT_OUTPUT_DIR = "./"

# distance2bbox decode-tail elementwise ops that sit AFTER /bbox_head/*.
# Quark 0.12.dev builds keep these FP32 by default; 0.12.post1 quantizes them.
# Excluding them reproduces the "already_done" (decode-tail FP32) QDQ layout.
DECODE_TAIL_NODES = [
    "/Add", "/Add_1", "/Sub", "/Sub_1", "/Concat_4",
    "/Gather_1", "/Gather_3", "/Gather_4", "/Gather_5",
    "/Reshape_9", "/Reshape_10", "/Reshape_11",
    "/Unsqueeze_6", "/Unsqueeze_7", "/Unsqueeze_8", "/Unsqueeze_9",
]


class RTMDetCalibDataReader:
    """Calibration data reader with RTMDet-specific preprocessing."""
    
    def __init__(
        self,
        calib_data_folder: str,
        model_input_name: list[str],
        onnx_model_path: str,
        max_calib_images: int = 0,
    ):
        self.model_input_name = model_input_name
        self.onnx_model_path = onnx_model_path
        self.max_calib_images = max_calib_images
        self.input_shape = self._get_input_shape_from_onnx()
        self.data = self._load_calibration_data(calib_data_folder)
        self.data_iter = None
    
    def _get_input_shape_from_onnx(self):
        """Extract input shape from ONNX model."""
        model = onnx.load(self.onnx_model_path)
        
        input_info = model.graph.input[0]
        shape = []
        for dim in input_info.type.tensor_type.shape.dim:
            if dim.dim_value > 0:
                shape.append(dim.dim_value)
            else:
                shape.append(1)
        
        return shape
    
    def preprocess_image(self, image_path: str, target_size: tuple[int, int] = (640, 640)) -> np.ndarray:
        """
        Preprocess image using RTMDet pipeline from mmdetection.
        
        Matches official mmdetection test_pipeline:
        - Resize with keep_ratio=True
        - Pad to 640x640 with bottom-right (114, 114, 114)
        - Normalize with BGR mean/std
        
        Args:
            image_path: Path to image file
            target_size: Target (height, width) for padding
            
        Returns:
            Preprocessed image array in CHW format [3, H, W] (no batch dimension)
        """
        img = cv2.imread(image_path)
        if img is None:
            raise ValueError(f"Could not load image: {image_path}")
        
        target_h, target_w = target_size
        orig_h, orig_w = img.shape[:2]
        
        scale = min(target_w / orig_w, target_h / orig_h)
        new_w = int(orig_w * scale)
        new_h = int(orig_h * scale)
        
        resized = cv2.resize(img, (new_w, new_h), interpolation=cv2.INTER_LINEAR)

        # Bottom-right pad to match mmdet DetDataPreprocessor / evaluate_coco.py
        padded = np.full((target_h, target_w, 3), 114, dtype=np.uint8)
        padded[:new_h, :new_w] = resized

        img_float = padded.astype(np.float32)
        mean = np.array([103.53, 116.28, 123.675], dtype=np.float32)
        std = np.array([57.375, 57.12, 58.395], dtype=np.float32)
        normalized = (img_float - mean) / std
        
        chw = normalized.transpose(2, 0, 1)
        
        return chw
    
    def _load_calibration_data(self, data_folder: str):
        """Load and preprocess calibration data using RTMDet preprocessing."""
        processed_data = []
        base_path = data_folder
        
        if len(self.model_input_name) == 1:
            file_names = []
            for root, dirs, files in os.walk(base_path):
                for file in files:
                    if file.lower().endswith(('.jpeg', '.jpg', '.png', '.bmp')):
                        file_names.append(os.path.join(root, file))
            file_names = sorted(file_names)
            if self.max_calib_images > 0:
                file_names = file_names[: self.max_calib_images]
            print(f"Found {len(file_names)} calibration images")
        else:
            print(f"Error: Model has {len(self.model_input_name)} inputs, expected 1")
            return []
        
        if len(self.model_input_name) == 1:
            print("Processing calibration images with RTMDet preprocessing...")
            for i, file_path in enumerate(file_names):
                try:
                    preprocessed_img = self.preprocess_image(file_path, target_size=(640, 640))
                    
                    input_feed = {
                        self.model_input_name[0]: preprocessed_img
                    }
                    processed_data.append(input_feed)
                    
                    if (i + 1) % 20 == 0 or (i + 1) == len(file_names):
                        print(f"Processed {i + 1}/{len(file_names)} images")
                        
                except Exception as e:
                    print(f"Error processing {file_path}: {e}")
                    continue
        
        print(f"Successfully loaded {len(processed_data)} calibration samples")
        return processed_data
    
    def get_next(self):
        """Get next calibration data sample."""
        if self.data_iter is None:
            self.data_iter = iter([
                {self.model_input_name[0]: np.expand_dims(data[self.model_input_name[0]], axis=0)} 
                for data in self.data
            ])
            print("Calibration data iterator created")
        return next(self.data_iter, None)


def add_quant_config_metadata(model_path, output_path, quant_config=None, use_calibration=False):
    """
    Add quantization metadata to ONNX model.
    
    Args:
        model_path: Path to input ONNX model
        output_path: Path to save model with metadata
        quant_config: Quantization configuration object
        use_calibration: Whether calibration data was used
    """
    try:
        model = onnx.load(model_path)
        
        metadata_dict = {
            "quant_config_type": "VINT8",
            "preprocessing": "RTMDet_official_mmdetection",
            "preprocessing_details": "BGR_keep_ratio_pad114_meanstd_normalization",
        }
        
        if quant_config:
            if hasattr(quant_config.global_quant_config, 'activation_type'):
                activation_type_str = str(quant_config.global_quant_config.activation_type).split('.')[-1]
                metadata_dict["activation_type"] = activation_type_str
            
            metadata_dict["UseRandomData"] = str(not use_calibration)
            
            if hasattr(quant_config.global_quant_config, 'extra_options') and quant_config.global_quant_config.extra_options:
                for key, value in quant_config.global_quant_config.extra_options.items():
                    metadata_dict[key] = str(value)
                print(f"Added {len(quant_config.global_quant_config.extra_options)} extra_options to metadata")
            
            print("\nConfiguration metadata:")
            for key, value in metadata_dict.items():
                print(f"  {key}: {value}")
        
        metadata_entries = []
        for key, value in metadata_dict.items():
            try:
                entry = onnx.StringStringEntryProto()
                entry.key = key
                entry.value = value
                metadata_entries.append(entry)
            except AttributeError:
                from onnx.onnx_pb2 import StringStringEntryProto
                entry = StringStringEntryProto()
                entry.key = key
                entry.value = value
                metadata_entries.append(entry)
        
        if hasattr(model, 'metadata_props'):
            if model.metadata_props:
                model.metadata_props.extend(metadata_entries)
            else:
                model.metadata_props[:] = metadata_entries
        
        onnx.save(model, output_path)
        print(f"Added {len(metadata_entries)} metadata entries to model")
        
        verify_model = onnx.load(output_path)
        if hasattr(verify_model, 'metadata_props') and verify_model.metadata_props:
            print(f"Verification: {len(verify_model.metadata_props)} metadata entries saved successfully")
        else:
            print("Warning: Metadata verification failed")
            
    except Exception as e:
        print(f"Metadata addition failed: {e}")
        print("Continuing without metadata...")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Quantize RTMDet ONNX model using Quark with proper preprocessing.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    
    parser.add_argument(
        "--input",
        "-i",
        type=str,
        default=DEFAULT_MODEL_PATH,
        help="Path to input float32 ONNX model"
    )
    
    parser.add_argument(
        "--output-dir",
        "-o",
        type=str,
        default=DEFAULT_OUTPUT_DIR,
        help="Directory to save quantized model"
    )
    
    parser.add_argument(
        "--calib-data",
        type=str,
        default=DEFAULT_CALIB_DATA_FOLDER,
        help="Path to calibration data folder"
    )
    
    parser.add_argument(
        "--use-calibration",
        "--calib",
        action="store_true",
        default=True,
        help="Use calibration data for quantization (default: True)"
    )
    
    parser.add_argument(
        "--no-calibration",
        dest="use_calibration",
        action="store_false",
        help="Skip calibration and use random data"
    )
    
    parser.add_argument(
        "--temp-dir",
        type=str,
        default="/tmp/rtmdet_quant_temp",
        help="Temporary directory for Quark intermediate files"
    )

    parser.add_argument(
        "--adaround",
        action="store_true",
        help="Enable AdaRound fast finetune (1000 iters, lr 0.1) per ModelZoo quark_12_int8 recipe",
    )

    parser.add_argument(
        "--head-exclusion",
        choices=("subgraph", "full", "backbone", "acts"),
        default=None,
        help="FP32 keep strategy: 'subgraph' = ModelZoo Exp/Reshape paths only; "
             "'full' = entire bbox_head FP32 (best AdaRound so far); "
             "'backbone' = neck+head FP32, backbone INT8 only; "
             "'acts' = keep Sigmoid/Mul/Exp in tail FP32, quantize all convs.",
    )

    parser.add_argument(
        "--max-calib-images",
        type=int,
        default=0,
        help="Max calibration images (0 = use all in calib-data folder)",
    )

    parser.add_argument(
        "--adaround-iters",
        type=int,
        default=1000,
        help="AdaRound iterations per layer (ModelZoo default: 1000)",
    )

    parser.add_argument(
        "--adaround-seed",
        type=int,
        default=1705472343,
        help="Fixed random seed for AdaRound fast-finetune (Quark default: 1705472343). "
             "Pins torch/numpy/random seeds so AdaRound results are reproducible.",
    )

    parser.add_argument(
        "--output-suffix",
        type=str,
        default="",
        help="Optional suffix before .onnx (e.g. '_v4' -> *_quantized_adaround_v4.onnx)",
    )

    parser.add_argument(
        "--quantize-nodes",
        type=str,
        default="",
        help="Comma-separated node names to REMOVE from the head exclusion set "
             "(i.e. force these to be quantized INT8 even under 'full' head exclusion).",
    )

    parser.add_argument(
        "--exclude-decode-tail",
        action="store_true",
        help="Also keep the distance2bbox decode-tail elementwise ops "
             "(/Add,/Sub,/Gather_1/3/4/5,/Reshape_9-11,/Unsqueeze_6-9,/Concat_4) FP32. "
             "Reproduces the 'already_done' QDQ layout under Quark 0.12.post1 "
             "(ignored for --head-exclusion subgraph).",
    )

    return parser.parse_args()


def main() -> int:
    args = parse_args()
    
    try:
        print("=" * 80)
        print("RTMDet ONNX Quantization with Quark")
        print("=" * 80)
        print(f"Input model: {args.input}")
        print(f"Output directory: {args.output_dir}")
        print(f"Calibration data: {args.calib_data}")
        print(f"Use calibration: {args.use_calibration}")
        print(f"AdaRound: {args.adaround}")
        print("=" * 80)
        
        if not Path(args.input).exists():
            raise FileNotFoundError(f"Input model not found: {args.input}")
        
        if args.use_calibration and not Path(args.calib_data).exists():
            raise FileNotFoundError(f"Calibration data folder not found: {args.calib_data}")
        
        os.makedirs(args.output_dir, exist_ok=True)
        os.makedirs(args.temp_dir, exist_ok=True)
        
        print("\nConfiguring Quark VINT8 quantization...")
        quant_config = QConfig.get_default_config("VINT8")
        quant_config.global_quant_config.extra_options["Int32Bias"] = False
        quant_config.global_quant_config.enable_npu_cnn = True
        quant_config.global_quant_config.extra_options["TmpDir"] = args.temp_dir

        # v1 AdaRound (full head FP32) reached 42.78 mAP; v3 subgraph head regressed to 33.5.
        head_exclusion = args.head_exclusion or "full"

        if args.adaround:
            # Match MLSoln_ModelZoo quark_12_int8 yolox_l full_quant_ar recipe.
            quant_config.global_quant_config.include_fast_ft = True
            quant_config.global_quant_config.extra_options.update({
                "ActivationSymmetric": True,
                "ConvertBNToConv": True,
                "ConvertSigmoidToHardSigmoid": False,
                "ConvertClipToRelu": True,
                "ConvertSplitToSlice": True,
                "SplitLargeKernelPool": False,
                "ReplaceClip6Relu": True,
                "ConvertReduceMeanToGlobalAvgPool": False,
                "RemoveQDQConvClip": False,
                "RemoveQDQConvPRelu": False,
                "RemoveQDQConvRelu": False,
                "RemoveQDQConvLeakyRelu": False,
                "DedicatedQDQPair": True,
                "QuantizeAllOpTypes": True,
                "SimplifyModel": True,
                "CopySharedInit": [],
                "AddQDQPairToWeight": False,
                "FastFinetune": {
                    "OptimAlgorithm": "adaround",
                    "OptimDevice": "cpu",
                    "BatchSize": 1,
                    "NumIterations": args.adaround_iters,
                    "LearningRate": 0.1,
                    "FixedSeed": args.adaround_seed,
                },
            })

        model = onnx.load(args.input)
        model_input_name = [x.name for x in model.graph.input]

        # ModelZoo subgraph exclusions: keep Exp/Reshape→Concat output paths in FP32.
        exclude_subgraphs = {
            "rtmdet_m_8xb32-300e_coco_1x3x640x640_without_smaller_tail": [
                (["/bbox_head/Exp_2", "/bbox_head/Exp_1", "/bbox_head/Exp"],
                 ["/Concat_6"]),
                (["/Reshape_8", "/Reshape_7", "/Reshape_6"],
                 ["/Concat_3"]),
            ]
        }

        output_keep_fp32 = [
            "/Concat_3", "/Concat_6",
            "/Reshape_6", "/Reshape_7", "/Reshape_8",
            "/bbox_head/Exp", "/bbox_head/Exp_1", "/bbox_head/Exp_2",
        ]
        head_nodes_to_exclude: list[str] = []
        if head_exclusion == "acts":
            # Keep only tail activations (SiLU = Sigmoid+Mul, and Exp) FP32; quantize all convs.
            head_nodes_to_exclude = [
                n.name for n in model.graph.node
                if "/bbox_head/" in n.name and n.op_type in ("Sigmoid", "Mul", "Exp")
            ]
            head_nodes_to_exclude.extend(output_keep_fp32)
            head_nodes_to_exclude = sorted(set(head_nodes_to_exclude))
            quant_config.global_quant_config.nodes_to_exclude = head_nodes_to_exclude
        elif head_exclusion == "backbone":
            head_nodes_to_exclude = [
                n.name for n in model.graph.node
                if "/neck/" in n.name or "/bbox_head/" in n.name
            ]
            head_nodes_to_exclude.extend(output_keep_fp32)
            head_nodes_to_exclude = sorted(set(head_nodes_to_exclude))
            quant_config.global_quant_config.nodes_to_exclude = head_nodes_to_exclude
        elif head_exclusion == "full":
            head_nodes_to_exclude = [
                n.name for n in model.graph.node if "/bbox_head/" in n.name
            ]
            head_nodes_to_exclude.extend(output_keep_fp32)
            head_nodes_to_exclude = sorted(set(head_nodes_to_exclude))
            force_quant = [n.strip() for n in args.quantize_nodes.split(",") if n.strip()]
            if force_quant:
                all_names = {n.name for n in model.graph.node}
                for fq in force_quant:
                    if fq not in all_names:
                        print(f"  WARNING: --quantize-nodes '{fq}' not found in graph")
                head_nodes_to_exclude = [n for n in head_nodes_to_exclude if n not in set(force_quant)]
                print(f"\nForcing INT8 quantization of {len(force_quant)} head nodes (removed from exclusion):")
                for fq in force_quant:
                    print(f"    {fq}")
            quant_config.global_quant_config.nodes_to_exclude = head_nodes_to_exclude

        if args.exclude_decode_tail and head_exclusion in ("full", "backbone", "acts"):
            all_names = {n.name for n in model.graph.node}
            tail = [n for n in DECODE_TAIL_NODES if n in all_names]
            missing = [n for n in DECODE_TAIL_NODES if n not in all_names]
            if missing:
                print(f"  WARNING: decode-tail nodes not in graph (skipped): {missing}")
            head_nodes_to_exclude = sorted(set(head_nodes_to_exclude).union(tail))
            quant_config.global_quant_config.nodes_to_exclude = head_nodes_to_exclude
            print(f"\nKeeping {len(tail)} decode-tail nodes FP32 (--exclude-decode-tail)")

        filename = Path(args.input).stem
        if head_exclusion == "subgraph" and filename in exclude_subgraphs:
            print(f"\nApplying subgraph exclusions for {filename}:")
            quant_config.global_quant_config.subgraphs_to_exclude = exclude_subgraphs[filename]
            for i, (start_nodes, end_nodes) in enumerate(exclude_subgraphs[filename], 1):
                print(f"  Subgraph {i}:")
                print(f"    Start nodes: {start_nodes}")
                print(f"    End nodes: {end_nodes}")

        if head_exclusion == "acts":
            print(f"\nExcluding {len(head_nodes_to_exclude)} tail activation/output nodes (Sigmoid/Mul/Exp) — all convs INT8")
        elif head_exclusion == "backbone":
            print(f"\nExcluding {len(head_nodes_to_exclude)} neck/head/output nodes — backbone INT8 only")
        elif head_exclusion == "full":
            print(f"\nExcluding {len(head_nodes_to_exclude)} head/output nodes from INT8 quantization")
        else:
            print("\nHead exclusion: subgraph only (ModelZoo recipe — bbox_head convs stay INT8)")
        
        print("\nQuark configuration:")
        print(f"  Config type: VINT8")
        print(f"  Int32Bias: False")
        print(f"  enable_npu_cnn: True")
        print(f"  TmpDir: {args.temp_dir}")
        print(f"  AdaRound: {args.adaround}")
        if args.adaround:
            print(f"  AdaRound iters/layer: {args.adaround_iters}")
            print(f"  AdaRound fixed seed: {args.adaround_seed}")
        print(f"  Head exclusion: {head_exclusion}")
        print(f"  Nodes excluded: {len(head_nodes_to_exclude)}")
        subgraph_count = len(exclude_subgraphs.get(filename, [])) if head_exclusion == "subgraph" else 0
        print(f"  Subgraphs excluded: {subgraph_count}")
        if args.max_calib_images:
            print(f"  Max calib images: {args.max_calib_images}")
        
        quantizer = ModelQuantizer(quant_config)
        
        print(f"\nModel input name(s): {model_input_name}")
        
        filename = os.path.basename(args.input)
        suffix = args.output_suffix or ""
        if args.adaround:
            output_filename = filename.replace(".onnx", f"_quantized_adaround{suffix}.onnx")
        else:
            output_filename = filename.replace(".onnx", f"_quantized{suffix}.onnx")
        output_path = os.path.join(args.output_dir, output_filename)
        
        if args.use_calibration:
            print("\n" + "=" * 80)
            print("Running quantization WITH calibration...")
            print("=" * 80)
            calib_data_reader = RTMDetCalibDataReader(
                args.calib_data,
                model_input_name,
                args.input,
                max_calib_images=args.max_calib_images,
            )
            quantizer.quantize_model(args.input, output_path, calib_data_reader)
        else:
            print("\n" + "=" * 80)
            print("Running quantization WITHOUT calibration (random data)...")
            print("=" * 80)
            quantizer.quantize_model(args.input, output_path)
        
        print("\n" + "=" * 80)
        print("Adding metadata to quantized model...")
        print("=" * 80)
        add_quant_config_metadata(output_path, output_path, quant_config, args.use_calibration)
        
        print("\n" + "=" * 80)
        print("Quantization completed successfully!")
        print("=" * 80)
        print(f"Quantized model saved to: {output_path}")
        
        model_size_before = Path(args.input).stat().st_size / (1024 * 1024)
        model_size_after = Path(output_path).stat().st_size / (1024 * 1024)
        print(f"Model size: {model_size_before:.2f} MB -> {model_size_after:.2f} MB")
        print(f"Compression ratio: {model_size_before / model_size_after:.2f}x")
        
        return 0
        
    except Exception as exc:
        print(f"\nError during quantization: {exc}")
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
