##########################################################################
 # Copyright (C) 2025 Advanced Micro Devices, Inc.
 #
 # Licensed under the Apache License, Version 2.0 (the "License");
 # you may not use this file except in compliance with the License.
 # You may obtain a copy of the License at
 #
 #     http://www.apache.org/licenses/LICENSE-2.0
 #
 # Unless required by applicable law or agreed to in writing, software
 # distributed under the License is distributed on an "AS IS" BASIS,
 # WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 # See the License for the specific language governing permissions and
 # limitations under the License.
###########################################################################

"""
get_onnx_in_out_names.py

Prints the input and output tensor names and shapes of an ONNX model.

Usage:
    python get_onnx_in_out_names.py <model.onnx>

Arguments:
    <model.onnx>   Path to the ONNX model file.

Example:
    python get_onnx_in_out_names.py resnet50.onnx

Notes:
    - Requires the onnxruntime Python package (`pip install onnxruntime`).
    - Useful for inspecting model I/O for configuration or debugging.
"""

import onnxruntime
import argparse

def print_onnx_io_info(model_path):
    # Load the ONNX model
    session = onnxruntime.InferenceSession(model_path)

    # Print input names and shapes
    print("Inputs:")
    for input_meta in session.get_inputs():
        print(f"  Name: {input_meta.name}, Shape: {input_meta.shape}")

    # Print output names and shapes
    print("\nOutputs:")
    for output_meta in session.get_outputs():
        print(f"  Name: {output_meta.name}, Shape: {output_meta.shape}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Print ONNX model input and output names and shapes.")
    parser.add_argument("model_path", help="Path to the ONNX model file")
    args = parser.parse_args()

    print_onnx_io_info(args.model_path)

