# preprocess-config Reference

<!--
## Copyright and license statement

Copyright (C) 2025 - 2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->


## Overview

`preprocess-config` configures the input pre-processing stage applied to raw image and video frames before inference. The framework is modular and works with the `x_plus_ml_ort`, `x_plus_ml_vart`, and `spatial_mt_ml_ort` applications, independent of the model.

Pre-processing runs on the `image_processing` HLS kernel on the PL fabric (driven through `vart::PreProcess`) and performs colour-space conversion, optional aspect-preserving resize, per-channel mean / scale normalization, and packing into the layout expected by the model's hardware input tensor.

The pipeline is configured by a fixed schema of common fields, plus an output `colour-format` selector that ties the pre-processed buffer to the model's tensor layout and data type.

---

## Common Fields

Every `preprocess-config` accepts the following fields.

| Field                              | Type    | Required | Default                                  | Description                                                                                                                              |
| ---------------------------------- | ------- | -------- | ---------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| `kernel-name`                      | string  | No       | `image_processing:{image_processing_1}`  | Hardware `image_processing` IP instance bound to this stage (see [Selecting `kernel-name`](#selecting-kernel-name))                      |
| `mean-r` / `mean-g` / `mean-b`     | float   | Yes      | —                                        | Per-channel mean used for normalization (`y = (x - mean) * scale`)                                                                       |
| `scale-r` / `scale-g` / `scale-b`  | float   | Yes      | —                                        | Per-channel scale used for normalization                                                                                                 |
| `colour-format`                    | string  | No\*     | Auto-derived                             | Pre-processor output format (see [Selecting `colour-format`](#selecting-colour-format)). \*In `x_plus_ml_vart` this field is optional; when omitted the format is derived from the inference tensor's layout and data type assuming RGB colour space. Other apps require this field. |
| `maintain-aspect-ratio`            | boolean | No       | `false`                                  | When `true`, scaling preserves aspect ratio; requires `resizing-type` to be set                                                          |
| `resizing-type`                    | string  | Cond.    | —                                        | `LETTERBOX` (pad to fit, used by YOLOX-style models) or `PANSCAN` (crop to fit, used by ResNet-style models); required when `maintain-aspect-ratio` is `true` |
| `symmetric-padding`                | boolean | No       | `false`                                  | `LETTERBOX` only; `true` splits padding equally across both sides of the image, `false` places all padding on one side                   |
| `in-mem-bank`                      | integer | Yes      | —                                        | Input memory bank index accessible by the `image_processing` PL kernel; the kernel reads the raw input frame from this bank             |
| `out-mem-bank`                     | integer | Yes      | —                                        | Output memory bank index accessible by the `image_processing` PL kernel; the kernel writes the pre-processed output to this bank        |
| `quant-scale-factor`               | float   | No       | from model                               | Overrides the input-tensor quantization factor compiled into the model                                                                   |

To determine the memory-bank connections exposed by a given XCLBIN, run on the target:

```bash
xclbinutil --info --input <xclbin>
```

---

## Selecting `kernel-name`

The pre-processing pipeline binds to a specific instance of the `image_processing` PL kernel inside the loaded XCLBIN. When `kernel-name` is omitted, the runtime defaults to `image_processing:{image_processing_1}`.

To list the kernel instances available in the currently loaded XCLBIN, run on the target:

```bash
xrt-smi program --device 1 --user /run/media/mmcblk0p1/x_plus_ml.xclbin    #program the XCLBIN if not already done
xrt-smi examine --device 1 --report dynamic-regions                        #list the dynamic regions, which include the `image_processing` kernel instances
```

Each kernel instance reported by the second command can be referenced as `image_processing:{<instance_name>}` in `preprocess-config.kernel-name`.

---

## Runtime override for `image_processing.cfg`

A runtime override for the `image_processing.cfg` location. When the environment variable `IMAGE_PROCESSING_CFG_PATH` is set, the HW image-processing library loads the configuration from that path; when it is unset, behavior is unchanged and the existing default path is used.

Example:

```bash
export IMAGE_PROCESSING_CFG_PATH=/home/amd-edf/image_processing.cfg
```

> **Note:** If the design contains multiple `image_processing` IP instances, a single `image_processing.cfg` is used for all of them today — every instance loads and applies the same configuration file.

---

## Selecting `colour-format`

The `image_processing` IP describes its output in video-format terms (`RGB`, `RGBP`, `RGBX`, `Y_UV8_420`, ...). The inference runtime describes its inputs in tensor-layout terms (`NCHW`, `NHWC`, `NHW`, `HCWNC4`). These two naming systems do not map one-to-one, so `colour-format` is the bridge: it must match both the tensor's memory layout and its element data type.

To look up the model's input tensor layout and data type, use the [`ml_vart`](../cpp_examples/ml_vart/README.md) companion application with the [`--get-model-info`](../cpp_examples/ml_vart/README.md#inspecting-model-metadata) flag:

```bash
ml_vart --get-model-info <model-path>
```

The dumped `<model_basename>_info.json` reports `memory_layout` and `dtype` for every input tensor under both a `cpu` and an `hw` block. Which block to read depends on the runtime that will consume the pre-processed buffer:

- **ONNX RT-based applications** (`x_plus_ml_ort`, `spatial_mt_ml_ort`) always operate against the CPU tensor type. Read `memory_layout` and `dtype` from the `cpu` block.
- **VART-ML-based applications** (`x_plus_ml_vart`) can operate against either the CPU or HW tensor type, selected per model via `inference-config.runner-options.input-tensor-type` (`"CPU"` or `"HW"`). Read `memory_layout` and `dtype` from the block that matches the configured tensor type.

Use that `memory_layout` / `dtype` pair to pick a value from the tables below.

### Quick mapping

The shortlist below is the common subset accepted by all three applications. For dtype / layout combinations not covered here (e.g. NCHW BGR planar, NHWC BGR FP16/BF16, HCWNC4 BGR FP16/BF16), see the per-app coverage in [Accepted values](#accepted-values).

| Tensor memory layout | `colour-format` (INT8) | `colour-format` (FP32)       |
| -------------------- | ---------------------- | ---------------------------- |
| `NCHW`               | `RGBP`                 | `RGBP_FLOAT`                 |
| `NHWC`               | `RGB`, `BGR`           | `RGB_FLOAT`, `BGR_FLOAT`     |
| `HCWNC4`             | `RGBX`, `BGRX`         | —                            |
| `NV12` (raw input)   | `Y_UV8_420`            | —                            |

### Accepted values

Each application parses its own list of `colour-format` strings. The table below is the union of what the three preprocess-driving applications recognise; the per-app columns mark which strings each app accepts. A value not accepted by the chosen app is rejected with `Unknown preprocessing Video Format`.

| Format        | Layout / dtype                                       | `x_plus_ml_vart` | `x_plus_ml_ort` | `spatial_mt_ml_ort` |
| ------------- | ---------------------------------------------------- | :--------------: | :-------------: | :-----------------: |
| `RGB`         | NHWC INT8 RGB interleaved                            | x                | x               | x                   |
| `BGR`         | NHWC INT8 BGR interleaved                            | x                | x               | x                   |
| `RGBP`        | NCHW INT8 RGB planar                                 | x                | x               | x                   |
| `BGRP`        | NCHW INT8 BGR planar                                 |                  | x               |                     |
| `RGBX`        | HCWNC4 INT8 RGB with 4-byte channel padding          | x                | x               | x                   |
| `BGRX`        | HCWNC4 INT8 BGR with 4-byte channel padding          | x                | x               | x                   |
| `Y_UV8_420`   | NV12 semi-planar Y plus interleaved UV at 4:2:0      | x                | x               | x                   |
| `RGB_FLOAT`   | NHWC FP32 RGB interleaved                            | x                | x               | x                   |
| `BGR_FLOAT`   | NHWC FP32 BGR interleaved                            | x                | x               | x                   |
| `RGBP_FLOAT`  | NCHW FP32 RGB planar                                 | x                | x               | x                   |
| `RGB_FP16`    | NHWC FP16 RGB interleaved                            | x                | x               |                     |
| `BGR_FP16`    | NHWC FP16 BGR interleaved                            |                  | x               |                     |
| `RGB_BF16`    | NHWC BF16 RGB interleaved                            | x                | x               |                     |
| `BGR_BF16`    | NHWC BF16 BGR interleaved                            |                  | x               |                     |
| `RGBP_FP16`   | NCHW FP16 RGB planar                                 | x                | x               |                     |
| `RGBP_BF16`   | NCHW BF16 RGB planar                                 | x                | x               |                     |
| `BGRP_FP16`   | NCHW FP16 BGR planar                                 |                  | x               |                     |
| `BGRP_BF16`   | NCHW BF16 BGR planar                                 |                  | x               |                     |
| `RGBX_BF16`   | HCWNC4 BF16 RGB with 4-byte channel padding          | x                | x (also accepts `RGBx_BF16`) | x (only `RGBx_BF16`) |
| `RGBX_FP16`   | HCWNC4 FP16 RGB with 4-byte channel padding          | x                |                 |                     |
| `BGRX_BF16`   | HCWNC4 BF16 BGR with 4-byte channel padding          | x                |                 |                     |
| `BGRX_FP16`   | HCWNC4 FP16 BGR with 4-byte channel padding          | x                |                 |                     |


---

## Resizing

`maintain-aspect-ratio` and `resizing-type` together control how the source frame is fitted to the model's input width and height.

| Configuration                                                       | Behaviour                                                                                                                                          |
| ------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| `maintain-aspect-ratio: false`                                      | Plain stretch to the model dimensions; aspect ratio is not preserved                                                                               |
| `maintain-aspect-ratio: true` + `resizing-type: PANSCAN`            | Scales and centre-crops the source to the model dimensions, preserving aspect ratio at the cost of cropping content (e.g. ResNet-style classifiers)|
| `maintain-aspect-ratio: true` + `resizing-type: LETTERBOX`          | Scales the source to fit inside the model dimensions and pads the remaining space, preserving the entire content (e.g. YOLOX-style detectors)      |

When `LETTERBOX` is selected, `symmetric-padding` controls whether the padding is split equally on both sides (`true`) or applied entirely on one side (`false`, the default).

---

## Examples

### ResNet50-style classification (NCHW INT8, PANSCAN)

```json
"preprocess-config": {
  "mean-r": 123.675,
  "mean-g": 116.28,
  "mean-b": 103.53,
  "scale-r": 0.017124,
  "scale-g": 0.017507,
  "scale-b": 0.017429,
  "colour-format": "RGBP",
  "maintain-aspect-ratio": true,
  "resizing-type": "PANSCAN",
  "in-mem-bank": 2,
  "out-mem-bank": 2
}
```

### YOLOX-style detection (HCWNC4 INT8, LETTERBOX)

```json
"preprocess-config": {
  "mean-r": 0,
  "mean-g": 0,
  "mean-b": 0,
  "scale-r": 1.0,
  "scale-g": 1.0,
  "scale-b": 1.0,
  "colour-format": "RGBX",
  "maintain-aspect-ratio": true,
  "resizing-type": "LETTERBOX",
  "in-mem-bank": 2,
  "out-mem-bank": 2
}
```

---

## Related documentation

- **[postprocessing_config.md](postprocessing_config.md)** — `postprocess-config`: post-processing functions applied to raw inference output tensors.
- **[metaconvert_config.md](metaconvert_config.md)** — `metaconvert-config`: overlay rendering (`MetaConvert` / `Overlay`) keyed off `postprocess-config.type`.
