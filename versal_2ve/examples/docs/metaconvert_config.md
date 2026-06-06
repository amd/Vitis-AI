# metaconvert-config Reference

<!--
## Copyright and license statement

Copyright (C) 2025 - 2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->


## Overview

`postprocess-config` configures the post-processing stage applied before overlay metadata is produced. For every JSON field, supported `type` values, detection `model-params`, and examples, see **[postprocessing_config.md](postprocessing_config.md)** — MetaConvert consumes post-process outputs whose interpretation follows **`postprocess-config.type`**.

`metaconvert-config` enables the VART-X Overlay pipeline stage:

1. `vart::MetaConvert` — converts raw postprocessor output into structured overlay metadata (bounding boxes, classification labels, segmentation masks)
2. `vart::Overlay` — renders the structured metadata onto the output frame using OpenCV drawing primitives

The **overlay type** (classification label, detection bounding box, segmentation colour mask) is determined automatically from `postprocess-config.type` and does not need to be specified in `metaconvert-config`.

---

## Schema Variants

The fields in `metaconvert-config` depend on the model task type. There are two schemas:

| Task type                  | Schema                                                                                                                         |
| -------------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| Classification / Detection | `display-level`, `font-size`, `font`, `thickness`, `radius`, `y-offset`, `draw-above-bbox-flag`, `label-filter`, `label-color` |
| Segmentation               | `mask-level`, `classes`                                                                                                        |

The correct schema is selected automatically based on the `InferResultType` derived from `postprocess-config.type`.

---

## Schema 1 — Classification and Detection

Used when `postprocess-config.type` resolves to a classification or detection postprocessor (e.g. `SOFTMAX`, `TOPK`, `NMS`, `CLASSWISE_NMS`).

### Fields

| Field                | Type    | Required | Default | Description                                                                                     |
| -------------------- | ------- | -------- | ------- | ----------------------------------------------------------------------------------------------- |
| display-level        | integer | No       | -1      | Pipeline level to display annotations for; `-1` annotates all levels                            |
| font-size            | float   | No       | 0.5     | Scale factor for OpenCV `putText` label rendering                                               |
| font                 | integer | No       | 0       | OpenCV font type (see [OpenCV Font Types](#opencv-font-types))                                  |
| thickness            | integer | No       | 1       | Stroke thickness in pixels for label text and bounding boxes                                    |
| radius               | integer | No       | 3       | Radius in pixels for circle drawing primitives (e.g. keypoint overlay)                          |
| y-offset             | integer | No       | 0       | Vertical offset in pixels applied to label placement on the frame                               |
| draw-above-bbox-flag | boolean | No       | true    | When `true`, label text is drawn above the bounding box; when `false`, below                    |
| label-filter         | array   | No       | —       | Allowlist of label strings; only labels in this list are annotated. Omit to annotate all labels |
| label-color          | array   | No       | —       | Per-pipeline-level label colour definitions (see below)                                         |

### label-filter (array element)

Each entry is a plain string containing a label name to include in the overlay. Labels not in this list are suppressed.

```json
"label-filter": ["cat", "dog", "person"]
```

### label-color (array element)

Each entry in `label-color` defines the annotation colour for a specific pipeline level. For single-model configs, use `"level": 1`.

| Field | Type    | Required | Description                                       |
| ----- | ------- | -------- | ------------------------------------------------- |
| level | integer | Yes      | Pipeline level this colour applies to (1-indexed) |
| red   | integer | Yes      | Red channel value (0–255)                         |
| green | integer | Yes      | Green channel value (0–255)                       |
| blue  | integer | Yes      | Blue channel value (0–255)                        |

### Example

```json
"metaconvert-config": {
  "font-size": 0.5,
  "font": 3,
  "thickness": 2,
  "y-offset": 0,
  "label-color": [
    { "level": 1, "red": 0, "green": 255, "blue": 0 }
  ]
}
```

### OpenCV Font Types

The `font` field maps to OpenCV `HersheyFonts` enum values:

| Value | OpenCV Font                   |
| ----- | ----------------------------- |
| 0     | `FONT_HERSHEY_SIMPLEX`        |
| 1     | `FONT_HERSHEY_PLAIN`          |
| 2     | `FONT_HERSHEY_DUPLEX`         |
| 3     | `FONT_HERSHEY_COMPLEX`        |
| 4     | `FONT_HERSHEY_TRIPLEX`        |
| 5     | `FONT_HERSHEY_COMPLEX_SMALL`  |
| 6     | `FONT_HERSHEY_SCRIPT_SIMPLEX` |
| 7     | `FONT_HERSHEY_SCRIPT_COMPLEX` |

---

## Schema 2 — Segmentation

Used when `postprocess-config.type` resolves to a segmentation postprocessor (e.g. `SOFTMAXSEG`, `SIGMOIDSEG`, `ARGMAXSEG`).

### Fields

| Field      | Type    | Required | Default | Description                                                          |
| ---------- | ------- | -------- | ------- | -------------------------------------------------------------------- |
| mask-level | integer | No       | 0       | Pipeline level at which the segmentation mask is applied (1-indexed) |
| classes    | array   | No       | —       | Per-class colour definitions used to render the segmentation mask    |

### classes (array element)

Each entry maps a class index (by position in the array) to its overlay colour.
| Field | Type | Required | Description |
| ----- | ------- | -------- | --------------------------- |
| name | string | Yes | Human-readable class name |
| red | integer | Yes | Red channel value (0–255) |
| green | integer | Yes | Green channel value (0–255) |
| blue | integer | Yes | Blue channel value (0–255) |

### Example

```json
"metaconvert-config": {
  "mask-level": 1,
  "classes": [
    { "name": "background",  "red": 0,   "green": 0,   "blue": 0   },
    { "name": "aeroplane",   "red": 128, "green": 0,   "blue": 0   },
    { "name": "bicycle",     "red": 0,   "green": 128, "blue": 0   },
    { "name": "bird",        "red": 128, "green": 128, "blue": 0   },
    { "name": "boat",        "red": 0,   "green": 0,   "blue": 128 }
  ]
}
```

> **Note:** The `classes` array must contain one entry per output class of the segmentation model, in the same order as the model's output channels.
