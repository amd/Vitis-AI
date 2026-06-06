#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

import onnxruntime

provider_options_dict = {
        "config_file": 'vitisai_config.json',
        "cache_dir":   './',
        "cache_key":   'resnet50-v1-12_quantized',
        "log_level":   'info',
        "target": "VAIML"
}

session = onnxruntime.InferenceSession(
        'models/resnet50-v1-12_quantized.onnx',
        providers=["VitisAIExecutionProvider"],
        provider_options=[provider_options_dict]
)
