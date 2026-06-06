#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

import numpy as np
import onnxruntime as ort
import os
import cv2
from torchvision import transforms

onnx_model='models/resnet50-v1-12.onnx'
onnx_model_vint8 ='models/resnet50-v1-12_quantized.onnx'

# CPU session
cpu_session = ort.InferenceSession(
    onnx_model,
    providers=["CPUExecutionProvider"]
)

# CPU session with VINT8 quantized model
cpu_session_vint8 = ort.InferenceSession(
    onnx_model_vint8,
    providers=["CPUExecutionProvider"]
)

# You can define your preprocess method
def preprocess_image(image_path):
    transform = transforms.Compose([
        transforms.ToPILImage(),
        transforms.Resize((224, 224)),
        transforms.ToTensor(),
        transforms.Normalize(mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225])
    ])

    image = cv2.imread(image_path)
    image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
    image = transform(image)
    image = image.unsqueeze(0)
    image = image.numpy()
    return image

input_folder="val_data"
def run_inference(session, output_dir="output_cpu"):
    files = sorted([f for f in os.listdir(input_folder) if f.endswith(".jpg")])
    input_name = session.get_inputs()[0].name
    runs=0
    for i,f in enumerate(files):
        runs+=1
        fp = os.path.join(input_folder, f)
        image = preprocess_image(fp)
        os.makedirs("input", exist_ok=True)
        np.save(f"input/input_{i}.npy", image)
        outputs = session.run(None, {input_name:image})
        # Create outpu directory if it doesn't exist
        os.makedirs(output_dir, exist_ok=True)
        for idx, out in enumerate(outputs):
            np.save(f"{output_dir}/output_{i}_{idx}.npy", out)

run_inference(cpu_session,"output_cpu")
run_inference(cpu_session_vint8,"output_cpu_vint8")
