import numpy as np
import onnxruntime as ort
import time
import os
import argparse

onnx_model_name='models/resnet50-v1-12.onnx'
model_key = 'resnet50-v1-12'

provider_options_dict = {
    "config_file": 'vitisai_config.json',
    "cache_dir":   'vek385_cache_dir',
    "cache_key":   model_key,
    "ai_analyzer_visualization": True,
    "ai_analyzer_profiling": True,
    "target": 'VAIML'
}

# NPU session
npu_session = ort.InferenceSession(
    onnx_model_name,
    providers=["VitisAIExecutionProvider"],
    provider_options=[provider_options_dict]
) 


num_iter = 4
total_time = 0

for i in range(num_iter):
    # Generate random data
    input_data = {}
    for input in npu_session.get_inputs():
        fixed_shape = [1 if isinstance(dim, str) else dim for dim in input.shape]
        input_data[input.name] = np.random.rand(*fixed_shape).astype(np.float32)

    # Compute NPU results
    try:
        start_time = time.time()
        npu_outputs = npu_session.run(None, input_data)
        end_time = time.time()
        total_time += end_time - start_time
    except Exception as e:
        print(f"Failed to run on NPU: {e}")
        sys.exit(1)

avg_time = total_time / num_iter
print(f"Average inference time over {num_iter} runs: {avg_time*1000} ms")
print("Inference done")



