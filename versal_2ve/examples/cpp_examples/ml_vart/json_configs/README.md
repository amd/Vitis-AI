# Inference Configuration JSON Guide

<!--
## Copyright and license statement

Copyright (C) 2025-2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->


This document explains the structure and usage of the `ml_vart_config.json` file for configuring model inference runs.

## Overview

The JSON file contains an object that describes the configuration for a single model inference session.

Field requirements depend on CLI mode:

- Always required: `inference-config.model-file`
- Conditionally required: `ifms-config` (required when `--dry-run` is **not** set)
- Optional: `ofms-dir` (defaults to `"output"` if not specified; only used during normal inference, ignored in `--dry-run` and `--benchmark` modes)

Let us consider the following example configuration for a ResNet50 model, which accepts one input feature map (IFM).

## JSON Structure Example

```json
{
  "inference-config": {
    "model-file": "/etc/vai/models/resnet50_int8/resnet50_int8.rai",
    "runner-options": {
      "log-level": "WARNING",
      "ai-analyzer-profiling": false
    }
  },
  "ifms-config": [
    {
      "name": "input",
      "file": "/etc/vai/models/resnet50_int8/data/ifm_input_int8_1x224x224x4.bin"
    }
  ],
  "ofms-dir": "output"
}
```

### Description of JSON Fields

#### Description of `inference-config` Object

| Field            | Type   | Description                                                            | Example Value                                       |
| ---------------- | ------ | ---------------------------------------------------------------------- | --------------------------------------------------- |
| `model-file`     | String | Path to the compiled model artifact (`.rai`) or model cache directory. | `"/etc/vai/models/resnet50_int8/resnet50_int8.rai"` |
| `runner-options` | Object | All `vart::Runner` specific options.                                   | See below                                           |

See [runner_options.md](../../../docs/runner_options.md) for the full `runner-options` schema (fields, defaults, NPU column placement, and auto-placement policy).

#### Description of `ifms-config` Array

`ifms-config` is an array containing one entry per input tensor of the model.

Entries are bound to the `vart::Runner` input tensors **by name** (not by JSON-array order). The `name` field of each entry must match a `vart::Runner`-reported input tensor name; the array may be authored in any order. The application aborts at startup if any `name` does not match a `vart::Runner` input tensor, if a `name` is duplicated, or if the entry count differs from the model's input-tensor count. Use `ml_vart --get-model-info <model-path>` to inspect the `vart::Runner`-reported input tensor names.

Requirement:

- Required when `--dry-run` is not set.
- Optional when `--dry-run` is set.

| Field  | Type   | Description                                | Example Value                                                         |
| ------ | ------ | ------------------------------------------ | --------------------------------------------------------------------- |
| `name` | String | `vart::Runner`-reported input tensor name this entry binds to. Must match exactly. | `"input"`                                                             |
| `file` | String | Path to the IFM `.bin` file for that tensor. File must exist and have a `.bin` extension. | `"/etc/vai/models/resnet50_int8/data/ifm_input_int8_1x224x224x4.bin"` |

#### Description of `ofms-dir` Field

| Field      | Type   | Description                                                                                                                                                                                                                                                                              | Example Value |
| ---------- | ------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------- |
| `ofms-dir` | String | Path to the directory where OFMs will be dumped. If not specified, defaults to `"output"`. Output directory is created automatically if it does not exist. **Note:** This field is only used during normal inference; it is ignored when `--dry-run` or `--benchmark` modes are enabled. | `"output"`    |
