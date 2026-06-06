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

  * **IFM load:** Application reads **at most `batch_size`** rows from the **start**.
    If there are **extra rows** in the input file, they are ignored.
    In case there are **fewer than `batch_size`** rows (≥1), the application **zero-fills** the rest of the batch.
    That buffer is **re-used** for each iteration. **`--num-iteration`** sets how many passes run from it ([Arguments](#arguments), [Input](#input)).
    **`--dry-run`** skips the file read and fills **`batch_size`** random rows into the buffer.
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
vart_infer_async <model dir> <ifm binary> [-n <num-iteration>] [-d] [--benchmark] [-h]
```

### Arguments

| Option              | Required  | Default | Description                                                  |
| ------------------- | --------- | ------- | ------------------------------------------------------------ |
| `--model-path`      | Mandatory |         | Path to the compiled model: a compiled `.rai` file or a compiled-model cache directory (mandatory) |
| `--input-binary`       | Mandatory |         | Input IFM binary (mandatory unless `--dry-run`)              |
| `-n, --num-iteration`  | Optional  | `10`    | Number of times the run replays inference using the one batch of IFM rows loaded from disk; may be raised to `kNumConcurrentJobs` to keep the async pipeline full (optional, default: `10`) |
| `-d, --dry-run`        | Optional  |         | Random IFM fill; no IFM read or OFM file writes (optional)   |
| `--benchmark`          | Optional  |         | Time async and sync passes; no OFM file writes (optional)    |
| `-h, --help`           | Optional  |         | Print help and exit                                          |

Print help:

```
vart_infer_async -h
```


### Input

**Model and IFM**

  * Use a **Vitis AI-compiled model cache** whose **batch size** and tensor layout match the IFM you generate. This example supports **only a single input tensor**.
  * Provide a **raw IFM binary** containing **at least one complete sample frame**. Trailing bytes shorter than one frame are ignored (see layout below).

**Input binary layout**

This example supports models with only single input tensor. Each **sample row** in IFM binary (one logical sample for **batch index** `b`) occupies size needed for one input tensor.
One **logical async frame** (one **`execute_async`** submission) consumes **`batch_size`**
contiguous sample rows when building the HW batch. If input binary size is less than batch size then rest of data is zero-filled.

**`--dry-run`** skips file I/O and fills **`kDefaultDryRunFrameCount × batch_size`** rows
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
export LD_LIBRARY_PATH=/usr/lib/python3.12/site-packages/voe/lib:/usr/lib/python3.12/site-packages/flexmlrt/lib:/usr/lib/python3.12/site-packages/onnxruntime/capi
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
  * On the **sync** benchmark path, the total number of logical frames is **(rows loaded from disk) × `--num-iteration`** (rows loaded are at most **`batch_size`**; see [Input](#input)).
  * On the **async** path, **`--num-iteration`** is how many submissions are issued over the **initially staged** buffers.

### Submissions and completions

  * A submission **does not** reload IFM data from host memory; it only tells the runner to run on the tensors already filled.
  * If the runner is temporarily busy, the app **waits briefly and retries**.
  * Completed jobs are always taken **in order**.
  * Optional OFM file output happens **only for the last** completed async frame when file writes are on.
