# YOLOX-M

Object detection model: **YOLOX-M** (medium) trained on **COCO** (80 classes).

| Property | Value |
| --- | --- |
| Source checkpoint | [yolox_m.pth](https://github.com/Megvii-BaseDetection/YOLOX/releases/download/0.1.1rc0/yolox_m.pth) |
| Input | `images` `[1, 3, 640, 640]` fp32, NCHW |
| Output | `output` `[1, 8400, 85]` fp32 |
| Opset | 17 |
| Device | ve2 (INT8) |

---

## 0. Setup and Environment

This model spans **three environments**. Run each step in the right place:

| Stage | Where | What runs |
| --- | --- | --- |
| 1. Export | **Host terminal** (no Docker) | `export_yolox_m.py`, CPU accuracy eval |
| 2. Quantize | **Vitis AI Docker** (Linux workstation) | `quantize.py` |
| 3. Compile | **Vitis AI Docker** (Linux workstation) | `../../../Utils/compile.py` |
| 4. Run inference | **Target board** (VEK385) | `ml_vart` / VART app |

For the full toolchain and board bring-up, follow the official
**Vitis AI Gen2 documentation**:
<https://vitisai.docs.amd.com/projects/gen2/en/latest/index.html>
(see the *Quick Start Guide* for board setup and *Development Flow* for Docker).

### Host dependencies (export)

#### Host machine

Install the Python dependencies in your **host terminal**:

```bash
pip install torch==2.4.0 torchvision==0.19.0
pip install --no-build-isolation --no-deps yolox
# install dependencies of yolox
pip install onnx opencv-python-headless loguru scikit-image tqdm thop ninja tabulate tensorboard pycocotools
# install from requirements by removing the yolox
pip install -r ../../requirements.txt
pip install onnxscript
```

#### Export inside Vitis AI Docker (recommended)

If exporting inside the Vitis AI Docker container:

```bash
python3 -m pip install -r ../../requirements.txt --no-deps
```

### Vitis AI Docker (quantization + compilation)

Model **quantization** and **compilation** must be run **inside the Vitis AI Docker
container**. Launch it *before* quantizing/compiling, mounting this cookbook into the
container (adjust the license dir and `REPOSITORY:TAG` for your install):

```bash
docker run -it --rm --network host \
    -v </yourLicenseDir>:/usr/licenses \
    -v <path>/<to>/cookbook:/cookbook \
    <Docker REPOSITORY:TAG> bash

# inside the container:
cd /cookbook/Models/YOLOX/yolox-m
```

The `onnxruntime` VitisAI Execution Provider, `../../../Utils/compile.py`, and Quark
are all provided by this Docker image.

Before quantizing, make the working directory writable (the container writes the quantized model, cache, and outputs here):

```bash
chmod -R a+w yolox-m
```

### Target board (run inference)

To run inference on the **target board**, first
**copy or mount this repository onto the board** (e.g. via `scp` or NFS) so the
compiled `.rai`, `vart_config.json`, and `input.bin` are available there:

```bash
# from the host, copy the yolox-m dir to the board (replace TARGET_IP):
scp -r yolox-m amd-edf@TARGET_IP:~/
```

Then, on the board (as root, once per boot), program the PL + AIE overlay via `fpgautil`
and set `LD_LIBRARY_PATH` for the runtime libraries:

```bash
# program the PL + AIE overlay (mandatory before any AI/ML app):
sudo fpgautil -b ./overlay/vpl_gen_fixed_pld.pdi -o ./overlay/pl_aiarm.dtbo

# runtime libraries (FlexML / VOE / VART):
export LD_LIBRARY_PATH=/usr/lib/python3.12/site-packages/flexmlrt/lib/:/usr/lib/python3.12/site-packages/voe/lib/:/usr/lib/python3.12/site-packages/onnxruntime/capi:/usr/lib/python3.12/site-packages/vart_ml/lib/
```

See the Vitis AI Gen2 *Quick Start Guide* and the repo's
`OSPI_SDCard_Bootflow.md` for the complete board bring-up (boot, overlay files,
firmware).

---

## 1. Export the model

Export the reference ONNX from official YOLOX checkpoint. Run this in
your **host terminal** or Vitis AI Docker as recommended (dependencies from Section 0):

```bash
python3 export_yolox_m.py
```

Outputs: `yolox_m.onnx`

---

## 2. Download COCO dataset (for calibration)

Download the COCO validation dataset for quantization calibration. Run this on
your **host machine**:

```bash
# Create dataset directory
mkdir -p coco && cd coco

# Download COCO 2017 validation images (~1GB, 5000 images)
wget http://images.cocodataset.org/zips/val2017.zip
unzip val2017.zip

# Download COCO 2017 annotations (~241MB, required for accuracy evaluation)
wget http://images.cocodataset.org/annotations/annotations_trainval2017.zip
unzip annotations_trainval2017.zip

cd ..
```

Only a subset of images is needed for calibration (e.g., 10-100 images). The annotations are required for accuracy evaluation.

---

## 3. Quantize the model

Quantize to INT8 using Quark with COCO calibration data. Run this **inside the
Vitis AI Docker container** (see Section 0):

```bash
python3 quantize.py \
    --input yolox_m.onnx \
    --output yolox_m_int8.onnx \
    --calib-dir <path/to/coco/dataset> \
    --max-images 10
```

---

## 4. Compile the model

Run this **inside the Vitis AI Docker container** (see Section 0 — launch Docker
first):

```bash
python3 ../../../Utils/compile.py \
    --onnx_model_path yolox_m_int8.onnx \
    --config_file vitisai_config.json \
    --cache_dir ./ \
    --cache_key yolox_m_int8 \
    --enable-ai-analyzer
```

During compilation the following parallelism parameters are selected:

| Parameter | Value |
|-----------|-------|
| TP_Size | 5 |
| DP_Size | 1 |

`../../../Utils/compile.py` runs the quantized model through the VitisAI Execution Provider (configured via `vitisai_config.json`, target VAIML). The `--enable-ai-analyzer` flag generates partition metadata required for runtime execution on the target device.

Emits the compiled artifact at `yolox_m_int8/yolox_m_int8.rai`.

---

## 5. Run inference (on board)

Run on the **target board**. First copy this repo onto the board and run the
board setup (see Section 0 — `scp` the dir, then run overlay setup).

### Generate input.bin

Before running inference, generate the input binary file:

```bash
python3 generate_input_bin.py
```

This creates `input.bin` containing a deterministic random tensor with shape
`[1, 3, 640, 640]` (NCHW, float32).

### Run ml_vart

```bash
ml_vart --app-config vart_config.json --dry-run --runs 100
```

`vart_config.json` points at the compiled `.rai` and feeds `input.bin` (FP32, BGR, unnormalized [0,255]).

> **Note:** `--dry-run` does not use `input.bin`. To run inference with the generated input, remove `--dry-run` from the command.

---

## 6. Evaluate model accuracy

Evaluate INT8 model accuracy on COCO validation set.

> **Note:** By default, evaluates on all 5000 COCO validation images. Use `--max-images <N>` to limit the number of images evaluated.

### CPU evaluation (inside Vitis AI Docker)

```bash
python3 -m pip install pycocotools

python3 evaluate_coco.py \
    --onnx_model_path yolox_m_int8.onnx \
    --coco-dir <path/to/coco/dataset>
```

### NPU evaluation (on target board)

```bash
python3 -m pip install tqdm pycocotools

python3 evaluate_coco.py \
    --onnx_model_path yolox_m_int8.onnx \
    --coco-dir <path/to/coco/dataset> \
    --use-npu \
    --vitis-config vitisai_config.json \
    --cache-dir ./ \
    --cache-key yolox_m_int8
```

Outputs mAP, mAP@0.50, and mAP@0.75 metrics.

---

