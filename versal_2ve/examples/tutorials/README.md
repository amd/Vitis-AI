# Tutorials

## Copyright and license statement

Copyright (C) 2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.

---

Guided **Python** tutorials for Versal AI Edge Series Gen 2: prepare models in the Vitis AI Docker on the host, run **Vitis AI compilation** (`compile.py`), then run **ONNX Runtime with the VitisAI Execution Provider** on the target (`runmodel.py`). Full steps, Docker mounts, and board setup are in each tutorial's README.

## Layout

```text
tutorials/
├── README.md                 # This file
├── cpu_subgraph/             # YOLOv7 CPU/NPU partition tutorial with in-graph NMS; Python sources + README
├── resnet18_bf16/            # ResNet-18 end-to-end (BF16); Python sources + README
├── resnet50_bf16_cifar10/    # ResNet-50 CIFAR-10 end-to-end (BF16); pretrained weights + Python sources + README
├── resnet50_quark/           # ResNet50 INT8 with AMD Quark; Python sources + README
├── resnet50Cpp/              # ResNet50: compile then C++ ORT on target
├── yolov8m/                  # YOLOv8m object detection (Quark VINT8 → compile → ORT); sources + README
└── yolox_nano_int8/          # YOLOX-Nano INT8 eval, compile, optional VART power app
```

## Tutorials (summary)

| Tutorial | Role | Quant | Main scripts |
| --- | --- | --- | --- |
| [**resnet18_bf16**](resnet18_bf16/) | Export ONNX, compile with Vitis AI, deploy and run ORT on the board; optional CPU vs NPU comparison | BF16 via compiler | `export_to_onnx.py`, `compile.py`, `runmodel.py` |
| [**resnet50_bf16_cifar10**](resnet50_bf16_cifar10/) | CIFAR-10 fine-tuned ResNet-50: download dataset, export ONNX from the bundled weights, compile, classify on CPU/NPU | BF16 via compiler | `prepare_model_data.py`, `compile.py`, `predict.py`, `runmodel.py` |
| [**resnet50_quark**](resnet50_quark/) | Download ONNX, Quark INT8 quantization, compile, evaluate accuracy, on-target inference | INT8 (Quark) | `quantize.py`, `compile.py`, `evaluate.py`, `runmodel.py`, `runmodel_pre_cpu.py` |
| [**yolov8m**](yolov8m/) | YOLOv8m detection: export, Quark VINT8 (with skip-nodes), compile, NPU timing / config tuning, on-target ORT inference | INT8 VINT8 (Quark); compiler BF16 tail per tutorial | `models/export_to_onnx.py`, `quantize.py`, `compile.py`, `evaluate.py`, `run_inference.py` |
| [**cpu_subgraph**](cpu_subgraph/) | Heterogeneous NPU+CPU partitioned execution using VART-ML, demonstrated with YOLOv7 with in-graph NMS | INT8 + CPU FP32 tail (mixed precision) | `export_yolov7_nms_onnx.py`, `quantize_int8.py`, `compile.py`, `prepare_input.py`, `postprocess_bin_output.py` |
| [**resnet50Cpp**](resnet50Cpp/) | ResNet50: download ONNX, compile, cross-compile C++ ORT app, run on the board | FP32 ONNX compiled for NPU | `compile.py`, `input.cpp`, `CppAppCompile.sh`, `runmodel.py` |
| [**yolox_nano_int8**](yolox_nano_int8/) | YOLOX-Nano INT8: depthwise-to-regular Conv, COCO eval, compile; optional VART power profiling | INT8 Quark | `compile.py`, `evaluate.py`, `ml_vart_power.cpp` |
