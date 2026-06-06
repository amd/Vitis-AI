# Inference Configuration JSON Guide

<!--
## Copyright and license statement

Copyright (C) 2025 - 2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->


This document explains the structure and fields of the provided JSON configuration file for the x_plus_ml_ort application.

## Overview

The x_plus_ml_ort application uses a hierarchical JSON configuration structure:

- **Main Configuration** (`x_plus_ml_ort.json`): Defines the pipeline structure and references individual model configurations
- **Individual Model Configurations** (`resnet50_int8.json`, `yolox_m_int8_nms.json`, ...): Contains model-specific settings for inference, preprocessing, and postprocessing

## Configuration Structure

### Main Configuration File (`x_plus_ml_ort.json`)

```json
{
  "xclbin-location": "/run/media/mmcblk0p1/x_plus_ml.xclbin",
  "device-index": 1,
  "models-config": [
    {
      "config-path": "/etc/vai/x_plus_ml_ort/json_configs/resnet50.json"
    }
  ]
}
```

To configure multiple models, add an entry per model under `models-config`. See `temporal_x_plus_ml_ort.json` for a two-model example:

```json
{
  "xclbin-location": "/run/media/mmcblk0p1/x_plus_ml.xclbin",
  "device-index": 1,
  "models-config": [
    {
      "config-path": "/etc/vai/x_plus_ml_ort/json_configs/resnet50.json"
    },
    {
      "config-path": "/etc/vai/x_plus_ml_ort/json_configs/yolox_m_int8_nms.json"
    }
  ]
}
```

| Field           | Type    | Description                                                                                                                                                                        | Example Value      |
| --------------- | ------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------ |
| xclbin-location | String  | Path to the XCLBIN file containing PL kernel binaries. The provided `x_plus_ml.xclbin` includes the `image_processing` HLS PL kernel used for hardware-accelerated pre-processing. | "x_plus_ml.xclbin" |
| device-index    | integer | Index of the PL device used to control PL kernels and perform all memory allocations                                                                                               | 1                  |
| models-config   | Array   | Array of configuration of different models                                                                                                                                         | [.....]            |

#### models-config array

| Field       | Type   | Description                               | Example Value   |
| ----------- | ------ | ----------------------------------------- | --------------- |
| config-path | String | Path to individual model config JSON file | "resnet50.json" |

### Individual Model Configuration Files

This JSON configuration contains the following sections:

- Preprocess configuration: Defines how to preprocess the inputs.
- Inference configuration: Specifies the path to the input ONNX model and the corresponding execution provider options.
- Post-process configuration: Details the steps for post-processing the inference results.
- Meta convert configuration: Converts the predicted data into a format that can be understood by the overlay component for drawing results on the image.

### Configuration Structure

```json
{
  "preprocess-config": {
    "kernel-name": "image_processing:{image_processing_1}",
    "mean-r": 123.675,
    "mean-g": 116.28,
    "mean-b": 103.53,
    "scale-r": 0.017124,
    "scale-g": 0.017507,
    "scale-b": 0.017429,
    "colour-format": "RGBP_FLOAT",
    "maintain-aspect-ratio": true,
    "resizing-type": "PANSCAN",
    "in-mem-bank": 2,
    "out-mem-bank": 2
  },
  "inference-config": {
    "model-file": "/etc/vai/models/resnet50_int8/resnet50_int8.onnx",
    "inputs-config": [
      {
        "memory-layout": "NCHW"
      }
    ],
    "execution-provider-options": {
      "config_file": "/etc/vai/models/resnet50_int8/vitisai_config.json",
      "target": "VAIML",
      "cache_dir": "/etc/vai/models",
      "cache_key": "resnet50_int8"
    }
  },
  "postprocess-config": {
    "topk": 5,
    "label-file-path": "/etc/vai/models/resnet50_int8/data/imagenet-classes-1000.txt",
    "type": "SOFTMAX"
  },
  "metaconvert-config": {
    "font-size": 0.5,
    "font": 3,
    "thickness": 2,
    "y-offset": 0,
    "label-color": [{ "level": 1, "red": 0, "green": 255, "blue": 0 }]
  }
}
```

### Top-Level Fields

| Field              | Type   | Description                                               | Example |
| ------------------ | ------ | --------------------------------------------------------- | ------- |
| preprocess-config  | object | Preprocessing parameters for input images                 | {...}   |
| inference-config   | object | Inference engine and model configuration                  | {...}   |
| postprocess-config | object | Postprocessing parameters for inference results           | {...}   |
| metaconvert-config | object | Prediction Metadata conversion and visualization settings | {...}   |

---

### preprocess-config

See [preprocessing_config.md](../../../docs/preprocessing_config.md) for the full `preprocess-config` schema, `kernel-name` discovery, `colour-format` selection and accepted values, resizing modes, and worked examples.

---

### inference-config

| Field                      | Type   | Description                                  | Example                                            |
| -------------------------- | ------ | -------------------------------------------- | -------------------------------------------------- |
| model-file                 | string | Path to the ONNX model file                  | "/etc/vai/models/resnet50_int8/resnet50_int8.onnx" |
| inputs-config              | array  | List of input configurations                 | [{...}] see below                                  |
| execution-provider-options | object | Options for the inference execution provider | {...} see below                                    |

#### inputs-config (array element)

| Field         | Type   | Description                   | Example |
| ------------- | ------ | ----------------------------- | ------- |
| memory-layout | string | Memory layout of input tensor | "NCHW"  |

#### execution-provider-options

| Field                     | Type    | Description                                                             | Example                                             |
| ------------------------- | ------- | ----------------------------------------------------------------------- | --------------------------------------------------- |
| config_file               | string  | Path to execution provider config file                                  | "/etc/vai/models/resnet50_int8/vitisai_config.json" |
| target                    | string  | Target hardware platform for VitisAI execution provider                 | "VAIML"                                             |
| cache_dir                 | string  | The path and name of the cache directory                                | "/etc/vai/models"                                   |
| cache_key                 | string  | The subfolder in the cache directory where the compiled model is stored | "resnet50_int8"                                     |
| encryption_key            | string  | (Optional) Encryption key for encrypted models                          | "my_encryption_key"                                 |
| ai_analyzer_visualization | boolean | (Optional) Enable AI analyzer visualization                             | true                                                |
| ai_analyzer_profiling     | boolean | (Optional) Enable AI analyzer profiling                                 | false                                               |

---

### postprocess-config

> **Note:** The table below reflects typical fields for the bundled examples (e.g. ResNet50). For all supported `type` values, optional fields (including detection `model-params`), and JSON examples, see **[postprocessing_config.md](../../../docs/postprocessing_config.md)**.

| Field           | Type    | Description                          | Example                                                        |
| --------------- | ------- | ------------------------------------ | -------------------------------------------------------------- |
| topk            | integer | Number of top predictions to return  | 5                                                              |
| label-file-path | string  | Path to label file to map prediction | "/etc/vai/models/resnet50_int8/data/imagenet-classes-1000.txt" |
| type            | string  | Postprocessing type/model            | "SOFTMAX"                                                      |

---

### metaconvert-config

Passed to `vart::MetaConvert`, which converts postprocessor output into structured overlay data. The overlay type (classification label, detection bounding box, segmentation mask) is determined automatically from `postprocess-config.type`.

There are two schema variants depending on the task type.

#### Classification / Detection

Used with classification and detection postprocessors (e.g. `SOFTMAX`, `TOPK`, `NMS`).

| Field       | Type    | Description                                                                |
| ----------- | ------- | -------------------------------------------------------------------------- |
| font-size   | float   | Scale factor for OpenCV `putText` label rendering                          |
| font        | integer | OpenCV font type; `3` = `FONT_HERSHEY_COMPLEX`                             |
| thickness   | integer | Stroke thickness in pixels for label text and bounding boxes               |
| y-offset    | integer | Vertical offset in pixels for label placement on the frame                 |
| label-color | array   | Per-pipeline-level colour — each entry has `level`, `red`, `green`, `blue` |

#### Segmentation

Used with segmentation postprocessors (e.g. `SOFTMAXSEG`, `ARGMAXSEG`).

| Field      | Type    | Description                                                                          |
| ---------- | ------- | ------------------------------------------------------------------------------------ |
| mask-level | integer | Pipeline level at which the segmentation mask is applied (1-indexed)                 |
| classes    | array   | Per-class colour map — each entry has `name`, `red`, `green`, `blue` (one per class) |

For the full field reference, see [metaconvert_config.md](../../../docs/metaconvert_config.md).
