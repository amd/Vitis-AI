#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

#!/usr/bin/env python3
"""
Evaluate YOLOX-Tiny ONNX models on COCO validation set.
Pure NumPy implementation without PyTorch dependency.

Usage:
    # CPU evaluation
    python evaluate_coco.py --onnx_model_path yolox_tiny.onnx --coco-dir /path/to/coco

    # NPU evaluation
    python evaluate_coco.py --onnx_model_path yolox_tiny_int8.onnx --coco-dir /path/to/coco --use-npu --vitis-config vitisai_config.json --cache-dir ./cache
"""

import argparse
import contextlib
import io
import os
from pathlib import Path

import cv2
import numpy as np
import onnxruntime as ort
from pycocotools.coco import COCO
from pycocotools.cocoeval import COCOeval
from tqdm import tqdm

ONNX_PATH = "yolox_tiny.onnx"
INPUT_SIZE = (416, 416)
NUM_CLASSES = 80
COCO_CLASSES = [
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck",
    "boat", "traffic light", "fire hydrant", "stop sign", "parking meter", "bench",
    "bird", "cat", "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra",
    "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
    "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove",
    "skateboard", "surfboard", "tennis racket", "bottle", "wine glass", "cup",
    "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange",
    "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
    "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse",
    "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink",
    "refrigerator", "book", "clock", "vase", "scissors", "teddy bear", "hair drier",
    "toothbrush"
]


def preprocess(image: np.ndarray, input_size: tuple) -> tuple:
    """Preprocess image for YOLOX (letterbox resize)."""
    ih, iw = image.shape[:2]
    h, w = input_size
    scale = min(w / iw, h / ih)
    nw, nh = int(iw * scale), int(ih * scale)

    resized = cv2.resize(image, (nw, nh), interpolation=cv2.INTER_LINEAR)

    padded = np.full((h, w, 3), 114, dtype=np.uint8)
    padded[:nh, :nw] = resized

    # HWC -> CHW, BGR (YOLOX uses BGR)
    padded = padded.transpose(2, 0, 1)
    padded = np.ascontiguousarray(padded, dtype=np.float32)
    padded = np.expand_dims(padded, axis=0)

    return padded, scale


def nms(boxes: np.ndarray, scores: np.ndarray, nms_thr: float) -> list:
    """Non-maximum suppression."""
    if len(boxes) == 0:
        return []

    x1, y1, x2, y2 = boxes[:, 0], boxes[:, 1], boxes[:, 2], boxes[:, 3]
    areas = (x2 - x1) * (y2 - y1)
    order = scores.argsort()[::-1]

    keep = []
    while order.size > 0:
        i = order[0]
        keep.append(i)

        if order.size == 1:
            break

        xx1 = np.maximum(x1[i], x1[order[1:]])
        yy1 = np.maximum(y1[i], y1[order[1:]])
        xx2 = np.minimum(x2[i], x2[order[1:]])
        yy2 = np.minimum(y2[i], y2[order[1:]])

        w = np.maximum(0.0, xx2 - xx1)
        h = np.maximum(0.0, yy2 - yy1)
        inter = w * h

        iou = inter / (areas[i] + areas[order[1:]] - inter)
        inds = np.where(iou <= nms_thr)[0]
        order = order[inds + 1]

    return keep


def multiclass_nms(boxes: np.ndarray, scores: np.ndarray, nms_thr: float, score_thr: float):
    """Multi-class NMS."""
    final_boxes, final_scores, final_cls = [], [], []

    for cls_idx in range(scores.shape[1]):
        cls_scores = scores[:, cls_idx]
        valid_mask = cls_scores > score_thr

        if not valid_mask.any():
            continue

        valid_boxes = boxes[valid_mask]
        valid_scores = cls_scores[valid_mask]

        keep = nms(valid_boxes, valid_scores, nms_thr)

        final_boxes.append(valid_boxes[keep])
        final_scores.append(valid_scores[keep])
        final_cls.append(np.full(len(keep), cls_idx))

    if not final_boxes:
        return None

    final_boxes = np.concatenate(final_boxes)
    final_scores = np.concatenate(final_scores)
    final_cls = np.concatenate(final_cls)

    return np.column_stack([final_boxes, final_scores, final_cls])


def decode_outputs(outputs: np.ndarray, input_size: tuple) -> np.ndarray:
    """Decode raw YOLOX outputs to boxes."""
    grids, strides = [], []
    for stride in [8, 16, 32]:
        h, w = input_size[0] // stride, input_size[1] // stride
        yv, xv = np.meshgrid(np.arange(h), np.arange(w), indexing='ij')
        grid = np.stack((xv, yv), axis=2).reshape(-1, 2)
        grids.append(grid)
        strides.append(np.full((h * w, 1), stride))

    grids = np.concatenate(grids, axis=0)
    strides = np.concatenate(strides, axis=0)

    outputs = outputs.squeeze(0)  # Remove batch dimension

    # Decode boxes
    outputs[:, :2] = (outputs[:, :2] + grids) * strides
    outputs[:, 2:4] = np.exp(outputs[:, 2:4]) * strides

    return outputs


def postprocess(outputs: np.ndarray, input_size: tuple, conf_thr: float, nms_thr: float, decoded: bool = False):
    """Postprocess YOLOX outputs."""
    if not decoded:
        outputs = decode_outputs(outputs, input_size)
    else:
        outputs = outputs.squeeze(0)

    # Convert xywh to xyxy
    boxes = outputs[:, :4].copy()
    boxes[:, 0] = outputs[:, 0] - outputs[:, 2] / 2
    boxes[:, 1] = outputs[:, 1] - outputs[:, 3] / 2
    boxes[:, 2] = outputs[:, 0] + outputs[:, 2] / 2
    boxes[:, 3] = outputs[:, 1] + outputs[:, 3] / 2

    # Get objectness and class scores
    obj_scores = outputs[:, 4:5]
    cls_scores = outputs[:, 5:]

    # Combined scores
    scores = obj_scores * cls_scores

    return multiclass_nms(boxes, scores, nms_thr, conf_thr)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--onnx_model_path", default=ONNX_PATH, help="Path to ONNX model")
    ap.add_argument("--coco-dir", required=True, help="Path to COCO dataset")
    ap.add_argument("--val-ann", default="instances_val2017.json", help="Validation annotation file")
    ap.add_argument("--conf-thre", type=float, default=0.001, help="Confidence threshold")
    ap.add_argument("--nms-thre", type=float, default=0.65, help="NMS threshold")
    ap.add_argument("--max-images", type=int, default=0, help="Max images to evaluate (0 for all)")
    ap.add_argument("--decoded", action="store_true", help="Model outputs are already decoded (pre-decoded model)")
    ap.add_argument("--use-npu", action="store_true", help="Use VitisAIExecutionProvider (NPU)")
    ap.add_argument("--vitis-config", type=str, default="vitisai_config.json", help="VitisAI config file path")
    ap.add_argument("--cache-dir", type=str, default="./cache", help="Cache directory for NPU")
    ap.add_argument("--cache-key", type=str, default=None, help="Cache key for NPU")
    args = ap.parse_args()

    # Setup paths
    data_dir = Path(args.coco_dir)
    ann_file = data_dir / "annotations" / args.val_ann
    img_dir = data_dir / "val2017"

    if not ann_file.exists():
        # Try alternative path
        img_dir = data_dir / "images" / "val2017"

    if not ann_file.exists():
        raise FileNotFoundError(f"Annotation file not found: {ann_file}")

    # Load COCO
    print(f"Loading COCO annotations from {ann_file}")
    coco = COCO(str(ann_file))
    img_ids = sorted(coco.getImgIds())

    if args.max_images > 0:
        img_ids = img_ids[:args.max_images]

    # COCO class id mapping (YOLOX uses 0-79, COCO uses specific IDs)
    coco_cat_ids = coco.getCatIds()
    cat_id_to_idx = {cat_id: idx for idx, cat_id in enumerate(coco_cat_ids)}
    idx_to_cat_id = {idx: cat_id for cat_id, idx in cat_id_to_idx.items()}

    # Setup execution provider
    if args.use_npu:
        print(f"Using VitisAIExecutionProvider (NPU)")
        print(f"  Config: {args.vitis_config}")
        print(f"  Cache dir: {args.cache_dir}")
        cache_key = args.cache_key or Path(args.onnx_model_path).stem
        providers = [
            (
                "VitisAIExecutionProvider",
                {
                    "config_file": str(args.vitis_config),
                    "target": "VAIML",
                    "cacheDir": str(args.cache_dir),
                    "cacheKey": cache_key,
                },
            )
        ]
    else:
        print("Using CPUExecutionProvider")
        providers = ["CPUExecutionProvider"]

    # Load ONNX model
    print(f"Loading ONNX model: {args.onnx_model_path}")
    sess = ort.InferenceSession(args.onnx_model_path, providers=providers)
    in_name = sess.get_inputs()[0].name
    out_name = sess.get_outputs()[0].name

    # Run inference
    results = []
    seen_ids = []

    for img_id in tqdm(img_ids, desc="Evaluating"):
        img_info = coco.loadImgs(img_id)[0]
        img_path = img_dir / img_info["file_name"]

        if not img_path.exists():
            # Try alternative path
            img_path = data_dir / "images" / "val2017" / img_info["file_name"]

        if not img_path.exists():
            print(f"Image not found: {img_path}")
            continue

        # Load and preprocess image
        img = cv2.imread(str(img_path))
        if img is None:
            print(f"Failed to load image: {img_path}")
            continue

        orig_h, orig_w = img.shape[:2]
        img_input, scale = preprocess(img, INPUT_SIZE)

        # Run inference
        outputs = sess.run([out_name], {in_name: img_input})[0]

        # Postprocess
        detections = postprocess(outputs, INPUT_SIZE, args.conf_thre, args.nms_thre, decoded=args.decoded)

        seen_ids.append(img_id)

        if detections is None:
            continue

        # Convert to COCO format
        for det in detections:
            x1, y1, x2, y2, score, cls_idx = det

            # Scale back to original image size
            x1 /= scale
            y1 /= scale
            x2 /= scale
            y2 /= scale

            # Clip to image bounds
            x1 = max(0, x1)
            y1 = max(0, y1)
            x2 = min(orig_w, x2)
            y2 = min(orig_h, y2)

            w = x2 - x1
            h = y2 - y1

            if w <= 0 or h <= 0:
                continue

            # Map class index to COCO category ID
            cat_id = idx_to_cat_id.get(int(cls_idx), int(cls_idx) + 1)

            results.append({
                "image_id": img_id,
                "category_id": cat_id,
                "bbox": [float(x1), float(y1), float(w), float(h)],
                "score": float(score)
            })

    if not results:
        print("No detections found!")
        return

    # Run COCO evaluation
    print(f"\nRunning COCO evaluation on {len(seen_ids)} images...")
    cocoDt = coco.loadRes(results)
    E = COCOeval(coco, cocoDt, "bbox")
    E.params.imgIds = sorted(set(seen_ids))
    E.evaluate()
    E.accumulate()

    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        E.summarize()

    # Extract metrics: mAP, mAP50, mAP75
    mAP = float(E.stats[0])      # AP@[0.50:0.95]
    mAP50 = float(E.stats[1])    # AP@0.50
    mAP75 = float(E.stats[2])    # AP@0.75

    print("\n===== Evaluation Result =====")
    print(f"ONNX Model: {args.onnx_model_path}")
    print(f"Execution Provider: {'VitisAIExecutionProvider (NPU)' if args.use_npu else 'CPUExecutionProvider'}")
    print(f"Images evaluated: {len(seen_ids)}")
    print(f"Total detections: {len(results)}")
    print("-" * 60)
    print(f"mAP@[0.50:0.95] = {mAP:.5f}")
    print(f"mAP@0.50        = {mAP50:.5f}")
    print(f"mAP@0.75        = {mAP75:.5f}")
    print("===== End of Evaluation Result =====")


if __name__ == "__main__":
    main()
