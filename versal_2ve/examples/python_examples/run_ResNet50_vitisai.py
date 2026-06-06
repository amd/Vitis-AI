#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

#!/usr/bin/env python3
"""Run ONNX with VitisAI EP. For ORT+VAIP, use the same name for the cache_key
and RAI"""

import argparse
import os

import numpy as np
import onnxruntime as ort


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument(
        "--model",
        default="resnet50_int8",
        help="Model folder name and cache_key (default: resnet50_int8)",
    )
    p.add_argument(
        "--base-dir",
        default="/etc/vai/models",
        help="Root directory containing per-model subfolders (default: /etc/vai/models)",
    )
    p.add_argument(
        "--input",
        default="/etc/vai/models/resnet50_int8/data/ifm_input_fp32_1x3x224x224.bin",
        help="Input IFM float32 NCHW .bin",
    )
    p.add_argument(
        "--onnx-name",
        default="resnet50_int8.onnx",
        help="ONNX filename inside model dir (default: resnet50_int8.onnx)",
    )
    p.add_argument(
        "--config-name",
        default="vitisai_config.json",
        help="Config filename inside model dir (default: vitisai_config.json)",
    )
    p.add_argument(
        "--input-name",
        default=None,
        help="Which ONNX input to feed (default: first input from the model)",
    )
    p.add_argument(
        "--output-prefix",
        default=None,
        help="Prefix for OFM .bin files: <prefix>_0.bin, <prefix>_1.bin, ... "
        "(default: ./{model}_ofm)",
    )
    p.add_argument(
        "--postprocess",
        action="store_true",
        help="After inference, softmax first output and print top-k class ids (default: off)",
    )
    p.add_argument(
        "--postprocess-top-k",
        type=int,
        default=5,
        help="With --postprocess, how many top classes to print (default: 5)",
    )
    p.add_argument(
        "--labels",
        default=None,
        help="Optional ImageNet-style labels file (1000 lines); used with --postprocess",
    )
    return p.parse_args()


def _ort_type_to_numpy(ort_type: str) -> np.dtype:
    mapping = {
        "tensor(float)": np.float32,
        "tensor(float16)": np.float16,
        "tensor(double)": np.float64,
        "tensor(int8)": np.int8,
        "tensor(uint8)": np.uint8,
        "tensor(int32)": np.int32,
        "tensor(int64)": np.int64,
        "tensor(bool)": np.bool_,
    }
    if ort_type not in mapping:
        raise ValueError(f"unsupported ORT type {ort_type!r} for raw .bin input")
    return mapping[ort_type]


def _fixed_shape_tuple(shape) -> tuple:
    out = []
    for d in shape:
        if isinstance(d, int):
            out.append(int(d))
        else:
            raise ValueError(
                f"shape {shape!r} is not fixed (found dim {d!r}); "
                "use a fixed-shape ONNX or a different feeder for dynamic axes"
            )
    return tuple(out)


args = parse_args()
base = os.path.abspath(args.base_dir)
model = args.model
onnx_name = args.onnx_name or f"{model}.onnx"

model_dir = os.path.join(base, model)
model_path = os.path.join(model_dir, onnx_name)
config_path = os.path.join(model_dir, args.config_name)
cache_dir = base if base.endswith(os.sep) else base + os.sep

sess = ort.InferenceSession(
    model_path,
    sess_options=ort.SessionOptions(),
    providers=["VitisAIExecutionProvider"],
    provider_options=[{
        "config_file": config_path,
        "cache_dir": cache_dir,
        "cache_key": model,
        "log_level": "info",
        "target": "VAIML",
    }],
)

print("model:", model_path)
print("config:", config_path)
print("cache_dir:", cache_dir, "cache_key:", model)
print("input file:", args.input)
print("input tensors:")
for t in sess.get_inputs():
    print(" ", t.name, t.type, t.shape)
print("output tensors:")
for t in sess.get_outputs():
    print(" ", t.name, t.type, t.shape)

io_in = sess.get_inputs()
if not io_in:
    raise RuntimeError("model has no inputs")
by_name = {t.name: t for t in io_in}
feed_name = args.input_name or io_in[0].name
if feed_name not in by_name:
    raise ValueError(f"--input-name {feed_name!r} not in model inputs {list(by_name)}")
inp_meta = by_name[feed_name]
dtype = _ort_type_to_numpy(inp_meta.type)
shape = _fixed_shape_tuple(inp_meta.shape)
elem = int(np.prod(shape))
byte_len = elem * int(np.dtype(dtype).itemsize)
with open(args.input, "rb") as f:
    raw = f.read()
if len(raw) != byte_len:
    raise ValueError(
        f"input file {args.input!r}: got {len(raw)} bytes, need {byte_len} for "
        f"{feed_name} shape={shape} dtype={dtype}"
    )
x = np.frombuffer(raw, dtype=dtype).reshape(shape)
print(
    "feeding",
    feed_name,
    "dtype=",
    dtype,
    "shape=",
    shape,
    "from",
    args.input,
)
y = sess.run(None, {feed_name: x})

out_prefix = args.output_prefix
if out_prefix is None:
    out_prefix = os.path.join(".", f"{model}_ofm")

for i, arr in enumerate(y):
    out_path = f"{out_prefix}_{i}.bin"
    np.asarray(arr, dtype=np.float32).tofile(out_path)
    print("wrote OFM", i, sess.get_outputs()[i].name, arr.shape, "->", out_path)

if args.postprocess:
    logits = np.asarray(y[0], dtype=np.float64)
    logits = logits - np.max(logits, axis=1, keepdims=True)
    probs = np.exp(logits)
    probs = probs / np.sum(probs, axis=1, keepdims=True)
    k = max(1, args.postprocess_top_k)
    idx = np.argsort(probs[0])[-k:][::-1]
    lines = None
    if args.labels:
        with open(args.labels, encoding="utf-8") as lf:
            lines = [ln.strip() for ln in lf.readlines()]
    print("postprocess: top", k, "(class_id, prob):")
    for j in idx:
        pj = float(probs[0, j])
        if lines is not None and 0 <= int(j) < len(lines):
            print(" ", int(j), pj, lines[int(j)])
        else:
            print(" ", int(j), pj)
