#!/usr/bin/env python3

# ===========================================================
# Copyright © 2025 Advanced Micro Devices, Inc. All rights reserved.
# MIT License
# ===========================================================

import numpy as np
import os
import argparse
import sys

try:
    import cv2
    if not hasattr(cv2, 'imread'):
        raise AttributeError("cv2.imread not available")
except (ImportError, AttributeError) as e:
    print(f"Error: OpenCV (cv2) is not properly installed: {e}")
    print("Please install opencv-python:")
    print("  pip install opencv-python --no-deps")
    sys.exit(1)


def preprocess_image(image_path, img_size=640):
    """
    Preprocess a single image: resize to img_size x img_size, BGR->RGB,
    normalize to [0, 1], add alpha channel, output NHWC float32.
    """
    img = cv2.imread(image_path)
    if img is None:
        raise ValueError(f"Failed to load image from {image_path}")

    img = cv2.resize(img, (img_size, img_size))
    img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    img = img.astype(np.float32) / 255.0
    alpha = np.ones((img_size, img_size, 1), dtype=np.float32)
    img = np.concatenate([img, alpha], axis=2)  # [H, W, 4]
    img = np.expand_dims(img, axis=0)           # -> NHWC [1, H, W, 4]

    return img


def quantize_to_int8(preprocessed_img, scale=2.0, zero_point=0, out_bin="input_int8.bin"):
    """
    Quantize a float32 preprocessed image to int8 using QuantizeLinear:
        q = clip(round(x / scale) + zero_point, -128, 127)

    Saves the result as a raw binary file (.bin) for VART/DPU input.

    Args:
        preprocessed_img: float32 NCHW numpy array
        scale:            quantization scale factor (default: 2.0)
        zero_point:       quantization zero point (default: 0)
        out_bin:          output .bin filepath
    """
    q = np.round(preprocessed_img / scale) + zero_point
    q = np.clip(q, -128, 127)
    q = q.astype(np.int8)

    print(f"  Quantized shape: {q.shape}")
    print(f"  dtype:           {q.dtype}")
    print(f"  min / max:       {q.min()} / {q.max()}")

    q.tofile(out_bin)
    print(f"  Saved int8 bin:  {out_bin}")

    return q


def main():
    parser = argparse.ArgumentParser(
        description="Preprocess images and save as INT8 binary files for VART/DPU"
    )
    parser.add_argument("--image_path", type=str, required=True,
                        help="Path to input image file or folder containing images")
    parser.add_argument("--output_dir", type=str, default="output",
                        help="Directory to save .bin files (default: output)")
    parser.add_argument("--out_bin", type=str, default=None,
                        help="Output .bin filename override (single-image mode only; "
                             "default: <image_stem>_int8.bin in --output_dir)")
    parser.add_argument("--img_size", type=int, default=640,
                        help="Resize images to this square size (default: 640)")
    parser.add_argument("--scale", type=float, default= 0.0078125,
                        help="Quantization scale factor (default: 2.0)")
    parser.add_argument("--zero_point", type=int, default=0,
                        help="Quantization zero point (default: 0)")

    args = parser.parse_args()

    if not os.path.exists(args.image_path):
        raise FileNotFoundError(f"Image path not found: {args.image_path}")

    if os.path.isfile(args.image_path):
        image_files = [args.image_path]
    else:
        image_files = [
            os.path.join(args.image_path, f)
            for f in os.listdir(args.image_path)
            if f.lower().endswith(('.png', '.jpg', '.jpeg'))
        ]
        if not image_files:
            raise ValueError(f"No image files found in {args.image_path}")

    output_dir = os.path.abspath(args.output_dir)
    os.makedirs(output_dir, exist_ok=True)

    print("=" * 60)
    print("YOLOv8m Preprocessing  ->  INT8 bin generation")
    print("=" * 60)
    print(f"Input:       {args.image_path}")
    print(f"Images:      {len(image_files)} file(s)")
    print(f"Output dir:  {output_dir}")
    print(f"Image size:  {args.img_size}x{args.img_size}x4")
    print(f"Scale:       {args.scale}")
    print(f"Zero point:  {args.zero_point}")
    print("=" * 60)

    for img_idx, image_path in enumerate(image_files):
        image_stem = os.path.splitext(os.path.basename(image_path))[0]
        print(f"\n[{img_idx + 1}/{len(image_files)}] {os.path.basename(image_path)}")

        try:
            preprocessed_img = preprocess_image(image_path, img_size=args.img_size)
            print(f"  Preprocessed shape: {preprocessed_img.shape}, dtype: {preprocessed_img.dtype}")
        except Exception as e:
            print(f"  Error during preprocessing: {e}")
            continue

        if len(image_files) == 1 and args.out_bin:
            bin_path = args.out_bin
        else:
            bin_path = os.path.join(output_dir, f"{image_stem}_int8.bin")

        try:
            quantize_to_int8(
                preprocessed_img,
                scale=args.scale,
                zero_point=args.zero_point,
                out_bin=bin_path
            )
        except Exception as e:
            print(f"  Error during int8 quantization: {e}")
            continue

    print(f"\n{'='*60}")
    print(f"Done. {len(image_files)} image(s) processed.")
    print(f"INT8 bin files saved to: {output_dir}")
    print("=" * 60)


if __name__ == "__main__":
    main()
