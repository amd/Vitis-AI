# Glossary — Versal AI Edge Series Gen 2 examples

<!--
Copyright (C) 2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0.

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->

Acronyms and terms used in the [`examples/`](../) tree (README tables, tutorials, and C++ application docs).

## Inference tensors and runtime

| Term | Meaning |
|------|---------|
| **IFM** | *Input feature map* — input tensor data fed to a model (often a `.bin` file or buffer from preprocessing). |
| **OFM** | *Output feature map* — output tensor data produced by inference (logits, detections, etc.). |
| **NPU** | *Neural processing unit* — the Versal AI Engine ML inference accelerator targeted by Vitis AI compilation. |
| **BO** | *Buffer object* — XRT memory buffer used for zero-copy data transfer between PL, NPU, and host (see `vart_zerocopy`). |
| **EP** | *Execution provider* — ONNX Runtime add-on that runs model operators on a chosen device (not only the CPU). In these examples, **EP** usually means the **Vitis AI Execution Provider** (see below; code name `VitisAIExecutionProvider`), which sends compiled work to the **NPU**. |
| **ORT** | *ONNX Runtime* — framework used to load ONNX models and invoke execution providers (Python or C++). |

## AMD software stacks

| Term | Meaning |
|------|---------|
| **Vitis AI** | AMD development stack for compiling and running AI inference on adaptive platforms. |
| **Vitis AI Execution Provider** | ONNX Runtime execution provider that partitions and runs supported operators on the NPU. Often abbreviated **VitisAI EP** in logs and code. |
| **VART** | *Vitis AI Runtime* — C++ runtime API for loading compiled models and running inference (`vart::Runner`, tensor I/O). |
| **VART-ML** | VART APIs for NPU inference via `vart::Runner` (compiled models, IFM/OFM tensors). Used in `ml_vart`, `x_plus_ml_vart`, and related samples. |
| **VART-X** | VART *extended* APIs — the **X** side of **X+ML**: preprocessing, postprocessing, and overlay around NPU inference (often with ORT+EP or VART-ML). |
| **X+ML** | *X plus ML* — **X** is the *extended* interface for a vision pipeline (**VART-X** and **PL**, for example preprocess / postprocess / overlay); **ML** is **AI Engine ML** (NPU inference). Used together in one platform overlay or end-to-end app (`x_plus_ml_ort`, `x_plus_ml_vart`, etc). |
| **`x_plus_ml`** | Naming prefix for **X+ML** example apps and artifacts (`x_plus_ml_ort`, `x_plus_ml_vart`, `x_plus_ml.pdi`, `x_plus_ml.xclbin`). |

## Platform and hardware

| Term | Meaning |
|------|---------|
| **PL** | *Programmable logic* — FPGA fabric; runs HLS kernels such as `image_processing` for video preprocessing. |
| **AI Engine** | Versal AI Engine tiles used for ML compute (see **NPU** in application docs). |
| **AIE-ML_v2** | AI Engine ML architecture generation used on Versal AI Edge Series Gen 2. |
| **PDI** | *Programmable device image* — bitstream/overlay package programmed onto the device (e.g. PL + AI Engine overlay). |
| **DTB** | *Device tree blob* — Linux device-tree overlay applied with the platform image at boot. |
| **xclbin** | Xilinx binary container for PL kernels and connectivity (e.g. `x_plus_ml.xclbin` on the target filesystem). |
