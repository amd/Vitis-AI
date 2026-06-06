# Inference Configuration JSON Guide
<!--
## Copyright and license statement

Copyright (C) 2025-2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->

This document is the field-level reference for the JSON configuration consumed by `x_plus_ml_vart`. For application usage, CLI, and example commands see [README.md](../README.md).

## Overview

The configuration is hierarchical:

- **Top-level config** - device-level fields (`xclbin-location`, `device-index`), the global `preprocess-en` flag, and a `models-config` array that references one or more per-model configs.
- **Per-model config** (one JSON per model) - `preprocess-config`, `inference-config` (with `runner-options`), optional `ifms-config`, optional `postprocess-config`, optional `metaconvert-config`. Device-level fields must not be repeated here.

Sample top-level config (two models):

```json
{
  "xclbin-location": "/run/media/mmcblk0p1/x_plus_ml.xclbin",
  "device-index": 1,
  "preprocess-en": true,
  "models-config": [
    {"config-path": "/etc/vai/x_plus_ml_vart/json_configs/resnet50_int8_postproc.json"},
    {"config-path": "/etc/vai/x_plus_ml_vart/json_configs/yolox_m_int8_nms.json"}
  ]
}
```

### Top-level configuration fields

| Field             | Type    | Required | Default | Description                                                         |
| ----------------- | ------- | -------- | ------- | ------------------------------------------------------------------- |
| `xclbin-location` | string  | yes      | -       | Path to the XCLBIN that exposes the PL kernels.                     |
| `device-index`    | integer | no       | `1`     | PL device index.                                                    |
| `preprocess-en`   | boolean | no       | `false` | `true` enables preprocessing for every model; `false` runs every model in inference-only mode (`.bin` IFMs from `ifms-config`). |
| `models-config`   | array   | yes      | -       | Array of `{ "config-path": "<per-model JSON path>" }` entries; one entry per model instance. |

## Per-model configuration

Sample per-model config (full pipeline ResNet50 SOFTMAX with overlay):

```json
{
  "preprocess-config": {
    "kernel-name": "image_processing:{image_processing_1}",
    "mean-r": 123.675, "mean-g": 116.28, "mean-b": 103.53,
    "scale-r": 0.017124, "scale-g": 0.017507, "scale-b": 0.017429,
    "colour-format": "RGBX",
    "maintain-aspect-ratio": true,
    "resizing-type": "PANSCAN",
    "in-mem-bank": 2,
    "out-mem-bank": 2,
    "quant-scale-factor": 1.0
  },
  "inference-config": {
    "model-file": "/etc/vai/models/resnet50_int8/resnet50_int8.rai",
    "runner-options": {
      "log-level": "WARNING"
    }
  },
  "ifms-config": [
    { "name": "input", "file": "/etc/vai/models/resnet50_int8/data/classification.jpg" }
  ],
  "postprocess-config": {
    "topk": 1,
    "label-file-path": "/etc/vai/models/resnet50_int8/data/imagenet-classes-1000.txt",
    "type": "SOFTMAX",
    "post-process-print": "false"
  },
  "metaconvert-config": {
    "font-size": 0.5,
    "font": 3,
    "thickness": 2,
    "y-offset": 0,
    "label-color": [
      { "level": 1, "red": 0, "green": 255, "blue": 0 }
    ]
  }
}
```

## preprocess-config

See [preprocessing_config.md](../../../docs/preprocessing_config.md) for the full `preprocess-config` schema, `kernel-name` discovery, `colour-format` selection and accepted values, resizing modes, and worked examples.

## inference-config

| Field            | Type   | Required | Description                                                                                          |
| ---------------- | ------ | -------- | ---------------------------------------------------------------------------------------------------- |
| `model-file`     | string | yes      | Compiled model artifact - either a `.rai` file or a directory containing `vaiml_par_0/`.             |
| `runner-options` | object | no       | Pass-through options for `vart::Runner` (see below).                                                |

See [runner_options.md](../../../docs/runner_options.md) for the full `runner-options` schema (fields, defaults, NPU column placement, and auto-placement policy).

## ifms-config

`ifms-config` declares the input file(s) for the model. The exact behaviour depends on the global `preprocess-en` flag:

| Mode                                    | Number of entries                       | Accepted file types         | Notes                                                                                                                             |
| --------------------------------------- | --------------------------------------- | --------------------------- | --------------------------------------------------------------------------------------------------------------------------------- |
| Preprocessing (`preprocess-en: true`)   | 1 (single-input models only; additional entries are ignored) | `.jpg`, `.jpeg`, `.nv12`, `.bgr` | Optional - the CLI `--input-file` overrides `ifms-config` when present. `dim` is mandatory for `.nv12` / `.bgr`, ignored for JPEG. In per-model mode (no CLI broadcast), the entry's `name` is validated against the model's single input tensor name. |
| Inference-only (`preprocess-en: false`) | One entry **per** model input tensor    | `.bin`                      | Bound **by name** to the model's input tensors (see binding rules below). `dim` is silently ignored.                                |

**Preprocessing mode is single-input only.** The HLS `image_processing` kernel produces one preprocessed tensor per inference call, so multi-input models must be run with `preprocess-en: false` (inference-only mode), where each input tensor gets its own `.bin` listed in `ifms-config`.

**Name-based binding for inference-only.** Each entry's `name` must match a `vart::Runner`-reported input tensor name. Entries may be listed in any order. The application aborts at startup if the entry count differs from the model's input-tensor count, if any `name` is empty, missing, or duplicated, if the referenced file does not exist or lacks a `.bin` extension, or if any `name` does not match a `vart::Runner` input tensor. To discover the input tensor names of a compiled model, run `ml_vart --get-model-info <model-path>` - see [../../ml_vart/README.md](../../ml_vart/README.md).

**Batched `.bin` layout (inference-only).** Each `.bin` listed in `ifms-config` is treated as one or more frames of raw tensor bytes concatenated back-to-back, in the layout `vart::Runner` expects. The per-frame size is the `size_in_bytes` reported by `ml_vart --get-model-info <model-path>` (always per frame, never the total batch buffer). The batch size `N` is fixed by the compiled model (`batch_size` in the same dump) and cannot be changed at runtime. For every inference call, `N` consecutive frames are pulled from each tensor's file, with the same frame index across all files forming one batch element. All input `.bin` files for a given model must therefore contain the same total number of frames. Partial batches at end-of-file are handled automatically - the final inference call runs with fewer frames, no padding or wraparound, and only the populated slots are written to the output files.

| Field   | Type              | Description                                                                       |
| ------- | ----------------- | --------------------------------------------------------------------------------- |
| `name`  | string            | Must match a `vart::Runner`-reported input tensor name. Used for name-based binding in inference-only mode and cross-checked against the model's single input tensor in preprocessing per-model mode. Ignored when the CLI broadcast `--input-file` is supplied. |
| `file`  | string            | Path to the input file (image in preprocessing mode, `.bin` in inference-only).   |
| `dim`   | string (optional) | `WxH` resolution; mandatory for `.nv12` / `.bgr` in preprocessing mode.           |

## postprocess-config

`postprocess-config` is forwarded almost verbatim to `vart::PostProcess`. Only the application-level fields below are interpreted by `x_plus_ml_vart`; every other field is consumed by the selected postprocess `type`.

Sample (SOFTMAX classification):

```json
{
  "postprocess-config": {
    "topk": 1,
    "label-file-path": "/etc/vai/models/resnet50_int8/data/imagenet-classes-1000.txt",
    "type": "SOFTMAX",
    "post-process-print": "false"
  }
}
```

### Application-level fields

| Field                 | Type   | Required | Description                                                                                                                          |
| --------------------- | ------ | -------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| `type`                | string | yes      | PostProcess type. Mapped to a `vart::PostProcessType` and forwarded to VART-X. Accepted values listed below.                         |
| `quant-scale-factors` | array  | no       | Per-output-tensor dequantisation factors. Length must equal the model's number of output tensors; overrides the compiled defaults.   |

All other keys (`topk`, `label-file-path`, `post-process-print`, `nms-threshold`, `conf-threshold`, `model-params`, `grid-shape`, `anchors`, `input-layout`, `output-layout`, calibration parameters, etc.) are **type-specific** and are passed straight through to `vart::PostProcess`. The exact set required by each `type` is documented in the VART-X release's "Post Processing Function" reference.

### Accepted `type` values

Every `vart::PostProcessType` exposed by VART-X is accepted. The currently mapped strings, grouped for orientation only:

| Category            | `type` strings                                                                                                                |
| ------------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| Classification      | `RESNET50`, `SOFTMAX`, `ARGMAX`, `TOPK`, `LABEL_MAPPING`, `NORMALIZATION`, `THRESHOLD`                                        |
| Detection           | `YOLOV2`, `SSDRESNET34`, `NMS`, `DISTANCE_IOU_NMS`, `SOFT_NMS`, `CLASSWISE_NMS`, `ANCHOR_ADJUSTMENT`, `OBJECT_COUNT`          |
| Segmentation        | `SOFTMAXSEG`, `SIGMOIDSEG`, `ARGMAXSEG`                                                                                       |
| Calibration / stats | `BIAS_CORRECTION`, `CALIBRATION_PLATT`, `CALIBRATION_TEMPERATURE`, `OUTLIER_DETECTION`, `UNCERTAINTY_ESTIMATION`              |


For the full `postprocess-config` reference (`type` values, detection and segmentation fields, and examples), see [postprocessing_config.md](../../../docs/postprocessing_config.md).

## metaconvert-config

`metaconvert-config` enables the overlay stage (PostProcess -> MetaConvert -> Overlay) and is honoured only when `preprocess-en` is `true` **and** the same per-model JSON also has `postprocess-config`. If either is absent, the entire `metaconvert-config` block is silently ignored.

See [metaconvert_config.md](../../../docs/metaconvert_config.md) for the full `metaconvert-config` schema (classification / detection fields, segmentation fields, and how the overlay type is derived from `postprocess-config.type`).
