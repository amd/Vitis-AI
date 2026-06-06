
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
bin_to_npy.py

Converts a raw binary file containing tensor data into a NumPy `.npy` file.

Usage:
    python bin_to_npy.py <input.bin> <output.npy> <dtype> <shape>

Arguments:
    <input.bin>   Path to the input binary file.
    <output.npy>  Path to the output NumPy file.
    <dtype>       Data type of the tensor (e.g., float32, int8).
    <shape>       Shape of the tensor, formatted as 1x3x224x224.

Example:
    python bin_to_npy.py input.bin output.npy float32 1x3x224x224

Notes:
    - The shape must match the total number of elements in the binary file.
    - Supported data types are those recognized by NumPy.
    - The script prints a confirmation message after successful conversion.
"""

import numpy as np
import sys
import re

def bin_to_npy(bin_file, npy_file, dtype, shape_str):
    shape = tuple(map(int, re.split('[x]', shape_str)))
    arr = np.fromfile(bin_file, dtype=dtype)
    arr = arr.reshape(shape)
    np.save(npy_file, arr)
    print(f"Dumped {bin_file} to {npy_file} as numpy array.")

if __name__ == "__main__":
    if len(sys.argv) != 5:
        print("Usage: python bin_to_npy.py input.bin output.npy dtype shape")
        print("Example: python bin_to_npy.py input.bin output.npy float32 1x3x224x224")
        sys.exit(1)
    dtype = sys.argv[3]
    shape_str = sys.argv[4]
    bin_to_npy(sys.argv[1], sys.argv[2], dtype, shape_str)

