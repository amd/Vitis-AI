
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
npy_to_bin.py

Converts a NumPy `.npy` file containing tensor data into a raw binary file.

Usage:
    python npy_to_bin.py <input.npy> <output.bin>

Arguments:
    <input.npy>    Path to the input NumPy file.
    <output.bin>   Path to the output binary file.

Example:
    python npy_to_bin.py input.npy output.bin

Notes:
    - The output binary file will contain the raw bytes of the tensor data.
    - The script prints a confirmation message after successful conversion.
"""

import numpy as np
import sys

def npy_to_bin(npy_file, bin_file):
    arr = np.load(npy_file)
    arr.tofile(bin_file)
    print(f"Dumped {npy_file} to {bin_file} as raw binary.")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python npy_to_bin.py input.npy output.bin")
        sys.exit(1)
    npy_to_bin(sys.argv[1], sys.argv[2])

