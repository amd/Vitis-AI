<table class="sphinxhide" width="100%">
 <tr width="100%">
    <td align="center"><img src="https://raw.githubusercontent.com/Xilinx/Image-Collateral/main/xilinx-logo.png" width="30%"/><h1> Getting Started with Vitis AI: ResNet-18 End-to-End Flow</h1>
    </td>
 </tr>
</table>

## Introduction

The Vitis AI toolchain supports compiling and deploying AI models in the ONNX format for efficient execution on Versal AI Edge Series Gen 2 devices. By using the Vitis AI Execution Provider (EP) within ONNX Runtime, developers can seamlessly run ONNX models and leverage hardware acceleration provided by the NPU.

This tutorial shows how to compile an ONNX model with the Vitis AI flow and deploy it on the **AMD Versal AI Edge Series Gen 2 VEK385 Evaluation Kit**.

## Requirements

To build the example and deploy it on board, the following software and hardware are required:

* Vitis AI 6.2 Docker for Versal AI Edge Series Gen 2:
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
chmod -R a+w <path/to/resnet18_bf16>
```

Refer to Vitis AI User Guide for Versal AI Edge Series Gen 2 to load and start docker:

```
docker run -it --network host \  
  -v /path/to/your/license:/usr/licenses \  
  -v /<host_path>:/<path_in_docker> \  
  --rm <REPOSITORY>:<TAG>  "bash"
```

## Vitis AI Compilation & Deployment Flow

1. Inside the docker, change directory to the tutorial folder, install python packages required by the example, and export the ResNet-18 ONNX model:

```
cd /resnet18_bf16
python3 -m pip install -r requirements.txt
python3 export_to_onnx.py 
```

**Note**: Inside the docker, `python3` or `/usr/bin/python` should be explicitly used to execute the python scripts.

The output model is saved to `models/resnet18.a1_in1k.onnx`:

```
Model exported successfully to: models/resnet18.a1_in1k.onnx
ONNX model path: models/resnet18.a1_in1k.onnx
```

2. Inside the docker, compile the ONNX model with Vitis AI flow:

```
python3 compile.py 
```

The input ONNX model name is hardcoded in `compile.py`.

The output should look as follows:

```
INFO: [VAIP-VAIML-PASS] No. of Operators :
INFO:  VAIML     49
INFO: [VAIP-VAIML-PASS] No. of Subgraphs :
INFO:    NPU     1
INFO: [VAIP-VAIML-PASS] For detailed compilation results, please refer to my_cache_dir/resnet18.a1_in1k/final-vaiml-pass-summary.txt
```

The number of operators accelerated on the NPU is displayed. 

To get more details about compilation results you can display the content of the file `my_cache_dir/resnet18.a1_in1k/final-vaiml-pass-summary.txt`:

```
--------- Final Summary of VAIML Pass ----------
OS: Linux X64
VAIP commit: ......
Model: ....../models/resnet18.a1_in1k.onnx
Model signature: 41d764d4ef1d716a260bc7b2b4e07ff1
Device: ve2
Model data type: float32
Device data type: bfloat16
Number of operators in the model: 49
GOPs of the model: 3.64388
Number of operators supported by VAIML: 49 (100.000%)
GOPs supported by VAIML: 3.644 (100.000%)
Number of subgraphs supported by VAIML: 1
Number of operators offloaded by VAIML: 49 (100.000%)
GOPs offloaded by VAIML: 3.644 (100.000%)
Number of subgraphs offloaded by VAIML: 1
Number of subgraphs with compilation errors (fall back to CPU): 0
Number of subgraphs below 20% GOPs threshold (fall back to CPU): 0
Number of subgraphs above max number of subgraphs allowed(7): 0 (fall back to CPU)
Stats for offloaded subgraphs
Subgraph vaiml_par_0 stats:
    Type: npu
    Operators: 49 (100.000%)
    GOPs : 3.644 (100.000%)  OPs: 3,643,881,552
    fp32 ops %: 99.731
```

3. Refer to Vitis AI User Guide for Versal AI Edge Series Gen 2, boot up the AIE-ML_v2 board, and setup environment:

```
export LD_LIBRARY_PATH=/usr/lib/python3.12/site-packages/voe/lib:/usr/lib/python3.12/site-packages/flexmlrt/lib:/usr/lib/python3.12/site-packages/onnxruntime/capi
```

4. Run the inference on the board. The working directory can be mounted on the board or copied to the board by scp:

```
scp -r <USER NAME>@<HOST MACHINE>:/<path to resnet18_bf16> .
cd resnet18_bf16
python runmodel.py
```

The input ONNX model name is hardcoded in `runmodel.py`.

The ONNX session will detect the presence of a pre-compiled model in the current directory and avoid model recompilation.

The script runs four inferences of the model and displays messages similar to the following:

```
Running 4 inferences, comparing CPU and NPU outputs
Iteration   1: Max absolute difference = 0.228190, Root mean squared error = 0.085001
Iteration   2: Max absolute difference = 0.231800, Root mean squared error = 0.086444
Iteration   3: Max absolute difference = 0.248591, Root mean squared error = 0.092887
Iteration   4: Max absolute difference = 0.177171, Root mean squared error = 0.069008
Inference Done!
```

If you want to see detailed NPU execution logs, set the environment variable `DEBUG_LOG_LEVEL=info` before running the script:

```
export DEBUG_LOG_LEVEL=info
python runmodel.py
```

The output includes the number of operators offloaded to the NPU and the number of NPU-executed subgraphs:

```
I20260608 01:57:50.007778  1212 stat.cpp:198] [Vitis AI EP] No. of Operators :
I20260608 01:57:50.007839  1212 stat.cpp:198]  VAIML    49 
I20260608 01:57:50.007958  1212 stat.cpp:198] 
I20260608 01:57:50.007978  1212 stat.cpp:198] [Vitis AI EP] No. of Subgraphs :
I20260608 01:57:50.007992  1212 stat.cpp:198]    NPU     1 
I20260608 01:57:50.008001  1212 stat.cpp:198] Actually running on NPU      1
......
Running 4 inferences, comparing CPU and NPU outputs
......
Iteration   1: Max absolute difference = 0.211893, Root mean squared error = 0.075283
......
Iteration   2: Max absolute difference = 0.220449, Root mean squared error = 0.082772
......
Iteration   3: Max absolute difference = 0.169577, Root mean squared error = 0.055290
......
Iteration   4: Max absolute difference = 0.223119, Root mean squared error = 0.077367
Inference Done!
```

If you want to see the column usage, add file xrt.ini to the working directory, and put following contents in `xrt.ini`:

```
[Runtime]
verbosity=7
```

And then run the inference. The output contains information as follows:

```
[xrt_xdna] DEBUG: Partition Created with start_col 0 num_columns 4 partition_id 1024
```

## Summary

By completing this tutorial, you learned:

1. The Vitis AI compilation flow with ResNet-18 example.

2. The deployment of Vitis AI compiled model on the board.

<p class="sphinxhide" align="center"><sub>Copyright © 2024–2026 Advanced Micro Devices, Inc.</sub></p>

<p class="sphinxhide" align="center"><sup><a href="https://www.amd.com/en/corporate/copyright">Terms and Conditions</a></sup></p>
