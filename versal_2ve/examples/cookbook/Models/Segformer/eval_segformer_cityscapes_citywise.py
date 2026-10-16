#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

import os
import cv2
import time
import argparse
import numpy as np
import onnxruntime as ort
from glob import glob
from tqdm import tqdm
from collections import defaultdict

# ==================================================
# Arguments
# ==================================================
parser = argparse.ArgumentParser("Cityscapes ONNX Evaluation (Final)")
parser.add_argument("--onnx_model", required=True)
parser.add_argument("--data_root", required=True)
parser.add_argument("--target", default="cpu", choices=["cpu", "npu"])
parser.add_argument("--vitis_config", default="vitisai_config.json",
                    help="Path to the VitisAI EP config file (used when --target npu)")
parser.add_argument("--cache_dir", default="./",
                    help="Directory holding the compilation cache (.rai) when --target npu")
parser.add_argument("--cache_key", default="segformer_eval")
parser.add_argument("--warmup", type=int, default=10)
args = parser.parse_args()

num_classes = 19

# ==================================================
# labelId -> trainId
# ==================================================
label_map = np.full(256, 255, dtype=np.uint8)
mapping = {
    7:0,8:1,11:2,12:3,13:4,17:5,
    19:6,20:7,21:8,22:9,23:10,
    24:11,25:12,26:13,27:14,
    28:15,31:16,32:17,33:18
}
for k,v in mapping.items():
    label_map[k] = v

# ==================================================
# Create Session
# ==================================================
if args.target == "cpu":
    providers = ["CPUExecutionProvider"]
    sess = ort.InferenceSession(args.onnx_model, providers=providers)
else:
    provider_options = {
        "config_file": os.path.abspath(args.vitis_config),
        "cache_dir": os.path.abspath(args.cache_dir),
        "cache_key": args.cache_key,
        "ai_analyzer_visualization": True,
        "ai_analyzer_profiling": True,
        "log_level": "info",
        "target": "VAIML",
    }
    sess = ort.InferenceSession(
        args.onnx_model,
        providers=["VitisAIExecutionProvider"],
        provider_options=[provider_options]
    )

print("Providers:", sess.get_providers())

# ==================================================
# Model Info (AUTO)
# ==================================================
inp = sess.get_inputs()[0]
out = sess.get_outputs()[0]

input_name = inp.name
input_shape = inp.shape
input_type = inp.type

print("Input shape:", input_shape)
print("Input type :", input_type)
print("Output shape:", out.shape)

# Layout
if input_shape[1] == 3:
    layout = "NCHW"
    H, W = input_shape[2], input_shape[3]
else:
    layout = "NHWC"
    H, W = input_shape[1], input_shape[2]

input_size = (W, H)

# dtype
if "int8" in input_type:
    model_dtype = np.int8
elif "float16" in input_type:
    model_dtype = np.float16
else:
    model_dtype = np.float32

print("Layout:", layout)
print("Model size:", H, W)
print("Model dtype:", model_dtype)
print("================================")

# ==================================================
# Preprocess
# ==================================================
mean = np.array([0.485,0.456,0.406])
std  = np.array([0.229,0.224,0.225])

def preprocess(img):
    img = cv2.resize(img, input_size)
    img = img[:, :, ::-1] / 255.0
    img = (img - mean) / std

    if layout == "NCHW":
        img = img.transpose(2,0,1)

    img = img[None]

    if model_dtype == np.int8:
        img = np.clip(img * 127, -128, 127).astype(np.int8)
    elif model_dtype == np.float16:
        img = img.astype(np.float16)
    else:
        img = img.astype(np.float32)

    return img

# ==================================================
# Metrics
# ==================================================
def fast_hist(pred, label, n):
    mask = (label >= 0) & (label < n)
    hist = np.bincount(
        n * label[mask].astype(int) + pred[mask],
        minlength=n*n
    ).reshape(n,n)
    return hist

def compute_miou(hist):
    iou = np.diag(hist) / (hist.sum(1)+hist.sum(0)-np.diag(hist)+1e-10)
    return iou, np.mean(iou)

# ==================================================
# Dataset
# ==================================================
root = args.data_root
img_list = glob(os.path.join(root, "leftImg8bit/val/*/*.png"))
print("Total images:", len(img_list))

# ==================================================
# Warmup
# ==================================================
if args.warmup > 0:
    print("Warmup...")
    dummy = np.zeros((H, W, 3), dtype=np.uint8)
    inp_tensor = preprocess(dummy)
    for _ in range(args.warmup):
        sess.run(None, {input_name: inp_tensor})

# ==================================================
# Evaluation
# ==================================================
city_hist = defaultdict(lambda: np.zeros((num_classes,num_classes), dtype=np.int64))
overall_hist = np.zeros((num_classes,num_classes), dtype=np.int64)

total_time = 0
count = 0

for img_path in tqdm(img_list):

    city = os.path.basename(os.path.dirname(img_path))
    name = os.path.basename(img_path).replace("_leftImg8bit.png","")

    label_path = os.path.join(
        root,
        "gtFine/val",
        city,
        name + "_gtFine_labelIds.png"
    )

    if not os.path.exists(label_path):
        continue

    img = cv2.imread(img_path)
    if img is None:
        print(f"[WARN] Skipping unreadable image: {img_path}")
        continue
    h, w = img.shape[:2]

    label = cv2.imread(label_path, 0)
    if label is None:
        print(f"[WARN] Skipping unreadable label: {label_path}")
        continue
    label = label_map[label]

    inp_tensor = preprocess(img)

    t0 = time.time()
    outputs = sess.run(None, {input_name: inp_tensor})[0]
    total_time += time.time() - t0
    count += 1

    # Decode
    if outputs.shape[1] == num_classes:
        pred = np.argmax(outputs[0], axis=0)
    else:
        pred = np.argmax(outputs[0], axis=-1)

    pred = cv2.resize(pred.astype(np.uint8),
                      (w,h),
                      interpolation=cv2.INTER_NEAREST)

    city_hist[city] += fast_hist(pred, label, num_classes)
    overall_hist += fast_hist(pred, label, num_classes)

# ==================================================
# Results
# ==================================================
iou, miou = compute_miou(overall_hist)

print("\n===== Evaluation Result =====")
for i, v in enumerate(iou):
    print(f"class {i:2d}: {v:.4f}")
print(f"Overall mIoU: {miou}")
if count > 0:
    fps = count / total_time
    print(f"FPS (model only): {fps}")
print("===== End of Evaluation Result =====")
