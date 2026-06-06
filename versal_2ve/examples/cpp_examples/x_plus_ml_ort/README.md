# X Plus ML ORT Application

<!--
## Copyright and license statement

Copyright (C) 2025 - 2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->


Note: Example model names, JSON files, and commands are for reference only. Modify them for your compiled models and board.

`x_plus_ml_ort` is a C++ application that demonstrates a complete, hardware-accelerated inference pipeline on the Versal AI Edge Series Gen 2. It combines three AMD acceleration layers into a single end-to-end flow:

1. **PL Preprocessing** — an input image or video frame is decoded and pre-processed (resize, normalise, colour-space conversion) by the `image_processing` HLS kernel running on the PL fabric, producing a tensor ready for the NPU.
2. **NPU Inference** — the preprocessed tensor is passed to ONNX Runtime with the Vitis AI Execution Provider, which dispatches inference to the NPU. Classification (e.g. ResNet50), object detection (e.g. YOLOX-M), and segmentation models are supported.
3. **Postprocessing and Overlay** — raw NPU output is postprocessed by VART-X (SOFTMAX, NMS, or segmentation decode), then converted to structured overlay metadata by `vart::MetaConvert`. The `vart::Overlay` module draws the results (labels, bounding boxes, or segmentation masks) onto the output frame using OpenCV.

The pipeline is configured entirely through JSON files — one top-level app config that references per-model configs for preprocessing, inference, postprocessing, and overlay settings.

## Key Features

- Supports JPEG, NV12, and BGR input formats
- PL-accelerated preprocessing using the `image_processing` HLS kernel (VART-X)
- NPU inference via ONNX Runtime with the Vitis AI Execution Provider
- Postprocessing via VART-X: SOFTMAX for classification, NMS for object detection
- Prediction overlay drawn on output frames using the VART-X Overlay module
- Multi-model support
- Temporal sharing support
- Batch size > 1 model support — models compiled with batch > 1 can process multiple frames per inference using raw video files (NV12/BGR)
- Frame-level processing control (`--frames`, `--runs`)

## Usage

```bash
x_plus_ml_ort --app-config <config json file> --input-file <path to input file> [--dim WIDTHxHEIGHT] [--runs count]
```

### Arguments

| Option                              | Required  | Default | Description                                                                                                                                                                                                                                         |
| ----------------------------------- | --------- | ------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `--app-config`                      | Mandatory |         | Config file path (mandatory)                                                                                                                                                                                                                        |
| `--input-file`                      | Mandatory |         | Input image file path (mandatory)                                                                                                                                                                                                                   |
| `--runs`                            | Optional  | `1`     | Number of iterations app should run (optional, default is 1)                                                                                                                                                                                        |
| `--benchmark`                       | Optional  | false   | Benchmark the metrics of all components (optional, default is false)                                                                                                                                                                                |
| `--log-level`                       | Optional  | WARNING | Application log level to print logs (optional, default is WARNING). Accepted log levels: 1 for ERROR, 2 for WARNING, 3 for INFERENCE RESULT, 4 for FIXME, 5 for INFO, 6 for DEBUG. Logs at the provided level and all levels below will be printed. |
| `--dim`                             | Optional  |         | Input video dimensions in WIDTHxHEIGHT format, e.g., 1280x720 (mandatory for raw video input like NV12 and BGR)                                                                                                                                     |
| `--frames`                          | Optional  |         | Number of frames to process (optional, default is all frames)                                                                                                                                                                                       |
| `--enable-timing-text-output`       | Optional  |         | Enable internal `utiltimer` instrumentation and print a human-readable per-component timing summary to the console after the run completes                                                                                                          |
| `--enable-chrome-trace-json <file>` | Optional  |         | Enable internal `utiltimer` instrumentation and write a Chrome trace JSON file to `<file>` after the run completes. The file can be visualized in any Chrome-trace-compatible viewer                                                                |
| `--help`                            | Optional  |         | Print this help and exit                                                                                                                                                                                                                            |

### Input

The application supports the following input formats:

- **JPEG** (`.jpg`, `.jpeg`) — Automatically decoded to BGR via OpenCV. `--dim` not required.
- **NV12** (`.nv12`) — Raw video format. `--dim WIDTHxHEIGHT` is mandatory.
- **BGR** (`.bgr`) — Raw video format. `--dim WIDTHxHEIGHT` is mandatory.

> **Note:** Models compiled with batch size > 1 can use raw video input (NV12 or BGR) with multiple frames (`--frames` ≥ batch size).

### Output

Overlayed results are written to the `output` directory. The filename reflects the input format, pipeline index `{N}`, and iteration index `{I}` (0-based, only when `--runs` > 1):

- **JPEG** — `postproc{N}_overlay.jpg` / `iter{I}_postproc{N}_overlay.jpg`
- **BGR** — `postproc{N}_overlay.bgr` / `iter{I}_postproc{N}_overlay.bgr`
- **NV12** — `postproc{N}_overlay.nv12` / `iter{I}_postproc{N}_overlay.nv12`

## Build

1. Source the Vitis AI SDK for Versal AI Edge Series Gen 2 environment:

```bash
source /path/to/sdk/environment-setup-cortexa72-cortexa53-amd-linux
```

2. Build the application:

```bash
make all
```

The resulting binary is `x_plus_ml_ort`.

3. To clean build artifacts:

```bash
make clean
```

## Running on the Board

### Prerequisites

Before running the commands below, finish board setup for your platform, program the required PL and AI Engine overlay on the board, and configure the runtime environment for your image (including `LD_LIBRARY_PATH`).

### Example commands

- **Image classification (ResNet50):**
  ```bash
  x_plus_ml_ort \
    --app-config /etc/vai/x_plus_ml_ort/json_configs/x_plus_ml_ort.json \
    --input-file /etc/vai/models/resnet50_int8/data/classification.jpg \
    --log-level 3
  ```

- **Object detection (YOLOX-M):**
  ```bash
  x_plus_ml_ort \
    --app-config /etc/vai/x_plus_ml_ort/json_configs/x_plus_ml_ort_od.json \
    --input-file /etc/vai/models/yolox_m_int8/data/detections.jpg \
    --log-level 3
  ```

- **Benchmark classification over 100 runs:**
  ```bash
  x_plus_ml_ort \
    --app-config /etc/vai/x_plus_ml_ort/json_configs/x_plus_ml_ort.json \
    --input-file /etc/vai/models/resnet50_int8/data/classification.jpg \
    --benchmark --runs 100
  ```

- **Benchmark object detection over 100 runs:**
  ```bash
  x_plus_ml_ort \
    --app-config /etc/vai/x_plus_ml_ort/json_configs/x_plus_ml_ort_od.json \
    --input-file /etc/vai/models/yolox_m_int8/data/detections.jpg \
    --benchmark --runs 100
  ```

- **Raw video input (NV12 or BGR)** — `--dim` is mandatory:
  ```bash
  x_plus_ml_ort \
    --app-config /etc/vai/x_plus_ml_ort/json_configs/x_plus_ml_ort_od.json \
    --input-file <path to nv12 file> \
    --dim 1920x1080
  ```

## Configuration JSON Guide

For the full JSON configuration schema reference, see [json_configs/README.md](json_configs/README.md).

## Two Pipelines with Temporal Sharing

For a detailed explanation of how the NPU auto-placement policy decides between spatial and temporal sharing, see [auto_placement_policy.md](../../docs/auto_placement_policy.md).

When all available NPU columns are occupied by spatially placed models, the runtime automatically time-shares those columns among additional models with a matching column footprint — the models take turns executing on the same hardware. This application demonstrates temporal sharing by restricting the `amdxdna` driver to a limited column range, which forces both models to share the same columns rather than being placed side by side.

To restrict the driver to 4 columns starting at column 0:

```bash
rmmod amdxdna
modprobe amdxdna max_col=4 start_col=0
```

With only 4 columns available, the first model occupies them spatially. When the second model loads and finds no free columns, the runtime detects a matching column footprint and places it as a temporal share on the same columns.

```bash
# Default run with 2-model temporal config
x_plus_ml_ort --app-config /etc/vai/x_plus_ml_ort/json_configs/temporal_x_plus_ml_ort.json --input-file /etc/vai/models/resnet50_int8/data/classification.jpg

# Benchmark 2 models for 1000 runs
x_plus_ml_ort --app-config /etc/vai/x_plus_ml_ort/json_configs/temporal_x_plus_ml_ort.json --input-file /etc/vai/models/resnet50_int8/data/classification.jpg --benchmark --runs 1000
```

To verify that both models are sharing the same columns, run the following in a **second terminal on the board** while inference is in progress:

```bash
xrt-smi examine --device 0 --report aie-partitions
```

With temporal sharing active, both models appear as two HW Contexts under a single Partition Index on the same four columns:

```
---------------------------
[0000:00:00.0] : Telluride
---------------------------
AIE Partitions
  Total Memory Usage: N/A
  Partition Index   : 0
    Columns: [0, 1, 2, 3]
    HW Contexts:
      |PID                 |Ctx ID     |Submissions |Migrations  |Err  |Priority |
      |Process Name        |Status     |Completions |Suspensions |     |GOPS     |
      |Memory Usage        |Instr BO   |            |            |     |FPS      |
      |                    |           |            |            |     |Latency  |
      |====================|===========|============|============|=====|=========|
      |1196                |1          |98          |0           |0    |Normal   |
      |N/A                 |Idle       |97          |0           |     |1        |
      |66 MB               |N/A        |            |            |     |1        |
      |                    |           |            |            |     |2000     |
      |--------------------|-----------|------------|------------|-----|---------|
      |1196                |2          |120         |0           |0    |Normal   |
      |N/A                 |Idle       |119         |0           |     |1        |
      |66 MB               |N/A        |            |            |     |1        |
      |                    |           |            |            |     |2000     |
      |--------------------|-----------|------------|------------|-----|---------|
```

Two contexts (`Ctx ID 1` and `Ctx ID 2`) sharing the same `Partition Index 0` on `Columns: [0, 1, 2, 3]` confirms temporal sharing is active. If the command is run after inference completes, the partition is released and `No hardware contexts running on device` is shown instead.

After testing, restore the driver to its default execution mode:

```bash
rmmod amdxdna
modprobe amdxdna enable_polling=0
```

## Batch Size Configuration

The application supports two batch size modes depending on how the ONNX model was compiled.

**Static batch** — the batch dimension is fixed in the ONNX model's input shape (e.g. `[6, 3, 224, 224]`). The app reads it directly from the model; no additional configuration is needed.

**Dynamic batch** — the ONNX model has a dynamic batch dimension (`-1`, e.g. `[-1, 3, 224, 224]`). The app cannot infer the intended batch size from the model alone, so it reads `dp_size` from the `vaiml_config` section of the VitisAI config JSON (the file referenced by `execution-provider-options.config_file` in the model config):

```json
{
  "passes": [
    {
      "name": "vaiml_partition",
      "vaiml_config": {
        "dp_size": 6
      }
    }
  ]
}
```

If `dp_size` is absent, the app falls back to `device_batch_size`. If neither field is present (or readable), the app defaults to batch size `1`.

> **Note:** Raw video input (NV12 or BGR) is required for batch size > 1. JPEG input is single-frame only.

## Performance Timing

The application includes internal `utiltimer` instrumentation to measure per-component latency across the pipeline (preprocessing, inference, postprocessing, overlay).

**Human-readable text output**

Prints a timing summary table to the console after all runs complete:

```bash
x_plus_ml_ort --app-config /etc/vai/x_plus_ml_ort/json_configs/x_plus_ml_ort.json \
  --input-file /etc/vai/models/resnet50_int8/data/classification.jpg \
  --enable-timing-text-output
```

Output is printed under the heading `UtilTimer Output` at the end of the run.

**Chrome trace JSON**

Writes a trace file that can be loaded into any Chrome-trace-compatible viewer for a visual flame-chart of component timings:

```bash
x_plus_ml_ort --app-config /etc/vai/x_plus_ml_ort/json_configs/x_plus_ml_ort.json \
  --input-file /etc/vai/models/resnet50_int8/data/classification.jpg \
  --enable-chrome-trace-json trace.json
```

To visualize:

1. Copy `trace.json` from the board to your host machine.
2. Open Google Chrome and navigate to `chrome://tracing`.
3. Click **Load** and select `trace.json`.
4. The flame chart shows per-component durations across the timeline.

> **Note:** Both `--enable-timing-text-output` and `--enable-chrome-trace-json` can be used together in the same run.

## Workflow Details

The application internally follows this pipeline per frame:

1. **Input Handling** — Decodes JPEG to BGR via OpenCV; reads NV12/BGR raw video directly
2. **Preprocessing** — Converts BGR/NV12 to normalized tensors using VART-X PreProcess APIs (hardware-accelerated via the `image_processing` HLS kernel)
3. **Inference** — Runs the ONNX model using ORT APIs with the Vitis AI Execution Provider on the NPU
4. **Post-processing** — VART-X PostProcess: SOFTMAX for classification, NMS for detection
5. **Overlay & Output** — Draws predictions on the output frame using the VART-X Overlay module; saves result to the `output` directory

### Limitations and Constraints

- **Single input tensor only:** Models with multiple input tensors are not supported.
- **Single output tensor only:** Models with multiple output tensors are not supported.
