#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT
"""
k-NN classification on ImageNet val for DINO ViT-B/16.

`facebook/dino-vitb16` is a self-supervised backbone with NO classification
head: the ONNX outputs `last_hidden_state [1,197,768]`. Following the DINO
protocol (Caron et al., 2021), we evaluate the frozen features with a weighted
k-NN classifier:

  1. Extract the [CLS] token feature (last_hidden_state[:, 0, :], 768-d) for
     every ImageNet *train* image (feature bank) and every *val* image (query).
  2. L2-normalize features; cosine similarity = dot product.
  3. For each query, take the k nearest train features and vote per class with
     weight exp(cos_sim / T). argmax -> top-1, top-5 from the class scores.

Reference hyper-parameters (DINO ViT-B/16): k=20, T=0.07.

Dataset layout (standard ImageNet, val restructured into WNID subfolders):

  <data_root>/
  |-- train/<wnid>/*.JPEG
  `-- val/<wnid>/*.JPEG

Class label indices are the sorted WNID order shared by train and val.

Examples
--------
CPU reference (subsample 100 train imgs/class for speed):
  python3 eval_dino_imagenet_knn.py \
      --onnx_model dino-vitb16_last_hidden_state.onnx \
      --data_root ../../../datasets/imagenet \
      --target cpu --train_per_class 100

NPU (VitisAI EP):
  python3 eval_dino_imagenet_knn.py \
      --onnx_model dino-vitb16_last_hidden_state.onnx \
      --data_root ../../../datasets/imagenet \
      --target npu --cache_key dino-vitb16_last_hidden_state \
      --train_per_class 100
"""

import os
import time
import argparse
import numpy as np
import onnxruntime as ort
from glob import glob
from tqdm import tqdm
from PIL import Image

# ==================================================
# Arguments
# ==================================================
parser = argparse.ArgumentParser("DINO ViT-B/16 ImageNet-val k-NN Evaluation")
parser.add_argument("--onnx_model", required=True)
parser.add_argument("--data_root", required=True,
                    help="ImageNet root. Either a parent holding train/ + "
                         "val/ (or val_data/), or a directory that itself "
                         "contains the WNID class subfolders (e.g. "
                         ".../imagenet-1k/val_data/).")
parser.add_argument("--val_only", action="store_true",
                    help="no train/ split available: use val/ as both the feature "
                         "bank and the queries with leave-one-out (each query "
                         "excludes itself from its own nearest-neighbor search)")
parser.add_argument("--target", default="cpu", choices=["cpu", "npu"])
parser.add_argument("--config_file", default="vitisai_config.json")
parser.add_argument("--cache_key", default="dino-vitb16_last_hidden_state")
parser.add_argument("--k", type=int, default=20, help="number of neighbors")
parser.add_argument("-T", "--temperature", type=float, default=0.07)
parser.add_argument("--resize", type=int, default=256, help="short-side resize before crop")
parser.add_argument("--crop", type=int, default=224, help="center-crop size")
parser.add_argument("--train_per_class", type=int, default=0,
                    help="images per class for the feature bank (0 = all)")
parser.add_argument("--max_val", type=int, default=0,
                    help="cap total val images evaluated (0 = all)")
parser.add_argument("--feature_cache", default="",
                    help="path to .npz to load/save extracted features "
                         "(default: knn_feats_<target>.npz next to the model)")
parser.add_argument("--recompute", action="store_true",
                    help="ignore an existing feature cache and re-extract")
parser.add_argument("--warmup", type=int, default=5)
parser.add_argument("--seed", type=int, default=0)
args = parser.parse_args()

rng = np.random.default_rng(args.seed)

# ==================================================
# Create Session (mirrors segformer eval conventions)
# ==================================================
if args.target == "cpu":
    sess = ort.InferenceSession(args.onnx_model, providers=["CPUExecutionProvider"])
else:
    provider_options = {
        "config_file": args.config_file,
        "cache_dir": "./",
        "cache_key": args.cache_key,
        "ai_analyzer_visualization": True,
        "ai_analyzer_profiling": True,
        "log_level": "info",
        "target": "VAIML",
    }
    sess = ort.InferenceSession(
        args.onnx_model,
        providers=["VitisAIExecutionProvider"],
        provider_options=[provider_options],
    )

print("Providers:", sess.get_providers())

inp = sess.get_inputs()[0]
out = sess.get_outputs()[0]
input_name = inp.name
input_shape = inp.shape
input_type = inp.type
print("Input :", input_name, input_shape, input_type)
print("Output:", out.name, out.shape)

# static [1,3,H,W] expected; fall back to the requested crop otherwise
if len(input_shape) == 4 and isinstance(input_shape[2], int) and input_shape[2] > 0:
    H, W = input_shape[2], input_shape[3]
else:
    H = W = args.crop

if "float16" in input_type:
    model_dtype = np.float16
else:
    model_dtype = np.float32
print("Model HxW:", H, W, "dtype:", model_dtype)
print("================================")

# ==================================================
# Preprocess (torchvision/DINO style: resize short side -> center crop -> normalize)
# ==================================================
mean = np.array([0.485, 0.456, 0.406], dtype=np.float32)
std = np.array([0.229, 0.224, 0.225], dtype=np.float32)


def preprocess(path):
    img = Image.open(path).convert("RGB")
    w, h = img.size
    if w <= h:
        nw, nh = args.resize, int(round(args.resize * h / w))
    else:
        nw, nh = int(round(args.resize * w / h)), args.resize
    img = img.resize((nw, nh), Image.BICUBIC)
    left = (nw - args.crop) // 2
    top = (nh - args.crop) // 2
    img = img.crop((left, top, left + args.crop, top + args.crop))

    x = np.asarray(img, dtype=np.float32) / 255.0        # HWC
    x = (x - mean) / std
    x = x.transpose(2, 0, 1)[None]                        # 1,C,H,W
    return np.ascontiguousarray(x, dtype=model_dtype)


def cls_feature(path):
    x = preprocess(path)
    y = sess.run(None, {input_name: x})[0]               # [1,197,768]
    return np.asarray(y, dtype=np.float32)[0, 0, :]      # CLS token -> [768]


# ==================================================
# Build file lists / label mapping
# ==================================================
def list_wnids(d):
    return sorted(x for x in os.listdir(d)
                  if os.path.isdir(os.path.join(d, x)))


def resolve_split(root, names):
    """Return the first of <root>/<name> that exists, else None."""
    for n in names:
        p = os.path.join(root, n)
        if os.path.isdir(p):
            return p
    return None


# Resolve the val directory. --data_root may either wrap the split(s)
# (contains val/ | val_data/ | validation/) or already BE the directory of
# WNID subfolders (e.g. .../imagenet-1k/val_data/).
root = os.path.normpath(args.data_root)
val_dir = resolve_split(root, ["val", "val_data", "validation"])
if val_dir is None:
    val_dir = root                       # data_root already points at the WNIDs
val_wnids = list_wnids(val_dir)
assert val_wnids, f"no class subfolders found under {val_dir}"

# train dir (feature bank). Absent -> leave-one-out on val.
train_dir = resolve_split(root, ["train", "train_data"])
val_only = args.val_only or train_dir is None

if val_only:
    all_wnids = val_wnids
    train_dir = val_dir
else:
    train_wnids = list_wnids(train_dir)
    assert train_wnids == val_wnids, \
        "train/ and val/ must share the same WNID subfolders"
    all_wnids = train_wnids
wnid_to_idx = {w: i for i, w in enumerate(all_wnids)}
num_classes = len(all_wnids)
print("Val dir  :", val_dir)
if not val_only:
    print("Train dir:", train_dir)
print("Mode:", "val-only (leave-one-out)" if val_only else "train-bank / val-query")
print("Classes:", num_classes)


def collect(split_dir, per_class):
    files, labels = [], []
    for w in all_wnids:
        fs = []
        for ext in ("*.JPEG", "*.jpeg", "*.jpg", "*.JPG",
                    "*.PNG", "*.png"):
            fs.extend(glob(os.path.join(split_dir, w, ext)))
        fs = sorted(fs)
        if per_class and per_class > 0 and len(fs) > per_class:
            sel = rng.choice(len(fs), size=per_class, replace=False)
            fs = [fs[i] for i in sorted(sel)]
        files.extend(fs)
        labels.extend([wnid_to_idx[w]] * len(fs))
    return files, np.asarray(labels, dtype=np.int64)


val_files, val_labels = collect(val_dir, 0)
if args.max_val and args.max_val > 0 and len(val_files) > args.max_val:
    sel = rng.choice(len(val_files), size=args.max_val, replace=False)
    sel = sorted(sel)
    val_files = [val_files[i] for i in sel]
    val_labels = val_labels[sel]

if val_only:
    # bank == query; leave-one-out handled in knn_predict
    train_files, train_labels = val_files, val_labels
else:
    train_files, train_labels = collect(train_dir, args.train_per_class)
print("Train (bank) images:", len(train_files))
print("Val   (query) images:", len(val_files))

# ==================================================
# Warmup
# ==================================================
if args.warmup > 0 and len(train_files) > 0:
    print("Warmup...")
    x = preprocess(train_files[0])
    for _ in range(args.warmup):
        sess.run(None, {input_name: x})

# ==================================================
# Feature extraction (+ optional cache)
# ==================================================
cache_path = args.feature_cache or os.path.join(
    os.path.dirname(os.path.abspath(args.onnx_model)) or ".",
    f"knn_feats_{args.target}.npz",
)


def extract(files, tag):
    feats = np.empty((len(files), 768), dtype=np.float32)
    t0 = time.time()
    for i, f in enumerate(tqdm(files, desc=f"extract[{tag}]")):
        feats[i] = cls_feature(f)
    dt = time.time() - t0
    return feats, dt


if (not args.recompute) and os.path.exists(cache_path):
    print("Loading cached features:", cache_path)
    z = np.load(cache_path)
    train_feat, train_lab = z["train_feat"], z["train_lab"]
    val_feat, val_lab = z["val_feat"], z["val_lab"]
    ext_time = float(z["ext_time"]) if "ext_time" in z else 0.0
    n_ext = int(z["n_ext"]) if "n_ext" in z else 0
    # sanity: cached bank must match current selection sizes
    if (train_feat.shape[0] != len(train_files) or
            val_feat.shape[0] != len(val_files)):
        print("  cache size mismatch -> re-extracting")
        args.recompute = True

if args.recompute or not os.path.exists(cache_path):
    val_feat, dt2 = extract(val_files, "val")
    val_lab = val_labels
    if val_only:
        # bank == query: extract only once, reuse
        train_feat, train_lab, dt1 = val_feat, val_lab, 0.0
    else:
        train_feat, dt1 = extract(train_files, "train")
        train_lab = train_labels
    ext_time = dt1 + dt2
    n_ext = len(val_files) if val_only else len(train_files) + len(val_files)
    np.savez(cache_path,
             train_feat=train_feat, train_lab=train_lab,
             val_feat=val_feat, val_lab=val_lab,
             ext_time=ext_time, n_ext=n_ext)
    print("Saved features ->", cache_path)

# ==================================================
# Weighted k-NN (cosine + softmax-over-similarity voting)
# ==================================================
def l2norm(a):
    return a / (np.linalg.norm(a, axis=1, keepdims=True) + 1e-12)


def knn_predict(bank, bank_lab, query, k, T, ncls, loo=False):
    bank = l2norm(bank.astype(np.float32))
    query = l2norm(query.astype(np.float32))
    # leave-one-out: a query cannot use itself as a neighbor
    k = min(k, bank.shape[0] - 1 if loo else bank.shape[0])
    kk = min(5, ncls)                                    # top-N width (<5 if few classes)
    top1 = np.empty(query.shape[0], dtype=np.int64)
    top5 = np.empty((query.shape[0], kk), dtype=np.int64)
    chunk = 256
    for i in range(0, query.shape[0], chunk):
        q = query[i:i + chunk]
        sim = q @ bank.T                                 # [c, Nb] cosine
        if loo:
            # bank order == query order: mask each query's own row
            local = np.arange(q.shape[0])
            sim[local, i + local] = -np.inf
        idx = np.argpartition(-sim, k - 1, axis=1)[:, :k]
        rows = np.arange(q.shape[0])[:, None]
        topk_sim = sim[rows, idx]                        # [c, k]
        topk_lab = bank_lab[idx]                         # [c, k]
        w = np.exp(topk_sim / T)                         # [c, k]
        flat = np.zeros(q.shape[0] * ncls, dtype=np.float32)
        lin = (rows * ncls + topk_lab).ravel()           # [c*k]
        np.add.at(flat, lin, w.ravel())
        scores = flat.reshape(q.shape[0], ncls)
        order = np.argsort(-scores, axis=1)
        top1[i:i + chunk] = order[:, 0]
        top5[i:i + chunk] = order[:, :kk]
    return top1, top5


print("Running k-NN (k=%d, T=%.3f)..." % (args.k, args.temperature))
pred1, pred5 = knn_predict(train_feat, train_lab, val_feat,
                           args.k, args.temperature, num_classes,
                           loo=val_only)

acc1 = float((pred1 == val_lab).mean()) * 100.0
acc5 = float((pred5 == val_lab[:, None]).any(axis=1).mean()) * 100.0

# ==================================================
# Results
# ==================================================
top1_accuracy = acc1 / 100.0
top5_accuracy = acc5 / 100.0

print("\n===== Evaluation Result =====")
print(f"Top-1 Accuracy: {top1_accuracy:.4f}")
print(f"Top-5 Accuracy: {top5_accuracy:.4f}")
if n_ext > 0 and ext_time > 0:
    print(f"FPS (model only): {n_ext / ext_time:.2f}")
print("===== End of Evaluation Result =====")
