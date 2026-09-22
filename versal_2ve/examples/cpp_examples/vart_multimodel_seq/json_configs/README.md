# Inference Configuration JSON Guide

<!--
## Copyright and license statement

Copyright (C) 2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->


This document explains the structure and usage of the JSON configuration file for the `vart_multimodel_seq` application.

## Overview

The JSON file contains an **array** of model objects. Each object describes one model to be executed sequentially. The application iterates over the full model sequence in a single thread, repeating for `N` iterations.

Field requirements:
- Always required: `model-cache-path`, `ifm-node-file-map`
- Optional: `start-column`, `aie-columns-sharing`, `ofm-dir`, `input-tensor-type`, `output-tensor-type`

## JSON Structure Example

```json
[
  {
    "model-cache-path": "/etc/vai/models/yolox_m_int8/yolox_m_int8.rai",
    "start-column": 0,
    "aie-columns-sharing": true,
    "ifm-node-file-map": {
      "images": "/etc/vai/models/yolox_m_int8/data/ifm_images_int8_1x640x640x4.bin"
    },
    "ofm-dir": "./"
  },
  {
    "model-cache-path": "/etc/vai/models/resnet50_int8/resnet50_int8.rai",
    "start-column": 0,
    "aie-columns-sharing": true,
    "ifm-node-file-map": {
      "input": "/etc/vai/models/resnet50_int8/data/ifm_input_int8_1x224x224x4.bin"
    },
    "ofm-dir": "./"
  }
]
```

### Description of JSON Fields

#### Description of Model Object

Each element in the top-level array is a model object with the following fields:

| Field                 | Type    | Required | Description                                                                                                                                   | Example Value                                                          |
| --------------------- | ------- | -------- | --------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------- |
| `model-cache-path`    | String  | Yes      | Path to a compiled model — either a model cache directory or an `.rai` file.                                                            | `"/etc/vai/models/yolox_m_int8/yolox_m_int8.rai"`                     |
| `start-column`        | Integer | No       | Starting NPU column for model placement. By default the runner selects an available column. Only set if explicit column control is needed.     | `0`                                                                    |
| `aie-columns-sharing` | Boolean | No       | Specify how to schedule column resources. `true` = shared/temporal (time-multiplexed with other models on the same columns); `false` = exclusive/spatial (columns reserved for this model only). Default `true`. | `true` |
| `ifm-node-file-map`   | Object  | Yes      | Mapping of input tensor node name → full path to the IFM binary file. Each key is a tensor name and the value is the file path.               | `{"images": "/data/ifm_images.bin"}`                                   |
| `ofm-dir`             | String  | No       | Base directory for OFM output. Per-model subdirectories (`ofm_model_1/`, `ofm_model_2/`, …) are created inside. Defaults to `"./"`.           | `"./"`                                                                 |
| `input-tensor-type`   | String  | No       | Tensor view used for the model's inputs: `"CPU"` (ONNX-format, e.g. FP32/NCHW) or `"HW"` (hardware-native, e.g. BF16/HCWNC4). Must be `"CPU"` when the model has a CPU subgraph at its input boundary. Defaults to `"HW"`.  | `"CPU"`                                                                |
| `output-tensor-type`  | String  | No       | Tensor view used for the model's outputs: `"CPU"` or `"HW"`. Must be `"CPU"` when the model has a CPU subgraph at its output boundary. Defaults to `"HW"`.  | `"CPU"`                                                                |

> **CPU subgraphs.** A Vitis AI–compiled model may have a CPU subgraph at its
> input and/or output boundary (operations that run on the CPU rather than the
> NPU). For such a boundary the runner must be created with the `"CPU"` tensor
> view for that direction — creating it with `"HW"` fails. Set
> `input-tensor-type` / `output-tensor-type` to `"CPU"` accordingly; each
> direction is independent, so a model can mix (e.g. `"CPU"` input with `"HW"`
> output). When `"CPU"` is selected, the IFM/OFM binaries must be in standard
> ONNX format (matching the CPU tensor shapes and data types), and IFM file
> sizes are validated against the CPU tensor sizes.

> **Tip:** The IFM node names are available in the model's ONNX file.
> Alternatively, you can pass any arbitrary string as the node name in
> `ifm-node-file-map`. The application validates node names before inference
> and will report the expected node names in its error output.

### Column Sharing Modes

The `start-column` and `aie-columns-sharing` fields together control NPU column placement:

| Mode                     | `start-column`          | `aie-columns-sharing` | Description                                                  |
| ------------------------ | ----------------------- | --------------------- | ------------------------------------------------------------ |
| **Temporal** (shared)    | Same value across models | `true`                | Models time-multiplex on the same NPU columns.               |
| **Spatial** (exclusive)  | Different, non-overlapping values | `false`      | Each model gets dedicated NPU columns with no swapping.      |




