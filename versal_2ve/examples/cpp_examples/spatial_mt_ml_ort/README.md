# Spatial Multi-Thread ML ORT Application

<!--
## Copyright and license statement

Copyright (C) 2025 - 2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->

Note: Example model names, JSON files, and commands are for reference only. Modify them for your compiled models and board.

The Spatial Multi-Thread Application is a **C++ multi-model inference** sample
that runs a **classification model (ResNet-50 INT8)** and an **object-detection
model (YOLOX-m INT8)** simultaneously on the **AMD Versal™ AI Edge Series Gen 2 VEK385 Evaluation Kit** using **ONNX Runtime** with the VitisAI Execution Provider.

**Spatial multithreading** here means each model is dispatched to its own
**dedicated set of AI Engine columns** — the models do not share hardware time-slots;
they occupy separate column ranges and execute **truly in parallel**.

This application runs **ResNet-50 INT8** and **YOLOX-m INT8**, each
compiled with `tp_size=1` and `dp_size=1` (see
[multi_tenancy.md](../../docs/multi_tenancy.md) for details on
these parameters).

Refer to [auto_placement_policy.md](../../docs/auto_placement_policy.md) to understand
why ONNX RT does not support manual column selection and how auto placement
works. Based on the auto placement policy, this application assigns:

| Model              | Columns | Placement |
| ------------------ | ------- | --------- |
| ResNet-50 INT8 | 0 – 3   | Spatial   |
| YOLOX-m INT8   | 4 – 7   | Spatial   |

Both models run concurrently on separate column ranges with no temporal sharing required.

```
VEK385 evaluation kit NPU (36 columns):

  +-----------+-----------+---------------------------+
  | ResNet-50 |  YOLOX-m  |          (free)           |
  |   INT8    |   INT8    |                           |
  | (col 0-3) | (col 4-7) |        (col 8-35)         |
  +-----------+-----------+---------------------------+
```

At a high level, each inference thread follows this pipeline:

  * **Decode:** OpenCV decodes the input JPEG to BGR format.
  * **Preprocess:** VART-X Preprocess APIs normalize the BGR frame into an
    input tensor via the image_processing HLS kernel. **ResNet-50 INT8** uses
    `PANSCAN` resizing; **YOLOX-m INT8** uses `LETTERBOX` resizing.
  * **Infer:** ONNX Runtime submits the tensor to the NPU via the VitisAI
    Execution Provider.
  * **Post-process:** VART-X Post Processing interprets the raw model output
    (top-K classification for **ResNet-50 INT8**, NMS post-processing for **YOLOX-m INT8**).
  * **Overlay:** VART-X Overlay draws predictions on the image and writes the
    result to the output directory (JPEG → JPEG). **ResNet-50 INT8** draws the top
    class label; **YOLOX-m INT8** draws bounding boxes with class labels.

Both threads share the same input image but route inferences through their own
column partitions independently.

**Note:** This application needs the ResNet-50 INT8 and YOLOX-m INT8 models
compiled for the platform. The two per-model JSON configs (paths,
labels, thresholds, decoder parameters) live under
[`json_configs/`](json_configs/README.md).


## Key Features

- **Spatial parallelism** — ResNet-50 INT8 and YOLOX-m INT8 run on
  separate AI Engine column ranges at the same time, with no temporal sharing.
- **Heterogeneous workload** — combines a classification model and an
  object-detection model in the same process, each with its own
  preprocessing (resizer) and post-processing (top-K vs NMS).
- **Two independent threads** — one thread per model; threads start
  concurrently and complete independently.
- **ONNX Runtime with VitisAI EP** — standard ORT API surface, no custom
  runtime calls required.
- **Full ML pipeline** — decode → preprocess → infer → post-process →
  overlay, all within the same process.
- **Flexible iteration count** — **`--runs`** drives repeated inference over
  the same input for throughput measurement.
- **Optional benchmarking** — **`--benchmark`** times all pipeline stages
  without writing output files.
- **Tensor dumps** — **`--dump-all`** writes raw input/output tensors to the
  `output` directory for debugging.


## Usage

```bash
spatial_mt_ml_ort --app-config <config json file> --input-file <path to input file> [--runs count]
```

### Arguments

| Option              | Required  | Default | Description                                                  |
| ------------------- | --------- | ------- | ------------------------------------------------------------ |
| `--app-config`      | Mandatory |         | Path to configuration JSON file (mandatory)                  |
| `--input-file`      | Mandatory |         | Input image file path (mandatory)                            |
| `--runs`            | Optional  |         | Number of iterations to run (optional)                       |
| `--benchmark`       | Optional  |         | Benchmark the metrics of all components (optional)           |
| `--log-level`       | Optional  |         | Application log level to print logs (optional, default is ERROR and WARNING). Accepted log levels: 1 for ERROR, 2 for WARNING, 3 for INFERENCE RESULT, 4 for FIXME, 5 for INFO, 6 for DEBUG. Logs at the provided level and all levels below will be printed. |
| `--help`            | Optional  |         | Prints the supported arguments by the app and exit.          |


### Input

The application processes **JPEG images**.

  * Provide a JPEG image whose dimensions are compatible with **ResNet-50 INT8** and **YOLOX-m INT8** input requirements (the preprocessor resizes the input —
    `PANSCAN` for ResNet-50 INT8, `LETTERBOX` for YOLOX-m INT8).
  * The same image is fed to both model threads on every iteration.
  * A configuration JSON (see **`--app-config`**) selects compiled model paths,
    label files, and pipeline parameters. Refer to the
    [Configuration JSON Guide](#configuration-json-guide) for the schema.


### Output

The application saves processed images to the **`output`** directory. Output
filenames encode the model index and iteration count.

**Filename convention for JPEG input:**

| Scenario | Output Filename(s) |
| -------- | ------------------ |
| Single run | `postproc0_overlay.jpg` · `postproc1_overlay.jpg` |
| Multiple runs | `iter_0_postproc0_overlay.jpg` · `iter_0_postproc1_overlay.jpg` … |

**Note:** When **`--log-level`** is set to `3` or higher, prediction results
are also printed to the console.

**Tensor dumps** (enabled with **`--dump-all`**): raw tensors are written to
`output/` as `.bin` files. Filename components:

  * `infer{N}` — inference instance index (pipeline ID from config)
  * `input` / `out{X}` — input tensor or X-th output tensor
  * `{dtype}` — tensor element type (int8, bf16, fp32, …)
  * `{shape}` — tensor shape (e.g., `1x1000`)
  * `{tensor_name}` — tensor name from the ONNX model (`/` replaced by `-`)

Example filenames — single iteration, two pipelines (ResNet-50 INT8 +
YOLOX-m INT8). Pipeline 0 (ResNet-50 INT8) emits a single classification
logits tensor; pipeline 1 (YOLOX-m INT8) emits the detection output tensor(s)
after NMS post-processing:

```
infer0_input-ifm_name.bin
infer0_out0-fp32_1x1000_ofm_name.bin
infer1_input-ifm_name.bin
infer1_out0-<dtype>_<shape>_ofm_name.bin
```

Multi-iteration (**`--runs 3`**):

```
iter0_infer0_input-ifm_name.bin
iter0_infer0_out0-fp32_1x1000_ofm_name.bin
iter1_infer0_input-ifm_name.bin
iter1_infer0_out0-fp32_1x1000_ofm_name.bin
iter2_infer0_input-ifm_name.bin
iter2_infer0_out0-fp32_1x1000_ofm_name.bin
```

Where `ifm_name` and `ofm_name` are placeholders for the actual tensor names
from the ONNX model.

**Note:** Enabling **`--benchmark`** suppresses all output file writes.


## Build

1. Source the Vitis AI SDK for Versal AI Edge Series Gen 2 environment:

```bash
source /path/to/sdk/environment-setup-cortexa72-cortexa53-amd-linux
```

2. Build the application:

```bash
make all
```

The resulting binary is `spatial_mt_ml_ort`.

3. To clean build artifacts:

```bash
make clean
```

## Running on the Board

### Prerequisites

Before running the commands below, finish board setup for your platform, program the required PL and AI Engine overlay on the board, and configure the runtime environment for your image (including `LD_LIBRARY_PATH`).

1. Copy the binary, config files, and labels to the target board:

```bash
scp spatial_mt_ml_ort <TARGET>:/usr/bin/
scp -r json_configs <TARGET>:/etc/vai/spatial_mt_ml_ort/
scp -r labels <TARGET>:/etc/vai/spatial_mt_ml_ort/
```

2. Run the application on the board with the default config:

```bash
spatial_mt_ml_ort --app-config /etc/vai/spatial_mt_ml_ort/json_configs/spatial_mt_r50_yoloxm.json \
  --input-file /etc/vai/models/yolox_m_int8/data/detections.jpg --log-level 3
```

3. Optionally benchmark for 100 runs:

```bash
spatial_mt_ml_ort --app-config /etc/vai/spatial_mt_ml_ort/json_configs/spatial_mt_r50_yoloxm.json \
  --input-file /etc/vai/models/yolox_m_int8/data/detections.jpg \
  --benchmark --runs 100
```

4. To inspect actual AI Engine column assignments while the application is running,
   execute the following command in a parallel terminal:

```bash
xrt-smi examine --device 0 --report aie-partitions
```

   Example output while `spatial_mt_ml_ort` is running:

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
      |1089                |1          |283         |0           |0    |Normal   |
      |N/A                 |Active     |283         |0           |     |1        |
      |54848 KB            |N/A        |            |            |     |1        |
      |                    |           |            |            |     |2000     |
      |--------------------|-----------|------------|------------|-----|---------|
  Partition Index   : 1
    Columns: [4, 5, 6, 7]
    HW Contexts:
      |PID                 |Ctx ID     |Submissions |Migrations  |Err  |Priority |
      |Process Name        |Status     |Completions |Suspensions |     |GOPS     |
      |Memory Usage        |Instr BO   |            |            |     |FPS      |
      |                    |           |            |            |     |Latency  |
      |====================|===========|============|============|=====|=========|
      |1089                |2          |283         |0           |0    |Normal   |
      |N/A                 |Active     |283         |0           |     |1        |
      |54848 KB            |N/A        |            |            |     |1        |
      |                    |           |            |            |     |2000     |
      |--------------------|-----------|------------|------------|-----|---------|
```

   The output confirms two active models under the same PID (1089) — Model 0 on
   columns [0–3] (ResNet-50 INT8, Ctx ID 1) and Model 1 on columns [4–7]
   (YOLOX-m INT8, Ctx ID 2). Both run concurrently within the same process,
   which is the expected behavior for spatial multithreading. If the models
   appear on different column ranges, another process is already occupying
   those columns and auto placement has adjusted accordingly.


## Configuration JSON Guide

The **`--app-config`** JSON controls model paths, label files, preprocessing
parameters, and pipeline layout. Refer to
[json_configs/README.md](json_configs/README.md) for the full schema and field
descriptions.


## Application Flow

### Spatial column assignment

The application launches two threads, one per model. ONNX Runtime places each
model on its own AI Engine column partition automatically (see
[auto_placement_policy.md](../../docs/auto_placement_policy.md)).

  * **ResNet-50 INT8** is placed on columns **0 – 3**.
  * **YOLOX-m INT8** is placed on columns **4 – 7**.

Because the column ranges do not overlap, both models execute simultaneously
with no contention for AI Engine resources.

### Per-thread pipeline

Each thread independently runs the full decode → preprocess → infer →
post-process → overlay pipeline for every iteration. The two threads share the
same host image buffer (read-only) but maintain separate ONNX Runtime sessions
and tensor buffers.
