<!--
Copyright (C) 2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0.

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->

# Vitis AI Model Cookbook

This cookbook provides recipes to help developers prepare, compile and execute performant AI model configurations using AMD Quark & Vitis AI for the VEK385, AMD's Versal AI Edge Series Gen 2 evaluation kit.

## What models are included?

The cookbook includes CNNs, Vision Transformers, and some hybrid architectures and the model list grows with each release of Vitis AI.

The cookbook illustrates how to go through the Vitis AI flow for a subset of supported models; the Vitis AI User Guide provides a complete view of models and operators (per data type) supported by the compiler – please consult the Vitis AI Gen2 documentation for details:

- [Vitis AI Gen2 Documentation](https://vitisai.docs.amd.com/projects/gen2/en/latest/index.html)

Developers can reuse the techniques and settings documented in the cookbook for other supported models or their custom models.

## Who Is This For?

- **ML engineers** deploying models to AMD edge AI hardware
- **System integrators** validating NPU performance with reference models
- **Researchers** exploring quantization and compilation for edge inference

## What's Included

| Folder | Description |
|--------|-------------|
| `Models/` | Self-contained model recipes (download → export → quantize → compile → run inference → evaluate) |
| `Utils/` | Shared compilation script (`compile.py`) for all models |

## Supported Models

| Model | Category | Task | Quantization | Dataset |
|-------|----------|------|--------------|---------|
| ResNet50 | CNN | Image Classification | INT8 | ImageNet-1k |
| YOLOv8n | CNN | Object Detection | INT8+BF16 | COCO |
| YOLOv8m (T2D4) | CNN | Object Detection | INT8+BF16 | COCO |
| YOLOX-Tiny | CNN | Object Detection | INT8 | COCO |
| YOLOX-S | CNN | Object Detection | INT8 | COCO |
| YOLOX-M | CNN | Object Detection | INT8 | COCO |
| RTMDet-M | CNN | Object Detection | INT8 | COCO |
| SegFormer-B0 | Hybrid | Semantic Segmentation | BF16 | Cityscapes |
| DINO ViT-B/16 | ViT | Feature Extraction | BF16 | ImageNet-1k |

> **Note:** This cookbook's recipes rely on open-source model sources that might change or become obsolete over time and without warning. AMD might remove models from the cookbook over time to ensure a consistent developer out of box experience.

---

## Prerequisites

### Hardware

- **Host machine**: Linux workstation (for model export, quantization, compilation)
- **Target board**: AMD VEK385 Versal AI Edge Series Gen 2 evaluation kit

### Software

| Environment | What It Provides |
|-------------|------------------|
| Host terminal | Python 3.10+, PyTorch, model-specific dependencies |
| Vitis AI Docker | AMD Quark (quantization), ONNX Runtime with VitisAI Execution Provider, compiler toolchain |
| Target board | FlexML runtime, VART, `ml_vart` / `x_plus_ml_vart` application |

For Docker setup and board bring-up, see:
- [Software Setup](https://vitisai.docs.amd.com/projects/gen2/en/latest/docs/setup_and_installation/software_setup.html) - VEK385 board setup and boot images
- [Docker Setup](https://vitisai.docs.amd.com/projects/gen2/en/latest/docs/setup_and_installation/docker-setup.html) - Vitis AI Docker container setup

---

## Quick Start

### 1. Clone and enter the cookbook

```bash
cd cookbook
```

### 2. Launch the Vitis AI Docker container

Mount the cookbook into the container (adjust paths for your system):

```bash
docker run -it --rm --ulimit stack=-1:-1 --network host \
    -v </yourLicenseDir>:/usr/licenses \
    -v $(pwd):/workspace \
    <Docker REPOSITORY:TAG> bash

# Inside the container:
cd /workspace
```

### 3. Pick a model and follow its README

Each model directory contains a self-contained README.md with step-by-step instructions:

```bash
cd Models/YOLOv8/yolov8n
cat README.md
```

---

## Typical Workflow

Each model recipe follows the same multi-environment workflow:

| Stage | Environment | Scripts |
|-------|-------------|---------|
| 1. Export | Host terminal (or Docker) | `export_*.py` |
| 2. Quantize | Vitis AI Docker | `quantize.py` (INT8 models only) |
| 3. Compile | Vitis AI Docker | `../../Utils/compile.py` |
| 4. Run Inference | Target board (VEK385) | `ml_vart` / `x_plus_ml_vart` --benchmark |
| 5. Evaluate | Target board (or Docker) | `evaluate*.py` |

### 1. Making the working directory writable

Before running quantization or compilation inside Docker, ensure the model directory is writable:

```bash
chmod -R a+w Models/<model_name>
```

### 2. Copying artifacts to the board

After compilation, copy the model directory to your target board:

```bash
scp -r Models/<model_name> amd-edf@<TARGET_IP>:~/
```

### 3. Board setup (once per boot)

On the target board, program the overlay and set library paths:

```bash
sudo fpgautil -b ./overlay/vpl_gen_fixed_pld.pdi -o ./overlay/pl_aiarm.dtbo

export LD_LIBRARY_PATH=/usr/lib/python3.12/site-packages/flexmlrt/lib/:/usr/lib/python3.12/site-packages/voe/lib/:/usr/lib/python3.12/site-packages/onnxruntime/capi:/usr/lib/python3.12/site-packages/vart_ml/lib/
```

---

## Directory Structure

```TXT
cookbook/
├── README.md                  # This file
├── Models
│   ├── requirements.txt       # Shared Python dependencies
│   ├── DinoViTB16
│   ├── Resnet50
│   ├── RTMDet-M
│   ├── Segformer
│   ├── YOLOv8
│   │   ├── yolov8m_t2d4
│   │   └── yolov8n
│   ├── YOLOX
│   │   ├── yolox-m
│   │   ├── yolox-s
│   │   └── yolox-tiny
│   └── ...                    # Other models follow the same pattern
└── Utils
    └── compile.py             # Shared compilation script
```

### Key Files in Each Model Directory

| File | Purpose |
|------|---------|
| `README.md` | Complete walkthrough for the model |
| `export_*.py` | Exports the model to ONNX from source checkpoint |
| `quantize.py` | Quantizes FP32 ONNX to INT8 (if applicable) |
| `evaluate*.py` | Runs accuracy evaluation (mAP, top-1/5, mIoU, etc.) |
| `vitisai_config.json` | VitisAI Execution Provider configuration |
| `vart_config.json` | Runtime configuration for ml_vart inference |
| `app_*.json` | Runtime configuration for x_plus_ml_vart inference |
| `input.bin` | Pre-generated device-native input for inference |

---

## Quantization Modes

| Mode | When to Use | Calibration Required |
|------|-------------|---------------------|
| **INT8 (Quark VINT8)** | Best throughput, CNNs, detection models | Yes (10-100 images) |
| **BF16** | Transformers, models sensitive to quantization | No |

**INT8 models** require running `quantize.py` with calibration data before compilation. **BF16 models** skip quantization entirely — the compiler converts FP32 → BF16 automatically.

---

## Additional Resources

### Vitis AI Gen2 Documentation
- [Vitis AI 6.2 User Guide for Versal AI Edge Series Gen2](https://vitisai.docs.amd.com/projects/gen2/en/latest/index.html) - Main documentation

### Setup and Installation
- [Software Setup](https://vitisai.docs.amd.com/projects/gen2/en/latest/docs/setup_and_installation/software_setup.html) - VEK385 board setup, boot images, environment configuration
- [Docker Setup](https://vitisai.docs.amd.com/projects/gen2/en/latest/docs/setup_and_installation/docker-setup.html) - Docker container setup for quantization and compilation

### Model Development
- [Model Compilation](https://vitisai.docs.amd.com/projects/gen2/en/latest/docs/model_compilation/mixed_precision_compilation.html) - Mixed precision compilation guide
- [ONNX Runtime C++ APIs](https://vitisai.docs.amd.com/projects/gen2/en/latest/docs/runtime_processes_and_apis/cpp_onnx_apis.html) - Runtime API documentation

### License
- [Obtaining License](https://vitisai.docs.amd.com/projects/gen2/en/latest/docs/additional_information/license.html) - AIE-MLv2 Compiler and Vivado ML Enterprise Edition license information

---

## License

This cookbook is licensed under the Apache License, Version 2.0. See the LICENSE file for details.
