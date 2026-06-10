<table class="sphinxhide" width="100%">
 <tr width="100%">
    <td align="center"><img src="https://raw.githubusercontent.com/Xilinx/Image-Collateral/main/xilinx-logo.png" width="30%"/><h1>Vitis AI Development</h1>
    </td>
 </tr>
</table>

# Getting Started with Vitis AI: ResNet-18 End-to-End Flow

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

Load the docker image: 

```
docker load -i <docker_image_file>.tgz
```

Run `docker images` to verify docker REPOSITORY, IMAGEID and TAG information. 

|REPOSITORY          | TAG               | IMAGE ID    | CREATED       | SIZE   |
|--------------------|-------------------|-------------|---------------|--------|
|vitis_ai_2ve_docker | release_v6.2 |  ???????    |  xx hours ago | 39.1GB |

Start the docker: 

```
docker run -it --network host \  
  -v /path/to/your/license:/usr/licenses \  
  -v $PWD/resnet18_bf16:/resnet18_bf16 \  
  --rm vitis_ai_2ve_docker:release_v6.2  "bash"
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
VAIP commit: 744227ab2a0fddec1eccdfe04ca222afd339f53f
Model: ....../models/resnet18.a1_in1k.onnx
Model signature: 41d764d4ef1d716a260bc7b2b4e07ff1
Device: ve2-xc2ve3858
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
```

3. Refer to Vitis AI User Guide for Versal AI Edge Series Gen 2, boot up the AIE-ML_v2 board, and run following commands in the board to setup environment:

```
sudo su  # To avoid permission issues while creating the hw context
echo 1 > /sys/module/rcupdate/parameters/rcu_cpu_stall_suppress 
export XRT_AIARM=true 
export LD_LIBRARY_PATH=/usr/lib/python3.12/site-packages/voe/lib/:/usr/lib/python3.12/site-packages/flexmlrt/lib/ 
export XLNX_ENABLE_CACHE=0 
export XRT_ELF_FLOW=1 
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
Iteration   1: Max absolute difference = 0.198444, Root mean squared error = 0.081654
Iteration   2: Max absolute difference = 0.154286, Root mean squared error = 0.068371
Iteration   3: Max absolute difference = 0.210051, Root mean squared error = 0.081457
Iteration   4: Max absolute difference = 0.196577, Root mean squared error = 0.063051
Inference Done!
```

If you want to see detailed NPU execution logs, set the environment variable `DEBUG_LOG_LEVEL=info` before running the script:

```
export DEBUG_LOG_LEVEL=info
python runmodel.py
```

The output includes the number of operators offloaded to the NPU and the number of NPU-executed subgraphs:

```
I20250529 19:23:43.187124  1265 stat.cpp:193] [Vitis AI EP] No. of Operators :
I20250529 19:23:43.187179  1265 stat.cpp:204]  VAIML    49
I20250529 19:23:43.187194  1265 stat.cpp:213]
I20250529 19:23:43.187206  1265 stat.cpp:218] [Vitis AI EP] No. of Subgraphs :
I20250529 19:23:43.187219  1265 stat.cpp:226]    NPU     1
I20250529 19:23:43.187227  1265 stat.cpp:229] Actually running on NPU      1
I20250529 19:23:43.188418  1265 vitisai_compile_model.cpp:1477] AVG CPU Usage 95.4545%
I20250529 19:23:43.188459  1265 vitisai_compile_model.cpp:1478] Peak Working Set size 213.195 MB
[2025-05-29 19:23:43.261] [console] [info] [FLEXMLRT] FlexMLClient.cpp:1269 FlexMLRT Git Hash: 512d4e65
Running 4 inferences, comparing CPU and NPU outputs
Iteration   1: Max absolute difference = 0.193388, Root mean squared error = 0.083394
Iteration   2: Max absolute difference = 0.241203, Root mean squared error = 0.090799
Iteration   3: Max absolute difference = 0.190464, Root mean squared error = 0.080506
Iteration   4: Max absolute difference = 0.217875, Root mean squared error = 0.087615
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

## Vitis AI Flow Essential

This section covers some essential concepts in Vitis AI model compilation and inference. By learning the concepts and example codes, the flow can be extended to other ONNX models.

1. Onnx model is used as input to the model compilation, which tries to accelerate the operators in NPU. So, prepare the ONNX model in ML frameworks. 

2. Models are compiled for the NPU by creating an ONNX inference session using the Vitis AI Execution Provider (VAI EP). The example python code can be found in `compile.py`.

```
import onnxruntime

provider_options_dict = {
    "config_file": 'vitisai_config.json',
    "cache_dir":   'my_cache_dir',
    "cache_key":   'resnet18.a1_in1k',
    "log_level": 'info',
    "target": 'VAIML'
}
   
print(f"Creating ORT inference session for model models/resnet18.a1_in1k.onnx")
session = onnxruntime.InferenceSession(
    'models/resnet18.a1_in1k.onnx',
    providers=["VitisAIExecutionProvider"],
    provider_options=[provider_options_dict]
)   
```

The example configuration file `vitisai_config.json` contains options for Vitis AI compiler:

```
{
  "passes": [
   {
     "name": "init",
     "plugin": "vaip-pass_init"
   },
   {
    "name": "vaiml_partition",
    "plugin": "vaip-pass_vaiml_partition",
    "vaiml_config":
    {
      "device": "ve2-xc2ve3858",
      "optimize_level": 2,
      "logging_level": "info",
      "keep_outputs": true,
      "threshold_gops_percent": 20
    }
   }
  ],
  "target": "VAIML",
  "targets": [
    {
        "name": "VAIML",
        "pass": [
            "init",
            "vaiml_partition"
        ]
    }
  ]
}
```

The value `ve2-xc2ve3858` for the `device` option selects the VEK385 part on Versal AI Edge Series Gen 2 (AIE-ML_v2) for Vitis AI 6.2 compilation.

3. To execute the compiled model on hardware, transfer the compiled model artifacts and the original ONNX model file to the target board. The compiled ONNX graph is automatically partitioned into multiple subgraphs by the VitisAI Execution Provider (EP). The subgraph(s) containing operators supported by the NPU are executed on the NPU. The remaining subgraph(s) are executed on the CPU. This graph partitioning and deployment technique across CPU and NPU is fully automated by the VAI EP and is totally transparent to the end-user.

Model execution is performed using a Python script that establishes an ONNX Runtime (ORT) inference session. This session is initialized with the target ONNX model and configured to utilize the Vitis AI Execution Provider (EP). Upon execution, the ORT session leverages the Vitis AI EP, which utilizes the compiled model binaries in the specified directory and deploys the ONNX subgraph(s) on the NPU and the CPU.  

The example python code for deploying on the hardware can be found in `runmodel.py`. It creates `InferenceSession` for CPU and NPU and runs inferences. And then compute the RMSE (Root Mean Square Error) between the CPU and NPU results:

```
import numpy as np
import onnxruntime as ort

provider_options_dict = {
    "config_file": 'vitisai_config.json',
    "cache_dir":   'my_cache_dir',
    "cache_key":   'resnet18.a1_in1k',
    "log_level":   'info',
    "target": 'VAIML',
}

print(f"Creating ORT inference session for model models/resnet18.a1_in1k.onnx")

onnx_model="models/resnet18.a1_in1k.onnx"
# CPU session to compute reference values
cpu_session = ort.InferenceSession(
    onnx_model,
) 
# NPU session
npu_session = ort.InferenceSession(
    onnx_model,
    providers=["VitisAIExecutionProvider"],
    provider_options=[provider_options_dict]
) 

num_iter = 4
print(f"Running {num_iter} inferences, comparing CPU and NPU outputs")
for i in range(num_iter):
    # Generate random data
    input_data = {}
    for input in npu_session.get_inputs():
        fixed_shape = [1 if isinstance(dim, str) else dim for dim in input.shape]
        input_data[input.name] = np.random.rand(*fixed_shape).astype(np.float32)

    # Compute CPU results (reference values)
    cpu_outputs = cpu_session.run(None, input_data)
    # Compute NPU results
    try:
        npu_outputs = npu_session.run(None, input_data)
    except Exception as e:
        print(f"Failed to run on NPU: {e}")
        sys.exit(1) 

    # Compare CPU and NPU results
    max_diff = np.max(np.abs(cpu_outputs[0] - npu_outputs[0]))
    rmse = np.sqrt(np.mean((cpu_outputs[0] - npu_outputs[0]) ** 2))
    print(f'Iteration {i+1:3d}: Max absolute difference = {max_diff:.6f}, Root mean squared error = {rmse:.6f}')

print("Inference Done!")
```

## Summary

By completing this tutorial, you learned:

1. The Vitis AI compilation flow with ResNet-18 example.

2. The deployment of Vitis AI compiled model on the board.

<p class="sphinxhide" align="center"><sub>Copyright © 2024–2026 Advanced Micro Devices, Inc.</sub></p>

<p class="sphinxhide" align="center"><sup><a href="https://www.amd.com/en/corporate/copyright">Terms and Conditions</a></sup></p>
