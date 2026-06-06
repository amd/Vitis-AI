# Python examples

<!--
## Copyright and license statement

Copyright (C) 2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->


Small Python scripts that exercise **ONNX Runtime with the VitisAI Execution Provider** on the embedded target (typical layout: model ONNX + `vitisai_config.json` under a per-model directory, IFM as raw `.bin`).

## `run_ResNet50_vitisai.py`

Runs a compiled ResNet-style ONNX model through ORT + VitisAI EP: loads IFM from a binary file, runs inference, and writes OFM blobs. Optional softmax postprocess and top-k class ids when `--postprocess` is set (with optional ImageNet-style `--labels` file).

### Default run (rootfs)

With no arguments, the script uses the **default model layout on the target rootfs** under `/etc/vai/models`:

```bash
python3 run_ResNet50_vitisai.py
```

That is equivalent to:

- **`--model`** `resnet50_int8` — subdirectory `<base-dir>/resnet50_int8/` and the VitisAI EP **`cache_key`** (must match how the compiled `.rai` was produced; see below).
- **`--base-dir`** `/etc/vai/models` — model roots and EP cache directory.
- **`--input`** `/etc/vai/models/resnet50_int8/data/` — default IFM for that flow.
- **`--onnx-name`** `resnet50_int8.onnx`, **`--config-name`** `vitisai_config.json` inside the model directory.

### Input file (IFM)

The script does **not** decode JPEGs or run full ImageNet preprocessing. It reads a **raw binary** (`--input`) and feeds the ONNX graph as-is.

For the default **ResNet50** ONNX in this flow, the graph expects **one float32 tensor** in **NCHW** layout with shape **`[1, 3, 224, 224]`** (batch × RGB × height × width). Your IFM `.bin` must contain exactly **`1 × 3 × 224 × 224` float32 values** in the order the model expects (same layout you used when generating IFMs for validation). If you use another model, check the printed **input tensors** line for that run’s shape and dtype, then build an IFM `.bin` that matches **byte-for-byte** (the script errors if the file size does not match).

### Using another model

To point at a different deployment, pass **`--model`**, **`--base-dir`**, **`--input`**, and optionally **`--onnx-name`** / **`--config-name`** as needed.

**Important:** `--model` is the VitisAI **`cache_key`**. Use the **same string** for the on-target model folder, the **`.rai` file name** (e.g. `MyModel.rai` → `--model MyModel`), and when you compiled the model. If these names do not match, inference may fail.

**Example (on target, paths may vary):**

```bash
python3 run_ResNet50_vitisai.py \
  --model resnet50_int8 \
  --base-dir /etc/vai/models \
  --input /etc/vai/models/resnet50_int8/data/ifm_input_fp32_1x3x224x224.bin \
  --postprocess --labels /etc/vai/models/resnet50_int8/data/imagenet-classes-1000.txt
```

### Sample output

A successful default run looks similar to:

```text
model: /etc/vai/models/resnet50_int8/resnet50_int8.onnx
config: /etc/vai/models/resnet50_int8/vitisai_config.json
cache_dir: /etc/vai/models/ cache_key: resnet50_int8
input file: /etc/vai/models/resnet50_int8/data/ifm_input_fp32_1x3x224x224.bin
input tensors:
  input tensor(float) [1, 3, 224, 224]
output tensors:
  output tensor(float) [1, 1000]
feeding input dtype= <class 'numpy.float32'> shape= (1, 3, 224, 224) from /etc/vai/models/resnet50_int8/data/ifm_input_fp32_1x3x224x224.bin
wrote OFM 0 output (1, 1000) -> ./resnet50_int8_ofm_0.bin
```

The **input / output** lines confirm tensor names, dtypes, and shapes. Here the OFM is **1000-way logits** (typical ImageNet head), written as **`./resnet50_int8_ofm_0.bin`** (float32 raw bytes).

### `python3 run_ResNet50_vitisai.py --help`

```text
python3 run_ResNet50_vitisai.py --help

usage: run_ResNet50_vitisai.py [-h] [--model MODEL] [--base-dir BASE_DIR] [--input INPUT] [--onnx-name ONNX_NAME]
                               [--config-name CONFIG_NAME] [--input-name INPUT_NAME] [--output-prefix OUTPUT_PREFIX]
                               [--postprocess] [--postprocess-top-k POSTPROCESS_TOP_K] [--labels LABELS]

options:
  -h, --help            show this help message and exit
  --model MODEL         Model folder name and cache_key (default: resnet50_int8)
  --base-dir BASE_DIR   Root directory containing per-model subfolders (default: /etc/vai/models)
  --input INPUT         Input IFM float32 NCHW .bin
  --onnx-name ONNX_NAME
                        ONNX filename inside model dir (default: resnet50_int8.onnx)
  --config-name CONFIG_NAME
                        Config filename inside model dir (default: vitisai_config.json)
  --input-name INPUT_NAME
                        Which ONNX input to feed (default: first input from the model)
  --output-prefix OUTPUT_PREFIX
                        Prefix for OFM .bin files: <prefix>_0.bin, <prefix>_1.bin, ... (default: ./{model}_ofm)
  --postprocess         After inference, softmax first output and print top-k class ids (default: off)
  --postprocess-top-k POSTPROCESS_TOP_K
                        With --postprocess, how many top classes to print (default: 5)
  --labels LABELS       Optional ImageNet-style labels file (1000 lines); used with --postprocess
```
