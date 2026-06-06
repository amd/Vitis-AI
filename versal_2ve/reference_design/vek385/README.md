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
| [`rev-a/`](rev-a/) | VEK385 Rev-A platform build (Vivado/Vitis design, Yocto Linux, artifacts) |
| [`rev-b/`](rev-b/) | VEK385 Rev-B platform build (Vivado/Vitis design, Yocto Linux, artifacts) |

Pick the folder that matches your board revision.

## Prerequisites for Platform Build

```text
Vivado Version : 2025.2
Host OS        : Ubuntu 22.04 LTS
```

Source the required environment variables from the bash shell:

```bash
source <VITIS_INSTALL_PATH>/2025.2/Vitis/settings64.sh

# Set this variable only when using an NFS-mounted path for Yocto builds
export YOCTO_TMP_DIR=<path_to_yocto_tmp_dir>
```

Download and apply both AR patches required for Vitis AI 6.2 platform builds:

- [AR000039757](https://adaptivesupport.amd.com/s/article/000039757?language=en_US) — Vivado 2025.2
- [AR000040013](https://adaptivesupport.amd.com/s/article/000040013?language=en_US) — Vitis 2025.2

Set `XILINX_PATH` to the patched Vivado and Vitis installations:

```bash
export XILINX_PATH=<AR000039757_Vivado_2025_2_preliminary_rev1>/vivado:<AR000040013_vitis_patch_external>/Vitis
```

## Build Steps

- **Rev-B** — see [rev-b/README.md](rev-b/README.md)
- **Rev-A** — see [rev-a/README.md](rev-a/README.md)
