#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

import os
import json
import argparse
import onnxruntime

def register_quark_custom_ops():
    from onnxruntime_custom_ops import (
        register_dynamic_custom_ops_to_onnxruntime,
        vaiml_custom_op_schema,
    )
    ops = [
        vaiml_custom_op_schema(
            domain="com.amd.quark",
            name="BFPQuantizeDequantize",
            nb_inputs=1,
            nb_outputs=1,
        )
    ]
    register_dynamic_custom_ops_to_onnxruntime(ops)

def _split_domain_name(op_key):
    # JSON key is the fully qualified custom op name, e.g. "com.amd.quark:TestDummyOp"
    # (preferred ':' separator) or "com.amd.quark.TestDummyOp" (legacy '.').
    if ":" in op_key:
        return op_key.rsplit(":", 1)
    if "." in op_key:
        return op_key.rsplit(".", 1)
    return "", op_key

def register_custom_ops(custom_ops_config_path):
    from onnxruntime_custom_ops import (
        register_dynamic_custom_ops_to_onnxruntime,
        vaiml_custom_op_schema,
    )

    with open(custom_ops_config_path, "r") as f:
        custom_ops = json.load(f)

    ops = []
    for op_key, op_info in custom_ops.items():
        op_domain, op_name = _split_domain_name(op_key)
        ops.append(
            vaiml_custom_op_schema(
                domain=op_domain,
                name=op_name,
                nb_inputs=op_info["inputs"],
                nb_outputs=op_info["outputs"],
            )
        )
        print(
            f"INFO: Registered custom op {op_domain}:{op_name} "
            f"(inputs={op_info['inputs']}, outputs={op_info['outputs']})"
        )
    register_dynamic_custom_ops_to_onnxruntime(ops)
    print(f"INFO: Registered {len(ops)} custom ops from {custom_ops_config_path}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Compile ONNX model with VitisAI Execution Provider')
    parser.add_argument('--onnx_model_path', type=str, required=True,
                        help='Path to the ONNX model file')
    parser.add_argument('--config_file', type=str, default='vitisai_config.json',
                        help='Path to the VitisAI config file (default: vitisai_config.json)')
    parser.add_argument('--cache_dir', type=str, default='./',
                        help='Cache directory (default: ./)')
    parser.add_argument('--cache_key', type=str, default=None,
                        help='Cache key (default: ONNX filename without path and .onnx extension)')
    parser.add_argument('--register_quark_custom_ops', action='store_true',
                        help='Register Quark BFPQuantizeDequantize custom op (needed for MX6 models)')
    parser.add_argument('--custom_ops_config', type=str, default=None,
                        help='JSON file describing custom ops to register. '
                             'Required for models containing custom ops such as '
                             'com.amd.quark:BFPQuantizeDequantize / com.amd.quark:TestDummyOp.')
    parser.add_argument('--enable-ai-analyzer', action='store_true',
                        help='Enable AI Analyzer profiling and visualization')

    args = parser.parse_args()

    if args.register_quark_custom_ops:
        register_quark_custom_ops()

    if args.custom_ops_config:
        if not os.path.exists(args.custom_ops_config):
            raise FileNotFoundError(
                f"custom_ops_config '{args.custom_ops_config}' not found. "
                f"A custom ops config was explicitly requested but is missing; "
                f"compilation would fail or silently drop custom ops. "
                f"Fix the path or omit --custom_ops_config.")
        register_custom_ops(args.custom_ops_config)

    if args.cache_key is None:
        args.cache_key = os.path.splitext(os.path.basename(args.onnx_model_path))[0]

    provider_options_dict = {
            "config_file": args.config_file,
            "cache_dir":   args.cache_dir,
            "cache_key":   args.cache_key,
            "log_level":   'info',
            "ai_analyzer_profiling": args.enable_ai_analyzer,
            "ai_analyzer_visualization": args.enable_ai_analyzer,
            "target": "VAIML"
    }

    session = onnxruntime.InferenceSession(
            args.onnx_model_path,
            providers=["VitisAIExecutionProvider"],
            provider_options=[provider_options_dict]
    )
