# VART Async Infer Application

<!--
## Copyright and license statement

Copyright (C) 2025 - 2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->

Note: Example model names, JSON files, and commands are for reference only. Modify them for your compiled models and board.

The VART Async Infer Application is an **asynchronous, multi-frame** inference
sample built on the AMD VART (Vitis AI Runtime) framework for Versal AI Edge
Series Gen 2 targets.

**Asynchronous execution** here means the CPU can **submit** the next inference
(**`execute_async`**) without waiting for the previous one to finish on the NPU.
Completion is pulled later in **FIFO** order (**`wait_job`** / **`Runner::wait`**), so
several jobs can be **in progress** at once—each bound to its own input/output tensor
buffers. That lets **submission and completion overlap**: while one batch is still
running on hardware, the host can stage inputs and queue another batch.

At a high level, multi-frame inference is structured as follows:

  * **IFM load:** Application reads **at most `input_batch_size`** rows from the **start**.
    If there are **extra rows** in the input file, they are ignored.
    In case there are **fewer than `input_batch_size`** rows (≥1), the application **zero-fills** the rest of the batch.
    That buffer is **re-used** for each iteration. **`--num-iteration`** sets how many passes run from it ([Arguments](#arguments), [Input](#input)).
    **`--dry-run`** skips the file read and fills **`input_batch_size`** random rows into the buffer.
  * Application maintains **`kNumConcurrentJobs` parallel job slots** (Default: **`2`**),
    each with its own preallocated input/output buffers.
  * Submitting work with **`execute_async`** and completing jobs in **FIFO**
    order via **`wait_job`**. When OFM file writes are enabled, OFM binaries are
    written **only for the last completed frame**.

The **`main`** scheduling loop starts the pipeline with up to **`kNumConcurrentJobs`**
async submissions, then for each additional frame alternates **`wait_job`** and
**`execute_async`** so completion overlaps with new submissions. Finally it
issues **`kNumConcurrentJobs`** calls to **`wait_job`** to drain any remaining
work inside the queue.

**Overview of the async execution flow:**

```
    +--+-------------+---------------------------+
    | Populate input data in all buffer slots    |
    +--+-----------------------------------------+
       |
       v
    +--+-------------+---------------------------+
    | execute_async                              |
    | (till job queue is full)                   | <-+
    +--+-----------------------------------------+   |
       |                                             |
       +--------------- loop ------------------------+
       |
       v
    +--+-------------+---------------------------+
    | wait (FIFO)                                |
    | execute_async                              |
    | (till all frames are submitted to queue)   | <-+
    +--+-----------------------------------------+   |
       |                                             |
       +--------------- loop ------------------------+
       |
       v
    +--+-------------+---------------------------+
    | wait                                       | <-+
    | (Till all jobs are completed)              |   |
    +--+-----------------------------------------+   |
       |                                             |
       +--------------- loop ------------------------+
       |
       v
     done
```


## Key Features

- **Higher throughput** — the NPU can work on one batch while the CPU queues the next.
- **Better accelerator utilization** when each inference takes noticeable time.
- **Overlap of host work with device work** so wall-clock time is not dominated by idle waits between **submit** and **finish**.
- **Multiple concurrent async jobs** (**`kNumConcurrentJobs`** slots, default **`2`**) with dedicated input/output tensor buffers per slot.
- **FIFO completion** via **`wait_job`** / **`Runner::wait`**, paired with **`execute_async`** submissions.
- Optional **`--dry-run`** (random IFM, no file I/O) and **`--benchmark`** (timing, no OFM file writes).


## Usage

```bash
vart_infer_async --model-path <model> --input-binary <ifm binary> [-n <num-iteration>] [-d] [--benchmark] [--input-tensor-type <HW|CPU>] [--output-tensor-type <HW|CPU>] [-h]
```

Positionals are accepted in the same order: `vart_infer_async <model> <ifm binary> ...`.

### Arguments

| Option              | Required  | Default | Description                                                  |
| ------------------- | --------- | ------- | ------------------------------------------------------------ |
| `--model-path`      | Mandatory |         | Path to the compiled model: a compiled `.rai` file or a compiled-model cache directory. Also accepted as the first positional argument. |
| `--input-binary` / `input_binary` | Mandatory |         | Input IFM binary (mandatory unless `--dry-run`). Also accepted as the second positional argument. |
| `-n, --num-iteration`  | Optional  | `10`    | Number of times the run replays inference using the one batch of IFM rows loaded from disk; may be raised to `kNumConcurrentJobs` to keep the async pipeline full (optional, default: `10`) |
| `-d, --dry-run`        | Optional  |         | Random IFM fill; no IFM read or OFM file writes (optional)   |
| `--benchmark`          | Optional  |         | Time async and sync passes; no OFM file writes (optional)    |
| `--input-tensor-type`  | Conditional | `HW`    | Input boundary tensor type: `HW` (hardware-native) or `CPU` (ONNX format). Optional for fully-NPU models; **mandatory** and must be `CPU` when the model has a CPU subgraph at its input |
| `--output-tensor-type` | Conditional | `HW`    | Output boundary tensor type: `HW` (hardware-native) or `CPU` (ONNX format). Optional for fully-NPU models; **mandatory** and must be `CPU` when the model has a CPU subgraph at its output |
| `-h, --help`           | Optional  |         | Print help and exit                                          |

Print help:

```
vart_infer_async -h
```


### Input

**Model and IFM**

  * Use a **Vitis AI-compiled model cache** whose **input batch size** and tensor layout match the IFM you generate. This example supports **only a single input tensor**.
  * Provide a **raw IFM binary** containing **at least one complete sample frame**. Trailing bytes shorter than one frame are ignored (see layout below).

**Input binary layout**

This example supports models with only single input tensor. Each **sample row** in IFM binary (one logical sample for **batch index** `b`) occupies size needed for one input tensor.
One **logical async frame** (one **`execute_async`** submission) consumes **`input_batch_size`**
contiguous sample rows when building the HW batch. If input binary size is less than input batch size then rest of data is zero-filled.

**`--dry-run`** skips file I/O and fills **`kDefaultDryRunFrameCount × input_batch_size`** rows
with random bytes (**`load_input_random`**); see **`kDefaultDryRunFrameCount`** in **`vart_infer_async.hpp`**.

### Output

When file writes are enabled (normal run, not **`--dry-run`** /
**`--benchmark`**), **`write_outputs_for_frame`** emits **`output_f<F>_<T>.bin`**
per output tensor **`T`** for the **last completed frame** **`F`** only (async
path); each file concatenates **all batch rows** for that tensor. See
**`write_outputs_for_frame`** in **`main.cpp`** for naming when **`num_iteration > 1`**.


## Build

1. Source the Vitis AI SDK for Versal AI Edge Series Gen 2 environment:

```bash
source /path/to/sdk/environment-setup-cortexa72-cortexa53-amd-linux
```

2. Build the application:

```bash
make all
```

The resulting binary is `vart_infer_async`.

3. To clean build artifacts:

```bash
make clean
```

## Running on the Board

### Prerequisites

Before running the commands below, finish board setup for your platform, program the required PL and AI Engine overlay on the board, and configure the runtime environment for your image (including `LD_LIBRARY_PATH`).

1. Copy the application binary, compiled model (`.rai` file or cache directory), and IFM binary to the
   target (or mount a workspace that contains them).

2. Set up the board environment:

```bash
export LD_LIBRARY_PATH=/usr/lib/python3.12/site-packages/voe/lib:/usr/lib/python3.12/site-packages/flexmlrt/lib:/usr/lib/python3.12/site-packages/onnxruntime/capi:/usr/lib/python3.12/site-packages/vart_ml/lib:/usr/lib/python3.12/site-packages/vart_x/lib
```

3. Run the application. **Required:** compiled model path (`.rai` or cache directory), then input IFM
   binary (positional order), unless **`--dry-run`** is set (IFM path not used).

```bash
vart_infer_async --model-path /etc/vai/models/resnet50_int8/resnet50_int8.rai --input-binary /path/to/multi_frame_ifm.bin
```

4. **Dry run** — test configuration without I/O overhead:

```bash
vart_infer_async --model-path /etc/vai/models/resnet50_int8/resnet50_int8.rai --dry-run
```

5. Optionally pass **`-n` / `--num-iteration`** to set how many times the **single loaded batch**
   of IFM rows is driven through the runner (see [Arguments](#arguments) and [Input](#input)).


## Application Flow

### Async pipeline

  * After inputs are copied into each concurrent job slot, the app **starts several inferences at once** (up to the number of parallel slots).
  * It then **repeats**: wait for the **oldest** finished job (**FIFO**), recycle that slot, and start the next inference—so device work and new submissions **overlap**.
  * Finally it **drains** the queue until every job has finished.
  * On the **sync** benchmark path, the total number of logical frames is **(rows loaded from disk) × `--num-iteration`** (rows loaded are at most **`input_batch_size`**; see [Input](#input)).
  * On the **async** path, **`--num-iteration`** is how many submissions are issued over the **initially staged** buffers.

### Submissions and completions

  * A submission **does not** reload IFM data from host memory; it only tells the runner to run on the tensors already filled.
  * If the runner is temporarily busy, the app **waits briefly and retries**.
  * Completed jobs are always taken **in order**.
  * Optional OFM file output happens **only for the last** completed async frame when file writes are on.


## Executing Models Containing CPU Subgraphs

A Vitis AI–compiled model is not always executed entirely on the NPU. Some
operations may be unsupported on (or better suited to) the CPU. The compiler
places those operations in **CPU subgraphs** that run on the host CPU, while the
rest of the model runs on the NPU. A single compiled model can therefore have a
CPU subgraph at its **input** boundary, its **output** boundary, both, or
neither.

VART-ML executes these CPU subgraphs **internally, within the same
`vart::Runner`** — no separate runner and no extra `execute_async` call are
required. The only requirement is that the application select the correct tensor
*type* for each direction when the runner is created.

### Tensor types: `CPU` vs `HW`

For each direction (input and output) the runner can expose the tensors in one
of two formats:

| Type  | Format                                | Typical data type / layout |
| ----- | ------------------------------------- | -------------------------- |
| `HW`  | Hardware-native (NPU-accepted) format | e.g. BF16, HCWNC4 layout   |
| `CPU` | Standard ONNX format                  | e.g. FP32, NCHW layout     |

Rules:

- A boundary that is a **CPU subgraph** has **no HW tensor type** for that
  direction — it must use the `CPU` type. Creating the runner with `HW` for such
  a direction **fails**.
- A boundary that is an **NPU (HW) subgraph** can use **either** `CPU` or `HW`.

### Selecting the tensor type

Two command-line flags select the type independently per direction:

| Flag                    | Default | Values       | Description                               |
| ----------------------- | ------- | ------------ | ----------------------------------------- |
| `--input-tensor-type`   | `HW`    | `CPU` / `HW` | Type used for the model's input tensors.  |
| `--output-tensor-type`  | `HW`    | `CPU` / `HW` | Type used for the model's output tensors. |

> **The tensor type is not optional for models with CPU subgraphs.** For any
> boundary that is a CPU subgraph you **must** pass the corresponding flag with
> the value `CPU` (`--input-tensor-type CPU` and/or `--output-tensor-type CPU`).
> The default `HW` only applies to boundaries that run fully on the NPU; using
> `HW` for a CPU-subgraph boundary causes runner creation to fail.

Behaviour:

- **Field not specified** → defaults to `HW`. This is valid **only** when that boundary is a
  fully-NPU (HW) subgraph.
- **`CPU` / `HW`** (case-insensitive) → used as given.
- **Any other value** → the application prints a diagnostic and exits.

The selected types are forwarded to the runner (`input_tensor_type` /
`output_tensor_type` options) and are also used to query the tensor metadata
(`get_tensors_info`), so IFM/OFM buffer sizes match the selected format.

> **Important:** When a direction uses the `CPU` type, the corresponding IFM /
> OFM binary must be in standard **ONNX format** (matching the CPU tensor shapes
> and data types), not the HW-native layout.

### Examples

Model with a CPU subgraph at its **input** boundary (NPU output):

```bash
vart_infer_async --model-path /etc/vai/models/modelA/modelA.rai \
    --input-binary /etc/vai/models/modelA/data/ifm_input_fp32_1x3x224x224.bin \
    --input-tensor-type CPU
```

Model with CPU subgraphs at **both** boundaries:

```bash
vart_infer_async --model-path /etc/vai/models/modelB/modelB.rai \
    --input-binary /etc/vai/models/modelB/data/ifm_input_fp32_1x3x224x224.bin \
    --input-tensor-type CPU --output-tensor-type CPU
```

