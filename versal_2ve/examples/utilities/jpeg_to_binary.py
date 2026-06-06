#!/usr/bin/env python3
"""
JPEG to Binary (.bin) Preprocessor

Resizes, normalizes (mean subtraction + scaling), and converts a JPEG image
into a raw binary file suitable for NPU inference input (IFM).

Processing pipeline:
  1. Load JPEG and convert to RGB.
  2. Resize to the target spatial dimensions (--shape, default 224x224).
  3. Rearrange channels to the requested pixel format (--format).
  4. Normalize: output = (pixel - MEAN) * SCALE   (per-channel).
  5. Cast to the requested data type (--dtype, default FP16).
  6. Write raw binary to disk.

The MEAN and SCALE constants at the top of this script control
per-channel normalization. They are specified in R, G, B order and
automatically reordered for BGR-based formats.
Edit them to match the preprocessing expected by your model.

Usage examples:
    python jpeg_to_binary.py -i input.jpg -f RGB
    python jpeg_to_binary.py -i input.jpg -f RGBP --shape 224x224 --dtype FP32
    python jpeg_to_binary.py -i input.jpg -f BGR -o output.bin
"""

import argparse
import numpy as np
from PIL import Image
import os
import sys

# ---------------------------------------------------------------------------
# Preprocessing constants — edit these to match your model's requirements.
# Per-channel values are applied in R, G, B order.
# Normalization: output = (pixel - MEAN) * SCALE
# Default values below are for ResNet50.
# ---------------------------------------------------------------------------
MEAN  = [123.675, 116.28, 103.53]      # per-channel mean subtraction
SCALE = [0.017124, 0.017507, 0.017429]  # per-channel scale factor

SUPPORTED_FORMATS = [
    "RGB", "BGR", "RGBx", "BGRx", "RGBP", "BGRP",
]

SUPPORTED_DTYPES = {
    "INT8":    np.int8,
    "FP16":    np.float16,
    "FP32":    np.float32,
    "BF16":    None,           # handled separately (no native numpy type)
}


def float32_to_bfloat16(arr):
    """Convert a float32 numpy array to bfloat16 stored as uint16."""
    f32 = arr.astype(np.float32)
    raw = np.frombuffer(f32.tobytes(), dtype=np.uint32)
    bf16 = (raw >> 16).astype(np.uint16)
    return bf16


def parse_args():
    parser = argparse.ArgumentParser(
        description="Convert JPEG image to raw binary (.bin) file",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Supported formats:\n  " + ", ".join(SUPPORTED_FORMATS)
               + "\n\nSupported data types:\n  " + ", ".join(SUPPORTED_DTYPES.keys())
               + "\n\nMean and scale are configured via the MEAN / SCALE constants\n"
                 "at the top of the script (per-channel, R/G/B order).",
    )
    parser.add_argument("-i", "--input", required=True,
                        help="Input JPEG file path")
    parser.add_argument("-f", "--format", default="RGBP", choices=SUPPORTED_FORMATS,
                        metavar="FORMAT", help="Output pixel format (default: RGBP)")
    parser.add_argument("-o", "--output",
                        help="Output .bin file (default: <input>_<format>.bin)")
    parser.add_argument("--shape", default="224x224",
                        help="Resize to WxH before conversion (default: 224x224)")
    parser.add_argument("--dtype", choices=list(SUPPORTED_DTYPES.keys()),
                        default="FP16", metavar="DTYPE",
                        help="Output data type (default: FP16)")
    return parser.parse_args()


def parse_shape(shape_str):
    try:
        w, h = shape_str.lower().split("x")
        return int(w), int(h)
    except (ValueError, AttributeError):
        print(f"Error: Invalid shape '{shape_str}'. Expected WxH (e.g. 224x224).")
        sys.exit(1)


def convert_format(img_rgb, fmt):
    """
    Convert uint8 RGB image to the requested pixel format.

    Returns (data, shape_info_str, layout_str).
    """
    h, w = img_rgb.shape[:2]

    if fmt == "RGB":
        return img_rgb.copy(), f"({h}, {w}, 3)", "HWC"

    if fmt == "BGR":
        return img_rgb[:, :, ::-1].copy(), f"({h}, {w}, 3)", "HWC"

    if fmt == "RGBx":
        x = np.zeros((h, w, 1), dtype=np.uint8)
        return np.concatenate([img_rgb, x], axis=2), f"({h}, {w}, 4)", "HWC"

    if fmt == "BGRx":
        x = np.zeros((h, w, 1), dtype=np.uint8)
        return np.concatenate([img_rgb[:, :, ::-1], x], axis=2), f"({h}, {w}, 4)", "HWC"

    if fmt == "RGBP":
        d = img_rgb.transpose(2, 0, 1).copy()
        return d, f"(3, {h}, {w})", "CHW (Planar RGB)"

    if fmt == "BGRP":
        d = img_rgb[:, :, ::-1].transpose(2, 0, 1).copy()
        return d, f"(3, {h}, {w})", "CHW (Planar BGR)"

    raise ValueError(f"Unsupported format: {fmt}")


def apply_mean_scale(data, fmt):
    """Apply per-channel MEAN subtraction and SCALE multiplication.

    MEAN and SCALE are defined in R, G, B order. For BGR-based formats the
    values are automatically reordered to B, G, R so that each channel gets
    the correct normalization.
    """
    num_ch = 3
    mean = np.array(MEAN[:num_ch], dtype=np.float64)
    scale = np.array(SCALE[:num_ch], dtype=np.float64)

    # Reorder mean/scale for BGR-based formats (B, G, R)
    if fmt in ("BGR", "BGRx", "BGRP"):
        mean = mean[::-1].copy()
        scale = scale[::-1].copy()

    data = data.astype(np.float64)

    if fmt in ("RGBP", "BGRP"):
        # CHW layout: shape is (C, H, W)
        for c in range(num_ch):
            data[c] = (data[c] - mean[c]) * scale[c]
    elif fmt in ("RGBx", "BGRx"):
        # HWC layout with 4 channels: apply to first 3 only
        for c in range(num_ch):
            data[:, :, c] = (data[:, :, c] - mean[c]) * scale[c]
    else:
        # HWC layout with 3 channels
        for c in range(num_ch):
            data[:, :, c] = (data[:, :, c] - mean[c]) * scale[c]

    return data


def main():
    args = parse_args()

    if not os.path.isfile(args.input):
        print(f"Error: Input file '{args.input}' not found.")
        sys.exit(1)

    try:
        img = Image.open(args.input).convert("RGB")
    except Exception as e:
        print(f"Error loading image: {e}")
        sys.exit(1)

    orig_w, orig_h = img.size

    w_t, h_t = parse_shape(args.shape)
    if (w_t, h_t) != (orig_w, orig_h):
        img = img.resize((w_t, h_t), Image.LANCZOS)

    img_np = np.array(img, dtype=np.uint8)

    data, shape_info, layout = convert_format(img_np, args.format)

    # Apply per-channel mean subtraction and scaling
    data = apply_mean_scale(data, args.format)

    out_dtype = args.dtype

    if out_dtype == "BF16":
        out_data = float32_to_bfloat16(data)
    else:
        out_data = data.astype(SUPPORTED_DTYPES[out_dtype])

    if args.output:
        out_path = args.output
    else:
        base = os.path.splitext(os.path.basename(args.input))[0]
        w_t, h_t = parse_shape(args.shape)
        num_ch = 4 if args.format in ("RGBx", "BGRx") else 3
        if args.format in ("RGBP", "BGRP"):
            shape_str = f"{num_ch}x{h_t}x{w_t}"   # CHW
        else:
            shape_str = f"{h_t}x{w_t}x{num_ch}"   # HWC
        out_path = f"{base}_{args.format}_{shape_str}_{out_dtype}.bin"

    out_data.tofile(out_path)

    sep = "=" * 55
    print(sep)
    print("  JPEG -> BIN Conversion Summary")
    print(sep)
    print(f"  Input file    : {args.input}")
    print(f"  Input size    : {orig_w} x {orig_h}")
    if args.shape != "224x224" or (w_t, h_t) != (orig_w, orig_h):
        print(f"  Resized to    : {args.shape}")
    print(f"  Mean          : {MEAN}")
    print(f"  Scale         : {SCALE}")
    print("-" * 55)
    print(f"  Output file   : {out_path}")
    print(f"  Output format : {args.format}")
    print(f"  Output shape  : {shape_info}")
    print(f"  Data layout   : {layout}")
    print(f"  Data type     : {out_dtype}")
    print(f"  File size     : {os.path.getsize(out_path):,} bytes")
    print(sep)


if __name__ == "__main__":
    main()
