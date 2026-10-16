# ResNet50 v1.12

ImageNet-1k image classifier: **ResNet50 v1.12** (ONNX Model Zoo). This flow
takes the public FP32 ONNX, **INT8-quantizes** it with **AMD Quark**, compiles
it for the NPU, and evaluates top-1/top-5 accuracy on the ImageNet validation
set. For BF16 you would not need Quark at all — the Vitis AI compiler converts
FP32→BF16 automatically — this model targets **INT8**.

| Property | Value |
| --- | --- |
| Source checkpoint | `onnxmodelzoo/resnet50-v1-12` |
| Input | `data` `[1, 3, 224, 224]` fp32, NCHW |
| Output | `resnetv17_dense0_fwd` `[1, 1000]` fp32 (ImageNet-1k logits) |
| Opset | 12 |
| Quantization | INT8 (Quark `VINT8`, per-tensor) |
| Device | ve2 (`ve2-xc2ve3858`, int8) |

---

## 0. Setup and Environment

This model spans **three environments**. Run each step in the right place:

| Stage | Where | What runs |
| --- | --- | --- |
| 1. Prepare | **Host terminal** (no Docker) | model download |
| 2. Quantize + Compile | **Vitis AI Docker** (Linux workstation) | `quantize.py`, `../../Utils/compile.py` |
| 3. Run inference / eval | **Target board** (VEK385) | `ml_vart` / `evaluate.py` |

For the full toolchain and board bring-up, follow the official
**Vitis AI Gen2 documentation**:
<https://vitisai.docs.amd.com/projects/gen2/en/latest/index.html>
(see the *Quick Start Guide* for board setup and *Development Flow* for Docker).

### Vitis AI Docker (quantize + compile)

Model **quantization** (Section 2) and **compilation** (Section 3) must be run
**inside the Vitis AI Docker container**. Launch it *before* those steps,
mounting this cookbook into the container (adjust the license dir and
`REPOSITORY:TAG` for your install):

```bash
docker run -it --rm --network host \
    -v </yourLicenseDir>:/usr/licenses \
    -v <path>/<to>/cookbook:/cookbook \
    <Docker REPOSITORY:TAG> bash

# inside the container, initialize the environment:
cd /cookbook/Models/Resnet50
```

The `onnxruntime` VitisAI Execution Provider, `../../Utils/compile.py`, and Quark
are all provided by this Docker image.

Before quantizing, make the working directory writable (the container writes the
quantized model, cache, and outputs here):

```bash
chmod -R a+w Resnet50
```

### Target board (run inference / eval)

Running inference (Section 4) and NPU accuracy eval (Section 5) run on
the **target board**. First **copy or mount this repository onto the board**
(e.g. via `scp` or NFS) so the compiled `.rai`, `vart_config.json`, and
`input.bin` are available there:

```bash
# from the host, copy the Resnet50 dir to the board (replace TARGET_IP):
scp -r Resnet50 amd-edf@TARGET_IP:~/
```

Then, on the board (as root, once per boot), program the PL + AIE overlay via
`fpgautil` and set `LD_LIBRARY_PATH` for the runtime libraries:

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

## 1. Download the model

Fetch the reference FP32 ONNX from the ONNX Model Zoo into `models/`. Run this in
your **host terminal**:

```bash
wget -P models https://huggingface.co/onnxmodelzoo/resnet50-v1-12/resolve/main/resnet50-v1-12.onnx
```

This produces `models/resnet50-v1-12.onnx` (the FP32 input to quantization).

## 2. Quantize the model (INT8)

Run this **inside the Vitis AI Docker container** (see Section 0 — launch Docker
first and `chmod -R a+w Resnet50`). Quark calibrates the INT8 scales on a handful
of representative images; point `--calib-data` at your ImageNet-1k images and cap
the count with `--max-calib-images`:

```bash
python3 quantize.py --calib-data <imagenet-1k> --max-calib-images 100
```

> `quantize.py` reads every `.png`/`.jpg` under `<imagenet-1k>` and preprocesses
> each (BGR→RGB, resize short side 256, center-crop 224, `/255`, ImageNet
> mean/std, NCHW). Increase `--max-calib-images` for more robust calibration.

Quark uses the `VINT8` preset (`enable_npu_cnn = True`, `Int32Bias = False`) and
emits the quantized model at `models/resnet50-v1-12_quantized.onnx`. The console
prints per-op float / INT8 tables showing Conv/Gemm activations, weights, and
biases quantized to INT8.

## 3. Compile the model

Run this **inside the Vitis AI Docker container**:

```bash
python3 ../../Utils/compile.py \
    --onnx_model_path models/resnet50-v1-12_quantized.onnx \
    --config_file vitisai_config.json \
    --cache_dir ./
```

During compilation the following parallelism parameters are selected:

| Parameter | Value |
|-----------|-------|
| TP_Size | 6 |
| DP_Size | 1 |


`../../Utils/compile.py` runs the quantized model through the VitisAI Execution
Provider (`vitisai_config.json`, target `VAIML`, `cache_key = resnet50-v1-12_quantized`)
and emits the compiled artifact at
`resnet50-v1-12_quantized/resnet50-v1-12_quantized.rai`.

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

This creates `input.bin` (602,112 bytes) containing a deterministic random tensor
with shape `[1, 3, 224, 224]` (NCHW, float32).

### Run ml_vart

```bash
ml_vart --app-config vart_config.json --dry-run --runs 100
```

`vart_config.json` points at the compiled `.rai` and feeds `input.bin`.

> **Note:** `--dry-run` does not use `input.bin`. To run inference with the generated input, remove `--dry-run` from the command.

For more details on running the models with the `ml_vart` app, see
[`versal_2ve/examples/cpp_examples/ml_vart`](../../../examples/cpp_examples/ml_vart#inspecting-model-metadata).

> **Note:** `input.bin` is **device-native** (INT8, HW `HCWNC4` layout, 200,704
> bytes = 224 × 224 × 4, with channels padded 3→4), matching the `.rai`'s
> `ifm_ddr` buffer, so `ml_vart` performs **no** host-side conversion. The
> `ifms-config` `name` must be the runner's **device** tensor name
> `data_QuantizeLinear_Output_4` (the quantized graph's `data` input after its
> `QuantizeLinear`), **not** the ONNX input name `data`. Outputs written to
> `output/` are device-native INT8 `[1, 1000]`.

---

## 5. Accuracy test (ImageNet val)

Measure top-1 / top-5 on the ImageNet-1k validation set with `evaluate.py`
(standard ResNet preprocessing: resize 256 → center-crop 224 → `/255` →
ImageNet mean/std → NCHW).

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
  (6.3 GB, the validation images).
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

Final ImageNet layout, with the val split restructured into WNID subfolders
(`evaluate.py` loads it via `torchvision.datasets.ImageFolder`):

```
imagenet-1k/
└── val_data/<wnid>/*.JPEG        # queries (WNID subfolders = class labels)
```

> On this system the val split is already available at
> `../../../datasets/imagenet-1k/val_data/<wnid>/*.JPEG` (used in the commands
> below), so no download is needed to reproduce the baseline.

### Run the evaluation

NPU (VitisAI Execution Provider), **on the board**:

```bash
python3 -m pip install pillow tqdm torch torchvision
python3 evaluate.py \
    --model models/resnet50-v1-12_quantized.onnx \
    --data ../../../datasets/imagenet-1k/val_data \
    --target=npu \
    --cache_key resnet50-v1-12_quantized
```

To sanity-check on the host instead, pass `--target=cpu` (drops the
`cache_key`); point `--model` at `models/resnet50-v1-12.onnx` for the FP32
baseline or the `_quantized.onnx` for the INT8 CPU reference. The script prints
top-1 / top-5 accuracy and inference FPS.
