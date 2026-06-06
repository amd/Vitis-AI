## Sources to build the Vitis platform design, Linux components and Vitis AI Engine example application for VEK385

## Copyright and license statement

Copyright (C) 2025 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.

### Prerequisites
Before building any of these sources, ensure the following environment is set:
```markdown
- AMD Vivado and Vitis (version 2025.2) are installed
- Required AR patches for Vivado and Vitis are applied
- The system has a minimum of 100 GB free disk space (required for the Yocto build)
- export YOCTO_TMP_DIR=<NFS mount path> Set this variable only if using an NFS-mounted path for Yocto builds
```
### To build the hardware platform, software, and example vitis application using a single script, run:
```
source create_all_images.sh
```
It generates the output files in the `artifacts` folder

### Build Steps (Individual Components)
#### 1. Vitis platform design

Run the following script to create the hardware platform
```bash
source create_pfm_hw.sh
```
 It generates the hardware artifacts in the `hw` folder:

- The extensible Vitis platform (XSA) for Vitis application integration
- The fixed XSA for software artifact generation
#### 2. Software components (Yocto build)
Run the following script to create the software componets using yocto. 
```bash
source create_pfm_sw.sh
```
It generates the following output files in the `artifacts` folder:
- System Device Tree (SDT)
- BOOT.bin
- Linux kernel image
- Root filesystem 
#### 3. Vitis AI Engine example application
Run the following script to build programmable logic (PL) and AI Engine kernels
```bash
source create_vitis_app.sh
```
It generates PL and AI Engine PDI and overlay dtbo files in the `artifacts` folder:
- x_plus_ml.pdi
- x_plus_ml.dtbo
### Prepare the UFS boot image from artifacts
Before generating the UFS boot image:

- Install `sdk.sh` to set up the sysroot environment required for UFS image creation:

```bash
sdk.sh -y -d <path_to_sysroot_install>
```

- Source the sysroot environment:

```bash
unset LD_LIBRARY_PATH
source <path_to_sysroot_install>/environment-setup-*
```

Run the following script to generate the UFS boot image.  
This process may take a few minutes to complete.

```bash
source create_ufs_image.sh
```
