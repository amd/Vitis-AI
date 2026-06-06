# Inference Configuration JSON Guide

<!--
## Copyright and license statement

Copyright (C) 2025-2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->


This document explains the structure and usage of the `ml_ort_config.json` file for configuring model inference runs.

## Overview

The JSON file contains an object called `inference-config`, where each element describes the configuration for a single model inference session. Each session run requires the model file, input feature map (IFM) configuration, and execution provider options.

Let us consider the following example of a matrix multiplication model, which accepts two input feature maps (IFMs). The input names of the ONNX model are input0 and input1, respectively.

## JSON Structure Example

```json
{
  "inference-config": [
    {
      "model-file": "MatMul.onnx",
      "ifms-config": [
        {
          "name": "input0",
          "file": "input0_float32_2x3.bin"
        },
        {
          "name": "input1",
          "file": "input1_float32_3x4.bin"
        }
      ],
      "execution-provider-options": {
        "config_file": "vitisai_config.json",
        "target": "VAIML",
        "cache_dir": "my_cache_dir",
        "cache_key": "MatMul"
      }
    }
  ]
}

```

#### Description of inference config Array

| Field                        | Type              | Description                                                                 | Example Value                 |
|------------------------------|-------------------|-----------------------------------------------------------------------------|-------------------------------|
| `model-file`                 | String            | Path to the ONNX model file to be used for inference.                       | `"MatMul.onnx"`             |
| `ifms-config`                | Array of Objects  | List of input feature maps (IFMs) info required by the model-file.          | See below                     |
| `execution-provider-options` | Object            | Options for the Vitis AI execution provider used during compilation.        | See below                     |

#### Description of `ifms-config` Array

`ifms-config` is an array containing information about the input tensors for the specified model file.

| Field   | Type   | Description             | Example Value      |
|---------|--------|-------------------------|--------------------|
| name    | String | Input tensor name of the onnx model.<br>Get the names of a model by utilizing common/utils/get_onnx_in_out.py script.      | `"input_name"`     |
| file    | String | Path to input data file | `"file_path.bin"`  |

#### Description of `execution-provider-options` Object

`execution-provider-options` contains the options that were used during the compilation of the model.


| Field        | Type   | Description                                                             | Example Value                |
|--------------|--------|-------------------------------------------------------------------------|------------------------------|
| config_file  | String | Path to provider config file                                            | `"vitisai_config.json"`      |
| target       | String | Target hardware platform for VitisAI execution provider                 | `"VAIML"`                    |
| cache_dir    | String | The path and name of the cache directory.                               |`"my_cache_dir"`              |
| cache_key    | String | The subfolder in the cache directory where the compiled model is stored | `"resnet50_quantized_xint8"` |

