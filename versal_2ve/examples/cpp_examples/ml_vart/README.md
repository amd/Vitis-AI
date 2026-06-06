# ML VART Application

<!--
## Copyright and license statement

Copyright (C) 2025-2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->

Note: Example model names, JSON files, and commands are for reference only. Modify them for your compiled models and board.

This is a C++ reference application built on `VART-ML` that demonstrates how to execute Vitis AI compiled models using the `VART-ML` APIs. The application accepts a JSON configuration as input, allowing it to be properly configured for different models. It supports various execution modes including normal inference, dry-run testing, and benchmarking.

For details about the JSON configuration schema, please refer to [json_configs/README.md](json_configs/README.md).

---

## Key Features
- **Model Metadata Inspection**:
  - Inspect a compiled model's full tensor view without running inference
  - HW and CPU metadata for every input and output: names, shapes, memory layouts, dtypes, byte sizes, and quantization parameters
  - Console summary plus a machine-readable `<model_basename>_info.json` dump

- **Flexible Execution Modes**:
  - Normal mode: Full inference with input/output file operations
  - Dry-run mode: Skip file I/O for configuration testing
  - Benchmark mode: Measure inference performance without saving outputs

- **Advanced Tensor Management**:
  - Zero-copy mode (HW tensors) for optimal performance
  - Automatic batch processing with partial batch support

- **Performance & Monitoring**:
  - Configurable logging levels (ERROR, WARNING, INFO, DEBUG)
  - Performance metrics collection and reporting
  - AI Analyzer profiling support

- **Resource Management**:
  - Configurable AI Engine column sharing
  - CMA buffer allocation control

## Usage

```bash
ml_vart --app-config <config json file>
```

### Arguments

| Option              | Required  | Default | Description                                                  |
| ------------------- | --------- | ------- | ------------------------------------------------------------ |
| `--app-config`      | Mandatory |         | Path to configuration JSON file. Mandatory for inference / dry-run / benchmark flows. Ignored when `--get-model-info <model-path>` is supplied. |
| `--runs`            | Optional  | `1`     | Number of iterations to run (optional, default is `1`).     |
| `--benchmark`       | Optional  | `false` | Benchmark the model for `n` runs (optional, default is `false`). |
| `--log-level`       | Optional  | `2`     | Application log level to print logs (optional, default is `2`=WARNING). Accepted log levels: `1` for ERROR, `2` for WARNING, `5` for INFO, `6` for DEBUG. |
| `--frames`          | Optional  |         | Number of frames to process per iteration (optional, default is all). |
| `--dry-run`         | Optional  |         | Skip file I/O (test configuration only); IFM files are not read and OFM files are not written. |
| `--get-model-info <model-path>` | Optional |  | Standalone mode. Query the model's tensor metadata in both CPU and HW views, print it to the console, and dump it as `<model_basename>_info.json` in the working directory; no inference is performed. `vart::Runner` is always created with CPU input/output tensor type. `--app-config` is ignored if also supplied; See [Inspecting Model Metadata](#inspecting-model-metadata) for details.  |
| `--help`            | Optional  |         | Print this help and exit.                                   |

### Input
The application accepts IFMs in binary format. Supported dtypes follow the compiled model metadata (for example INT8, BF16, FP16, and float32 tensors as exposed by `vart::Runner`). Multiple input tensors are supported.

**File layout (one file per input tensor):** every input tensor gets its own `.bin` file containing one or more frames of raw tensor bytes concatenated back-to-back, in the layout `vart::Runner` expects (use `--get-model-info <model-path>` to inspect the per-frame `size_in_bytes`, `memory_layout`, and `dtype`):
- All IFMs for `input0` in one file (`input0.bin`).
- All IFMs for `input1` in another file (`input1.bin`).
- Repeat for every additional input tensor.

Each file's size must be an exact multiple of that tensor's per-frame `size_in_bytes`.

**Batch size:** the batch size (`N`) is fixed by the compiled model and cannot be changed at runtime. Query it with `--get-model-info <model-path>` (the `batch_size` field). The per-tensor `size_in_bytes` reported there is always **per frame**, not the total batch buffer.

**Batch composition:** for each inference call, the application takes `N` consecutive frames from every input file (the same frame index across all tensor files forms one batch element). All input files must therefore contain the same total number of frames; mismatched counts are rejected at startup.

**Partial batches:** if the total frame count is not an exact multiple of `N`, the final batch is run with fewer frames - no padding or wraparound, and outputs for the unused batch slots are not written.

### Output
The application dumps OFMs into the output directory in binary format using the following naming convention:
- **Single run:** `infer_out{tensor_idx}-{dtype}_{shape}_{tensor_name}.bin`
- **Multiple runs (`--runs > 1`):** `iter{run_index}_infer_out{tensor_idx}-{dtype}_{shape}_{tensor_name}.bin` (`{run_index}` is 0-based).

The `{dtype}` segment uses the same short type labels as in the naming convention above.

**File layout (one file per output tensor):** outputs are written using the same concatenated convention as inputs - one `.bin` per output tensor with `N` frames stored back-to-back per inference call, sized at the `vart::Runner`-reported per-frame `size_in_bytes` per slot. Partial-batch runs only write the slots that were actually populated.

**File organization:**
- One output file per tensor.
- All frames concatenated sequentially in the same file.

## Build

1. Source the Vitis AI SDK for Versal AI Edge Series Gen 2 environment:

```bash
source /path/to/sdk/environment-setup-cortexa72-cortexa53-amd-linux
```

2. Build the application:

```bash
make all
```

The resulting binary is `ml_vart`.

3. To clean build artifacts:

```bash
make clean
```

## Running on the Board

### Prerequisites

Before running the commands below, finish board setup for your platform, program the required PL and AI Engine overlay on the board, and configure the runtime environment for your image (including `LD_LIBRARY_PATH`).

The examples below use a JSON config available in the rootfs. For details about the JSON configuration schema, refer to [json_configs/README.md](json_configs/README.md).

### Example commands
- **Run with default options** — runs with optimized application performance by using HW tensor mode for tensor metadata and execution paths:
  ```bash
  ml_vart --app-config /etc/vai/ml_vart/json_configs/ml_vart_config.json
  ```

- **Benchmark the model for 100 runs** — for performance testing:
  ```bash
  ml_vart --app-config /etc/vai/ml_vart/json_configs/ml_vart_config.json --benchmark --runs 100
  ```

- **Run the model for 10 iterations with a specific log level (INFO)** — for troubleshooting and debugging:
  ```bash
  ml_vart --app-config /etc/vai/ml_vart/json_configs/ml_vart_config.json --runs 10 --log-level 5
  ```

- **Dry run** — test configuration without I/O overhead:
  ```bash
  ml_vart --app-config /etc/vai/ml_vart/json_configs/ml_vart_config.json --dry-run
  ```

## Inspecting Model Metadata

`--get-model-info <model-path>` prints the `vart::Runner` CPU + HW tensor view to the console and writes `<model_basename>_info.json` in the working directory. `<model-path>` is the compiled model. See the [Arguments](#arguments) row for full flag semantics.

Typical uses: verifying compiled artifacts before integration, sanity-checking dtypes / shapes / sizes against the source model, inspecting quantization parameters, and looking up the `vart::Runner`-reported input tensor names needed to author the `ifms-config` entries of `ml_vart` or `x_plus_ml_vart` (both bind `ifms-config[i].name` -> `vart::Runner` input tensor by name).

```bash
ml_vart --get-model-info /etc/vai/models/resnet50_int8/resnet50_int8.rai
```

The dumped `<model_basename>_info.json` mirrors the console summary in JSON format. The schema (example dump and field reference) and a console-output reference are documented in [Appendix: --get-model-info output formats](#appendix---get-model-info-output-formats).

## Configuration JSON Guide
For more detailed information about the JSON configuration schema, refer to [json_configs/README.md](json_configs/README.md).

## Related Documents

- [json_configs/README.md](json_configs/README.md) - full JSON schema for the app-config (top-level fields, `runner-options`, `ifms-config`).
- [../../docs/multi_tenancy.md](../../docs/multi_tenancy.md) - data vs. tensor parallelism, spatial vs. temporal multi-tenancy, and how `runner-options.aie-columns-sharing` / `start-column` map to NPU column layouts.
- [../../docs/auto_placement_policy.md](../../docs/auto_placement_policy.md) - how the runtime places a model on the NPU when `start-column` is omitted.

## Appendix: --get-model-info output formats

`--get-model-info <model-path>` produces two views of the same content (see [Inspecting Model Metadata](#inspecting-model-metadata) for the feature overview and invocation examples):

- A human-readable console summary on stdout (printed regardless of `--log-level`).
- A machine-readable `<model_basename>_info.json` written to the working directory.

Conventions shared by both views:

- Tensors appear in the order returned by `vart::Runner`. For each tensor the CPU view is emitted first, then the HW view when `vart::Runner` exposes it.
- The reported per-tensor byte size is **per frame**; multiply by `batch_size` for a full-batch buffer.
- Quantization parameters (`scale`, `zero_point`) describe how integer tensor data maps to real-valued data.
- For `GENERIC` memory layouts, see `memory_layout_order` for the dimension permutation.

The JSON view is the canonical contract; the console view is its line-oriented projection for quick inspection on the board.

### Console summary

Example (single-input, single-output classification model):

```text

--- Model info ---
Model file        : /etc/vai/models/resnet50_int8/resnet50_int8.rai
Batch size : 1
  Inputs (1):
    [0] input
         cpu: shape=[1,3,224,224]  dtype=fp32  memory_layout=NCHW  size=602112B
         hw : shape=[224,1,224,1,4]  dtype=int8  memory_layout=HCWNC4  size=200704B  quant{scale=0.03125, zero_point=0, rounding_mode=ROUND_TO_NEAREST_EVEN}

  Outputs (1):
    [0] output
         cpu: shape=[1,1000]  dtype=fp32  memory_layout=GENERIC(memory_layout_order=[0,1])  size=4000B
         hw : shape=[1,1000]  dtype=int8  memory_layout=GENERIC(memory_layout_order=[0,1])  size=1000B  quant{scale=0.25, zero_point=0, rounding_mode=ROUND_TO_NEAREST_EVEN}

Model info written to: /work/resnet50_int8_info.json
```

### Dumped JSON schema

Example (same model as above):

```json
{
    "model_file": "/etc/vai/models/resnet50_int8/resnet50_int8.rai",
    "batch_size": "1",
    "inputs": [
        {
            "name": "input",
            "cpu": {
                "shape": [
                    "1",
                    "3",
                    "224",
                    "224"
                ],
                "memory_layout": "NCHW",
                "dtype": "fp32",
                "size_in_bytes": "602112"
            },
            "hw": {
                "shape": [
                    "224",
                    "1",
                    "224",
                    "1",
                    "4"
                ],
                "memory_layout": "HCWNC4",
                "dtype": "int8",
                "size_in_bytes": "200704",
                "quantization": {
                    "scale": "0.03125",
                    "zero_point": "0",
                    "rounding_mode": "1"
                }
            }
        }
    ],
    "outputs": [
        {
            "name": "output",
            "cpu": {
                "shape": [
                    "1",
                    "1000"
                ],
                "memory_layout": "GENERIC",
                "memory_layout_order": ["0", "1"],
                "dtype": "fp32",
                "size_in_bytes": "4000"
            },
            "hw": {
                "shape": [
                    "1",
                    "1000"
                ],
                "memory_layout": "GENERIC",
                "memory_layout_order": ["0", "1"],
                "dtype": "int8",
                "size_in_bytes": "1000",
                "quantization": {
                    "scale": "0.25",
                    "zero_point": "0",
                    "rounding_mode": "1"
                }
            }
        }
    ]
}
```

Field reference:

Field reference (the `<view>` prefix below stands for either `cpu` or `hw`; CPU is always present, HW is optional):

| Field                          | Type             | Description                                                                                                                                              |
| ------------------------------ | ---------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `model_file`                   | string           | The `<model-path>` passed on the command line (`.rai` file or compiled-model directory).                                                                |
| `batch_size`                   | integer | Batch size fixed by the compiled model.                                                                                                                 |
| `inputs[]` / `outputs[]`       | array            | One entry per tensor.                                                                                                                                    |
| `inputs[].name` / `outputs[].name` | string       | `vart::Runner`-reported tensor name (same identifier for the CPU and HW views).                                                                          |
| `<view>.shape`                 | array of int     | Tensor shape; element count and dimension semantics depend on `memory_layout`.                                                                          |
| `<view>.memory_layout`         | string           | Memory layout (`NCHW`, `NHWC`, `HCWNC4`, ..., `GENERIC`).                                                                                                |
| `<view>.memory_layout_order`   | array of int     | Dimension permutation for `GENERIC` layouts only. `memory_layout_order[i]` is the source-CPU dimension index that ends up at position `i` in this view's `shape` (e.g. CPU `ABCD` -> `[0, 1, 2, 3]`; HW `ADBC` -> `[0, 3, 1, 2]`). |
| `<view>.dtype`                 | string           | Element data type (`int8`, `uint8`, `bf16`, `fp16`, `fp32`, `int32`, ...).                                                                                |
| `<view>.size_in_bytes`         | integer | Per-frame byte size.                                                                                                                                     |
| `<view>.quantization`          | object           | Per-tensor quantization parameters. Emitted only on views whose `dtype` is an integer type; if both `cpu` and `hw` views are integer they carry identical values.                                                                                                                                                                                                                                                                                                                                          |
| `<view>.quantization.scale`    | number  | Quantization scale factor.                                                                                                                                |
| `<view>.quantization.zero_point` | integer | Quantization zero point.                                                                                                                                |
| `<view>.quantization.rounding_mode` | integer | Numeric value of `vart::RoundingMode`.                                                                                                                |
