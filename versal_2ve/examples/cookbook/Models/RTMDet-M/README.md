# RTMDet-M

Object detection model: **RTMDet-M** (mmdetection) on **COCO** (80 classes),
exported to ONNX at 640×640 and cut **before** the NMS tail.

| Property | Value |
| --- | --- |
| Source | `rtmdet_m_8xb32-300e_coco` (mmdetection / mmdeploy) |
| Input | `input` `[1, 3, 640, 640]` fp32, NCHW |
| Outputs | `/Concat_6_output_0` `[1, 8400, 4]` (decoded xyxy boxes)<br>`/Concat_3_output_0` `[1, 8400, 80]` (per-class logits, pre-NMS) |
| Opset | 11 |
| Device | ve2 (int8 + bfloat16) |

---

## 0. Setup and Environment

This model spans **three environments**. Run each step in the right place:

| Stage | Where | What runs |
| --- | --- | --- |
| 1. Export | **Host terminal** (no Docker) | `export_rtmdet_m.py` |
| 2. Quantize | **Vitis AI Docker** (Linux workstation) | `quantize.py` |
| 3. Compile | **Vitis AI Docker** (Linux workstation) | `../../Utils/compile.py` |
| 4. Run inference | **Target board** (VEK385) | `ml_vart` / `evaluate_coco.py` |

For the full toolchain and board bring-up, follow the official
**Vitis AI Gen2 documentation**:
<https://vitisai.docs.amd.com/projects/gen2/en/latest/index.html>
(see the *Quick Start Guide* for board setup and *Development Flow* for Docker).

### Host dependencies (export + CPU eval)

The export and CPU-reference steps run in your **host terminal**, *not* inside the
Docker container.

**Export toolchain (isolated conda env).** The ONNX export needs an isolated
**OpenMMLab** toolchain (torch 2.0.1 CPU + mmengine / mmcv / mmdet / mmdeploy)
that is incompatible with the base env. Build it with the provided script:

```bash
bash setup_rtmdet_env.sh
```

This creates a conda env (default prefix `<CONDA_ROOT>/envs/rtmdet`) with the pinned
stack (torch 2.0.1+cpu, mmengine 0.10.4, mmcv 2.1.0, mmdet 3.3.0, mmdeploy 1.3.1,
onnx 1.15.0, onnxruntime 1.16.3, onnxsim 0.7.0) and runs an import self-check.

Configurable via environment variables:

| Var | Default | Purpose |
| --- | --- | --- |
| `CONDA_ROOT` | conda in the environment | conda install to use |
| `ENV_PREFIX` | `${CONDA_ROOT}/envs/rtmdet` | env location |
| `PY_VERSION` | `3.11` | Python version |
| `FORCE` | `0` | set `1` to remove & recreate an existing env |

> **Python must be 3.10/3.11, not 3.12** — torch 2.0.1+cpu ships no cp312 wheel on
> the PyTorch index, so 3.12 + torch 2.0.1 is not pip-reproducible. torch is kept at
> 2.0.1 (it drives ONNX tracing / bit-exactness); Python is the axis that moves.

Activate it before the export step:

```bash
source <CONDA_ROOT>/etc/profile.d/conda.sh
conda activate <CONDA_ROOT>/envs/rtmdet
```

### Vitis AI Docker (quantization + compilation)

Model **quantization** (Section 2) and **compilation** (Section 3) must be run
**inside the Vitis AI Docker container**. Launch it *before* those steps, mounting
this cookbook into the container (adjust the license dir and `REPOSITORY:TAG` for
your install):

```bash
docker run -it --rm --network host \
    -v </yourLicenseDir>:/usr/licenses \
    -v <path>/<to>/cookbook:/cookbook \
    <Docker REPOSITORY:TAG> bash

# inside the container, initialize the environment:
cd /cookbook/Models/RTMDet-M
```

The `onnxruntime` VitisAI Execution Provider and `../../Utils/compile.py` are
provided by this Docker image.

### Target board (run inference)

To run inference (Section 4) on the **target board**, first
**copy or mount this repository onto the board** (e.g. via `scp` or NFS) so the
compiled `.rai`, `vart_config.json` are available there:

```bash
# from the host, copy the RTMDet-M dir to the board (replace TARGET_IP):
scp -r RTMDet-M amd-edf@TARGET_IP:~/
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

Run this in your **host terminal** (activate the `rtmdet` conda env from
Section 0 first). Download the config + checkpoint once, then export:

```bash
# config + checkpoint -> ckpt/
mim download mmdet --config rtmdet_m_8xb32-300e_coco --dest ckpt

python export_rtmdet_m.py
```

Produces `rtmdet_m_8xb32-300e_coco_1x3x640x640_without_smaller_tail.onnx`.
The pipeline is: mmdeploy `torch2onnx` (end2end, static 640×640, opset 11) →
onnx-simplifier constant-fold → `onnx.utils.extract_model` cutting at
`/Concat_6_output_0` (boxes) and `/Concat_3_output_0` (scores), dropping NMS.

---

## 2. Quantize the model

Run this **inside the Vitis AI Docker container** (see Section 0 — launch Docker first).
It quantizes the exported FP32 graph to INT8 (Quark VINT8 preset, `Int32Bias=False`,
`enable_npu_cnn=True`) with the entire detection head kept FP32 (`--head-exclusion
full`), the distance2bbox decode-tail elementwise ops kept FP32
(`--exclude-decode-tail`), and **AdaRound** fast-finetune (1000 iters, lr 0.1):

```bash
python3 quantize.py \
    --input rtmdet_m_8xb32-300e_coco_1x3x640x640_without_smaller_tail.onnx \
    --output-dir ./ --calib-data <coco> --max-calib-images 100 \
    --adaround --adaround-iters 1000 --adaround-seed 666 --head-exclusion full \
    --exclude-decode-tail \
    --temp-dir /tmp/
```

Produces `rtmdet_m_8xb32-300e_coco_1x3x640x640_without_smaller_tail_quantized_adaround.onnx`.

> **AdaRound is essential** for this head-FP32 recipe (plain PTQ collapses the box
> mAP). `--calib-data <coco>` points at the COCO images
> (same layout as Section 5); 

> **Note:** different calibration datasets and different AdaRound seeds
> (`--adaround-seed`) can produce different accuracy.

---

## 3. Compile the model

Run this **inside the Vitis AI Docker container** (see Section 0 — launch Docker
first). Compile the **quantized** model from Section 2:

```bash
python3 ../../Utils/compile.py \
    --onnx_model_path rtmdet_m_8xb32-300e_coco_1x3x640x640_without_smaller_tail_quantized_adaround.onnx \
    --config_file vitisai_config.json \
    --cache_dir ./
```

During compilation the following parallelism parameters are selected:

| Parameter | Value |
|-----------|-------|
| TP_Size | 6 |
| DP_Size | 1 |

Emits the compiled artifact at
`rtmdet_m_8xb32-300e_coco_1x3x640x640_without_smaller_tail_quantized_adaround/rtmdet_m_8xb32-300e_coco_1x3x640x640_without_smaller_tail_quantized_adaround.rai`.

---

## 4. Run inference (on board)

Run on the **target board**. First copy this repo onto the board and run the
board setup (see Section 0 — `scp` the dir, then program the overlay and set
`LD_LIBRARY_PATH`).

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

`vart_config.json` points at the compiled `.rai`.

> **Note:** `--dry-run` does not use `input.bin`. To run inference with the generated input, remove `--dry-run` from the command.

For more details on running the models with the `ml_vart` app, see
[`versal_2ve/examples/cpp_examples/ml_vart`](../../../examples/cpp_examples/ml_vart#inspecting-model-metadata).

---

## 5. Accuracy test (COCO bbox mAP)

`evaluate_coco.py` reproduces the RTMDet post-processing the exported graph drops
(sigmoid on logits, per-class score threshold, per-class NMS, box rescale) and
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

Expected COCO layout under `--coco-dir`:

```
coco/
├── annotations/instances_val2017.json
└── images/val2017/*.jpg
```

### Run the evaluation

CPU reference (ONNXRuntime `CPUExecutionProvider`) — run inside Vitis AI Docker:

```bash
python3 -m pip install pycocotools

python3 evaluate_coco.py \
    --model rtmdet_m_8xb32-300e_coco_1x3x640x640_without_smaller_tail_quantized_adaround.onnx \
    --coco-dir <coco> --max-images 0 --target cpu
```

NPU (VitisAI Execution Provider) — run on the **target board**:

```bash
python3 -m pip install tqdm pycocotools

python3 evaluate_coco.py \
    --model rtmdet_m_8xb32-300e_coco_1x3x640x640_without_smaller_tail_quantized_adaround.onnx \
    --coco-dir <coco> --max-images 0 --target npu \
    --vitis-config vitisai_config.json --cache-dir ./ \
    --cache_key rtmdet_m_8xb32-300e_coco_1x3x640x640_without_smaller_tail_quantized_adaround
```

The script prints `mAP@[.5:.95]`, `mAP@50`, and `mAP@75`.
