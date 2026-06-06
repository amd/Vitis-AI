# postprocess-config Reference

<!--
## Copyright and license statement

Copyright (C) 2025 - 2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->


## Overview

`postprocess-config` configures the post-processing function applied to raw model output tensors. The framework is modular and works with both `x_plus_ml_ort` and `x_plus_ml_vart` applications, independent of the model.

After post-processing, overlay pipelines use `metaconvert-config` to turn structured results into on-frame annotations; see **[metaconvert_config.md](metaconvert_config.md)**.

The base class handles tensor data conversion for `INT8`, `FLOAT32`, `BF16`, and `FP16` tensors, so derived post-processing functions always operate on `FLOAT32` buffers regardless of the model's quantized data type.

The post-processing function is selected by the `type` field. The fields required in `postprocess-config` depend on the **post-processing category** (classification, detection, or segmentation) of the chosen function.

---

## Post-Processing Categories

There are three categories, selected automatically from `postprocess-config.type`:

| Category         | Selected by `type` values                                                                                                                              | Required fields                                                              |
| ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------- |
| Classification   | `SOFTMAX`, `TOPK`, `ARGMAX`, `LABEL_MAPPING`, `NORMALIZATION`, `THRESHOLD`, `BIAS_CORRECTION`, `CALIBRATION_PLATT`, `CALIBRATION_TEMPERATURE`, `OUTLIER_DETECTION`, `UNCERTAINTY_ESTIMATION` | Common fields (+ optional `function-options` / `outlier-threshold`)          |
| Object Detection | `NMS`, `CLASSWISE_NMS`, `DISTANCE_IOU_NMS`, `SOFT_NMS`, `OBJECT_COUNT`, `ANCHOR_ADJUSTMENT`                                                            | Common fields + `nms-threshold`, `conf-threshold`, `model-params`            |
| Segmentation     | `SOFTMAXSEG`, `SIGMOIDSEG`, `ARGMAXSEG`                                                                                                                | Common fields only                                                           |

---

## Common Fields

Every `postprocess-config` shares the following fields regardless of task category.

| Field                | Type        | Required | Default                | Description                                                                                                       |
| -------------------- | ----------- | -------- | ---------------------- | ----------------------------------------------------------------------------------------------------------------- |
| `type`               | string      | Yes      | —                      | Selects the post-processing function (see [Post-Processing Categories](#post-processing-categories))              |
| `topk`               | integer     | No       | `1`                    | Number of top predictions to report (parsed for parity with classification configs in segmentation configs)       |
| `label-file-path`    | string      | No       | `""` (no labels loaded)| Path to a label file mapping class indices to human-readable names                                                |
| `post-process-print` | string/bool | No       | `false`                | When `true`, prints post-processing results to the console                                                        |

---

## Classification

Used when `type` is one of: `SOFTMAX`, `TOPK`, `ARGMAX`, `LABEL_MAPPING`, `NORMALIZATION`, `THRESHOLD`, `BIAS_CORRECTION`, `CALIBRATION_PLATT`, `CALIBRATION_TEMPERATURE`, `OUTLIER_DETECTION`, `UNCERTAINTY_ESTIMATION`.

### Function reference

| Function (`type`)         | Description                                                                                  |
| ------------------------- | -------------------------------------------------------------------------------------------- |
| `SOFTMAX`                 | Converts logits to a probability distribution                                                |
| `TOPK`                    | Extracts the top-K elements with their indices and values                                    |
| `ARGMAX`                  | Finds the index of the maximum value in a tensor                                             |
| `LABEL_MAPPING`           | Maps top-K class indices to human-readable labels                                            |
| `NORMALIZATION`           | Min-max normalizes logits to the `[0, 1]` range                                              |
| `THRESHOLD`               | Filters predictions based on a value threshold                                               |
| `BIAS_CORRECTION`         | Adjusts logits by subtracting a bias vector and applies Platt scaling                        |
| `CALIBRATION_PLATT`       | Calibrates probabilities using Platt scaling                                                 |
| `CALIBRATION_TEMPERATURE` | Calibrates probabilities using temperature scaling                                           |
| `OUTLIER_DETECTION`       | Detects outlier predictions based on Z-score                                                 |
| `UNCERTAINTY_ESTIMATION`  | Computes entropy to estimate prediction uncertainty                                          |


### Extra fields per function

`ARGMAX`, `TOPK`, `LABEL_MAPPING`, `NORMALIZATION`, and `UNCERTAINTY_ESTIMATION` work with just the [Common Fields](#common-fields). The functions below need additional fields.

| Function (`type`)         | Extra field                                | Type    | Default | Description                                                          |
| ------------------------- | ------------------------------------------ | ------- | ------- | -------------------------------------------------------------------- |
| `SOFTMAX`                 | (none)                                     | —       | —       | Common fields only                                                   |
| `BIAS_CORRECTION`         | `function-options.platt-param-a`           | float   | `1.0`   | Platt scaling parameter `A` applied after bias correction            |
| `BIAS_CORRECTION`         | `function-options.platt-param-b`           | float   | `0.0`   | Platt scaling parameter `B` applied after bias correction            |
| `CALIBRATION_PLATT`       | `function-options.platt-param-a`           | float   | `1.0`   | Platt scaling parameter `A`                                          |
| `CALIBRATION_PLATT`       | `function-options.platt-param-b`           | float   | `0.0`   | Platt scaling parameter `B`                                          |
| `CALIBRATION_TEMPERATURE` | `function-options.temperature-scaling`     | integer | `1`     | Temperature divisor applied to logits before softmax                 |
| `THRESHOLD`               | `function-options.threshold`               | float   | `4.0`   | Minimum value (inclusive) for a class to be reported                 |
| `OUTLIER_DETECTION`       | `outlier-threshold`                        | float   | `3.0`   | Z-score threshold above which a class is flagged as an outlier       |

> **Note:** `function-options.<param-name>` denotes a field nested inside a `function-options` object:
> ```json
> "function-options": {
>   "<param-name>": <value>
> }
> ```

### Examples

**SOFTMAX (common fields only):**

```json
"postprocess-config": {
  "type": "SOFTMAX",
  "topk": 1,
  "label-file-path": "/etc/vai/x_plus_ml_ort/labels/imagenet-classes-1000.txt",
  "post-process-print": "false"
}
```

**CALIBRATION_TEMPERATURE (with extra `function-options` field):**

```json
"postprocess-config": {
  "type": "CALIBRATION_TEMPERATURE",
  "topk": 1,
  "label-file-path": "/etc/vai/x_plus_ml_ort/labels/imagenet-classes-1000.txt",
  "post-process-print": "false",
  "function-options": {
    "temperature-scaling": 2
  }
}
```

---

## Object Detection

Used when `type` is one of: `NMS`, `CLASSWISE_NMS`, `DISTANCE_IOU_NMS`, `SOFT_NMS`, `OBJECT_COUNT`, `ANCHOR_ADJUSTMENT`.

### Function reference

| Function (`type`)    | Description                                                  |
| -------------------- | ------------------------------------------------------------ |
| `NMS`                | Standard Non-Maximum Suppression                             |
| `CLASSWISE_NMS`      | NMS applied independently per class                          |
| `DISTANCE_IOU_NMS`   | NMS using the Distance IoU metric                            |
| `SOFT_NMS`           | Soft Non-Maximum Suppression (Gaussian score decay)          |
| `OBJECT_COUNT`       | Detects objects with NMS and counts per class                |
| `ANCHOR_ADJUSTMENT`  | Decodes bounding boxes using anchor boxes and grid position  |

### Fields

In addition to the [Common Fields](#common-fields):

| Field             | Type        | Required | Default | Description                                              |
| ----------------- | ----------- | -------- | ------- | -------------------------------------------------------- |
| `nms-threshold`   | float       | No       | `0.4`   | IoU threshold used by Non-Maximum Suppression            |
| `conf-threshold`  | float       | No       | `0.5`   | Confidence threshold for filtering detections            |
| `model-params`    | object      | Yes      | —       | Model-specific decoding parameters (see below)           |

### model-params

| Field                                 | Type             | Required   | Default                  | Description                                                              |
| ------------------------------------- | ---------------- | ---------- | ------------------------ | ------------------------------------------------------------------------ |
| `is-obj-score-included`               | string/bool      | No         | `false`                  | Whether an objectness score is included in the model output              |
| `grid-shape`                          | array of arrays  | Yes        | —                        | Grid shape `[H, W]` for each output scale                                |
| `anchors`                             | array of arrays  | No         | `[]` (no anchors)        | Anchor box dimensions for each output scale                              |
| `input-layout`                        | array of strings | Yes        | —                        | Layout of each input tensor (`NCHW`, `NHWC`, `HCWNC4`)                   |
| `output-layout`                       | array of strings | Yes        | —                        | Layout of each output tensor (`NCHW`, `NHWC`, `NCH`, `NHC`)              |
| `is-scaling-required`                 | string/bool      | No         | `false`                  | Whether bbox coordinates need to be scaled to the input image size       |
| `apply-sigmoid-to-obj-conf`           | string/bool      | No         | `false`                  | Whether to apply sigmoid to objectness/class scores during decoding      |
| `num-anchorboxes`                     | array of integers| No         | `[]` (no anchor boxes)   | Number of anchor boxes for each output scale                             |

### Example

```json
"postprocess-config": {
  "type": "NMS",
  "topk": 100,
  "label-file-path": "/etc/vai/x_plus_ml_ort/labels/yolov2_labels.txt",
  "nms-threshold": 0.4,
  "conf-threshold": 0.4,
  "post-process-print": "false",
  "model-params": {
    "is-obj-score-included": "true",
    "grid-shape": [[13, 13]],
    "anchors": [[0.57, 0.67, 1.87, 2.06, 3.33, 5.47, 7.88, 3.52, 9.77, 9.16]],
    "input-layout": ["NCHW"],
    "output-layout": ["NCHW"],
    "is-scaling-required": "true",
    "apply-sigmoid-to-obj-conf": "false",
    "num-anchorboxes": [5]
  }
}
```

> The same skeleton applies to `CLASSWISE_NMS`, `DISTANCE_IOU_NMS`, `SOFT_NMS`, and `OBJECT_COUNT` — only the `type` field changes.

---

## Segmentation

Used when `type` is one of: `SOFTMAXSEG`, `SIGMOIDSEG`, `ARGMAXSEG`.

All segmentation functions expect a 4-D output tensor of shape `[batch, height, width, num_classes]` and produce a per-pixel class-index map (`uint16_t`) of length `height * width` for each batch element.

### Function reference

| Function (`type`) | Description                                                                                                                |
| ----------------- | -------------------------------------------------------------------------------------------------------------------------- |
| `SOFTMAXSEG`      | Per-pixel softmax across class channels followed by per-pixel argmax to produce a single-channel segmentation map          |
| `SIGMOIDSEG`      | Per-class sigmoid (`1 / (1 + exp(-x))`) followed by per-pixel argmax to produce a single-channel segmentation map          |
| `ARGMAXSEG`       | Per-pixel argmax directly on raw logits (no probability transform) to produce a single-channel segmentation map            |

### Fields

Segmentation uses the [Common Fields](#common-fields) only. Note that `topk` is parsed for parity with classification configs but is not used by the core segmentation-map generation.

### Example

```json
"postprocess-config": {
  "type": "SOFTMAXSEG",
  "topk": 5,
  "label-file-path": "/etc/vai/x_plus_ml_ort/labels/segmentation_labels.txt",
  "post-process-print": "false"
}
```

---

## Related documentation

- **[metaconvert_config.md](metaconvert_config.md)** — `metaconvert-config`: overlay rendering (`MetaConvert` / `Overlay`) keyed off `postprocess-config.type`.
