#!/usr/bin/env python3

# Copyright (C) 2026 Advanced Micro Devices, Inc.

import numpy as np
import os
import time
import argparse
from PIL import Image
import cv2
import VART

WIDTH = 640             # Model required width
HEIGHT = 640            # Model required height

def preprocess_image (img, input_size, swap=(2, 0, 1)):
    if len(img.shape) == 3:
        padded_img = np.ones((input_size[0], input_size[1], 3), dtype=np.uint8) * 114
    else:
        padded_img = np.ones(input_size, dtype=np.uint8) * 114

    r = min(input_size[0] / img.shape[0], input_size[1] / img.shape[1])

    resized_img = cv2.resize(
        img,
        (int(img.shape[1] * r), int(img.shape[0] * r)),
        interpolation=cv2.INTER_LINEAR,
    ).astype(np.uint8)

    padded_img[: int(img.shape[0] * r), : int(img.shape[1] * r)] = resized_img

    padded_img = padded_img.transpose(swap)
    padded_img = np.ascontiguousarray(padded_img, dtype=np.float32)

    padded_img = np.expand_dims(padded_img, axis=0)   # Change shape to 1x3x640x640

    return padded_img, r

def run(snapshot_dir, image_path, dump_input, dump_output, num_inferences):
    print ('Creating VART Runner for snapshot: {}' .format(snapshot_dir))

    model = VART.Runner(snapshot_dir=snapshot_dir)

    # Preprocess the input image once
    origin_img = cv2.imread(image_path, cv2.IMREAD_COLOR)
    preprocessed_image, ratio = preprocess_image(origin_img, (640,640))

    total_time = 0

    # Run model multiple times using the same preprocessed image
    for i in range(num_inferences):
        print(f"Frame {i+1}")
        before = time.time()
        outp = model([preprocessed_image])
        after = time.time()
        inference_time = (after - before) * 1000  # Convert to milliseconds
        total_time += inference_time
        print(f"Total Inference (NPU + ONNX Graph) took {inference_time:.2f} ms\n")

        if dump_output:
            # Dump output in raw format
            for j, output in enumerate(outp):
                output_file = f"/tmp/yolox_output{j}_{i}.raw"
                output.tofile(output_file)
                print(f"Output {j} of inference {i+1} dumped to {output_file}")

    average_time = total_time / num_inferences
    print(f" ")
    print(f"Average Total Inference Time for {i+1} Frames: {average_time:.2f} ms")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description='Options for yolox_npu_runner',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument("--snapshot", type=str, default='', help="Path to the snapshot folder")
    parser.add_argument("--image", type=str, required=True, help="Path to the input JPEG image")
    parser.add_argument("--dump_input", action="store_true", help="Dump input data of model")
    parser.add_argument("--dump_output", action="store_true", help="Dump output data of model")
    parser.add_argument("--num_inferences", type=int, default=1, help="Number of inferences to run")
    args = parser.parse_args()
    snapshot_dir = args.snapshot
    image_path = args.image
    dump_input = args.dump_input
    dump_output = args.dump_output
    num_inferences = args.num_inferences

    run(snapshot_dir, image_path, dump_input, dump_output, num_inferences)
