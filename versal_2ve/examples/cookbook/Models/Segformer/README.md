# SegFormer-B0

Semantic segmentation model: **SegFormer-B0** finetuned on **Cityscapes** (19 classes).

| Property | Value |
| --- | --- |
| Source checkpoint | `nvidia/segformer-b0-finetuned-cityscapes-1024-1024` |
| Input | `input` `[1, 3, 256, 512]` fp32, NCHW |
| Output | `logits` `[1, 19, 64, 128]` fp32 (H/4 × W/4) |
| Opset | 20 |
| Device | ve2 (bfloat16) |

---

## 0. Setup and Environment

This model spans **three environments**. Run each step in the right place:

| Stage | Where | What runs |
| --- | --- | --- |
| 1. Export | **Host terminal** (no Docker) | `export_segformer_b0_cityscapes.py`, CPU accuracy eval |
| 2. Compile | **Vitis AI Docker** (Linux workstation) | `../../Utils/compile.py` |
| 3. Run inference | **Target board** (VEK385) | `ml_vart` / VART app |

For the full toolchain and board bring-up, follow the official
**Vitis AI Gen2 documentation**:
<https://vitisai.docs.amd.com/projects/gen2/en/latest/index.html>
(see the *Quick Start Guide* for board setup and *Development Flow* for Docker).

### Host dependencies (export + CPU eval)

The export and CPU-reference steps run in your **host terminal**, *not* inside the
Docker container. Install the Python dependencies listed in
[`requirements.txt`](requirements.txt):

```bash
python3 -m pip install -r requirements.txt
```

These cover `torch`, `transformers`, `onnx`, `onnxscript` (needed by
`torch.onnx.export(..., dynamo=True)`), `onnxruntime`, `numpy`, `opencv-python`,
and `tqdm`.

### Vitis AI Docker (compilation)

Model **compilation** (Section 2) must be run **inside the Vitis AI Docker
container**. Launch it *before* compiling, mounting this cookbook into the
container (adjust the license dir and `REPOSITORY:TAG` for your install):

```bash
docker run -it --rm --network host \
    -v </yourLicenseDir>:/usr/licenses \
    -v <path>/<to>/cookbook:/cookbook \
    <Docker REPOSITORY:TAG> bash

# inside the container, initialize the environment:
cd /cookbook/Models/segformer
```

The `onnxruntime` VitisAI Execution Provider, `../../Utils/compile.py`, and Quark
are all provided by this Docker image.

### Target board (run inference)

To run inference (Section 3) on the **target board**, first
**copy or mount this repository onto the board** (e.g. via `scp` or NFS) so the
compiled `.rai`, `vart_config.json`, and `input.bin` are available there:

```bash
# from the host, copy the segformer dir to the board (replace TARGET_IP):
scp -r segformer amd-edf@TARGET_IP:~/
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

Export the reference ONNX from the public HuggingFace checkpoint. Run this in
your **host terminal** (dependencies from [`requirements.txt`](requirements.txt),
see Section 0):

```bash
python3 export_segformer_b0_cityscapes.py
```

## 2. Compile the model

Run this **inside the Vitis AI Docker container** (see Section 0 — launch Docker
first):

```bash
python3 ../../Utils/compile.py \
    --onnx_model_path segformer_b0_cityscapes_256x512.onnx \
    --config_file vitisai_config.json \
    --cache_dir ./
```

During compilation the following parallelism parameters are selected:

| Parameter | Value |
|-----------|-------|
| TP_Size | 6 |
| DP_Size | 1 |

Emits the compiled artifact at
`segformer_b0_cityscapes_256x512/segformer_b0_cityscapes_256x512.rai`.

---

## 3. Run inference (on board)

Run on the **target board**. First copy this repo onto the board and run the
board setup (see Section 0 — `scp` the dir, then program the overlay and set
`LD_LIBRARY_PATH`).

### Generate input.bin

Before running inference, generate the input binary file:

```bash
python3 generate_input_bin.py
```

This creates `input.bin` containing a deterministic random tensor with shape
`[1, 3, 256, 512]` (NCHW, float32).

### Run ml_vart

```bash
ml_vart --app-config vart_config.json --dry-run --runs 100
```

`vart_config.json` points at the compiled `.rai` and feeds `input.bin`.

> **Note:** `--dry-run` does not use `input.bin`. To run inference with the generated input, remove `--dry-run` from the command.

For more details on running the models with the `ml_vart` app, see
[`versal_2ve/examples/cpp_examples/ml_vart`](../../../examples/cpp_examples/ml_vart#inspecting-model-metadata).

> **Note:** `input.bin` is **device-native** (bfloat16, HW layout `HCWNC4`,
> 1,048,576 bytes) so `ml_vart` performs **no** host-side fp32→bf16 conversion.
> The config intentionally omits `input-tensor-type` / `output-tensor-type`
> (which would enable CPU-side conversion). Outputs written to `output/` are
> therefore device-native bf16 (`HCWNC8`), not fp32 `[1,19,64,128]`.

---

## 4. Accuracy test (mIoU on Cityscapes val)

Per-city and overall mIoU on the Cityscapes validation set.

### Download the Cityscapes dataset

Download Cityscapes datasets from https://www.cityscapes-dataset.com/downloads/.
The following packages are required :

- `gtFine_trainvaltest.zip`
- `leftImg8bit_trainvaltest.zip`

After downloading and extracting, the directory structure should be:

```
cityscapes/
├── gtFine
│   ├── train
│   ├── val
│   └── test
├── leftImg8bit
│   ├── train
│   ├── val
│   └── test
├── gtFine_trainvaltest.zip
├── leftImg8bit_trainvaltest.zip
├── license.txt
└── README
```

### Run the evaluation

CPU reference (ONNXRuntime `CPUExecutionProvider`):

```bash
python3 eval_segformer_cityscapes_citywise.py \
    --data_root ../../../datasets/cityscapes \
    --onnx_model segformer_b0_cityscapes_256x512.onnx \
    --target cpu
```

NPU (VitisAI Execution Provider):

```bash
python3 eval_segformer_cityscapes_citywise.py \
    --data_root <path to cityscapes> \
    --onnx_model segformer_b0_cityscapes_256x512.onnx \
    --target npu \
    --vitis_config vitisai_config.json \
    --cache_dir ./ \
    --cache_key segformer_b0_cityscapes_256x512
```

When running from a different folder, point `--vitis_config` and `--cache_dir`
at the compiled model location so the existing `.rai` cache is reused (otherwise
the model is recompiled with the CPU EP, overwriting the cached `.rai`).

Expects the standard Cityscapes layout under `--data_root`:

```
cityscapes/
├── leftImg8bit/val/<city>/*_leftImg8bit.png
└── gtFine/val/<city>/*_gtFine_labelIds.png
```

The script prints per-city mIoU, per-class IoU, overall mIoU, and model-only FPS.
