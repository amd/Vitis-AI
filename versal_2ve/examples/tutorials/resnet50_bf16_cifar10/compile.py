#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

import argparse
import pathlib

import onnxruntime

from resnet_utils import get_directories


def main():
    parser = argparse.ArgumentParser(description="Compile BF16 CNN model")
    parser.add_argument(
        "--model",
        "-i",
        default="models/resnet_trained_for_cifar10.onnx",
        help="Path to the ONNX model",
    )
    args = parser.parse_args()

    onnx_model = args.model
    config_file = "vitisai_config.json"
    _, _, _, cache_dir = get_directories()
    cache_key = pathlib.Path(onnx_model).stem

    provider_options_dict = {
        "config_file": config_file,
        "cache_dir": str(cache_dir),
        "cache_key": cache_key,
        "log_level": "info",
        "target": "VAIML",
    }

    print(f"Creating ORT inference session for model {onnx_model}")
    session_options = onnxruntime.SessionOptions()
    session_options.log_severity_level = 1  # 0=Verbose, 1=Info, 2=Warning, 3=Error, 4=Fatal
    session = onnxruntime.InferenceSession(
        onnx_model,
        sess_options=session_options,
        providers=["VitisAIExecutionProvider"],
        provider_options=[provider_options_dict],
    )

    print("Done")


if __name__ == "__main__":
    main()
