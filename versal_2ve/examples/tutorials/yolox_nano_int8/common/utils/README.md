# Utility Scripts

<!--
## Copyright and License Statement

Copyright (C) 2025 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at  
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->


This module helps understand various utilities available for support.

---

## Binary to NumPy Array Converter

### **Script Name**: `bin_to_npy.py`

### **Description**:
Converts a raw binary file containing tensor data into a NumPy `.npy` file. Specify the data type and shape to correctly interpret the binary data.

---

## NumPy Array to Binary Converter

### **Script Name**: `npy_to_bin.py`

### **Description**:
Converts a NumPy `.npy` file containing tensor data into a raw binary file. This is useful for exporting tensor data for use in other tools or frameworks that require binary input.

---

## ONNX Model Input/Output Inspector

### **Script Name**: `get_onnx_in_out_names.py`

### **Description**:
Prints the input and output tensor names and shapes of an ONNX model.
Useful for inspecting model I/O for configuration, debugging, or integration with other tools.

---
