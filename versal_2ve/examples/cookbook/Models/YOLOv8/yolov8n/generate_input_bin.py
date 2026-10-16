#!/usr/bin/env python3
# Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
# SPDX-License-Identifier: MIT
"""
Generate reproducible input.bin for YOLOv8n inference testing.

This script generates a deterministic random input tensor that is byte-identical
across runs. The generated file can be used with ml_vart for benchmarking.

Usage:
    python generate_input_bin.py
    python generate_input_bin.py --output custom_input.bin
    python generate_input_bin.py --verify  # verify existing input.bin
"""

import argparse
import hashlib
import sys

import numpy as np

# Model configuration
MODEL_NAME = "yolov8n"
INPUT_SHAPE = (1, 3, 640, 640)  # NCHW format
INPUT_DTYPE = np.float32
RANDOM_SEED = 42

# Expected SHA256 hash (first 16 chars) for verification
EXPECTED_HASH = "8fe668ec31f1c3f9"


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
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--output",
        type=str,
        default="input.bin",
        help="Output file path (default: input.bin)",
    )
    parser.add_argument(
        "--verify",
        action="store_true",
        help="Verify existing input.bin matches expected hash",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show what would be generated without writing",
    )

    args = parser.parse_args()

    if args.verify:
        try:
            existing = np.fromfile(args.output, dtype=INPUT_DTYPE)
            actual_hash = compute_hash(existing)[:16]
            if actual_hash == EXPECTED_HASH:
                print(f"OK: {args.output} matches expected hash ({EXPECTED_HASH})")
                return 0
            else:
                print(f"MISMATCH: {args.output} hash is {actual_hash}, expected {EXPECTED_HASH}")
                return 1
        except FileNotFoundError:
            print(f"ERROR: {args.output} not found")
            return 1

    data = generate_input()
    actual_hash = compute_hash(data)[:16]

    print(f"Model: {MODEL_NAME}")
    print(f"Shape: {data.shape} (NCHW)")
    print(f"Dtype: {data.dtype}")
    print(f"Size: {data.nbytes:,} bytes")
    print(f"Hash: {actual_hash}")
    print(f"Seed: {RANDOM_SEED}")

    if actual_hash != EXPECTED_HASH:
        print(f"WARNING: Hash mismatch (expected {EXPECTED_HASH})")

    if args.dry_run:
        print(f"\nDry run: would write to {args.output}")
    else:
        data.tofile(args.output)
        print(f"\nGenerated: {args.output}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
