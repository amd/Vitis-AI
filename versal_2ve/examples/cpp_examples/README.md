# C++ examples

<!--
## Copyright and license statement

Copyright (C) 2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->


- **`ml_ort`** – An ONNX Runtime-based C++ application that runs ONNX models using the Vitis AI execution provider.
- **`ml_vart`** – A C++ application that runs Vitis AI compiled ONNX models using VART-ML APIs.
- **`x_plus_ml_ort`** – End-to-end [**X+ML**](../docs/glossary.md#amd-software-stacks) sample: a C++ application based on ONNX Runtime that runs ONNX models (machine learning components) using the Vitis AI execution provider. Components like preprocessing, postprocessing, and overlay (which displays post-processed predictions on the output) are implemented using VART-X APIs. It can scale up to 2 models and execute them sequentially on a temporal basis.
- **`x_plus_ml_vart`** – This application runs Vitis AI compiled ONNX models using VART-ML APIs. The compiled model should be entirely offloaded to the NPU to execute this application effectively. It follows the same [**X+ML**](../docs/glossary.md#amd-software-stacks) pattern as `x_plus_ml_ort` (PL preprocessing plus NPU inference with VART-X I/O). JSON can describe **1..N** models with spatial and/or temporal placement (see that app’s README).
- **`spatial_mt_ml_ort`**:  The spatial multi-threading inference application runs ResNet50 and ResNet18 models in separate threads showcasing the spatial loading of models on the AI Engine. Runs ML with ONNX Runtime APIs. Components like preprocessing, postprocessing, and overlay (which displays post-processed predictions on the output) are implemented using VART-X APIs.
- **`vart_zerocopy`** – A standalone VART zero-copy image pipeline sample (decode -> preprocess -> zero-copy IFM bind -> infer -> postprocess).
- **`vart_multimodel_seq`** – A VART-based multi-model sequential runner for temporal model execution.
- **`vart_multi_tenancy`** – A VART multi-tenancy sample for concurrent model execution scenarios.
- **`vart_infer_async`**: A VART-based async runner for non-blocking VART inference API demonstration.

See `../README.md` under the **examples** tree for the **Application profiles (summary)** tables (tutorials and C++ apps).

## Build Instructions for Cross-Compilation

### 1. Source Cross-Compilation Environment

Before compiling, set up the cross-compilation environment.  
**Important:** Unset `LD_LIBRARY_PATH` to avoid conflicts with host libraries.

```bash
unset LD_LIBRARY_PATH
source <sysroot>/environment-setup-cortexa72-cortexa53-amd-linux
```
Replace <sysroot> with the actual path to your sysroot directory.


### 2. Compile the applications

After sourcing the environment, compile the project using:
```bash
make
```
It creates install/applications.tar.gz . User can copy this to target and extract in the root directory.
```
scp install/applications.tar.gz <TARGET_BOARD>:/
#Extract on target board
cd /
tar -xvzf applications.tar.gz -C /
```



To clean the build artifacts, use:
```bash
make clean
```

### 3. Compile individual application
One can build or clean individual components by running:
```bash
make <subdir>
make -C <subdir> clean
```

Replace <subdir> with the name of the subdirectory (e.g., ml_ort, x_plus_ml_ort, x_plus_ml_vart, etc.,).

## Run on the target board

Complete the following on the board before launching applications:

- Complete board setup (OSPI / SD).
- Program the required PL and AI Engine overlay on the board.
- Configure the board and runtime environment as needed for your image. After boot, set `LD_LIBRARY_PATH` once in the shell where you run binaries:

```bash
export LD_LIBRARY_PATH=/usr/lib/python3.12/site-packages/voe/lib:\
/usr/lib/python3.12/site-packages/flexmlrt/lib:\
/usr/lib/python3.12/site-packages/onnxruntime/capi
```

- For prebuilt runs, confirm required assets exist under `/etc/vai/models/` and application JSON under `/etc/vai/<app_name>/json_configs/` (see each app README).

Commands, JSON paths, and data locations for each app are documented in that app's subdirectory **README**.

## Recommended Starting Points

- **New to VART and want the simplest path:** start with [ml_vart](ml_vart/README.md), then [ml_ort](ml_ort/README.md).

- **decode -> preprocess -> infer -> postprocess, Zero-copy:** [vart_zerocopy](vart_zerocopy/README.md).

- **Other VART-ML patterns (async runner, multi-model, spatial/temporal):** [vart_infer_async](vart_infer_async/README.md), [vart_multimodel_seq](vart_multimodel_seq/README.md), and [vart_multi_tenancy](vart_multi_tenancy/README.md).

- **Full decode -> preprocess -> infer -> postprocess -> overlay image or video pipelines:** [x_plus_ml_vart](x_plus_ml_vart/README.md), [x_plus_ml_ort](x_plus_ml_ort/README.md), and [spatial_mt_ml_ort](spatial_mt_ml_ort/README.md).
