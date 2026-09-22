<!--
Copyright (C) 2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0.

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->

# VEK385 Reference Design

This directory contains the [**X+ML**](../../examples/docs/glossary.md#amd-software-stacks) reference design for the **VEK385** evaluation board — a platform that combines **PL + VART-X** preprocessing/postprocessing (**X**) with **AI Engine ML** inference (**ML**). Use it to build the hardware platform, Linux software stack, and runtime environment needed for AI inference on the AI Engines together with PL HLS kernels.

## What's here

| Folder | Purpose |
|--------|---------|
| [`rev-a/`](rev-a/) | **VEK385 AI Reference Design (Rev-A)** — File-based AI inferencing with PL preprocessing and AI Engine inference. Includes Vivado/Vitis design, Yocto Linux, and build artifacts. |
| [`rev-b/`](rev-b/) | **VEK385 Rev-B Reference Designs** — Vivado/Vitis design, Yocto Linux, and build artifacts. This folder contains two reference designs: <br>• **VEK385 AI Reference Design** — file-based inferencing. <br>• **VEK385 MIPI Streaming AI Reference Design** — 4-camera MIPI streaming, AI inferencing, and 4K display. See [docs/VEK385_MIPI_Streaming_AI_Reference_Design.md](docs/VEK385_MIPI_Streaming_AI_Reference_Design.md) |

Pick the folder that matches your board revision. For Rev-B, follow the AI or MIPI Streaming AI readme for the design you are building.

## Prerequisites for Platform Build

```text
Vivado Version : 2026.1
Host OS        : Ubuntu 22.04 LTS
```

Source the required environment variables from the bash shell:

```bash
source <VITIS_INSTALL_PATH>/2026.1/Vitis/settings64.sh

# Set this variable only when using an NFS-mounted path for Yocto builds
export YOCTO_TMP_DIR=<path_to_yocto_tmp_dir>
```

Apply the following Vivado tool patches **before** running `create_pfm_hw.sh` or `create_vitis_app.sh`:

- [AR000040517](https://adaptivesupport.amd.com/s/article/000040517?language=en_US)
- [AR000040616](https://adaptivesupport.amd.com/s/article/000040616?language=en_US)

```bash
export XILINX_PATH=<AR000040517_PATCH_PATH>/vivado:<AR000040616_PATCH_PATH>/vivado
```

## Build Steps

- **Rev-B** — see [rev-b/README.md](rev-b/README.md)
- **Rev-A** — see [rev-a/README.md](rev-a/README.md)
