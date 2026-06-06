# X Plus ML VART Application

<!--
## Copyright and license statement

Copyright (C) 2025-2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->

Note: Example model names, JSON files, and commands are for reference only. Modify them for your compiled models and board.

A C++ sample application that demonstrates how to build an end-to-end inference pipeline using **`vart::Runner`** (VART-ML) for NPU execution and **VART-X** for pre and postprocessing (PostProcess + MetaConvert + Overlay). It serves as a reference for hosting one or more Vitis AI compiled models in a single process, wiring stages with zero-copy IFM/OFM, and running them in spatial or temporal multi-model configurations - all driven from a JSON configuration so the same binary can be retargeted to different models without code changes.

### Key features

- **Four operating modes** selected by the global `preprocess-en` flag and the per-model `postprocess-config`: full pipeline, preprocess+inference, inference+postprocess, and inference-only.
- **Zero-copy IFM and OFM** between preprocess, inference, and postprocess - no extra staging copies between stages.
- **Three input-source modes** selected by `preprocess-en` and the presence of `--input-file`: CLI broadcast (one file shared across all models), per-model preprocess (each model reads its own file), and inference-only (each model reads `.bin` IFMs from its `ifms-config`).
- **VART-X PostProcess + MetaConvert + Overlay** pipeline for classification, detection, and segmentation models, accepting any `vart::PostProcessType` exposed by VART-X.
- **Iteration-aware output naming** that switches between single-iteration and multi-iteration filenames automatically.
- **Built-in benchmarking** option that disables file I/O and reports per-stage averages plus pipeline FPS.

## Usage

```bash
x_plus_ml_vart --app-config <config json file> [--input-file <input image file> [--dim WxH]] [--runs N] [--frames M] [--benchmark] [--log-level L]
```

`--input-file` is required for CLI broadcast mode, omitted for per-model preprocess mode, and must not be passed in inference-only mode (see the Input table below).

### Arguments

| Option              | Required       | Default | Description                                                  |
| ------------------- | -------------- | ------- | ------------------------------------------------------------ |
| `--app-config`      | Mandatory      |         | Path to configuration JSON file (mandatory).                 |
| `--input-file`      | Mode-dependent |         | Input image path for preprocess modes; required for CLI broadcast (`preprocess-en` true + shared CLI input), omitted for per-model preprocess, and must not be passed in inference-only mode (see [Input](#input)). |
| `--runs`            | Optional       | `1`     | Number of iterations to run (optional, default `1`; valid range `[1, 10000]` — see Argument validation). |
| `--benchmark`       | Optional       | `false` | Measure per-stage timings and pipeline FPS; disables OFM and overlay file writes (optional, default `false`). |
| `--log-level`       | Optional       | `2`     | Application log level (optional, default `2`). Accepted values: `1` ERROR, `2` WARNING, `3` INFERENCE RESULT, `4` FIXME, `5` INFO, `6` DEBUG. The chosen level and all lower-numbered levels are printed. |
| `--dim`             | Optional       |         | Resolution `WxH` for raw NV12/BGR when using CLI `--input-file` (mandatory for `.nv12`/`.bgr` in that mode); ignored for JPEG and in per-model preprocess / inference-only modes (with a warning where applicable). |
| `--frames`          | Optional       | `-1`    | Frames processed per iteration (`-1` = all: one for JPEG, `file_size / frame_size` for NV12/BGR clip, IFM count for `.bin`); effective upper bound with `--runs N` is `N × frames` (see Argument validation). |
| `--help`            | Optional       |         | Print the supported arguments and exit.                      |

### Argument validation

- `--input-file` accepted formats: `.jpg`, `.jpeg`, `.nv12`, `.bgr`. Whether the flag is required, optional, or rejected depends on the mode - see the Input table below for the full matrix. In inference-only mode the per-tensor binary inputs come from `ifms-config` and are bound **by name** to the model's input tensors (each `name` must match a `vart::Runner`-reported input tensor name; see [json_configs/README.md](json_configs/README.md#ifms-config)).
- `--runs` must be a positive integer in `[1, 10000]`; `0` or values above the cap are rejected at startup.
- `--benchmark` disables OFM and overlay file writes for accurate measurement.
- `--log-level` prints the chosen level and every level numbered below it (e.g. `5` prints `1`-`5`).
- `--dim` is mandatory when CLI `--input-file` is `.nv12` or `.bgr`; ignored (with a warning) for JPEG inputs, in per-model preprocess mode, and in inference-only mode.
- `--frames` is per iteration. With `--runs N --frames M` the application reads up to `M` frames per iteration and repeats `N` times, so the effective upper bound on processed frames is `N * M`.

### Input

The JSON configuration is split into a **top-level config** (device fields, the global `preprocess-en` flag, and a `models-config` array referencing one or more per-model configs) and a **per-model config** (`preprocess-config`, `inference-config`, optional `ifms-config`, optional `postprocess-config`, optional `metaconvert-config`).

The global `preprocess-en` flag and the presence of `--input-file` together select one of three input modes:

| Mode                       | `preprocess-en` | `--input-file`     | Input source                                                                                                |
|----------------------------|-----------------|--------------------|-------------------------------------------------------------------------------------------------------------|
| Preprocess - CLI broadcast | true            | required           | The CLI file is fed to every model and overrides every per-model `ifms-config`.                             |
| Preprocess - per-model     | true            | omitted            | Each model reads the file declared in its own `ifms-config`; the entry's `name` must match the model's input tensor name. |
| Inference-only             | false           | must NOT be passed | Each model reads the raw `.bin` IFMs from its own `ifms-config`, bound by `name` to the `vart::Runner` input tensors.   |

Preprocessing-mode files are `.jpg`/`.jpeg` (dimensions auto-detected) or `.nv12`/`.bgr` (dimensions supplied via `--dim` for CLI broadcast, or inside `ifms-config` for per-model).

Preprocessing mode is restricted to **single-input** models because the HLS `image_processing` kernel produces a single preprocessed tensor per inference call. Multi-input models must be run with `preprocess-en: false` (inference-only mode), where each input tensor gets its own `.bin` via `ifms-config`.

When the model's input tensor reports `GENERIC` memory layout, preprocessing now accepts only these tensor shape patterns:

- 3D tensor shape: treated as `NHW`
- 4D tensor shape where `shape[1]` is `3` or `4`: treated as `NCHW`
- 4D tensor shape where `shape[3]` is `3` or `4`: treated as `NHWC`

All other `GENERIC` tensor shapes, including ambiguous 4D tensors where both `shape[1]` and `shape[3]` are `3` or `4`, are rejected at startup with an explicit error.

To discover the `vart::Runner`-reported input-tensor names (needed to author each `ifms-config` entry's `name`), use the `ml_vart` companion application's `--get-model-info <model-path>` flag - see [ml_vart/README.md](../ml_vart/README.md).

#### Batch processing

The batch size `N` is fixed by the compiled model and cannot be changed at runtime. Query it (and per-tensor `size_in_bytes`, always per frame) with `ml_vart --get-model-info <model-path>` - see [ml_vart/README.md](../ml_vart/README.md). The application handles the batch slicing automatically across all three input modes:

- **Preprocess modes** (`preprocess-en: true`): the file reader pulls `N` consecutive frames from each input source (CLI broadcast file or per-model `ifms-config` file) per inference call. JPEG inputs are inherently single-frame, so JPEG runs use batch size 1 regardless of model `N`; NV12/BGR clips and binary inputs supply enough frames to fill full batches.
- **Inference-only mode** (`preprocess-en: false`): each `.bin` listed in `ifms-config` must contain `total_frames * per_frame_size_in_bytes` bytes of raw tensor data, with frames concatenated back-to-back in the layout `vart::Runner` expects. All input files for a given model must contain the same total number of frames; the application takes `N` consecutive frames from each tensor's file per inference call, with the same frame index across all files forming one batch element. Partial batches at end-of-file are handled automatically - the final inference call runs with fewer frames, no padding or wraparound, and only the populated slots are written to the output files.

See [json_configs/README.md](json_configs/README.md#ifms-config) for the inference-only `.bin` contract.

### Output

All artifacts are written under `output/` (created automatically, relative to the working directory; not configurable). With `--runs N` (`N>1`) every per-iteration file is prefixed `iter{I}_`.

| Per-model mode                    | Files in `output/`                                                                                                       |
|-----------------------------------|--------------------------------------------------------------------------------------------------------------------------|
| PostProcess disabled              | Binary OFM per output tensor: `infer{N}_out{X}-{dtype}_{shape}_{tensor_name}.bin`                                        |
| PostProcess enabled               | Same OFMs plus `postproc{N}_results.txt` (classification scores or detections)                                           |
| PostProcess + Preprocess enabled  | Same plus an overlay `postproc{N}_overlay.{jpg,nv12,bgr}` matching the input format (JPG: one file per frame; NV12/BGR: all frames concatenated in one container) |

`N` is the model instance index from `models-config`; `X` is the output tensor index; `{dtype}` and `{shape}` come from the model's output tensor metadata; `{tensor_name}` is the model tensor name with `/` replaced by `-`.

Example tree (single iteration, full pipeline, JPG input):

```
output/
├── infer0_out0-bf16_1x1000_compute_graph.ofm_ddr.bin
├── postproc0_results.txt
└── postproc0_overlay.jpg
```

#### Batched outputs

For models with batch size larger than 1, each inference call appends the full batch into each output tensor's `.bin` file - frames are stored back-to-back, sized at the `vart::Runner`-reported per-frame `size_in_bytes` per slot, and in the same batch order as the input. Postprocess text results and overlays are emitted per-frame, also in batch order. Partial batches at end-of-file only emit the slots that were actually populated. See [Input -> Batch processing](#batch-processing) for the input contract that produces these batches.

Full naming rules (multi-iteration prefix, multi-frame batching, NV12/BGR container layout, all per-(format x iteration) example trees) are in [output_files.md](output_files.md).

## Build

1. Source the Vitis AI SDK for Versal AI Edge Series Gen 2 environment:

```bash
source /path/to/sdk/environment-setup-cortexa72-cortexa53-amd-linux
```

2. Build the application:

```bash
make all
```

The resulting binary is `x_plus_ml_vart`.

3. To clean build artifacts:

```bash
make clean
```

## Running on the Board

### Board Environment Setup

Refer to the board setup guide for instructions on setting up the board environment. _(Link pending - TODO)_

The binary (`x_plus_ml_vart`), JSON configs (`/etc/vai/x_plus_ml_vart/json_configs/`), and model artifacts including sample JPEG inputs and `.bin` IFMs (`/etc/vai/models/<model>/`) are all pre-installed, so the commands below run as-is on the board. The NV12/BGR examples are an exception and require an externally supplied raw clip in place of the `<input_nv12_file>` placeholder.

### Prerequisites

Before running the commands below, finish board setup for your platform, program the required PL and AI Engine overlay on the board, and configure the runtime environment for your image (including `LD_LIBRARY_PATH`).

### Example commands

**Single-model classification (ResNet50)** - full pipeline (preprocess + inference + postprocess + overlay) on one JPEG passed via `--input-file`. Runs the SOFTMAX top-k postprocess defined in `resnet50_int8_postproc.json` and writes the binary OFM, `postproc0_results.txt`, and `postproc0_overlay.jpg` to `output/`. Classification scores are printed to the console as well as written to the text file.

```bash
x_plus_ml_vart --app-config /etc/vai/x_plus_ml_vart/json_configs/x_plus_ml_vart_1model.json --input-file /etc/vai/models/resnet50_int8/data/classification.jpg --log-level 3
```

**Single-model object detection (YOLOX-M)** - full pipeline on one JPEG passed via `--input-file`. Runs the NMS postprocess defined in `yolox_m_int8_nms.json`; the overlay carries the detected bounding boxes and class labels.

```bash
x_plus_ml_vart --app-config /etc/vai/x_plus_ml_vart/json_configs/x_plus_ml_vart_od.json --input-file /etc/vai/models/yolox_m_int8/data/detections.jpg --log-level 3
```

**Two models, CLI broadcast input** - ResNet50 and YOLOX-M run concurrently in their own preprocess + inference + postprocess thread sets, driven from the same input image. The CLI `--input-file` overrides the `ifms-config` of both per-model configs, and each model emits its own `infer{N}_*`, `postproc{N}_results.txt`, and `postproc{N}_overlay.jpg`.

```bash
x_plus_ml_vart --app-config /etc/vai/x_plus_ml_vart/json_configs/x_plus_ml_vart_2models.json --input-file /etc/vai/models/yolox_m_int8/data/detections.jpg --log-level 3
```

**Two models, per-model inputs** - same top-level config as above (`x_plus_ml_vart_2models.json`), but `--input-file` is omitted so each model reads the JPEG declared in its own per-model `ifms-config` (ResNet50 reads `classification.jpg`, YOLOX-M reads `detections.jpg`). The input-mode selection is purely a CLI concern - the same top-level config drives both broadcast and per-model behaviour depending on whether `--input-file` is present.

```bash
x_plus_ml_vart --app-config /etc/vai/x_plus_ml_vart/json_configs/x_plus_ml_vart_2models.json --log-level 3
```

**Raw NV12 input** - same single-model pipeline as the first example but fed a raw NV12 frame instead of a JPEG. `--dim` supplies the source resolution (the same flag is required for `.bgr` input). No NV12 clip is pre-installed; replace `<input_nv12_file>` with an externally supplied path.

```bash
x_plus_ml_vart --app-config /etc/vai/x_plus_ml_vart/json_configs/x_plus_ml_vart_1model.json --input-file <input_nv12_file> --dim 1920x1080 --log-level 3
```

**Multi-frame, multi-iteration NV12 stream** - reads the first 5 frames of a raw NV12 stream and repeats the run 3 times through the 2-model pipeline. Every iteration's output is written to `output/` with the `iter{I}_` prefix so artifacts from different iterations don't overwrite each other.

```bash
x_plus_ml_vart --app-config /etc/vai/x_plus_ml_vart/json_configs/x_plus_ml_vart_2models.json --input-file <input_nv12_file> --dim 1920x1080 --frames 5 --runs 3 --log-level 3
```

**Inference-only run** - skips preprocessing entirely (`preprocess-en: false` in the top-level config). The IFM tensor is read directly from the pre-installed `.bin` listed in the per-model config, so no `--input-file` is needed; only the binary OFM is written (no postprocess, no overlay).

```bash
x_plus_ml_vart --app-config /etc/vai/x_plus_ml_vart/json_configs/x_plus_ml_vart_inference_only.json
```

> **Note:** The pre-installed top-level configs demonstrate up to two models, but the application imposes no upper bound on `models-config`. To run more models, author one per-model config per additional model and append a `{"config-path": "<path>"}` entry to the `models-config` array of a custom top-level config. See [json_configs/README.md](json_configs/README.md) for the per-model schema.

## Configuration JSON Guide

The complete JSON schema (field types, defaults, required/optional, supported color formats, accepted postprocess `type` values, and per-mode validation rules) is documented in [json_configs/README.md](json_configs/README.md).

## Benchmarking and Performance Analysis

The `--benchmark` flag disables file writes and reports per-stage latency and pipeline FPS for every model instance:

```bash
x_plus_ml_vart --app-config /etc/vai/x_plus_ml_vart/json_configs/x_plus_ml_vart_1model.json --input-file /etc/vai/models/resnet50_int8/data/classification.jpg --benchmark --runs 100
```

Steady-state metrics reported per model instance (preprocess and postprocess lines only appear when those stages are enabled):

- `Average PreProcess latency` - ms/frame
- `Average Inference latency` - ms/batch
- `Average PostProcess latency` - ms/frame
- `Average pipeline latency` - ms/frame, sum of the enabled stages
- `Average throughput` - end-to-end FPS, bounded by the slowest stage

Sample output (full pipeline, 100 iterations of a batch-1 ResNet50):

```
Total number of frames processed: 100
---------------------------------------------------------------------------------------
Model [/etc/vai/models/resnet50_int8/resnet50_int8.rai] with device batch size 1 processed 100 frames
Steady-State Benchmark Results [Pipeline: Preprocess + Inference + Postprocess]...
Average PreProcess latency   : 0.30081 ms/frame
Average Inference latency    : 2.14292 ms/batch
Average PostProcess latency  : 0.08108 ms/frame
Average pipeline latency     : 2.52481 ms/frame
Average throughput           : 466.653 FPS
---------------------------------------------------------------------------------------
```

The `[Pipeline: ...]` tag reflects which stages were active for that instance, and the latency lines for disabled stages are omitted accordingly.

## Additional Considerations

- **Pipeline idle timeout**: the run is aborted if no frame completes for 30 seconds in normal mode or 5 seconds in benchmark mode, with a `[WARNING] Application exited with N frames still in pipeline` log line.
- **Quantisation factor overrides**: by default the application reads the input and per-output-tensor quantisation factors from the compiled model. Setting `preprocess-config.quant-scale-factor` (single float, input side) or `postprocess-config.quant-scale-factors` (array of floats, one per output tensor) overrides the model-supplied values for that stage. See [../../docs/preprocessing_config.md](../../docs/preprocessing_config.md#common-fields) and [json_configs/README.md](json_configs/README.md#application-level-fields) for the field definitions.

## Related Documents

- [json_configs/README.md](json_configs/README.md) - full JSON schema (top-level + per-model, all sections).
- [../../docs/mixed_precision.md](../../docs/mixed_precision.md) - quantize, compile, and run INT8-head + BF16/FP16-tail models on this application (AMD Quark VINT8 + Vitis AI compiler config + on-board execution).
- [internals.md](internals.md) - system architecture, operating-mode derivation, canonical full-pipeline diagram with mode variants, threading model, priority-based event loop, zero-copy IFM/OFM, and `vart::NpuTensor` cache.
- [output_files.md](output_files.md) - full output filename rules: per-iteration prefixing, binary OFM naming, postprocess text and overlay artifacts, and example directory trees.
- [../../docs/multi_tenancy.md](../../docs/multi_tenancy.md) - data vs. tensor parallelism, spatial vs. temporal multi-tenancy, and how `inference-config.runner-options.aie-columns-sharing` / `start-column` map to NPU column layouts.
- [../../docs/auto_placement_policy.md](../../docs/auto_placement_policy.md) - how the runtime places models on the NPU when `start-column` is omitted.
