#!/usr/bin/env python3
# Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
# SPDX-License-Identifier: MIT
"""
Generate reproducible input.bin for YOLOX-S inference testing.

This script generates a deterministic random input tensor that is byte-identical
across runs. The generated file can be used with ml_vart for benchmarking.

Usage:
    python generate_input_bin.py
    python generate_input_bin.py --output custom_input.bin
"""

import argparse
import hashlib
import sys

import numpy as np

# Model configuration
MODEL_NAME = "YOLOX-S"
INPUT_SHAPE = (1, 3, 640, 640)  # NCHW format
INPUT_DTYPE = np.float32
RANDOM_SEED = 42


def generate_input() -> np.ndarray:
    """Generate deterministic random input tensor."""
    np.random.seed(RANDOM_SEED)
    return np.random.rand(*INPUT_SHAPE).astype(INPUT_DTYPE)


def compute_hash(data: np.ndarray) -> str:
    """Compute SHA256 hash of numpy array."""
    return hashlib.sha256(data.tobytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(
        description=f"Generate reproducible input.bin for {MODEL_NAME}",
    )
    parser.add_argument(
        "--output",
        type=str,
        default="input.bin",
        help="Output file path (default: input.bin)",
    )

    args = parser.parse_args()

    data = generate_input()
    actual_hash = compute_hash(data)[:16]

    print(f"Model: {MODEL_NAME}")
    print(f"Shape: {data.shape} (NCHW)")
    print(f"Dtype: {data.dtype}")
    print(f"Size: {data.nbytes:,} bytes")
    print(f"Hash: {actual_hash}")

    data.tofile(args.output)
    print(f"\nGenerated: {args.output}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
