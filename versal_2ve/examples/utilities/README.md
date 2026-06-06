# Utilities (Linux development host)

Scripts and small tools that run on the **Linux development host** to support embedded examples—for example JPEG-to-IFM conversion, batch preparation, and checks before deploy to the target.

Document prerequisites and usage for each utility (shell interpreter, Python version, paths).


## JPEG to Binary Preprocessing Script

The `jpeg_to_binary.py` script resizes, normalizes, and converts a JPEG image
into a raw binary `.bin` file suitable for use as an IFM (Input Feature Map)
input to NPU inference applications (e.g. `vart_multi_tenancy`,
`vart_multimodel_seq`).

**Processing pipeline:**
1. Load JPEG and convert to RGB.
2. Resize to the target spatial dimensions (`--shape`, default 224×224).
3. Rearrange channels to the requested pixel format (`--format`).
4. Normalize per-channel: `output = (pixel - MEAN) * SCALE`.
5. Cast to the requested data type (`--dtype`, default FP16).
6. Write raw binary to disk.

### Prerequisites

- Python 3.6+
- NumPy (`pip install numpy`)
- Pillow (`pip install Pillow`)

### Mean and Scale Configuration

Per-channel **mean** and **scale** values are defined as constants at the top of the script.
The default values are set for **ResNet50**:

```python
MEAN  = [123.675, 116.28, 103.53]      # per-channel mean subtraction (R, G, B)
SCALE = [0.017124, 0.017507, 0.017429]  # per-channel scale factor     (R, G, B)
```

If your model requires different preprocessing values, edit the `MEAN` and
`SCALE` constants in the script before running it. The normalization applied
to each pixel is:

```
output = (pixel - MEAN) * SCALE
```

> **Note:** MEAN and SCALE are always specified in **R, G, B** order. For
> BGR-based formats (`BGR`, `BGRA`, `BGRx`, `BGRP`) the script automatically
> reorders them so that the correct value is applied to each channel.

### Usage

```bash
python jpeg_to_binary.py -i <input.jpg> -f <format> [options]
```

### Arguments

| Argument           | Required | Default               | Description                                              |
| ------------------ | -------- | --------------------- | -------------------------------------------------------- |
| `-i, --input`      | Yes      |                       | Input JPEG file path                                     |
| `-f, --format`     | No       | `RGBP`                | Output pixel format (see supported formats below)        |
| `-o, --output`     | No       | `<input>_<fmt>_<shape>_<dtype>.bin`| Output `.bin` file path                                  |
| `--shape`          | No       | `224x224`             | Resize to WxH before conversion (e.g. `640x640`)        |
| `--dtype`          | No       | `FP16`                | Output data type (`INT8`, `FP16`, `FP32`, `BF16`) |

### Supported Pixel Formats

`RGB`, `BGR`, `RGBx`, `BGRx`, `RGBP`, `BGRP`

| Format | Layout | Output Shape (default 224×224) | Description |
| ------ | ------ | ------------------------------ | ----------- |
| `RGB`  | HWC    | (224, 224, 3) | Interleaved Red, Green, Blue |
| `BGR`  | HWC    | (224, 224, 3) | Interleaved Blue, Green, Red |
| `RGBx` | HWC    | (224, 224, 4) | RGB + padding byte (0) |
| `BGRx` | HWC    | (224, 224, 4) | BGR + padding byte (0) |
| `RGBP` | CHW    | (3, 224, 224) | Planar RGB (channels first — use for NCHW models) |
| `BGRP` | CHW    | (3, 224, 224) | Planar BGR (channels first — use for NCHW models) |

> **Tip:** Most deep learning models (e.g. ResNet50, YOLOX) expect **NCHW** input layout.
> Use `RGBP` or `BGRP` for these models.

### Supported Data Types

| Data Type   | Description                                   |
| ----------- | --------------------------------------------- |
| `INT8`      | 8-bit signed integer (default)                |
| `FP16`      | IEEE 754 half-precision floating point        |
| `FP32`      | IEEE 754 single-precision floating point      |
| `BF16`      | Brain floating point (truncated float32)      |

### Examples

1. **Simple RGB conversion** (no resize, default int8):

```bash
python jpeg_to_binary.py -i cat.jpg -f RGB
```

2. **ResNet50 input preparation** (resize to 224x224, planar RGB, float32):

```bash
python jpeg_to_binary.py -i cat.jpg -f RGBP --dtype FP32
```

3. **BGR with resize**:

```bash
python jpeg_to_binary.py -i input.jpg -f BGR --shape 640x640
```

4. **Output to a specific file**:

```bash
python jpeg_to_binary.py -i frame.jpg -f RGB -o frame_rgb.bin
```

### Output

The script prints a summary after conversion:

```
=======================================================
  JPEG -> BIN Conversion Summary
=======================================================
  Input file    : cat.jpg
  Input size    : 800 x 600
  Resized to    : 224x224
  Mean          : [123.675, 116.28, 103.53]
  Scale         : [0.017124, 0.017507, 0.017429]
-------------------------------------------------------
  Output file   : cat_RGBP_224x224_FP32.bin
  Output format : RGBP
  Output shape  : (3, 224, 224)
  Data layout   : CHW (Planar RGB)
  Data type     : FP32
  File size     : 602,112 bytes
=======================================================
```
