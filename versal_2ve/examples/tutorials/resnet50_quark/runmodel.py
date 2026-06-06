#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

import numpy as np
import onnxruntime as ort
import os

onnx_model_vint8 ='models/resnet50-v1-12_quantized.onnx'

provider_options_dict = {
    "config_file": 'vitisai_config.json',
    "cache_dir":   './',
    "cache_key":   'resnet50-v1-12_quantized',
    "log_level":   'info',
        "target": "VAIML"
}

# NPU session
npu_session = ort.InferenceSession(
    onnx_model_vint8,
    providers=["VitisAIExecutionProvider"],
    provider_options=[provider_options_dict]
)

input_folder="input"
output_folder="output_vek385"
files = sorted([f for f in os.listdir(input_folder) if f.endswith(".npy")])
input_name = npu_session.get_inputs()[0].name
for i,f in enumerate(files):
    fp = os.path.join(input_folder, f)
    image = np.load(fp)
    outputs = npu_session.run(None, {input_name:image})
    # Create outpu directory if it doesn't exist
    os.makedirs(output_folder, exist_ok=True)
    for idx, out in enumerate(outputs):
        np.save(f"{output_folder}/output_{i}_{idx}.npy", out)
print("Inference done")
