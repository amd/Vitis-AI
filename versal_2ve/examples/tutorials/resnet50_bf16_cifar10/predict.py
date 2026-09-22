#!/bin/python3
#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

import argparse
import pathlib

import numpy as np
import onnxruntime
from PIL import Image
from torchvision.datasets import CIFAR10

from resnet_utils import get_directories


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--ep",
        type=str,
        default="cpu",
        choices=["cpu", "npu"],
        help="EP backend selection",
    )
    parser.add_argument("--num-images", type=int, default=10)
    opt = parser.parse_args()

    current_dir, models_dir, data_dir, cache_dir = get_directories()
    onnx_model_path = str(models_dir / "resnet_trained_for_cifar10.onnx")
    config_file = "vitisai_config.json"
    cache_key = pathlib.Path(onnx_model_path).stem

    # default provider is CPUExecutionProvider
    providers = ["CPUExecutionProvider"]
    provider_options_dict = {}

    # NPU/CPU setup
    if opt.ep == "npu":
        print("execution started on NPU")
        providers = ["VitisAIExecutionProvider"]
        provider_options_dict = {
            "config_file": config_file,
            "cache_dir": str(cache_dir),
            "cache_key": cache_key,
            "target": "VAIML",
        }
    else:
        print("execution started on CPU")

    # Create session options
    session_options = onnxruntime.SessionOptions()
    session_options.log_severity_level = 3  # 0=Verbose, 1=Info, 2=Warning, 3=Error, 4=Fatal

    session = onnxruntime.InferenceSession(
        onnx_model_path,
        sess_options=session_options,
        providers=providers,
        provider_options=[provider_options_dict],
    )

    dataset = CIFAR10(root=str(data_dir), train=False, download=False)
    images = dataset.data
    labels = np.array(dataset.targets)
    label_names = dataset.classes

    # create images folder
    dirname = current_dir / "images"
    dirname.mkdir(parents=True, exist_ok=True)

    # Extract and dump the first images
    for i in range(0, opt.num_images):
        Image.fromarray(images[i]).save(str(dirname / f"image_{i}.png"))

    # Pick dumped images and predict
    correct = 0
    for i in range(0, opt.num_images):
        image = Image.open(str(dirname / f"image_{i}.png")).convert("RGB")
        # Resize the image to match the input size expected by the model
        image = image.resize((32, 32))
        image_array = np.array(image).astype(np.float32)
        image_array = image_array / 255

        # Reshape the array to match the input shape expected by the model
        image_array = np.transpose(image_array, (2, 0, 1))

        # Add a batch dimension to the input image
        input_data = np.expand_dims(image_array, axis=0)

        # Run the model
        outputs = session.run(None, {"input": input_data})

        # Process the outputs
        predicted_class = int(np.argmax(outputs[0]))
        predicted_label = label_names[predicted_class]
        label = label_names[labels[i]]
        correct += int(predicted_class == labels[i])

        print(f"Image {i}: Actual Label {label}, Predicted Label {predicted_label}")

    print(
        f"Top-1 accuracy on {opt.num_images} images: "
        f"{100.0 * correct / opt.num_images:.2f}% ({correct}/{opt.num_images})"
    )


if __name__ == "__main__":
    main()
