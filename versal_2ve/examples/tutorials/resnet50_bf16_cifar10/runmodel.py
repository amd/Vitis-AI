#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

import sys

import numpy as np
import onnxruntime as ort

from resnet_utils import get_directories

_, models_dir, _, cache_dir = get_directories()
onnx_model = str(models_dir / "resnet_trained_for_cifar10.onnx")

provider_options_dict = {
    "config_file": "vitisai_config.json",
    "cache_dir": str(cache_dir),
    "cache_key": "resnet_trained_for_cifar10",
    "log_level": "info",
    "target": "VAIML",
}

print(f"Creating ORT inference session for model {onnx_model}")

# CPU session to compute reference values
cpu_session = ort.InferenceSession(onnx_model)
# NPU session
npu_session = ort.InferenceSession(
    onnx_model,
    providers=["VitisAIExecutionProvider"],
    provider_options=[provider_options_dict],
)

num_iter = 4
print(f"Running {num_iter} inferences, comparing CPU and NPU outputs")
for i in range(num_iter):
    # Generate random data
    input_data = {}
    for model_input in npu_session.get_inputs():
        fixed_shape = [1 if isinstance(dim, str) else dim for dim in model_input.shape]
        input_data[model_input.name] = np.random.rand(*fixed_shape).astype(np.float32)

    # Compute CPU results (reference values)
    cpu_outputs = cpu_session.run(None, input_data)
    # Compute NPU results
    try:
        npu_outputs = npu_session.run(None, input_data)
    except Exception as e:
        print(f"Failed to run on NPU: {e}")
        sys.exit(1)

    # Compare CPU and NPU results
    max_diff = np.max(np.abs(cpu_outputs[0] - npu_outputs[0]))
    rmse = np.sqrt(np.mean((cpu_outputs[0] - npu_outputs[0]) ** 2))
    print(
        f"Iteration {i+1:3d}: Max absolute difference = {max_diff:.6f}, "
        f"Root mean squared error = {rmse:.6f}"
    )

print("Inference Done!")
