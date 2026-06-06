import numpy as np
import onnxruntime as ort
import time
import os

onnx_model_name='models/resnet50-v1-12.onnx'
model_key = 'resnet50-v1-12'

provider_options_dict_quant = {
    "config_file": 'vitisai_config.json',
    "cache_dir":   'vek385_cache_dir',
    "cache_key":   model_key,
    "target": 'VAIML'
}

# NPU session
session = ort.InferenceSession(
    onnx_model_name,
    providers=["VitisAIExecutionProvider"],
    provider_options=[provider_options_dict_quant]
)

input_shape = session.get_inputs()[0].shape
input_shape = tuple(1 if isinstance(dim, str) else dim for dim in input_shape)
input_data = np.random.rand(*input_shape).astype(np.float32)
outputs = session.run(None, {session.get_inputs()[0].name: input_data})

input_data.tofile("input0.bin")
outputs=outputs[0]
outputs.tofile("output0_py.bin")
print('Saved to raw file output0_py.bin')