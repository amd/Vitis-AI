#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

#!/usr/bin/env python3
"""
Evaluate YOLOv8n ONNX models on COCO validation set.
Pure NumPy implementation without PyTorch/Ultralytics dependency.

Usage:
    # CPU evaluation
    python evaluate_coco.py --onnx_model_path yolov8n.onnx --coco-dir /path/to/coco

    # NPU evaluation
    python evaluate_coco.py --onnx_model_path yolov8n_int8.onnx --coco-dir /path/to/coco --use-npu --vitis-config vitisai_config.json --cache-dir ./cache
"""

import argparse
import contextlib
import io
from pathlib import Path

import cv2
import numpy as np
import onnxruntime as ort
from pycocotools.coco import COCO
from pycocotools.cocoeval import COCOeval
from tqdm import tqdm

ONNX_PATH = "yolov8n.onnx"
INPUT_SIZE = (640, 640)
NUM_CLASSES = 80

# COCO category IDs (non-contiguous, 1-indexed)
COCO_CATEGORIES = [
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 14, 15, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 27, 28, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42,
    43, 44, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
    62, 63, 64, 65, 67, 70, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 84,
    85, 86, 87, 88, 89, 90,
]


def preprocess(image: np.ndarray, input_size: tuple) -> tuple:
    """Preprocess image for YOLOv8 (letterbox resize with center padding)."""
    ih, iw = image.shape[:2]
    h, w = input_size
    scale = min(w / iw, h / ih)
    nw, nh = int(iw * scale), int(ih * scale)

    resized = cv2.resize(image, (nw, nh), interpolation=cv2.INTER_LINEAR)

    # Create padded image with gray background (114, 114, 114)
    padded = np.full((h, w, 3), 114, dtype=np.uint8)

    # Center padding
    pad_x = (w - nw) // 2
    pad_y = (h - nh) // 2
    padded[pad_y:pad_y + nh, pad_x:pad_x + nw] = resized

    # HWC to CHW, normalize to [0, 1]
    padded = padded.transpose(2, 0, 1)
    padded = np.ascontiguousarray(padded, dtype=np.float32) / 255.0
    padded = np.expand_dims(padded, axis=0)

    return padded, scale, (pad_x, pad_y)


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

        iou = inter / (areas[i] + areas[order[1:]] - inter + 1e-6)
        inds = np.where(iou <= nms_thr)[0]
        order = order[inds + 1]

    return keep


def postprocess(outputs: np.ndarray, scale: float, pad: tuple, orig_shape: tuple,
                conf_thr: float, nms_thr: float):
    """
    Postprocess YOLOv8 outputs.

    YOLOv8 output format: [1, 84, 8400] where:
    - 84 = 4 (xywh) + 80 (class scores)
    - 8400 = number of predictions
    - No objectness score (unlike YOLOX)
    - Boxes are already decoded (absolute coordinates)
    """
    # Transpose from [1, 84, 8400] to [8400, 84]
    predictions = outputs[0].T

    # Extract boxes and class scores
    boxes_xywh = predictions[:, :4]
    class_scores = predictions[:, 4:]

    # Filter by confidence
    max_scores = class_scores.max(axis=1)
    mask = max_scores > conf_thr

    boxes_xywh = boxes_xywh[mask]
    class_scores = class_scores[mask]
    max_scores = max_scores[mask]
    class_ids = class_scores.argmax(axis=1)

    if len(boxes_xywh) == 0:
        return None

    # Convert xywh to xyxy
    boxes = np.zeros_like(boxes_xywh)
    boxes[:, 0] = boxes_xywh[:, 0] - boxes_xywh[:, 2] / 2  # x1
    boxes[:, 1] = boxes_xywh[:, 1] - boxes_xywh[:, 3] / 2  # y1
    boxes[:, 2] = boxes_xywh[:, 0] + boxes_xywh[:, 2] / 2  # x2
    boxes[:, 3] = boxes_xywh[:, 1] + boxes_xywh[:, 3] / 2  # y2

    # Apply NMS
    keep = nms(boxes, max_scores, nms_thr)
    boxes = boxes[keep]
    max_scores = max_scores[keep]
    class_ids = class_ids[keep]

    # Scale back to original image (account for center padding)
    pad_x, pad_y = pad
    boxes[:, [0, 2]] = (boxes[:, [0, 2]] - pad_x) / scale
    boxes[:, [1, 3]] = (boxes[:, [1, 3]] - pad_y) / scale

    # Clip to image bounds
    h, w = orig_shape
    boxes[:, [0, 2]] = np.clip(boxes[:, [0, 2]], 0, w)
    boxes[:, [1, 3]] = np.clip(boxes[:, [1, 3]], 0, h)

    return np.column_stack([boxes, max_scores, class_ids])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--onnx_model_path", default=ONNX_PATH, help="Path to ONNX model")
    ap.add_argument("--coco-dir", required=True, help="Path to COCO dataset")
    ap.add_argument("--val-ann", default="instances_val2017.json", help="Validation annotation file")
    ap.add_argument("--conf-thre", type=float, default=0.001, help="Confidence threshold")
    ap.add_argument("--nms-thre", type=float, default=0.65, help="NMS threshold")
    ap.add_argument("--max-images", type=int, default=0, help="Max images to evaluate (0 for all)")
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
        ann_file = data_dir / "labels" / args.val_ann
        img_dir = data_dir / "images" / "val2017"

    if not ann_file.exists():
        raise FileNotFoundError(f"Annotation file not found: {ann_file}")

    # Find image directory
    if not img_dir.exists():
        for candidate in (data_dir / "images" / "val2017", data_dir / "val2017"):
            if candidate.is_dir():
                img_dir = candidate
                break

    if not img_dir.exists():
        raise FileNotFoundError(f"Image directory not found under {data_dir}")

    # Load COCO
    print(f"Loading COCO annotations from {ann_file}")
    coco = COCO(str(ann_file))
    img_ids = sorted(coco.getImgIds())

    if args.max_images > 0:
        img_ids = img_ids[:args.max_images]

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

    # Get input shape from model
    input_shape = sess.get_inputs()[0].shape
    if input_shape[2] and input_shape[3]:
        model_input_size = (input_shape[2], input_shape[3])
    else:
        model_input_size = INPUT_SIZE
    print(f"Model input size: {model_input_size}")

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
        img_input, scale, pad = preprocess(img, model_input_size)

        # Run inference
        outputs = sess.run([out_name], {in_name: img_input})[0]

        # Postprocess
        detections = postprocess(outputs, scale, pad, (orig_h, orig_w),
                                 args.conf_thre, args.nms_thre)

        seen_ids.append(img_id)

        if detections is None:
            continue

        # Convert to COCO format
        for det in detections:
            x1, y1, x2, y2, score, cls_idx = det

            w = x2 - x1
            h = y2 - y1

            if w <= 0 or h <= 0:
                continue

            # Map class index to COCO category ID
            cat_id = COCO_CATEGORIES[int(cls_idx)]

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
