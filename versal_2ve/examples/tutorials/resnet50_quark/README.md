<table class="sphinxhide" width="100%">
 <tr width="100%">
    <td align="center"><img src="https://raw.githubusercontent.com/Xilinx/Image-Collateral/main/xilinx-logo.png" width="30%"/><h1>Vitis AI Development</h1>
    </td>
 </tr>
</table>

# ResNet50 INT8 Quantization with AMD Quark and Inference

## Introduction

The `Model Quantization` section in the Vitis AI User Guide for Versal AI Edge Series Gen 2 provides an overview of quantization for Versal AI Edge Series Gen 2 devices using the AMD Quark tool.

This tutorial demonstrates how to perform INT8 quantization with AMD Quark and how to deploy the resulting INT8 model on the **AMD Versal AI Edge Series Gen 2 VEK385 Evaluation Kit** using the Vitis AI workflow.

For BF16 quantization, it is not necessary to invoke Quark explicitly. The Vitis AI compiler automatically converts FP32 models to BF16 during the compilation process.

In addition, this tutorial includes scripts for evaluating model accuracy on both CPU and NPU using the ImageNet dataset.

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

Before starting Docker, get the tutorial repository and download the ONNX model:

```
cd resnet50_quark
wget -P models https://huggingface.co/onnxmodelzoo/resnet50-v1-12/resolve/main/resnet50-v1-12.onnx
```

Adjust the access permissions of the working directories on the host machine:

```
chmod -R a+w <path/to/resnet50_quark>
```

Load the docker image: 

```
docker load -i <docker_image_file>.tgz
```

Run `docker images` to verify docker REPOSITORY, IMAGEID and TAG information. 

|REPOSITORY          | TAG               | IMAGE ID    | CREATED       | SIZE   |
|--------------------|-------------------|-------------|---------------|--------|
|vitis_ai_2ve_docker | release_v6.2      |   ??????    |  xx hours ago | 39.1GB |

Star the docker: 

```
docker run -it --network host \  
  -v /path/to/your/license:/usr/licenses \  
  -v $PWD/resnet50_quark:/resnet50_quark \  
  --rm vitis_ai_2ve_docker:release_v6.2  "bash"
```
## Evaluate the Float Model Accuracy with ImageNet Dataset

Evaluate the float model accuracy before quantization and deployment:

1. Download the ImageNet 2012 validation images (50000 images) from https://www.image-net.org/download

In this tutorial, the validation images are placed in the folder `../datasets/imagenet-1k/val_data/` relative to the working directory.

2. Evaluate the float ONNX model accuracy with the ImageNet dataset (running inside the docker):

```
python3 evaluate.py --model models/resnet50-v1-12.onnx --data ../datasets/imagenet-1k/val_data/ --target=cpu
```

Example output:

```
===== Evaluation Result =====
Top-1 Accuracy: 74.11%
Top-5 Accuracy: 91.72%
```

This result matches the accuracy number in the model zoo - https://huggingface.co/onnxmodelzoo/resnet50-v1-12

## Quantize the ONNX Model 

The following steps outline the process for quantizing the ResNet50 ONNX model.

1. Prepare Calibration Data & Validation Data

Create a folder for calibration data and download sample images (PNG or JPG). For example, use images from https://github.com/microsoft/onnxruntime-inference-examples/tree/main/quantization/image_classification/cpu/test_images as a quick start.

```
mkdir calib_data
wget -O calib_data/daisy.jpg https://github.com/microsoft/onnxruntime-inference-examples/blob/main/quantization/image_classification/cpu/test_images/daisy.jpg?raw=true
```

And prepare some pictures for validation:

```
mkdir val_data
wget -O val_data/daisy.jpg https://github.com/microsoft/onnxruntime-inference-examples/blob/main/quantization/image_classification/cpu/test_images/daisy.jpg?raw=true
wget -O val_data/rose.jpg https://github.com/microsoft/onnxruntime-inference-examples/blob/main/quantization/image_classification/cpu/test_images/rose.jpg?raw=true
wget -O val_data/tulip.jpg https://github.com/microsoft/onnxruntime-inference-examples/blob/main/quantization/image_classification/cpu/test_images/tulip.jpg?raw=true
```

3. Run Quark Quantizer

Inside the docker, run quantization:

```
python3 quantize.py
```

Example output:

```
┏━━━━━━━━━━━━━━━━━━━━━━┳━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
┃ Op Type              ┃ Float Model                          ┃
┡━━━━━━━━━━━━━━━━━━━━━━╇━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┩
│ Conv                 │ 53                                   │
│ BatchNormalization   │ 53                                   │
│ Relu                 │ 49                                   │
│ MaxPool              │ 1                                    │
│ Add                  │ 16                                   │
│ GlobalAveragePool    │ 1                                    │
│ Flatten              │ 1                                    │
│ Gemm                 │ 1                                    │
├──────────────────────┼──────────────────────────────────────┤
│ Quantized model path │ models/resnet50-v1-12_quantized.onnx │
└──────────────────────┴──────────────────────────────────────┘

┏━━━━━━━━━━━━━━━━━━━┳━━━━━━━━━━━━┳━━━━━━━━━━┳━━━━━━━━━━┓
┃ Op Type           ┃ Activation ┃ Weights  ┃ Bias     ┃
┡━━━━━━━━━━━━━━━━━━━╇━━━━━━━━━━━━╇━━━━━━━━━━╇━━━━━━━━━━┩
│ Conv              │ INT8(53)   │ INT8(53) │ INT8(53) │
│ Relu              │ INT8(49)   │          │          │
│ MaxPool           │ INT8(1)    │          │          │
│ Add               │ INT8(16)   │          │          │
│ GlobalAveragePool │ INT8(1)    │          │          │
│ Flatten           │ INT8(1)    │          │          │
│ Gemm              │ INT8(1)    │ INT8(1)  │ INT8(1)  │
└───────────────────┴────────────┴──────────┴──────────┘
```

The output quantized model is saved to `models/resnet50-v1-12_quantized.onnx`. 

* The first table lists the number of each operator type in the original float model (resnet50-v1-12). For example, there are 53 convolution layers, 53 batch normalization layers, and 49 ReLU activations.

* The second table shows the quantization status of each operator in the INT8 model (resnet50-v1-12_quantized.onnx). For instance, all 53 convolution layers have their activations, weights, and biases quantized to INT8. ReLU layers only have activations quantized because they do not have weights or biases.

* Some operators are missing from the quantization table; this usually happens because they have been fused with other layers (e.g., Conv + BatchNorm) during optimization.

Overall, the tables help verify that compute-heavy layers like Conv and Gemm are fully quantized, while the remaining operators are quantized as appropriate, ensuring proper layer-wise INT8 coverage.

## Evaluate the Quantized Model

After quantizing your model with Quark, it's important to verify that the quantization process hasn't significantly degraded model accuracy. 

Quark provides a tool to compare the differences between different inference results using L2 Loss and other metrics. 

1. Run Inference with float and quantized models

Run CPU inference inside the docker. It will run inferences on CPU and save input numpy vectors for NPU processing. This step is required before running inference on the board.  

```
python3 runmodel_pre_cpu.py
```

2. Evaluation between CPU and NPU inferences

Use the following command to compare inference results between CPU float (baseline) and CPU int8 (quantized) versions (running inside the docker):

```
python3 -m quark.onnx.tools.evaluate --baseline_results_folder output_cpu --quantized_results_folder output_cpu_vint8
```

Parameters:

* baseline_results_folder: Directory containing inference results from the baseline model 

* quantized_results_folder: Directory containing inference results from the model to be evaluated

Example output:

```
Mean cos similarity: 0.9866917729377747
Min cos similarity: 0.9761126041412354
Mean l2 distance: 11.406590461730957
Max l2 distance: 15.4661865234375
Mean psnr: 30.162633856137592
Min psnr: 26.91958248615265
Mean ssim: 0.9931399822235107
Min ssim: 0.9888220429420471
```

The evaluation tool provides several metrics to assess quantization quality. 

Metric Definitions:

| Metric | Description | Interpretation |
|--------|------------|----------------|
| Cosine Similarity | Measures how similar the output vectors are in direction (range: 0–1, where 1.0 = identical) | Higher is better. Values > 0.98 indicate good preservation of output patterns. |
| L2 Distance | Euclidean distance measuring numerical differences between outputs | Lower is better. Smaller values indicate outputs are numerically closer. |
| PSNR (Peak Signal-to-Noise Ratio) | Measures signal quality in decibels (dB) | Higher is better. Values > 30 dB indicate low quantization noise. |
| SSIM (Structural Similarity Index) | Measures structural similarity (range: 0–1, where 1.0 = identical structure) | Higher is better. Values > 0.99 indicate excellent structural preservation. |

For the ResNet50 model quantization above:

* Cosine Similarity: The high mean cosine similarity (0.9867) indicates strong directional consistency between the quantized and original model outputs.

* L2 Distance: The moderate L2 distance values suggest some numerical deviation, but not severe enough to significantly alter output behavior.

* PSNR: A mean PSNR of 30.16 dB reflects acceptable signal fidelity with manageable quantization noise.

* SSIM: The very high SSIM (0.9931 mean) demonstrates excellent structural preservation after quantization.

**Note**: Metric thresholds may vary across different model architectures, as models can respond differently to quantization effects. Therefore, these metrics should be interpreted alongside task-level performance results for a reliable evaluation.

In addition to the built-in evaluation capabilities, you can also develop and integrate domain specific post-processing scripts. These scripts can be tailored to specific use cases, enabling more detailed comparison of results or the computation of custom accuracy metrics. 

Next, we evaluate the quantized ResNet50 model using the ImageNet dataset to measure its task-level performance.

3. Evaluate the quantized model with ImageNet dataset

Run the following command inside the docker:

```
python3 evaluate.py --model models/resnet50-v1-12_quantized.onnx --data ../datasets/imagenet-1k/val_data/ --target=cpu
```

Example output:

```
===== Evaluation Result =====
Top-1 Accuracy: 71.09%
Top-5 Accuracy: 91.54%
```

There is a slight accuracy drop after quantization, which is within the expected range.

Next, proceed to the Vitis AI compilation step.

## Compile and Deploy the Quantized Model

1. Compile the Quantized Model in Vitis AI

Inside the docker, run Vitis AI compilation with the following command:

```
python3 compile.py
```

It takes time to compile the model. After compilation you get the following output:

```
subpartition path = my_cache_dir/resnet50-v1-12_quantized/vaiml_par_0/0
INFO: [VAIP-VAIML-PASS] No. of Operators :
INFO:  VAIML     491
INFO: [VAIP-VAIML-PASS] No. of Subgraphs :
INFO:    NPU     1
INFO: [VAIP-VAIML-PASS] For detailed compilation results, please refer to my_cache_dir/resnet50-v1-12_quantized/final-vaiml-pass-summary.txt
```

You can get more details about the compilation results by displaying the content of ``my_cache_dir/resnet50-v1-12_quantized/final-vaiml-pass-summary.txt`` :

```
--------- Final Summary of VAIML Pass ----------
OS: Linux X64
VAIP commit: bd5c863c3084e7534f5cd59173c631bb8fa07491
Model: /Resnet/models/resnet50-v1-12_quantized.onnx
Model signature: 5633833a66fc76d4758bcb34d9f75ee3
Device: ve2-xc2ve3858
Model data type: int8 quantized
Device data type: int8
Number of operators in the model: 493
GOPs of the model: 8.09703
Number of operators supported by VAIML: 491 (99.594%)
GOPs supported by VAIML: 8.096 (99.993%)
Number of subgraphs supported by VAIML: 1
Number of operators offloaded by VAIML: 491 (99.594%)
GOPs offloaded by VAIML: 8.096 (99.993%)
Number of subgraphs offloaded by VAIML: 1
Number of subgraphs with compilation errors (fall back to CPU): 0
Number of subgraphs below 20% GOPs threshold (fall back to CPU): 0
Number of subgraphs above max number of subgraphs allowed(7): 0 (fall back to CPU)
Stats for offloaded subgraphs
Subgraph vaiml_par_0 stats:
    Type: npu
    Operators: 491 (99.594%)
    GOPs : 8.096 (99.993%)  OPs: 8,096,419,216
```


Refer to Vitis AI User Guide for Versal AI Edge Series Gen 2, set up the VEK385 evaluation kit. On the board, run inference with the following command:

```
python3 runmodel.py
```

The script runs three inferences of the model and displays messages similar to the following:

```
Inference done
```

If you want to see detailed NPU execution logs, set the environment variable ``DEBUG_LOG_LEVEL=info`` before running the script:

```
export DEBUG_LOG_LEVEL=info
python runmodel.py
```

The output includes the number of operators offloaded to the NPU and the number of NPU-executed subgraphs:

```
I20380419 04:55:16.083313  2046 stat.cpp:193] [Vitis AI EP] No. of Operators :
I20380419 04:55:16.083366  2046 stat.cpp:204]  VAIML   491
I20380419 04:55:16.083384  2046 stat.cpp:204] VITIS_EP_CPU     2
I20380419 04:55:16.083395  2046 stat.cpp:213]
I20380419 04:55:16.083404  2046 stat.cpp:218] [Vitis AI EP] No. of Subgraphs :
I20380419 04:55:16.083410  2046 stat.cpp:226]    NPU     1
I20380419 04:55:16.083419  2046 stat.cpp:229] Actually running on NPU      1
I20380419 04:55:16.088616  2046 vitisai_compile_model.cpp:1477] AVG CPU Usage 93.3333%
I20380419 04:55:16.088671  2046 vitisai_compile_model.cpp:1478] Peak Working Set size 129.445 MB
2038-04-19 04:55:16.093139780 [W:onnxruntime:, session_state.cc:1316 VerifyEachNodeIsAssignedToAnEp] Some nodes were not assigned to the preferred execution providers which may or may not have an negative impact on performance. e.g. ORT explicitly assigns shape related ops to CPU to improve perf.
2038-04-19 04:55:16.093185510 [W:onnxruntime:, session_state.cc:1318 VerifyEachNodeIsAssignedToAnEp] Rerunning with verbose output on a non-minimal build will show node assignments.
[2038-04-19 04:55:16.146] [console] [info] [FLEXMLRT] FlexMLClient.cpp:1269 FlexMLRT Git Hash: 512d4e65
Inference done
```

## Evaluation between CPU and NPU inferences

1. After obtaining the CPU and NPU inference results of the quantized model in the previous steps, run the following command inside the Docker to compare their output similarity:

```
python3 -m quark.onnx.tools.evaluate --baseline_results_folder output_cpu_vint8 --quantized_results_folder output_vek385
```

Example output:

```
Mean cos similarity: 0.9987082481384277
Min cos similarity: 0.9984074831008911
Mean l2 distance: 3.657916784286499
Max l2 distance: 4.2315778732299805
Mean psnr: 38.722272316614784
Min psnr: 36.79283380508423
Mean ssim: 0.9994073510169983
Min ssim: 0.9993097186088562
```

The CPU and NPU inference results are nearly identical. Any differences are negligible and well within acceptable limits, so the NPU faithfully reproduces the CPU outputs:

* Mean cosine similarity (0.9987) and minimum (0.9984): The output vectors are almost perfectly aligned in direction, indicating excellent preservation of feature patterns.

* Mean L2 distance (3.66) and max (4.23): The numerical differences between CPU and NPU outputs are very small, showing minimal deviation.

* Mean PSNR (38.72 dB) and minimum (36.79 dB): Very high signal quality, indicating low quantization or numerical noise.

* Mean SSIM (0.9994) and minimum (0.9993): Structural similarity is near perfect, meaning the output structure is almost identical.

2. Evaluate the quantized ResNet50 model using the ImageNet dataset to measure its task-level performance on NPU

Evaluate the quantized int8 model accuracy on NPU (running on the board):

```
python3 -m pip install pillow tqdm torch torchvision
python3 evaluate.py --model models/resnet50-v1-12_quantized.onnx --data ../datasets/imagenet-1k/val_data/ --target=npu --cache_key resnet50-v1-12_quantized
```

Example output:

```
===== Evaluation Result =====
Top-1 Accuracy: 71.15%
Top-5 Accuracy: 91.52%

Total Inference Time (pure): 188.3308 seconds
Inference FPS: 265.49 images/second
```

The NPU inference results closely match the CPU results, with virtually identical Top-1 and Top-5 accuracies.

## Accuracy Summary:

The table below compares model accuracy across data types and platforms:

| Platform & Data Type | Accuracy (Top1) | Accuracy (Top5)| 
|----------------------|----------------:|---------------:|
| CPU FP32             | 74.11%          | 91.72%         | 
| CPU INT8             | 71.09%          | 91.54%         | 
| NPU INT8             | 71.15%          | 91.52%         | 

## Optional: Quantization Using Random Data

To use random data rather than calibration images, replace ``quantize.py`` with:

```
import os
import cv2
import numpy as np
from torchvision import transforms

from quark.onnx.quantization.config import Config, get_default_config
from quark.onnx import ModelQuantizer
from onnxruntime.quantization import QuantType

quant_config = get_default_config("VINT8")
quant_config.extra_options["Int32Bias"] = False
quant_config.enable_npu_cnn = True
quant_config.extra_options['UseRandomData'] = True

# Set up quantization with a specified configuration
quantization_config = Config(global_quant_config=quant_config)
quantizer = ModelQuantizer(quantization_config)
float_model_path = "models/resnet50-v1-12.onnx"
quantized_model_path = "models/resnet50-v1-12_quantized.onnx"
# Quantize the ONNX model and save to specified path
quantizer.quantize_model(float_model_path, quantized_model_path, calibration_data_reader=None)
```

Execute:

```
python3 quantize.py
```

Other steps remain same for quantization with calibration data or random data.

For more information about Quark quantization, please refer to https://quark.docs.amd.com/latest/

## Summary

By completing this tutorial, you learned:

1. INT8 quantization workflow with AMD Quark.

2. The compilation and deployment of Vitis AI quantized model on the board.

3. End-to-End evaluation of model accuracy with ImageNet dataset.

<p class="sphinxhide" align="center"><sub>Copyright © 2024–2026 Advanced Micro Devices, Inc.</sub></p>

<p class="sphinxhide" align="center"><sup><a href="https://www.amd.com/en/corporate/copyright">Terms and Conditions</a></sup></p>
