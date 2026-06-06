# ML ORT Application

<!--
## Copyright and license statement

Copyright (C) 2025-2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->


Note: Example model names, JSON files, and commands are for reference only. Modify them for your compiled models and board.

This is an `infer-only` C++ reference application built on the ONNX Runtime that demonstrates how to execute Vitis AI compiled ONNX models through the Vitis AI Execution Provider. It accepts a JSON configuration specifying the ONNX model, input IFM (Input Feature Map) binary files, and execution provider options, and produces OFM (Output Feature Map) files as output.

For details about the JSON configuration schema, please refer to [json_configs/README.md](json_configs/README.md).

---

## Key Features

- **ONNX Runtime Integration**:
  - Uses ONNX Runtime APIs and dispatches inference through the Vitis AI Execution Provider
  - Runs Vitis AI compiled ONNX models on the target NPU

- **Flexible Tensor Handling**:
  - Supports multiple input tensors and multiple output tensors
  - Supports multiple data types (INT8, BF16, FP16, and float32)

- **Batch Processing**:
  - Supports models with batch size greater than 1
  - Supports models with a dynamic batch dimension
  - Handles partial batches seamlessly when the input size is smaller than the model's batch size

- **Execution Modes**:
  - Normal mode: full inference with input/output file operations
  - Benchmark mode: measure inference performance over multiple runs

## Usage

```bash
ml_ort --app-config <config json file>
```

### Arguments

| Option         | Required  | Default | Description                                                      |
| -------------- | --------- | ------- | ---------------------------------------------------------------- |
| `--app-config` | Mandatory |         | Path to the application configuration JSON file (mandatory).     |
| `--runs`       | Optional  | `1`     | Number of iterations to run (optional, default is `1`).          |
| `--benchmark`  | Optional  | `false` | Benchmark the model for `n` runs (optional, default is `false`). |
| `--help`       | Optional  |         | Print this help and exit.                                        |

### Input

The application accepts IFMs in binary format. Supported data types are INT8, BF16, FP16, and float32. Multiple input tensors are supported.

**For batch processing:** concatenate all IFMs for each input tensor into a single file. For example:

- Store all IFMs for `input0` in one file (`input0.bin`).
- Store all IFMs for `input1` in another file (`input1.bin`).
- Repeat for additional input tensors.

### Output

The application dumps OFMs into the outputs directory in binary format using the following naming convention:

- **Single run:** `infer_out{tensor_idx}-{dtype}_{shape}_{tensor_name}.bin`
- **Multiple runs (`--runs > 1`):** `iter{run_index}_infer_out{tensor_idx}-{dtype}_{shape}_{tensor_name}.bin` (`{run_index}` is 0-based).

**For batch processing:** all OFMs for each output tensor are concatenated in a single output file. For example:

- Store all OFMs for `output0` in one file.
- Store all OFMs for `output1` in another file.
- Repeat for additional output tensors.

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

The resulting binary is `ml_ort`.

3. To clean build artifacts:

```bash
make clean
```

## Running on the Board

### Prerequisites

Before running the commands below, finish board setup for your platform, program the required PL and AI Engine overlay on the board, and configure the runtime environment for your image (including `LD_LIBRARY_PATH`).

The examples below use a JSON config available in the rootfs. For details about the JSON configuration schema, refer to [json_configs/README.md](json_configs/README.md).

### Example commands

- **Run with default options:**

  ```bash
  ml_ort --app-config /etc/vai/ml_ort/json_configs/ml_ort_config.json
  ```

- **Benchmark the model for 100 runs** — for performance testing:
  ```bash
  ml_ort --app-config /etc/vai/ml_ort/json_configs/ml_ort_config.json --benchmark --runs 100
  ```

## Batch Size Configuration

The application supports two batch size modes depending on how the ONNX model was compiled.

**Static batch** - the batch dimension is fixed in the ONNX model's input shape (for example, `[6, 3, 224, 224]`). The app reads it directly from the model; no additional configuration is needed.

**Dynamic batch** - the ONNX model has a dynamic batch dimension (`-1`, for example, `[-1, 3, 224, 224]`). The app cannot infer the intended batch size from the model alone, so it reads `dp_size` from the `vaiml_config` section of the VitisAI config JSON (the file referenced by `execution-provider-options.config_file` in the app config):

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

If `dp_size` is absent (or unreadable), the app defaults to batch size `1`.

For dynamic-batch models, the app pads partial input batches to the resolved batch size by repeating the last available sample. This keeps the dumped output tensor batch dimension aligned with the configured batch size.

> **Note:** For batch processing, input IFMs for each tensor must be concatenated into a single input file in frame order.

## Configuration JSON Guide

For more detailed information about the JSON configuration schema, refer to [json_configs/README.md](json_configs/README.md).
