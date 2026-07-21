#!/usr/bin/env python3
# Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.
# pyright: reportMissingImports=false
"""Prepare any image as a YOLOv7 raw .bin input file."""

import argparse
from pathlib import Path

import numpy as np
from PIL import Image


RESAMPLE_BILINEAR = getattr(getattr(Image, "Resampling", Image), "BILINEAR")


def preprocess(
    image: Image.Image,
    image_size: int,
    input_format: str,
    normalize: bool,
    center: bool,
) -> tuple[np.ndarray, float, tuple[int, int]]:
    # Match YOLO-style letterbox preprocessing so postprocessing can map boxes
    # back to the original image using the returned scale and padding.
    rgb = image.convert("RGB")
    width, height = rgb.size
    scale = min(image_size / height, image_size / width)
    resized_size = (int(width * scale), int(height * scale))
    resized = rgb.resize(resized_size, RESAMPLE_BILINEAR)

    padded = Image.new("RGB", (image_size, image_size), (114, 114, 114))
    pad_x = (image_size - resized_size[0]) // 2 if center else 0
    pad_y = (image_size - resized_size[1]) // 2 if center else 0
    padded.paste(resized, (pad_x, pad_y))

    array = np.asarray(padded, dtype=np.float32)
    if input_format == "bgr":
        array = array[:, :, ::-1]
    if normalize:
        array /= 255.0
    array = array.transpose(2, 0, 1)
    return np.expand_dims(np.ascontiguousarray(array), axis=0), scale, (pad_x, pad_y)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="Input image path.")
    parser.add_argument("--img-size", type=int, default=640, help="Square model input size.")
    parser.add_argument(
        "--output-prefix",
        type=Path,
        default=None,
        help="Output prefix. Defaults to '<image_stem>_ifm' beside the image.",
    )
    parser.add_argument(
        "--output-format",
        choices=["bin"],
        default="bin",
        help="Output artifact format. Only bin is supported.",
    )
    parser.add_argument("--input-format", choices=["rgb", "bgr"], default="rgb", help="Channel order.")
    parser.add_argument("--no-normalize", action="store_true", help="Disable division by 255.0.")
    parser.add_argument("--top-left", action="store_true", help="Place resized image at top-left instead of centered.")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if not args.image.exists():
        raise FileNotFoundError(args.image)

    output_prefix = args.output_prefix or args.image.with_name(f"{args.image.stem}_ifm")
    bin_path = output_prefix.with_suffix(".bin")

    image = Image.open(args.image)
    input_tensor, scale, padding = preprocess(
        image,
        args.img_size,
        input_format=args.input_format,
        normalize=not args.no_normalize,
        center=not args.top_left,
    )

    output_tensor = input_tensor.astype(np.float32, copy=False)
    output_prefix.parent.mkdir(parents=True, exist_ok=True)
    output_tensor.tofile(bin_path)

    print("Prepared input tensor:")
    print(f"  shape: {input_tensor.shape}")
    print(f"  dtype: {input_tensor.dtype}")
    print("Letterbox metadata (used by downstream postprocessing for box mapping):")
    print(f"  scale: {scale:.8f}")
    print(f"  padding (x, y): {padding}")
    print(f"Wrote {bin_path}")


if __name__ == "__main__":
    main()
