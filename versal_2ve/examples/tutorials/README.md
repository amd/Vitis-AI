# Tutorials

## Copyright and license statement

Copyright (C) 2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.

---

Guided **Python** tutorials for Versal AI Edge Series Gen 2: prepare models in the Vitis AI Docker on the host, run **Vitis AI compilation** (`compile.py`), then run **ONNX Runtime with the VitisAI Execution Provider** on the target (`runmodel.py`). Full steps, Docker mounts, and board setup are in each tutorial’s README.

## Layout

```text
tutorials/
├── README.md                 # This file
├── resnet18_bf16/            # ResNet-18 end-to-end (BF16); Python sources + README
├── resnet50_quark/           # ResNet50 INT8 with AMD Quark; Python sources + README
└── yolov8m/                  # YOLOv8m object detection (Quark VINT8 → compile → ORT); sources + README
```

## Tutorials (summary)

| Tutorial | Role | Quant | Main scripts |
|----------|------|-------|----------------|
| [**resnet18_bf16**](resnet18_bf16/) | Export ONNX, compile with Vitis AI, deploy and run ORT on the board; optional CPU vs NPU comparison | BF16 via compiler | `export_to_onnx.py`, `compile.py`, `runmodel.py` |
| [**resnet50_quark**](resnet50_quark/) | Download ONNX, Quark INT8 quantization, compile, evaluate accuracy, on-target inference | INT8 (Quark) | `quantize.py`, `compile.py`, `evaluate.py`, `runmodel.py`, `runmodel_pre_cpu.py` |
| [**yolov8m**](yolov8m/) | YOLOv8m detection: export, Quark VINT8 (with skip-nodes), compile, NPU timing / config tuning, on-target ORT inference | INT8 VINT8 (Quark); compiler BF16 tail per tutorial | `models/export_to_onnx.py`, `quantize.py`, `compile.py`, `evaluate.py`, `run_inference.py` |

