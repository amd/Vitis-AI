## Vitis X+ML application build for VEK385

**X+ML** — PL/VART-X vision pipeline interface plus AI Engine ML inference; see [examples glossary](../../../examples/docs/glossary.md#amd-software-stacks).

## Copyright and license statement

Copyright (C) 2025 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.

### Vitis Application Build

To build the Vitis application, ensure that the required platform is available in the `../hw/` folder.

Before building, source the Vitis environment (if not already sourced):
```
source <VITIS_INSTALL_PATH>/2025.2/Vitis/settings64.sh
```

Building this Vitis application generates:
- A **36-column AI Engine graph**
- An **image processing PL kernel**

Both of these kernels are linked with the extensible platform `.xsa`, which produces the **X+ML overlay PDI**.

### Usage
```
make clean
make all
```
