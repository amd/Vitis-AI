<!--
## Copyright and license statement

Copyright (C) 2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0.

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->


## Repository layout (under `examples/`)

```text
examples/
├── README.md
├── data/                          # IFM, JPEGs, test vectors for on-target use (see data/)
├── utilities/                     # Host-side helpers (e.g. jpeg_to_binary.py → IFM .bin; see utilities/)
├── tutorials/                     # Guided tutorials (see tutorials/)
│   ├── resnet18_bf16/             # ResNet-18 BF16 flow
│   ├── resnet50_quark/            # ResNet50 INT8 with AMD Quark
│   ├── yolov8m/                   # YOLOv8m detection: Quark VINT8, compile, ORT on target
│   └── README.md
├── python_examples/               # Python ORT + VitisAI EP on the embedded target (see python_examples/)
│   ├── run_ResNet50_vitisai.py
│   └── README.md                  # ORT+VitisAI EP usage
└── cpp_examples/                  # C++ applications for the embedded target (see cpp_examples/)
    ├── common/                    # Shared code (e.g. utility_timer) linked by several apps
    ├── ml_ort/
    ├── ml_vart/
    ├── x_plus_ml_ort/
    ├── x_plus_ml_vart/
    ├── spatial_mt_ml_ort/
    ├── vart_zerocopy/
    ├── vart_infer_async/
    ├── vart_multimodel_seq/
    ├── vart_multi_tenancy/
    └── README.md                  # app catalog
```

Paths are relative to this `examples/` directory.

## Documentation

Per-app details live in each application **README** (see also **[cpp_examples](cpp_examples/)** and **[python_examples](python_examples/)**). For acronyms used in the tables below (IFM, OFM, NPU, EP, ORT, VART, etc.), see **[docs/glossary.md](docs/glossary.md)**.

The tables below are the reference overview:

1. **Tutorials** under [`tutorials`](tutorials/) (host Docker + on-target ORT).
2. **Python samples** for [`python_examples`](python_examples/) (ORT + VitisAI EP from Python).
3. **Each Cpp app:** use case, runtime stack, flow type, and multi-model / AI Engine placement.
4. **Vitis AI compiler feature** and which apps exercise it.

### Tutorials (`tutorials`)

| Package | Language | Role | Vitis AI compile | ORT on target | Quant | Typical data |
|---------|----------|------|---------------|---------------|-------|--------------|
| [`resnet18_bf16`](tutorials/resnet18_bf16/) | Python | ResNet-18: export ONNX → Vitis AI compile → deploy; `runmodel.py` compares CPU vs NPU (e.g. RMSE) | Yes (`compile.py` in Docker) | Yes (`runmodel.py` on board) | BF16 (compiler from FP32 ONNX) | ImageNet-style validation; ONNX under `models/` |
| [`resnet50_quark`](tutorials/resnet50_quark/) | Python | ResNet50: Quark INT8 quant → compile → accuracy on CPU/NPU → on-target inference | Yes (`compile.py` in Docker) | Yes (`runmodel.py`; `runmodel_pre_cpu.py` for host checks) | INT8 (AMD Quark calibration) | ImageNet val / calibration JPEGs; ONNX under `models/` |
| [`yolov8m`](tutorials/yolov8m/) | Python | YOLOv8m: Quark VINT8 (skip-nodes), compile, latency tuning, ORT EP on board | Yes (`compile.py` in Docker) | Yes (`run_inference.py` on board) | INT8 VINT8 + BF16 tail (per tutorial) | Calibration / val images; COCO-style labels |

### Python samples (`python_examples`)

| Script | Role | Stack | Input | Output |
|--------|------|-------|-------|--------|
| [`run_ResNet50_vitisai.py`](python_examples/run_ResNet50_vitisai.py) | IFM `.bin` → ORT infer → OFM `.bin`; optional top-k softmax | `ORT+EP` | Raw tensor `.bin` (shape/dtype from ONNX) | OFM `.bin` per output + optional stdout labels |

### 1. Applications — use cases and runtime profiles

#### Runtime stack legend

| Tag | Full name |
|-----|-----------|
| `ORT+EP` | ONNX Runtime with VitisAI Execution Provider |
| `VART-ML` | Vitis AI runtime ML API (`vart::Runner`) |
| `VART-X` | VART-X I/O pipeline (decode, preprocess, postprocess, overlay) |

#### Infer-only apps

| App | Use Case | Stack | Multi-Model / AI Engine placement | Input | Output |
|-----|----------|-------|-----------------------------|-------|--------|
| [`ml_ort`](cpp_examples/ml_ort/) | Minimal inference: IFM + JSON via ORT | `ORT+EP` | — | Raw quantized tensors (INT8/NCHW per JSON) | OFM logits / blobs per ONNX graph |
| [`ml_vart`](cpp_examples/ml_vart/) | VART-ML inference (normal / dry-run / bench) | `VART-ML` | AI Engine column placement configurable via JSON | Packed NPU tensors (dtype per compiled model) | Packed tensors (INT8/BF16 per metadata) |
| [`vart_multimodel_seq`](cpp_examples/vart_multimodel_seq/) | Sequential multi-model loop, temporal partition swap | `VART-ML` | Temporal sharing via JSON | IFM .bin per model step (quantized per JSON) | OFM tensors per model + logs / metrics |
| [`vart_multi_tenancy`](cpp_examples/vart_multi_tenancy/) | Concurrent multi-model, multi-tenancy (thread per model) | `VART-ML` | Spatial / temporal / combined via JSON | IFM .bin per runner (per JSON) | OFM tensors per thread / model + logs |
| [`vart_infer_async`](cpp_examples/vart_infer_async/) | Async inference scheduling | `VART-ML` | - |Packed NPU tensors (dtype per compiled model)  | Packed tensors (INT8/BF16 per metadata) |

#### E2E vision apps

| App | Use Case | Stack | Multi-Model / AI Engine placement | Input | Output |
|-----|----------|-------|-----------------------------|-------|--------|
| [`x_plus_ml_ort`](cpp_examples/x_plus_ml_ort/) | Full vision pipeline: decode → preprocess → NPU → postprocess / overlay | `ORT+EP` + `VART-X` | 2 models, temporal sharing | JPEG frames (decoded BGR / NV12) | BGR display frame + overlay + detection scores |
| [`x_plus_ml_vart`](cpp_examples/x_plus_ml_vart/) | Full vision pipeline or infer-only (JSON-driven) † | `VART-ML` + `VART-X` | 1..N models; spatial / temporal per config | JPEG / NV12 / BGR (E2E) or IFM .bin (infer-only) | BGR + overlay (E2E) or OFM / labels (infer-only) |
| [`spatial_mt_ml_ort`](cpp_examples/spatial_mt_ml_ort/) | Two parallel vision pipelines, spatial AI Engine column split | `ORT+EP` + `VART-X` | 2 models, spatial + 2 threads | JPEG (one stream per pipeline) | BGR + overlay + per-model labels / scores |
| [`vart_zerocopy`](cpp_examples/vart_zerocopy/) | Single-image E2E with zero-copy IFM (BO as runner input) | `VART-ML` + `VART-X` | — | JPEG (host decode to BGR `cv::Mat`) | Top-k labels + softmax scores (stdout) |

† **`x_plus_ml_vart`**: infer-only vs full E2E depends on JSON enablement of preprocess/postprocess (and related pipeline stages).

### 2. Feature coverage matrix

| Feature | `ml_ort` | `ml_vart` | `x_plus_ml_ort` | `x_plus_ml_vart` | `spatial_mt_ml_ort` | `vart_zerocopy` | `vart_multimodel_seq` | `vart_multi_tenancy` | `vart_infer_async` |
|---------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **ORT + VitisAI EP** | ✓ | | ✓ | | ✓ | | | | |
| **VART-ML runner** | | ✓ | | ✓ | | ✓ | ✓ | ✓ | ✓ |
| **VART-X I/O pipeline** | | | ✓ | ✓ | ✓ | ✓ | | | |
| **Infer-only flow** | ✓ | ✓ | | | | | ✓ | ✓ | |
| **E2E vision flow** | | | ✓ | ✓ | ✓ | ✓ | | | |
| **Multi-model (JSON)** | | | ✓ | ✓ | | | ✓ | ✓ | |
| **Temporal sharing** | | | ✓ | ✓ | | | ✓ | ✓ | |
| **Spatial column split** | | | | ✓ | ✓ | | | ✓ | |
| **Sequential multi-model** | | | | | | | ✓ | | |
| **Concurrent multi-tenancy** | | | | | | | | ✓ | |
| **Overlay / visualization** | | | ✓ | ✓ | ✓ | | | | |
| **Zero-copy / HW tensor** | | ✓ | | ✓ | | ✓ | | | |
| **Mixed precision** | | ✓ | | ✓ | | | | | |
| **CPU partition** | | ✓ | | | | | | | |
| **Async inference API** | | ✓ | | | | | | | ✓ |

#### Feature descriptions

| Feature | Description |
|---------|-------------|
| **ORT + VitisAI EP** | Model-driven inference through the ONNX Runtime C/Cpp or Python API with the VitisAI Execution Provider targeting the NPU |
| **VART-ML runner** | Direct Vitis AI runtime ML API (`vart::Runner`): IFM/OFM tensors, batching, compiled `.rai` / partition layout |
| **VART-X I/O pipeline** | Decode, preprocess, postprocess, overlay (vision graphs around ORT or VART-ML) |
| **Infer-only flow** | Precomputed IFM or minimal host path; no full decode → pre → post vision chain in-app |
| **E2E vision flow** | Image or buffer → preprocess → NPU → postprocess (and optionally overlay) in one application |
| **Multi-model (JSON)** | Number of models, order, and policies driven by application JSON |
| **Temporal sharing** | Models take turns on the same NPU columns (time-sharing); one runs, then another |
| **Spatial column split** | Each model uses a different slice of NPU columns (no overlap), enabling parallel execution |
| **Sequential multi-model** | One thread runs a fixed sequence of models in order (e.g. A → B → … → repeat) |
| **Concurrent multi-tenancy** | Multiple threads/runners in parallel to stress allocator and placement |
| **Overlay / visualization** | On-frame labels or meta-convert style output where applicable |
| **Zero-copy / HW tensor** | Runner or pipeline options for HW-backed tensors where the stack exposes it |
| **Mixed precision** | Compilation option to strategically apply different numerical precisions across model operations. |
| **CPU partition** | Compiler splits the graph across NPU and CPU. |
| **Async inference API** | Non-blocking inference API demonstration |
