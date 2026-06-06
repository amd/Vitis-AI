## Yocto rootfs,kernel Image build for vek385

## Copyright and license statement

Copyright (C) 2025 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.


### Usage
Run the Yocto build script to generate the **rootfs**, **Kernel Image**, **\*.wic** (for SD card), and the **sysroot**:

```bash
export YOCTO_TMP_DIR=<Set this variable only if user is using NFS mount path to run Yocto>
source create_yocto.sh


