# DINO ViT-B/16

Self-supervised vision transformer backbone: **DINO ViT-B/16**
(`facebook/dino-vitb16`). This is a **headless** feature extractor — it has **no
classification head**. It outputs the raw transformer sequence
`last_hidden_state`; token 0 is the `[CLS]` feature used for downstream tasks.
Accuracy is therefore measured with a **k-NN classifier** on the frozen `[CLS]`
features (the standard DINO protocol), not with a softmax head.

| Property | Value |
| --- | --- |
| Source checkpoint | `facebook/dino-vitb16` |
| Input | `pixel_values` `[1, 3, 224, 224]` fp32, NCHW |
| Output | `last_hidden_state` `[1, 197, 768]` fp32 (196 patches + 1 `[CLS]`) |
| Opset | 17 |
| Device | ve2 (bfloat16) |

---

## 0. Setup and Environment

This model spans **three environments**. Run each step in the right place:

| Stage | Where | What runs |
| --- | --- | --- |
| 1. Export | **Host terminal** (no Docker) | `export_dino_vitb16.py`, CPU accuracy eval |
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

These cover `torch`, `transformers`, `onnx`, `onnxruntime`, `numpy`, `Pillow`,
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
cd /cookbook/Models/DinoViTB16
```

The `onnxruntime` VitisAI Execution Provider, `../../Utils/compile.py`, and Quark
are all provided by this Docker image.

### Target board (run inference)

To run inference (Section 3) on the **target board**, first
**copy or mount this repository onto the board** (e.g. via `scp` or NFS) so the
compiled `.rai`, `vart_config.json`, and `input.bin` are available there:

```bash
# from the host, copy the DinoViTB16 dir to the board (replace TARGET_IP):
scp -r DinoViTB16 amd-edf@TARGET_IP:~/
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
python3 export_dino_vitb16.py
```

> The original was exported with the **classic TorchScript exporter** (opset 17,
> `dynamo=False`), so GELU appears as `Erf` (unfused) and attention as an
> explicit `Softmax` (`--attn eager`). These defaults reproduce the reference
> op graph; `--dynamo` would instead emit fused `Gelu`/`LayerNormalization`.

## 2. Compile the model

Run this **inside the Vitis AI Docker container** (see Section 0 — launch Docker
first):

```bash
python3 ../../Utils/compile.py \
    --onnx_model_path dino-vitb16_last_hidden_state.onnx \
    --config_file vitisai_config.json \
    --cache_dir ./
```

During compilation the following parallelism parameters are selected:

| Parameter | Value |
|-----------|-------|
| TP_Size | 6 |
| DP_Size | 1 |

Emits the compiled artifact at
`dino-vitb16_last_hidden_state/dino-vitb16_last_hidden_state.rai`.

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
`[1, 3, 224, 224]` (NCHW, float32).

### Run ml_vart

```bash
ml_vart --app-config vart_config.json --dry-run --runs 100
```

`vart_config.json` points at the compiled `.rai` and feeds `input.bin`.

> **Note:** `--dry-run` does not use `input.bin`. To run inference with the generated input, remove `--dry-run` from the command.

For more details on running the models with the `ml_vart` app, see
[`versal_2ve/examples/cpp_examples/ml_vart`](../../../examples/cpp_examples/ml_vart#inspecting-model-metadata).

> **Note:** `input.bin` is **device-native** (bfloat16, HW layout, 301,056
> bytes = fp32 size / 2) so `ml_vart` performs **no** host-side fp32→bf16
> conversion. The config intentionally omits `input-tensor-type` /
> `output-tensor-type` (which would enable CPU-side conversion). Outputs written
> to `output/` are therefore device-native bf16, not fp32 `[1,197,768]`.

---

## 4. Accuracy test (k-NN on ImageNet val)

DINO ViT-B/16 is a self-supervised backbone with **no classification head**, so
we evaluate its frozen `[CLS]` features with a weighted **k-NN** classifier
(Caron et al., 2021; k=20, T=0.07): extract the 768-d `[CLS]` feature for every
image, L2-normalize, and vote over the nearest neighbors by cosine similarity.

### Download the ImageNet dataset

The eval uses the **ImageNet-1k (ILSVRC2012) validation set** (1000 classes ×
50 = 50,000 images). Get it from any of:

- **HuggingFace** (gated — accept the license, then login): the
  [`ILSVRC/imagenet-1k`](https://huggingface.co/datasets/ILSVRC/imagenet-1k)
  dataset. Note this is distributed as **parquet shards**
  (`data/*.parquet`), *not* as ready-made `val_data/<wnid>/*.JPEG` folders, so
  after downloading you must decode it into WNID subfolders (`valprep.sh` below
  only applies to the official `.tar`, not the parquet):
  ```python
  from datasets import load_dataset

  ds = load_dataset("ILSVRC/imagenet-1k", split="validation")  # prompts hf login
  wnids = ds.features["label"].names  # sorted WNID order
  for i, ex in enumerate(ds):
      wnid = wnids[ex["label"]]
      d = f"val_data/{wnid}"; import os; os.makedirs(d, exist_ok=True)
      ex["image"].convert("RGB").save(f"{d}/{i:08d}.JPEG")
  ```
- **Official ImageNet** (requires a free account):
  <https://www.image-net.org/download.php> → `ILSVRC2012_img_val.tar`
  (6.3 GB, the validation images) and `ILSVRC2012_img_train.tar` (138 GB, only
  needed for a paper-comparable train bank).
- **Academic mirror** (Kaggle):
  <https://www.kaggle.com/c/imagenet-object-localization-challenge/data>.

The raw `ILSVRC2012_img_val.tar` is a **flat** folder of
`ILSVRC2012_val_000xxxxx.JPEG` files. Restructure it into per-class WNID
subfolders with the standard `valprep.sh` script
(<https://raw.githubusercontent.com/soumith/imagenetloader.torch/master/valprep.sh>):

```bash
mkdir -p imagenet/val && tar -xf ILSVRC2012_img_val.tar -C imagenet/val
cd imagenet/val && wget -qO- https://raw.githubusercontent.com/soumith/imagenetloader.torch/master/valprep.sh | bash
```

Final ImageNet layout, with the val split restructured into WNID subfolders:

```
imagenet-1k/
├── train_data/<wnid>/*.JPEG      # feature bank (optional)
└── val_data/<wnid>/*.JPEG        # queries
```

Class label indices are the sorted WNID order shared by train and val.

> On this system the val split is already available at
> `../../../datasets/imagenet-1k/val_data/<wnid>/*.JPEG` (used in the commands
> below), so no download is needed to reproduce the baseline.

### Run the evaluation

CPU reference (ONNXRuntime `CPUExecutionProvider`). With only the val split
available, use `--val_only` (leave-one-out: each query excludes itself from its
own nearest-neighbor search):

```bash
python3 eval_dino_imagenet_knn.py \
    --onnx_model dino-vitb16_last_hidden_state.onnx \
    --data_root ../../../datasets/imagenet-1k/val_data \
    --target cpu --val_only
```

NPU (VitisAI Execution Provider):

```bash
python3 -m pip install pillow

python3 eval_dino_imagenet_knn.py \
    --onnx_model dino-vitb16_last_hidden_state.onnx \
    --data_root ../../../datasets/imagenet-1k/val_data \
    --target npu \
    --cache_key dino-vitb16_last_hidden_state \
    --val_only
```

The script prints Top-1 / Top-5 accuracy and model-only FPS (extraction).

> **Note on the val-only baseline:** the accuracy printed here is *by design*
> lower than the published DINO paper number, because the paper uses the full
> ImageNet **train** set as the feature bank (~1,300 imgs/class) whereas the
> val-only leave-one-out bank is ~26× smaller (50/class). The gap is a property
> of the smaller bank, not the exported backbone — the features themselves are
> faithful. For a paper-comparable number, supply a `train/` split (drop
> `--val_only`) and optionally cap it with `--train_per_class`.
