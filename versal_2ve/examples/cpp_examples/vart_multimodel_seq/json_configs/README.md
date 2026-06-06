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
- Always required: `model_cache_path`, `ifm_node_file_map`
- Optional: `start_column`, `aie_columns_sharing`, `ofm_dir`

## JSON Structure Example

```json
[
  {
    "model_cache_path": "/etc/vai/models/yolox_m_int8/yolox_m_int8.rai",
    "start_column": 0,
    "aie_columns_sharing": true,
    "ifm_node_file_map": {
      "images": "/etc/vai/models/yolox_m_int8/data/ifm_images_int8_1x640x640x4.bin"
    },
    "ofm_dir": "./"
  },
  {
    "model_cache_path": "/etc/vai/models/resnet50_int8/resnet50_int8.rai",
    "start_column": 0,
    "aie_columns_sharing": true,
    "ifm_node_file_map": {
      "input": "/etc/vai/models/resnet50_int8/data/ifm_input_int8_1x224x224x4.bin"
    },
    "ofm_dir": "./"
  }
]
```

### Description of JSON Fields

#### Description of Model Object

Each element in the top-level array is a model object with the following fields:

| Field                 | Type    | Required | Description                                                                                                                                   | Example Value                                                          |
| --------------------- | ------- | -------- | --------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------- |
| `model_cache_path`    | String  | Yes      | Path to a compiled model — either a model cache directory or an `.rai` file.                                                            | `"/etc/vai/models/yolox_m_int8/yolox_m_int8.rai"`                     |
| `start_column`        | Integer | No       | Starting NPU column for model placement. By default the runner selects an available column. Only set if explicit column control is needed.     | `0`                                                                    |
| `aie_columns_sharing` | Boolean | No       | Specify how to schedule column resources. `true` = shared/temporal (time-multiplexed with other models on the same columns); `false` = exclusive/spatial (columns reserved for this model only). Default `true`. | `true` |
| `ifm_node_file_map`   | Object  | Yes      | Mapping of input tensor node name → full path to the IFM binary file. Each key is a tensor name and the value is the file path.               | `{"images": "/data/ifm_images.bin"}`                                   |
| `ofm_dir`             | String  | No       | Base directory for OFM output. Per-model subdirectories (`ofm_model_1/`, `ofm_model_2/`, …) are created inside. Defaults to `"./"`.           | `"./"`                                                                 |

> **Tip:** The IFM node names are available in the model's ONNX file.
> Alternatively, you can pass any arbitrary string as the node name in
> `ifm_node_file_map`. The application validates node names before inference
> and will report the expected node names in its error output.

### Column Sharing Modes

The `start_column` and `aie_columns_sharing` fields together control NPU column placement:

| Mode                     | `start_column`          | `aie_columns_sharing` | Description                                                  |
| ------------------------ | ----------------------- | --------------------- | ------------------------------------------------------------ |
| **Temporal** (shared)    | Same value across models | `true`                | Models time-multiplex on the same NPU columns.               |
| **Spatial** (exclusive)  | Different, non-overlapping values | `false`      | Each model gets dedicated NPU columns with no swapping.      |




