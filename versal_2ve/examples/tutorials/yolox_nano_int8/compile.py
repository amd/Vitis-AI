#Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

import argparse
import onnxruntime


def main():
    parser = argparse.ArgumentParser(
        description="Compile ONNX model for VEK385 using VitisAI Execution Provider"
    )
    parser.add_argument(
        "--model", "-m",
        default="onnx_model/yolox_nano_onnx_pt_regular_conv.onnx",
        help="Path to input ONNX model (default: onnx_model/yolox_nano_onnx_pt_regular_conv.onnx)"
    )
    parser.add_argument(
        "--cache-dir",
        default="./",
        help="Directory for compilation cache (default: ./)"
    )
    parser.add_argument(
        "--cache-key",
        default="yolox_nano_onnx_pt_regular_conv",
        help="Cache key for compiled model (default: yolox_nano_onnx_pt_regular_conv)"
    )
    parser.add_argument(
        "--config", "-c",
        default="vitisai_config.json",
        help="Path to VitisAI config file (default: vitisai_config.json)"
    )
    args = parser.parse_args()

    provider_options_dict = {
        "config_file": args.config,
        "cache_dir": args.cache_dir,
        "cache_key": args.cache_key,
        "ai_analyzer_visualization": True,
        "ai_analyzer_profiling": True,
        "log_level": "info",
        "target": "VAIML"
    }

    print(f"Creating ORT inference session for: {args.model}")
    print(f"  config_file: {args.config}")
    print(f"  cache_dir: {args.cache_dir}")
    print(f"  cache_key: {args.cache_key}")

    session = onnxruntime.InferenceSession(
        args.model,
        providers=["VitisAIExecutionProvider"],
        provider_options=[provider_options_dict]
    )

    print("Compilation complete!")


if __name__ == "__main__":
    main()
