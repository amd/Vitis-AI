# VART Multi-Tenancy Application

<!--
## Copyright and license statement

Copyright (C) 2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->

Note: Example model names, JSON files, and commands are for reference only. Modify them for your compiled models and board.

The VART Multi-Tenancy Application is a generic, multi-model inference runner
built on the AMD VART (Vitis AI Runtime) framework for Versal AI Edge Series Gen 2
devices. It demonstrates **multi-tenant** use of the NPU: several Vitis AI-compiled
models share the same device, each running concurrently in its own thread.

The application reads a JSON configuration file describing one or more models
and schedules them with one of three NPU column placement policies:

- **Spatial sharing** — each model owns a disjoint set of NPU columns and runs
  in parallel on dedicated hardware.
- **Temporal sharing** — multiple models map to the same NPU columns; the
  hardware time-multiplexes between them.
- **Combined** — a mix of spatial and temporal placements in one run.

---

## Key Features

- **Multi-Tenant NPU Execution**: Run multiple Vitis AI-compiled models concurrently,
  each in its own `std::thread`, with flexible NPU column assignment.
- **Three Column Placement Policies**: Spatial (exclusive columns per model),
  temporal (time-multiplexed on shared columns), or combined spatial + temporal
  in a single deployment.
- **Batched Model Support**: Handles single-frame and multi-frame IFM files with
  automatic partial-batch detection and warnings.
- **Dry-Run Mode**: Run inference without real input data — IFM buffers are
  filled with random bytes, IFM files are not read from disk, and OFM results
  are not written. This allows quick validation of the JSON configuration and
  NPU column placement, as well as latency benchmarking of the inference
  pipeline itself without requiring actual model input binaries.
- **Configurable Verbosity**: Adjustable logging levels (0=errors, 1=+warnings,
  2=+info) for development and debugging.
- **Column Conflict Detection**: Validates NPU column assignments across all
  models before inference and aborts with diagnostics on conflicts.

## Usage

```
vart_multi_tenancy -c <config json file> [options]
```

### Arguments

| Option              | Required  | Default | Description                                                  |
| ------------------- | --------- | ------- | ------------------------------------------------------------ |
| `-c, --config`      | Mandatory |         | Path to the JSON configuration file                          |
| `-r, --runs`        | Optional  | `1`     | Number of inference iterations per model with the same IFM   |
| `-b, --benchmark`   | Optional  | off     | Print performance summary table (intended for benchmark runs) |
| `-d, --dry-run`     | Optional  | off     | Dry run: fill IFMs with random bytes; skip IFM/OFM file I/O  |
| `-l, --log-level`   | Optional  | `2`     | Log level (0=errors, 1=+warnings, 2=+info)                   |
| `-h, --help`        | Optional  |         | Print help and exit                                          |

### Input

Each model requires one or more IFM (Input Feature Map) binary files, specified
via the `ifm_node_file_map` field in the JSON configuration. Each IFM file must
contain data for at least one complete frame — a "frame" is one full input tensor
worth of data (e.g. for a model expecting a `1×3×224×224` bf16 input, one frame =
`1×3×224×224×2` bytes).

For models compiled with a batch size greater than 1, IFM files may contain
multiple frames concatenated end-to-end:

- **Partial batch (fewer frames than batch size):** Only the available frames
  are loaded and a `[WARN]` is logged. Inference executes with the partial batch.
- **More frames than batch size:** Only the first `batch_size` frames are
  loaded. The remaining frames are ignored and a `[WARN]` is logged.

> **Note 1:** When multiple iterations are specified (`-r N`), the same input
> frame(s) are reused for each iteration. This application is intended for
> demonstration purposes; multiple iterations are designed for performance
> benchmarking rather than for processing different inputs.

> **Note 2:** Each model input tensor must be mapped to an IFM file in the JSON
> config using `ifm_node_file_map`, where the key is the model's input node name.
> If unsure of node names, provide any name—the application will validate during
> initialization and print a diagnostic table for each model showing:
>
> - **Configured IFM Nodes:** The node names from your JSON config
> - **Expected Input Nodes:** The actual model input node names with data types and shapes
>
> The diagnostic is saved to `ifm_node_data.txt` in the current directory
> for easy reference when correcting the mapping.

### Output

OFM (Output Feature Map) results are written as one binary file per OFM tensor
node (`<node_name>_<shape>_<datatype>.bin`). Each model's outputs are stored in a
dedicated subdirectory within `ofm_dir`, named `ofm_model_1/`, `ofm_model_2/`,
etc. (numbered by their order in the JSON config, starting from 1). When
`batch_size > 1`, all batch frames are concatenated into the same file per OFM
node. If `ofm_dir` is not specified in the JSON config, it defaults to the
current working directory (`"./"`).

Example output directory structure for a 2-model config with `"ofm_dir": "./outputs"`:

```
./outputs/
├── ofm_model_1/
│   └── output_QuantizeLinear_Output_1x1000_int8.bin
└── ofm_model_2/
    └── output_QuantizeLinear_Output_1x1000_int8.bin
```

## Build

1. Source the Vitis AI SDK for Versal AI Edge Series Gen 2 environment:

```bash
source /path/to/sdk/environment-setup-cortexa72-cortexa53-amd-linux
```

2. Build the application:

```bash
make all
```

The resulting binary is `vart_multi_tenancy`.

3. To clean build artifacts:

```bash
make clean
```

## Running on the Board

### Prerequisites

Before running the commands below, finish board setup for your platform, program the required PL and AI Engine overlay on the board, and configure the runtime environment for your image (including `LD_LIBRARY_PATH`).

1. Set up the board environment:

```
export LD_LIBRARY_PATH=/usr/lib/python3.12/site-packages/voe/lib:/usr/lib/python3.12/site-packages/flexmlrt/lib:/usr/lib/python3.12/site-packages/onnxruntime/capi
```

2. The following pre-built configurations are available on the board and can be
   run directly:

**Temporal sharing policy** - Run precompiled models with temporal sharing policy
```bash
vart_multi_tenancy -c /etc/vai/vart_multi_tenancy/json_configs/temporal_config.json
```

**Spatial sharing policy** - Run precompiled models with spatial sharing policy
```bash
vart_multi_tenancy -c /etc/vai/vart_multi_tenancy/json_configs/spatial_config.json
```

**Combined temporal + spatial sharing policy** - Run precompiled models with combined temporal and spatial sharing policy
```bash
vart_multi_tenancy -c /etc/vai/vart_multi_tenancy/json_configs/temporal_spatial_config.json
```

3. To run multiple inference iterations with the same IFM

```bash
vart_multi_tenancy -c /etc/vai/vart_multi_tenancy/json_configs/spatial_config.json -r 10
```

4. To print performance summary for benchmark runs

```bash
vart_multi_tenancy -c /etc/vai/vart_multi_tenancy/json_configs/spatial_config.json -r 10 -b
```

5. To run with a custom compiled model, copy or mount the working directory on
   the target board. Ensure the compiled model caches and IFM binaries are
   available at the paths specified in the JSON config.

```bash
vart_multi_tenancy -c config.json
```

6. To see help and available options:

```bash
vart_multi_tenancy -h
```

## Configuration JSON Guide

For details about the JSON configuration schema, please refer to [json_configs/README.md](json_configs/README.md).

---

## How `start_column` and `aie_columns_sharing` are Configured

The `start_column` and `aie_columns_sharing` options are read from the JSON
configuration file and passed to the `vart::Runner` as shown in the code snippet
below:

```cpp
std::unordered_map<std::string, std::any> options = {
    {"input_tensor_type",
     std::string(m_input_tensor_type == vart::TensorType::CPU ? "CPU" : "HW")},
    {"output_tensor_type",
     std::string(m_output_tensor_type == vart::TensorType::CPU ? "CPU" : "HW")},
};

// Pass start_column to the runner
if (cfg.is_start_column_provided) {
  options["start_column"] = cfg.start_column;
}
// Pass aie_columns_sharing to the runner
if (cfg.is_columns_sharing_provided) {
  options["aie_columns_sharing"] = cfg.aie_columns_sharing;
}

m_runner = vart::RunnerFactory::create_runner(
    vart::RunnerType::VAIML, m_model_cache_dir, options);
```

Key points:

- **`start_column`** — `uint32_t`. Selects the
  first NPU column the model is placed on.
- **`aie_columns_sharing`** — `bool`. `true` = shared/temporal (column block
  is time-multiplexed with other models that target the same columns);
  `false` = exclusive/spatial (column block is owned by this model only).
- **Conflict detection** — `utils::load_config()` cross-checks all model
  entries: if any two models have overlapping column ranges and at least one
  sets `aie_columns_sharing=false`, the application aborts with a
  diagnostic before any runner is created.


## NPU Column Sharing Modes

The NPU on Versal AI Edge Series Gen 2 has 36 columns. The number of columns a model
occupies is determined by the overlay size used during compilation. Overlay size
can be configured by setting `tp_size` and `dp_size`. For more information
please refer to [multi_tenancy.md](../../docs/multi_tenancy.md).

### Single Model

A single model uses all the NPU columns determined by the overlay size used
during compilation. No column or sharing configuration is needed.

```json
[
  {
    "model_cache_path": "/opt/models/resnet50/cache",
    "ifm_node_file_map": { "images_QuantizeLinear_Output": "/data/resnet50/inputs/image.bin" },
    "ofm_dir": "/data/resnet50/outputs"
  }
]
```

```
Column layout:  Columns 0-3 : Model_1 (all columns, default)
```

### Temporal Sharing

In temporal sharing, multiple models are mapped to the **same** set of NPU
columns. Instead of running simultaneously, the models take turns — the NPU
time-multiplexes between them so that only one model executes on those columns
at any given moment. This is configured by setting `start_column` to the
**same** value and `aie_columns_sharing` to **`true`** for every model in the
group.

Temporal sharing is useful when the total column demand of all models exceeds
the available hardware, or when models do not need to run at the same time.
Since models share the same physical columns, there is a context-switch
overhead each time the NPU swaps from one model to another.

The diagram below illustrates temporal sharing. During **time slot t1**,
Model A occupies columns 0–3 (shown in green) while all other columns remain
idle (grey). When Model A finishes, the NPU swaps to **time slot t2** and
Model B takes over the **same** columns 0–3 (shown in purple). The dashed
line between the two time slots highlights that both models target identical
columns.

<p align="center">
  <img src="images/temporal_sharing.svg" alt="Temporal Sharing diagram" width="90%"/>
</p>

Key settings (highlighted):

- **`start_column`** — must be **identical** across all models that share the
  block (e.g. all set to `0` to share columns 0–3).
- **`aie_columns_sharing`** — must be **`true`** on every sharing model.

```json
[
  {
    "model_cache_path": "/opt/models/model_a/cache",
    "start_column": 0,
    "aie_columns_sharing": true,
    "ifm_node_file_map": { "input_node": "/data/model_a/inputs/input.bin" },
    "ofm_dir": "/data/model_a/outputs"
  },
  {
    "model_cache_path": "/opt/models/model_b/cache",
    "start_column": 0,
    "aie_columns_sharing": true,
    "ifm_node_file_map": { "input_node": "/data/model_b/inputs/input.bin" },
    "ofm_dir": "/data/model_b/outputs"
  }
]
```

```
Column layout:  Column 0 : Model_1, Model_2 (shared / temporal)
```

### Spatial Sharing

In spatial sharing, each model is assigned its **own separate** set of NPU
columns. Because the column ranges do not overlap, all models can execute
truly in parallel on dedicated hardware with no context-switch overhead.
This is configured by giving each model a **different** `start_column` value
and setting `aie_columns_sharing` to **`false`**.

Spatial sharing delivers the best throughput when multiple models must run
concurrently, but it requires that the combined column footprint of all
models fits within the 36 available NPU columns. You can compile the model
with a reduced column footprint (e.g. 1x4x4) by setting `dp_size = 1` and
`tp_size = 1` in the `vitisai_config.json` file.

The diagram below illustrates spatial sharing. Model A occupies columns 0–3
(green) while Model B simultaneously occupies columns 4–7 (blue). The
remaining columns (8–35) are unused and shown in grey. Both models run at
the same time on their dedicated column slices.

<p align="center">
  <img src="images/spatial_sharing.svg" alt="Spatial Sharing diagram" width="90%"/>
</p>

Each model owns a **separate** set of NPU columns with
`aie_columns_sharing = false`. Models run truly in parallel on dedicated
hardware — no time-sharing overhead.

> **Note 1:** When running models in spatial mode, ensure that there is
> enough column space available for all models. You can compile the model
> with a reduced column footprint (e.g. 1x4x4) by setting `dp_size = 1` and
> `tp_size = 1` in the `vitisai_config.json` file.
>
> Example `vitisai_config.json` for a **1x4x4** overlay:
>
> ```json
> {
>   "passes": [
>     {
>       "name": "init",
>       "plugin": "vaip-pass_init"
>     },
>     {
>       "name": "vaiml_partition",
>       "plugin": "vaip-pass_vaiml_partition",
>       "vaiml_config": {
>         "keep_outputs": true,
>         "device": "ve2-xc2ve3858",
>         "logging_level": "info",
>         "dp_size": 1,
>         "tp_size": 1
>       }
>     }
>   ],
>   "target": "VAIML",
>   "targets": [
>     {
>       "name": "VAIML",
>       "pass": [
>         "init",
>         "vaiml_partition"
>       ]
>     }
>   ]
> }
> ```

Key settings (highlighted):

- **`start_column`** — must be **distinct** per model (e.g. `0`, `4`, `8`,
  ...). Two models must not map to overlapping columns.
- **`aie_columns_sharing`** — set to **`false`** for exclusive column
  reservation (no swapping with other models).

> **Note 2:** `aie_columns_sharing` does not need to be `false` for spatial
> sharing. Models can still be spatially shared by controlling `start_column`
> alone. Setting `aie_columns_sharing` to `false` exclusively reserves those
> columns for that model, preventing any swapping or time-multiplexing on
> those columns.

```json
[
  {
    "model_cache_path": "/opt/models/model_a/cache",
    "start_column": 0,
    "aie_columns_sharing": false,
    "ifm_node_file_map": { "input_node": "/data/model_a/inputs/input.bin" },
    "ofm_dir": "/data/model_a/outputs"
  },
  {
    "model_cache_path": "/opt/models/model_b/cache",
    "start_column": 4,
    "aie_columns_sharing": false,
    "ifm_node_file_map": { "input_node": "/data/model_b/inputs/input.bin" },
    "ofm_dir": "/data/model_b/outputs"
  }
]
```

```
Column layout:  Column 0 : Model_1 (exclusive)
                Column 4 : Model_2 (exclusive)
```

> **Note:** If two models with overlapping columns are both set to exclusive,
> or one exclusive and one shared, the application will print an error and exit
> before inference.

### Combined Spatial + Temporal

Temporal and spatial sharing can coexist in a single deployment. A subset of
models is configured to time-multiplex on a shared column range (temporal),
while one or more other models are each assigned their own exclusive column
range (spatial). In the example below, Model_1 and Model_2 share columns 0–3
with `aie_columns_sharing = true`, while Model_3 exclusively owns columns 4–7
with `aie_columns_sharing = false`.

The diagram below illustrates the combined mode. Columns 0–3 are shared
temporally by Model A (green) and Model B (purple) — they take turns on the
same hardware. Columns 4–7 are exclusively owned by Model C (blue), which
runs in parallel on dedicated hardware. The remaining columns are unused
(grey).

<p align="center">
  <img src="images/temporal_spatial_sharing.svg" alt="Combined Spatial + Temporal Sharing diagram" width="90%"/>
</p>

Key settings (highlighted):

- **`start_column`** — use the **same** value across all models in a temporal
  group, and a **different**, non-overlapping value for each spatial model.
- **`aie_columns_sharing`** — set to **`true`** for every model in a temporal
  group, and **`false`** for spatial (exclusive) models.

```json
[
  {
    "model_cache_path": "/opt/models/model_a/cache",
    "start_column": 0,
    "aie_columns_sharing": true,
    "ifm_node_file_map": { "input_node": "/data/model_a/inputs/input.bin" },
    "ofm_dir": "/data/model_a/outputs"
  },
  {
    "model_cache_path": "/opt/models/model_b/cache",
    "start_column": 0,
    "aie_columns_sharing": true,
    "ifm_node_file_map": { "input_node": "/data/model_b/inputs/input.bin" },
    "ofm_dir": "/data/model_b/outputs"
  },
  {
    "model_cache_path": "/opt/models/model_c/cache",
    "start_column": 4,
    "aie_columns_sharing": false,
    "ifm_node_file_map": { "input_node": "/data/model_c/inputs/input.bin" },
    "ofm_dir": "/data/model_c/outputs"
  }
]
```

```
Column layout:  Column 0 : Model_1, Model_2 (shared / temporal)
                Column 4 : Model_3 (exclusive / spatial)
```


### Verifying AI Engine Column Allocation

During inference execution you can cross-check which AI Engine columns each model was
assigned to by using the `xrt-smi` command on the board:

```
xrt-smi examine --device 0 --report aie-partitions
```

> **Note:** You must run `xrt-smi examine` **during** inference execution.
> If the command is executed after inference completes, the partitions will
> have been released and no report will be shown. Open a second terminal on
> the board and run the command while inference is in progress.

#### Temporal sharing

Both models share the **same** partition (same column set). Two HW Contexts
appear under a single Partition Index because both models time-multiplex on
columns 0–3:

```
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

#### Spatial sharing

Each model appears under its **own** Partition Index with a disjoint column
set. The two partitions run in parallel on separate hardware.

```
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
      |1179                |1          |870         |0           |0    |Realtime |
      |N/A                 |Idle       |869         |0           |     |1        |
      |66 MB               |N/A        |            |            |     |1        |
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
      |1179                |2          |1000        |0           |0    |Realtime |
      |N/A                 |Active     |1000        |0           |     |1        |
      |66 MB               |N/A        |            |            |     |1        |
      |                    |           |            |            |     |2000     |
```

#### Combined spatial + temporal sharing

The output shows **two** partitions. Partition 0 (columns 0–3) has two HW
Contexts sharing the same columns temporally, while Partition 1 (columns 4–7)
has a single HW Context running exclusively.

```
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
      |1213                |1          |39          |0           |0    |Normal   |
      |N/A                 |Idle       |38          |0           |     |1        |
      |106 MB              |N/A        |            |            |     |1        |
      |                    |           |            |            |     |2000     |
      |--------------------|-----------|------------|------------|-----|---------|
      |1213                |2          |40          |0           |0    |Normal   |
      |N/A                 |Idle       |39          |0           |     |1        |
      |106 MB              |N/A        |            |            |     |1        |
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
      |1213                |3          |46          |0           |0    |Realtime |
      |N/A                 |Idle       |45          |0           |     |1        |
      |106 MB              |N/A        |            |            |     |1        |
      |                    |           |            |            |     |2000     |
      |--------------------|-----------|------------|------------|-----|---------|
```


## Application Flow

```
  +--------------+     +------------+     +-----------+
  | Parse Config |---->| Initialize |---->| Validate  |
  | (JSON + CLI) |     | Runner(s)  |     | IFM Names |
  +--------------+     +------------+     | & Sizes   |
                                          +-----+-----+
                                                |
                                                v
                              +-----------------+-----------------+
                              |  Phase 2: Allocate & Load IFMs   |
                              |  (sequential, per model)          |
                              +-----------------+-----------------+
                                                |
                                                v
                              +-----------------+-----------------+
                              |  Phase 3: spawn one std::thread   |
                              |  per model – inference only       |
                              +---+-----------+-----------+-------+
                                  |           |           |
                                  v           v           v
                            +-----------+ +-----------+ +-----------+
                            | Thread 0  | | Thread 1  | | Thread N  |
                            | Model_0   | | Model_1   | | Model_N   |
                            |-----------| |-----------| |-----------|
                            | Infer x I | | Infer x I | | Infer x I |
                            +-----+-----+ +-----+-----+ +-----+-----+
                                  |             |             |
                                  +-----+-------+------+------+
                                        v
                                   +---------+
                                   |  join   |
                                   +---------+
                                        |
                                        v
                              +-----------------+-----------------+
                              |  Phase 4: Save OFMs              |
                              |  (sequential, per model)          |
                              +-----------------+-----------------+
                                                |
                                                v
                                           +---------+
                                           |  exit   |
                                           +---------+
```

The application runs in four phases:

**Phase 1 — Initialise and Validate** (`init_and_validate_models`):
1. Parse CLI arguments and load the JSON config.
2. Validate NPU column assignments and sharing constraints.
3. For each model: create the VART runner, fetch tensor metadata.
4. Validate IFM node names and file sizes across all models.
5. Print summary tables for any mismatches and exit on error.

**Phase 2 — Allocate and Load** (sequential, per model):
1. Allocate IFM and OFM NpuTensor buffers.
2. Load IFM data from binary files (or fill with random bytes in dry-run mode).

**Phase 3 — Inference** (parallel threads, one per model):
1. Spawn one `std::thread` per model.
2. Execute inference on the NPU for the configured number of iterations.
3. Join all threads before proceeding.

**Phase 4 — Save OFMs** (sequential, per model):
1. Save OFM results to binary files (skipped in dry-run mode).


## Sample Output

```
Loaded 2 model(s) from config.
Iterations per model: 10

========== Phase 1: Initialise & Validate ==========

========== Model_1 ==========
  model_cache_path      : /opt/models/model_a/cache
  start_column          : 0
  aie_columns_sharing   : exclusive
[INFO] Model_1: creating VART runner with model cache: /opt/models/model_a/cache
[INFO] Model_1: VART runner created successfully.

========== Model_2 ==========
  model_cache_path      : /opt/models/model_b/cache
  start_column          : 4
  aie_columns_sharing   : exclusive
[INFO] Model_2: creating VART runner with model cache: /opt/models/model_b/cache
[INFO] Model_2: VART runner created successfully.

[INFO] IFM validation passed for all models.

========== Phase 2: Allocate & Load ==========

========== Phase 3: Inference (parallel threads) ==========
[INFO] Model_1 [Step 3/4 Infer]: 10 iteration(s) completed.
[INFO] Model_2 [Step 3/4 Infer]: 10 iteration(s) completed.

========== Phase 4: Save OFMs ==========

[INFO] All models completed successfully.

========== Execution Summary ==========
-----------------+-----------------+------------------------------------------------------------------------
Start Column     | Models executed | OFMs file saved
-----------------+-----------------+------------------------------------------------------------------------
0 (exclusive)    | Model_1         | /data/model_a/outputs/ofm_model_1/output_QuantizeLinear_Output_1x1000_int8.bin
4 (exclusive)    | Model_2         | /data/model_b/outputs/ofm_model_2/output_QuantizeLinear_Output_1x1000_int8.bin
-----------------+-----------------+------------------------------------------------------------------------
```


## Summary

By using this application, you can:

1. Run one or more Vitis AI-compiled models on the NPU as **multiple tenants**
   with flexible column assignment (spatial, temporal, or combined sharing).
2. Validate IFM/OFM tensor mappings before inference with detailed error
   reporting.
3. Execute multi-model inference in parallel threads, optionally repeating each
   model for a configurable number of iterations.
