<table class="sphinxhide" width="100%">
 <tr width="100%">
    <td align="center"><img src="https://raw.githubusercontent.com/Xilinx/Image-Collateral/main/xilinx-logo.png" width="30%"/><h1> Getting Started with Vitis AI: ResNet-50 CIFAR-10 BF16 End-to-End Flow</h1>
    </td>
 </tr>
</table>

## Introduction

This tutorial demonstrates the inference workflow of a ResNet-50 model
fine-tuned on the CIFAR-10 dataset. The focus is on converting the model to
BF16 precision with the VAIML compiler and offloading it to the NPU of the
**AMD Versal AI Edge Series Gen 2 VEK385 Evaluation Kit**, using ONNX Runtime
with the Vitis AI Execution Provider (EP).

BF16 quantization does not require an explicit quantization step: the Vitis AI
compiler converts the FP32 model to BF16 during compilation
(`enable_f32_to_bf16_conversion`).

## Overview

This tutorial covers the following steps:

* Download the CIFAR-10 dataset and export the provided fine-tuned ResNet-50
  model to ONNX format.
* Convert the model to BF16 with the VAIML compiler.
* Compile and run the model on the NPU using ONNX Runtime with the Vitis AI
  Execution Provider.

## Model and Dataset

| Item | Description |
| --- | --- |
| Architecture | `torchvision` ResNet-50 backbone, the 1000-class classifier replaced by a 10-class CIFAR-10 head (`Linear(2048, 64)` → `ReLU` → `Linear(64, 10)`) |
| Weights | `models/resnet_trained_for_cifar10.pt` — a PyTorch `state_dict` fine-tuned on CIFAR-10, provided with this tutorial |
| Input | `input`, `float32`, `1x3x32x32` (RGB, values scaled to `[0, 1]`) |
| Output | `output`, `float32`, `1x10` (class logits) |
| Dataset | CIFAR-10, downloaded from <https://www.cs.toronto.edu/~kriz/cifar.html> by `prepare_model_data.py` |

The checkpoint is a plain `state_dict`, so it is loaded with
`torch.load(..., weights_only=True)` and never unpickles arbitrary code.

## Requirements

To build the example and deploy it on board, the following software and hardware are required:

* Vitis AI Docker for Versal AI Edge Series Gen 2:
    * Instructions for installation and startup are in the Vitis AI User Guide for Versal AI Edge Series Gen 2.
* VEK385 evaluation kit:
    * Setup instructions are available in the Vitis AI User Guide for Versal AI Edge Series Gen 2.
* Internet access:
    * Necessary for downloading resources.
    * Get the tutorial repository
* AIE-ML_v2 license file:
    * For license file, follow instructions in the Vitis AI User Guide for Versal AI Edge Series Gen 2.

## Prepare and Run Docker

Before starting Docker, get the tutorial repository and adjust the access permissions of the working directories on the host machine:

```
chmod -R a+w <path/to/resnet50_bf16_cifar10>
```

Refer to the [Vitis AI User Guide for Versal AI Edge Series Gen 2](https://vitisai.docs.amd.com/projects/gen2/en/latest/docs/setup_and_installation/docker-setup.html) to load and start docker:

```
docker run -it --network host \
  -v /path/to/your/license:/usr/licenses \
  -v /<host_path>:/<path_in_docker> \
  --rm <REPOSITORY>:<TAG>  "bash"
```

## Vitis AI Compilation & Deployment Flow

1. Inside the docker, change directory to the tutorial folder, install python packages required by the example, download the CIFAR-10 dataset, and export the ResNet-50 ONNX model:

```
cd /resnet50_bf16_cifar10
python3 -m pip install -r requirements.txt
python3 prepare_model_data.py
```

All the required packages are already present in the Vitis AI Docker, so the
`pip install` step is a no-op there. It is listed for environments that do not
provide them.

The dataset is extracted to `data/cifar-10-batches-py` and the model is saved to `models/resnet_trained_for_cifar10.onnx`:

```
Downloading cifar-10-python.tar.gz...
  100.0% (162 MB)
Extracting cifar-10-python.tar.gz...
Model exported successfully to: /resnet50_bf16_cifar10/models/resnet_trained_for_cifar10.onnx
```

If you prefer to fine-tune the model yourself instead of using the provided
weights, run `python3 prepare_model_data.py --train --num_epochs <N>`. This
overwrites `models/resnet_trained_for_cifar10.pt`.

2. Optionally, check the exported FP32 model on the CPU before compiling it:

```
python3 predict.py
```

The script classifies the first CIFAR-10 test images and prints the actual and
predicted labels:

```
execution started on CPU
Image 0: Actual Label cat, Predicted Label cat
Image 1: Actual Label ship, Predicted Label ship
Image 2: Actual Label ship, Predicted Label ship
Image 3: Actual Label airplane, Predicted Label airplane
Image 4: Actual Label frog, Predicted Label frog
Image 5: Actual Label frog, Predicted Label frog
Image 6: Actual Label automobile, Predicted Label truck
Image 7: Actual Label frog, Predicted Label frog
Image 8: Actual Label cat, Predicted Label cat
Image 9: Actual Label automobile, Predicted Label automobile
Top-1 accuracy on 10 images: 90.00% (9/10)
```

Use `--num-images` to classify more images; over the full CIFAR-10 test set
(`--num-images 10000`) this model reaches a top-1 accuracy of 81.68%.

3. Inside the docker, compile the ONNX model with Vitis AI flow:

```
python3 compile.py
```

The default input ONNX model is `models/resnet_trained_for_cifar10.onnx`; use
`--model` to override it. Compilation takes a few minutes (about 6 minutes on a
typical host).

The output should look as follows:

```
INFO: [VAIP-VAIML-PASS] No. of Operators :
INFO:  VAIML    124
INFO: [VAIP-VAIML-PASS] No. of Subgraphs :
INFO:    NPU     1
```

The number of operators accelerated on the NPU is displayed.

To get more details about compilation results you can display the content of the file `my_cache_dir/resnet_trained_for_cifar10/final-vaiml-pass-summary.txt`:

```
--------- Final Summary of VAIML Pass ----------
OS: Linux X64
Model: ....../resnet_trained_for_cifar10.onnx
Model signature: 9d9110c306b8e68d8e622b3414dd5982

Compiler Information
  Target Device: ve2 (part number: xc2ve3858)
  Device Data Type: bfloat16
  Flow: default
  Version: VAIP ......
  FlexML Version: rai_*_* (hash: ......, built: 2026-**-**-**:**:**)
  Overlay: aie2_6x4x4.yaml
  NPU Frequency: 1267 MHz
  AIE Single Core Compiler: peano
  Recipes: mlopslib
  DP size: 1
  TP size: 1
  Optimize Level: 2
  Preferred Data Storage: auto
  Threshold GOPs Percent: 20
  Logging Level: info
  Preemption: disabled
  Model data type: float32
Number of operators in the model: 124
GOPs of the model: 0.168
Number of operators supported by VAIML: 124 (100.000%)
GOPs supported by VAIML: 0.168 (100.000%)
Number of subgraphs supported by VAIML: 1
Number of operators offloaded by VAIML: 124 (100.000%)
GOPs offloaded by VAIML: 0.168 (100.000%)
Number of subgraphs offloaded by VAIML: 1
Number of partitions offloaded to NPU: 1
Number of partitions executed on CPU: 0
Number of subgraphs with compilation errors (fall back to CPU): 0
Number of subgraphs below 20% GOPs threshold (fall back to CPU): 0
Number of subgraphs above max number of subgraphs allowed(7): 0 (fall back to CPU)
Stats for offloaded subgraphs
Subgraph vaiml_par_0 stats:
    Type: npu
    Operators: 124 (100.000%)
    GOPs : 0.168 (100.000%)  OPs: 167,999,964
    fp32 ops %: 99.574

Compilation Information
  Frontend (FE): 23.7 s (6.5%)
  Backend (BE): 342.0 s (93.2%)
    AIE Compile: 72.7 s (19.8%)
  Overhead (partitioner, cache, etc.): 1.0 s (0.3%)
  Total: 366.7 s
```

`Device Data Type: bfloat16` confirms the FP32 model was compiled to BF16 for
the NPU. `Model data type: float32` refers to the input ONNX model, and
`fp32 ops %` reports the data type of the operators as declared in that ONNX
graph — the conversion to BF16 is done by the compiler.

4. Refer to Vitis AI User Guide for Versal AI Edge Series Gen 2, boot up the AIE-ML_v2 board, and setup environment:

```
export LD_LIBRARY_PATH=/usr/lib/python3.12/site-packages/flexmlrt/lib/:/usr/lib/python3.12/site-packages/voe/lib/:/usr/lib/python3.12/site-packages/onnxruntime/capi:/usr/lib/python3.12/site-packages/vart_ml/lib:/usr/lib/python3.12/site-packages/vart_x/lib
```

5. Run the inference on the board. The working directory can be mounted on the board or copied to the board by scp:

```
scp -r <USER NAME>@<HOST MACHINE>:/<path to resnet50_bf16_cifar10> .
cd resnet50_bf16_cifar10
python3 predict.py --ep npu
```

The ONNX session detects the pre-compiled model in `my_cache_dir` and avoids model recompilation.

Expected output:

```
execution started on NPU
Image 0: Actual Label cat, Predicted Label cat
Image 1: Actual Label ship, Predicted Label ship
Image 2: Actual Label ship, Predicted Label ship
Image 3: Actual Label airplane, Predicted Label airplane
Image 4: Actual Label frog, Predicted Label frog
Image 5: Actual Label frog, Predicted Label frog
Image 6: Actual Label automobile, Predicted Label truck
Image 7: Actual Label frog, Predicted Label frog
Image 8: Actual Label cat, Predicted Label cat
Image 9: Actual Label automobile, Predicted Label automobile
Top-1 accuracy on 10 images: 90.00% (9/10)
```

The BF16 predictions match the CPU FP32 predictions.

6. To compare CPU and NPU numerical outputs directly, run:

```
python3 runmodel.py
```

The script runs four inferences of the model and displays messages similar to the following:

```
Running 4 inferences, comparing CPU and NPU outputs
Iteration   1: Max absolute difference = ......, Root mean squared error = ......
Iteration   2: Max absolute difference = ......, Root mean squared error = ......
Iteration   3: Max absolute difference = ......, Root mean squared error = ......
Iteration   4: Max absolute difference = ......, Root mean squared error = ......
Inference Done!
```

If you want to see detailed NPU execution logs, set the environment variable `DEBUG_LOG_LEVEL=info` before running the script:

```
export DEBUG_LOG_LEVEL=info
python3 runmodel.py
```

The output includes the number of operators offloaded to the NPU and the number of NPU-executed subgraphs:

```
...... stat.cpp:198] [Vitis AI EP] No. of Operators :
...... stat.cpp:198]  VAIML   124
...... stat.cpp:198]
...... stat.cpp:198] [Vitis AI EP] No. of Subgraphs :
...... stat.cpp:198]    NPU     1
...... stat.cpp:198] Actually running on NPU      1
```

If you want to see the column usage, add file `xrt.ini` to the working directory, and put following contents in `xrt.ini`:

```
[Runtime]
verbosity=7
```

And then run the inference. The output contains information as follows:

```
[xrt_xdna] DEBUG: Partition Created with start_col 0 num_columns 4 partition_id 1024
```

## Files

| File | Role |
| --- | --- |
| `prepare_model_data.py` | Downloads CIFAR-10 and exports `models/resnet_trained_for_cifar10.onnx` from the provided weights (optional `--train` to fine-tune). |
| `resnet_utils.py` | Directory helpers and the CIFAR-10 ResNet-50 model definition. |
| `compile.py` | Compiles the ONNX model with the Vitis AI EP (BF16 conversion by the VAIML compiler). |
| `predict.py` | CIFAR-10 classification on CPU (`--ep cpu`, default) or NPU (`--ep npu`). |
| `runmodel.py` | Runs the model on CPU and NPU and compares the outputs. |
| `vitisai_config.json` | Vitis AI EP configuration: device `ve2-xc2ve3858`, `enable_f32_to_bf16_conversion`. |
| `models/resnet_trained_for_cifar10.pt` | CIFAR-10 fine-tuned ResNet-50 weights (`state_dict`). |

## Summary

By completing this tutorial, you learned:

1. The Vitis AI BF16 compilation flow with a CIFAR-10 ResNet-50 example.

2. The deployment of a Vitis AI compiled model on the board and how to validate its predictions against the CPU reference.

<p class="sphinxhide" align="center"><sub>Copyright © 2024–2026 Advanced Micro Devices, Inc.</sub></p>

<p class="sphinxhide" align="center"><sup><a href="https://www.amd.com/en/corporate/copyright">Terms and Conditions</a></sup></p>
