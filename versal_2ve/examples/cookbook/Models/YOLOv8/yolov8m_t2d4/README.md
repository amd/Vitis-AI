# YOLOv8m (T2D4)

Object detection model: **YOLOv8m** (Ultralytics) on **COCO** (80 classes),
exported to ONNX at 640×640 with **T2D4 tensor-parallelism** (TP=2, DP=4).

| Property | Value |
| --- | --- |
| Source | `yolov8m.pt` (Ultralytics) |
| Input | `images` `[1, 3, 640, 640]` fp32, NCHW |
| Outputs | Detection outputs (boxes + scores, pre-NMS) |
| Opset | 22 |
| Device | ve2 (int8 + bfloat16, T2D4) |
| Parallelism | TP=2, DP=4 (Tensor-parallel + Data-parallel) |

---

## 0. Setup and Environment

This model spans **three environments**. Run each step in the right place:

| Stage | Where | What runs |
| --- | --- | --- |
| 1. Export | **Host terminal** (no Docker) | `export.py` |
| 2. Optimize | **Host terminal** (no Docker) | `optimize.py` |
| 3. Quantize | **Vitis AI Docker** (Linux workstation) | `quantize.py` |
| 4. Compile | **Vitis AI Docker** (Linux workstation) | `../../Utils/compile.py` |
| 5. Run inference | **Target board** (VEK385) | `x_plus_ml_vart` / `evaluate.py` |

For the full toolchain and board bring-up, follow the official
**Vitis AI Gen2 documentation**:
<https://vitisai.docs.amd.com/projects/gen2/en/latest/index.html>
(see the *Quick Start Guide* for board setup and *Development Flow* for Docker).

### Host dependencies (export + optimize)

The export and optimize steps run in your **host terminal**, *not* inside the
Docker container.

**Export + Optimize toolchain (isolated venv).** Create a Python virtual environment
with the required packages:

```bash
python -m venv export_venv
source export_venv/bin/activate
python -m pip install --upgrade pip setuptools wheel
python -m pip install \
    "ultralytics" \
    "onnx>=1.17" \
    "onnxruntime" \
    "onnxslim" \
    "onnxscript" \
    "numpy"
```

This creates a virtual environment with the Ultralytics YOLOv8 toolkit and ONNX tools
for model export and optimization.

Activate it before the export and optimize steps:

```bash
source export_venv/bin/activate
```

### Vitis AI Docker (quantization + compilation)

Model **quantization** (Section 3) and **compilation** (Section 4) must be run
**inside the Vitis AI Docker container**. Launch it *before* those steps, mounting
this cookbook into the container (adjust the license dir and `REPOSITORY:TAG` for
your install):

```bash
docker run -it --rm --network host \
    -v </yourLicenseDir>:/usr/licenses \
    -v <path>/<to>/cookbook:/cookbook \
    <Docker REPOSITORY:TAG> bash

# inside the container, initialize the environment:
cd /cookbook/Models/YOLOv8/yolov8m_t2d4
```

The `onnxruntime` VitisAI Execution Provider, `../../Utils/compile.py`, and AMD Quark
are provided by this Docker image.

**Install additional dependencies for quantization:**

Before running quantization, install the required Python packages inside the Docker
container:

```bash
# Install ultralytics and dependencies
python3 -m pip install ultralytics --no-deps
```

The `--no-deps` flag prevents pip from installing conflicting dependency versions that
might interfere with the Docker environment's pre-installed packages.

### Target board (run inference)

To run inference (Section 5) on the **target board**, first
**copy or mount this repository onto the board** (e.g. via `scp` or NFS) so the
compiled `.rai`, `vart_config.json` are available there:

```bash
# from the host, copy the yolov8m_t2d4 dir to the board (replace TARGET_IP):
scp -r yolov8m_t2d4 amd-edf@TARGET_IP:~/
```

Then, on the board (as root, once per boot), program the PL + AIE overlay via `fpgautil`
and set `LD_LIBRARY_PATH` for the runtime libraries:

```bash
# program the PL + AIE overlay (mandatory before any AI/ML app):
sudo fpgautil -b ./overlay/vpl_gen_fixed_pld.pdi -o ./overlay/pl_aiarm.dtbo

# runtime libraries (FlexML / VOE / VART):
export LD_LIBRARY_PATH=/usr/lib/python3.12/site-packages/flexmlrt/lib/:/usr/lib/python3.12/site-packages/voe/lib/:/usr/lib/python3.12/site-packages/onnxruntime/capi
```

See the Vitis AI Gen2 *Quick Start Guide* and the
[board setup guide](https://vitisai.docs.amd.com/projects/gen2/en/latest/docs/setup_and_installation/board_setup_ospi_sd_eou.html)
for the complete board bring-up (boot, overlay files, firmware).

---

## 1. Export the model

Run this in your **host terminal** (activate the `export_venv` virtual env from
Section 0 first). This step exports the YOLOv8m PyTorch model to ONNX format:

```bash
python export.py
```

Produces `yolov8m_fp32.onnx`.

The pipeline uses the Ultralytics YOLO export with a patched BN fusion for
numerical stability (divide form instead of rsqrt form), ensuring bit-exact
fold during export.

---

## 2. Optimize the model

Run this in your **host terminal** (keep the `export_venv` virtual env active from
Section 1). This step applies graph optimizations including DFL (Distribution
Focal Loss) simplification:

```bash
python optimize.py
```

Produces `yolov8m_optimized_fp32.onnx`.

The optimizer simplifies the DFL decode tail (distance2bbox) via k-means
clustering on the DFL softmax weights, replacing the full box-decoding
Conv→Softmax→MatMul chain with a single optimized Conv with clustered weights.

---

## 3. Quantize the model

Run this **inside the Vitis AI Docker container** (see Section 0 — launch Docker first
and install the additional dependencies).

### Step 1: Prepare calibration dataset

Create a calibration dataset directory with representative images. For COCO, use 100-200
validation images:

```bash
# Create calibration directory
mkdir -p calib_100

# Copy 100 random COCO images (adjust path to your COCO dataset)
ls /path/to/coco/images/val2017/*.jpg | sort -R | head -100 | xargs -I {} cp {} calib_100/
```

Alternatively, you can use any representative images from your target dataset. The
images will be automatically resized to 640×640 during calibration.

### Step 2: Run quantization script

Quantize the optimized FP32 model to INT8 using AMD Quark:

```bash
python3 quantize.py --calib-dir calib_100
```

Or specify a different calibration directory:

```bash
python3 quantize.py --calib-dir /path/to/coco/images
```

**Default behavior:**
- Input model: `yolov8m_optimized_fp32.onnx`
- Output model: `yolov8m_int8.onnx`
- Calibration: All images in the specified `--calib-dir` directory
- Preprocessing: Letterbox resize to 640×640, RGB conversion, normalization to [0, 1]

### Step 3: Verify quantization output

The script will output:
```
[quantize] calibration: 100 image(s) @ 640x640
[quantize] box decode kept in FLOAT (X nodes): ...
[quantize] input model: yolov8m_optimized_fp32.onnx
[quantize] output model: yolov8m_int8.onnx
[quantize] quantized model saved: yolov8m_int8.onnx
```

**Quantization details:**
- Uses AMD Quark **VINT8** preset optimized for NPU
- Configuration: `enable_npu_cnn=True`, `Int32Bias=False`
- **Box decode tail excluded**: The distance-to-bbox decode operations (Concat, Mul,
  Add, Sub, Div) are kept in FP32 for better numerical stability and accuracy
- **Per-tensor quantization**: INT8 quantization applied to Conv/Gemm layers

### Optional parameters

**Limit calibration images:**
```bash
python3 quantize.py --calib-dir calib_100 --max-calib-images 50
```

**Custom input/output model paths:**
```bash
python3 quantize.py \
    --calib-dir /path/to/coco/images \
    --input-model custom_fp32.onnx \
    --output-model custom_int8.onnx
```

**All parameters combined:**
```bash
python3 quantize.py \
    --calib-dir /path/to/coco/images \
    --max-calib-images 100 \
    --input-model yolov8m_optimized_fp32.onnx \
    --output-model yolov8m_int8.onnx
```

---

## 4. Compile the model

Run this **inside the Vitis AI Docker container** (see Section 0 — launch Docker
first). Compile the **quantized** model from Section 3:

```bash
python3 ../../../Utils/compile.py \
    --onnx_model_path yolov8m_int8.onnx \
    --config_file vitisai_config.json \
    --cache_dir ./ \
    --cache_key yolov8m_int8
```

During compilation the following parallelism parameters are selected:

| Parameter | Value |
|-----------|-------|
| TP_Size | 2 |
| DP_Size | 4 |

Emits the compiled artifact at
`yolov8m_int8/yolov8m_int8.rai`.

The `vitisai_config.json` specifies T2D4 parallelism (`tp_size=2`, `dp_size=4`)
for tensor-parallel and data-parallel execution on the NPU. The TSG (Tile
Scheduling Graph) configuration is specified in `tsg_config.json`.

---

## 5. Run inference (on board)

### Generate hardware-ready input binary

Before running inference, you need to generate the hardware-ready input binary
(`ifm_0.bin`) from a FP32 input array. This converts the FP32 NCHW input to
INT8 NHWC format with proper channel padding and replicates it for data
parallelism.

If you don't already have an `ifm_0.npy` file, create one first:

```python
import numpy as np
# Create a random FP32 input in NCHW format [1, 3, 640, 640]
rng = np.random.default_rng(0)
data = rng.random((1, 3, 640, 640), dtype=np.float32) * 255.0
np.save("ifm_0.npy", data)
print(f"Saved ifm_0.npy with shape {data.shape}")
```

Then generate the hardware-ready binary. Run this in a Python environment with `onnx` and `numpy` installed (you can use the `export_venv` from Section 0):

```bash
# Activate the export_venv (or any venv with onnx and numpy)
source export_venv/bin/activate

# Generate the hardware-ready input binary
python3 make_board_ifm.py \
    ifm_0.npy \
    yolov8m_int8.onnx \
    ifm_0.bin \
    --cache-dir ./ \
    --dp 4
```

This script performs the following transformations:

1. **Load FP32 input**: Reads `ifm_0.npy` containing FP32 data in NCHW format `[1, 3, 640, 640]`
2. **Extract quantization parameters**: Reads scale and zero_point from the first `QuantizeLinear` node in `yolov8m_int8.onnx`
3. **Quantize to INT8**: Applies the formula `int8_value = clip(round(fp32_value / scale) + zero_point, int8_min, int8_max)`
4. **Transpose to NHWC**: Converts from `[N, C, H, W]` to `[N, H, W, C]` layout
5. **Apply channel padding**: Pads the channel dimension from 3 to 4 (align=4) for NPU hardware alignment, filling with zero_point value
6. **Replicate for data parallelism**: Creates 4 copies of the input (one per data-parallel stream) for `dp_size=4`
7. **Write binary**: Saves the contiguous binary array to `ifm_0.bin`

**Output format:**
```
saved ifm_0.bin  (1, 640, 640, 4) int8  align=4  3276800 B/slot x dp=4 = 13107200 B
```

**Understanding the output:**
- **Shape**: `(1, 640, 640, 4)` = batch=1, height=640, width=640, channels=4 (3 RGB + 1 padding)
- **Data type**: `int8` (signed 8-bit integer)
- **Alignment**: `align=4` means channels are padded to multiples of 4
- **Per-slot size**: `3,276,800 bytes` = 1 × 640 × 640 × 4 × 1 byte (single image)
- **Total size**: `13,107,200 bytes` = 3,276,800 × 4 (replicated 4 times for dp_size=4)
- **Memory layout**: Contiguous NHWC INT8 array, ready for direct DMA to NPU

**Why this format?**
- **INT8**: Matches the quantized model's input data type
- **NHWC**: NPU hardware expects height-width-channel layout (not NCHW)
- **Channel padding**: NPU processes channels in groups of 4 for efficiency
- **Replication**: Data parallelism (dp_size=4) requires 4 copies of the input, one for each parallel stream
- **No host conversion**: The `x_plus_ml_vart` runtime reads this binary directly without any preprocessing, minimizing latency

### Run inference with x_plus_ml_vart

Run on the **target board**. First copy this repo onto the board and run the
board setup (see Section 0 — `scp` the dir, then program the overlay and set
`LD_LIBRARY_PATH`):

```bash
x_plus_ml_vart --app-config app_1model.json --runs 1000
```

`app_1model.json` points at the compiled `.rai` and feeds the quantized input
`ifm_0.bin`.

For more details on running models with the `x_plus_ml_vart` app, see the
Vitis AI documentation for x_plus_ml_vart usage.

> **Note:** `ifm_0.bin` is **device-native** (INT8, NPU layout), matching the
> `.rai`'s input buffer, so `x_plus_ml_vart` performs **no** host-side conversion. The
> input configuration is specified in `app_1model.json` with the quantized input
> name `images_QuantizeLinear_Output` (the quantized graph's input after the
> `QuantizeLinear` node).

---

## 6. Accuracy test (COCO bbox mAP)

`evaluate.py` reproduces the YOLOv8 post-processing (NMS, box rescale) and
scores COCO bbox mAP via pycocotools.

### Download the COCO dataset

The eval uses the **COCO 2017 validation set** (5,000 images) and its
annotations. Download both from the official COCO site:

```bash
# val images (~1 GB) + annotations (~240 MB)
wget http://images.cocodataset.org/zips/val2017.zip
wget http://images.cocodataset.org/annotations/annotations_trainval2017.zip
unzip val2017.zip -d coco/images
unzip annotations_trainval2017.zip -d coco
```

Expected COCO layout under `--coco_dataset`:

```
coco/
├── annotations/instances_val2017.json
└── images/val2017/*.jpg
```

> **Note:** The `evaluate.py` script expects images at `<coco_dataset>/images/val2017/`. If your COCO dataset has a different structure (e.g., images directly in `<coco_dataset>/val2017/`), you have two options:
> 1. **Adjust the path**: Modify line 155 in `evaluate.py` to match your directory structure
> 2. **Reorganize**: Move your images to match the expected structure above

### Run the evaluation

CPU reference (ONNXRuntime `CPUExecutionProvider`) — run on the **host
terminal** in the `export_venv` environment (install additional dependencies
if needed):

```bash
python -m pip install pycocotools tqdm ultralytics==8.3.241--no-deps
python3 evaluate.py \
    --model yolov8m_int8.onnx \
    --coco_dataset <path/to/coco/dir> \
    --device cpu-int8-fp32
```

NPU (VitisAI Execution Provider) — run on the **target board**:

```bash
python -m pip install pycocotools tqdm ultralytics==8.3.241 --no-deps
python3 evaluate.py \
    --model yolov8m_int8.onnx \
    --coco_dataset <path/to/coco/dir> \
    --device npu-int8-bf16 \
    --cache_dir ./ \
    --cache_key yolov8m_int8
```

The script prints `mAP@[.5:.95]`, `mAP@50`, and `mAP@75`.

---

## T2D4 Parallelism

This model uses **T2D4** (Tensor-parallel 2, Data-parallel 4) configuration for
optimal performance on the VEK385 NPU:

- **TP=2**: Tensor parallelism splits the model across 2 NPU cores, allowing
  larger models to fit and improving throughput.
- **DP=4**: Data parallelism processes 4 batches concurrently across NPU cores,
  maximizing utilization for inference workloads.

The configuration is specified in `vitisai_config.json` via:
```json
"tp_size": 2,
"dp_size": 4
```

---

## Model Details

**YOLOv8m** is a medium-sized variant of the YOLOv8 object detection family,
balancing accuracy and speed. Key characteristics:

- **Architecture**: CSPDarknet backbone + PAN neck + decoupled head
- **Parameters**: ~25M
- **FLOPs**: ~80 GFLOPs (at 640x640)
- **COCO mAP**: ~50.2 (FP32 baseline)

The model is optimized for NPU deployment with:
- DFL decode tail simplification
- INT8 quantization on backbone/neck (FP32 decode tail)
- T2D4 parallelism for high-throughput inference
