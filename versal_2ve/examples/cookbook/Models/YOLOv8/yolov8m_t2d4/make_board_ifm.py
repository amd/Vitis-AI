#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

import argparse
import json
import pathlib

import numpy as np
import onnx
from onnx import numpy_helper


def input_quant_params(model_path):
    graph = onnx.load(model_path, load_external_data=False).graph
    initializers = {t.name: t for t in graph.initializer}
    graph_inputs = {i.name for i in graph.input}

    for node in graph.node:
        if node.op_type == "QuantizeLinear" and any(i in graph_inputs for i in node.input):
            scale = numpy_helper.to_array(initializers[node.input[1]])
            zero_point = numpy_helper.to_array(initializers[node.input[2]])
            return float(scale), zero_point

    raise RuntimeError(f"no QuantizeLinear on a graph input of {model_path}")


def hw_input_spec(cache_dir):
    for h in sorted(pathlib.Path(cache_dir).glob("**/flexmlrt-hsi.json")):
        try:
            entries = json.load(open(h)).get("inputs") or []
        except Exception:
            continue
        for e in entries:
            shape, dtype = e.get("hw_shape"), e.get("hw_dtype")
            if not shape or not dtype:
                continue
            total = np.dtype(dtype).itemsize
            for d in shape:
                total *= d
            fmt = e.get("hw_format", "")
            padded = shape[-1] if fmt.rstrip("0123456789").endswith("C") else None
            return total, padded
    return None, None


def derive_align(channels, height, width, itemsize, expected):
    if expected is None:
        return None
    per_pixel, rem = divmod(expected, height * width * itemsize)
    if rem or per_pixel < channels:
        raise RuntimeError(
            f"compiled input buffer is {expected} B, which is not "
            f"{height}*{width}*itemsize*<padded C> for C>={channels}")
    return per_pixel


def to_hw_ifm(nchw, scale, zero_point, align):
    limits = np.iinfo(zero_point.dtype)
    quantized = np.clip(np.rint(nchw / scale) + zero_point,
                        limits.min, limits.max).astype(zero_point.dtype)

    nhwc = np.transpose(quantized, (0, 2, 3, 1))
    channels = nhwc.shape[-1]
    padded = -(-channels // align) * align
    if padded != channels:
        nhwc = np.pad(nhwc, ((0, 0), (0, 0), (0, 0), (0, padded - channels)),
                      constant_values=zero_point)
    return np.ascontiguousarray(nhwc)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("npy")
    ap.add_argument("model")
    ap.add_argument("out")
    ap.add_argument("--cache-dir", default=".", help="dir holding the compiled model")
    ap.add_argument("--dp", type=int, default=4, help="dp_size: image slots per buffer")
    args = ap.parse_args()

    scale, zero_point = input_quant_params(args.model)
    nchw = np.load(args.npy)
    if nchw.ndim != 4:
        raise RuntimeError(f"{args.npy} is {nchw.shape}, expected NCHW")
    n, c, h, w = nchw.shape

    expected, padded = hw_input_spec(args.cache_dir)
    align = padded or derive_align(
        c, h, w, np.dtype(zero_point.dtype).itemsize, expected)
    if align is None:
        raise SystemExit(
            "Could not derive channel alignment: no flexmlrt-hsi.json under "
            f"{args.cache_dir!r}. Compile first.")

    ifm = to_hw_ifm(nchw, scale, zero_point, align)
    per_slot = ifm.nbytes // n

    if per_slot != expected:
        raise SystemExit(
            f"REFUSING to write: built {per_slot} B/slot but the compiled "
            f"partition expects {expected} B. A mismatched IFM is read as "
            f"garbage on the board rather than failing loudly.")

    with open(args.out, "wb") as f:
        for _ in range(args.dp):
            ifm.tofile(f)

    print(f"saved {args.out}  {ifm.shape} {ifm.dtype}  align={align}  "
          f"{per_slot} B/slot x dp={args.dp} = {per_slot * n * args.dp} B")


if __name__ == "__main__":
    main()
