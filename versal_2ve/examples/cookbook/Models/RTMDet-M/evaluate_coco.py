#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

#!/usr/bin/env python3
"""Evaluate RTMDet-M (COCO) ONNX accuracy on a COCO val2017 subset.

The exported RTMDet-M graph is cut *before* the NMS tail, so it has two
outputs (order as returned by onnxruntime):

    /Concat_6_output_0  [1, 8400, 4]   xyxy boxes in the 640x640 input frame
    /Concat_3_output_0  [1, 8400, 80]  per-class *logits* (NOT sigmoided)

This script therefore reproduces the RTMDet test-time post-processing itself:
sigmoid on the class logits, per-class score thresholding, per-class NMS, box
rescaling back to the original image, then COCO bbox mAP via pycocotools.

Pre-processing matches mmdet's RTMDet test pipeline / DetDataPreprocessor:
    - keep-ratio resize so max side == img_size (default 640)
    - pad bottom-right to img_size x img_size with value 114
    - channel order BGR (config has bgr_to_rgb=False)
    - normalize (x - mean) / std with BGR mean=[103.53,116.28,123.675],
      std=[57.375,57.12,58.395]

Runs on CPUExecutionProvider (--target cpu, default) or VitisAIExecutionProvider
(--target npu), mirroring segformer/eval_segformer_cityscapes_citywise.py.

Usage:
    # CPU
    python evaluate_coco.py \
        --model rtmdet_m_8xb32-300e_coco_1x3x640x640_without_smaller_tail.onnx \
        --coco-dir /path/to/coco --max-images 100 --target cpu

    # NPU (VitisAIExecutionProvider)
    python evaluate_coco.py \
        --model rtmdet_m_8xb32-300e_coco_1x3x640x640_without_smaller_tail.onnx \
        --coco-dir /path/to/coco --max-images 100 --target npu \
        --vitis-config vitisai_config.json --cache-dir ./ \
        --cache_key rtmdet_m_8xb32-300e_coco_1x3x640x640_without_smaller_tail
"""

from __future__ import annotations

import subprocess
import sys


# Auto-install missing packages with --no-deps to avoid dependency conflicts
def install_package_no_deps(package_name: str, import_name: str = None) -> bool:
    """Install a package with --no-deps if import fails."""
    if import_name is None:
        import_name = package_name
    try:
        __import__(import_name)
        return True
    except ImportError:
        print(f"Package '{import_name}' not found. Installing '{package_name}' with --no-deps...")
        try:
            subprocess.check_call(
                [sys.executable, "-m", "pip", "install", "--no-deps", package_name],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            print(f"\u2713 Installed {package_name}")
            return True
        except subprocess.CalledProcessError:
            print(f"\u2717 Failed to install {package_name}")
            return False


install_package_no_deps("Pillow", "PIL")
install_package_no_deps("numpy", "numpy")
install_package_no_deps("pycocotools", "pycocotools")

import argparse
from pathlib import Path

import numpy as np
import onnxruntime as ort
from PIL import Image

try:
    from pycocotools.coco import COCO
    from pycocotools.cocoeval import COCOeval
except ImportError:
    print("Error: pycocotools required. Install: pip install pycocotools")
    sys.exit(1)


# RTMDet DetDataPreprocessor (BGR order, bgr_to_rgb=False)
MEAN_BGR = np.array([103.53, 116.28, 123.675], dtype=np.float32)
STD_BGR = np.array([57.375, 57.12, 58.395], dtype=np.float32)
PAD_VAL = 114


def preprocess(image: Image.Image, img_size: int) -> tuple[np.ndarray, float, float]:
    """Keep-ratio resize + bottom-right pad(114) + BGR normalize -> NCHW.

    Returns the [1,3,img_size,img_size] tensor and the per-axis resize scales
    (new/orig) used to map predicted boxes back to the original image.
    """
    rgb = image.convert("RGB")
    w0, h0 = rgb.size
    scale = min(img_size / h0, img_size / w0)
    new_w = int(round(w0 * scale))
    new_h = int(round(h0 * scale))
    resized = rgb.resize((new_w, new_h), Image.Resampling.BILINEAR)

    canvas = np.full((img_size, img_size, 3), PAD_VAL, dtype=np.uint8)  # RGB
    canvas[:new_h, :new_w] = np.asarray(resized)

    arr = canvas.astype(np.float32)
    arr = arr[:, :, ::-1]  # RGB -> BGR (bgr_to_rgb=False)
    arr = (arr - MEAN_BGR) / STD_BGR
    chw = np.ascontiguousarray(arr.transpose(2, 0, 1))[None]  # 1,3,H,W
    return chw, new_w / w0, new_h / h0


def sigmoid(x: np.ndarray) -> np.ndarray:
    return 1.0 / (1.0 + np.exp(-x))


def nms(boxes: np.ndarray, scores: np.ndarray, threshold: float) -> list[int]:
    """Single-class NMS on xyxy boxes."""
    if len(boxes) == 0:
        return []
    x1, y1, x2, y2 = boxes.T
    areas = np.maximum(0.0, x2 - x1) * np.maximum(0.0, y2 - y1)
    order = scores.argsort()[::-1]
    keep: list[int] = []
    while order.size > 0:
        current = int(order[0])
        keep.append(current)
        if order.size == 1:
            break
        rest = order[1:]
        xx1 = np.maximum(x1[current], x1[rest])
        yy1 = np.maximum(y1[current], y1[rest])
        xx2 = np.minimum(x2[current], x2[rest])
        yy2 = np.minimum(y2[current], y2[rest])
        inter = np.maximum(0.0, xx2 - xx1) * np.maximum(0.0, yy2 - yy1)
        union = areas[current] + areas[rest] - inter
        iou = inter / np.maximum(union, 1e-6)
        order = rest[iou <= threshold]
    return keep


def postprocess(
    boxes: np.ndarray,
    logits: np.ndarray,
    scale_x: float,
    scale_y: float,
    orig_w: int,
    orig_h: int,
    score_thr: float,
    nms_thr: float,
    max_per_img: int,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """RTMDet post-processing: sigmoid -> per-class threshold+NMS -> rescale."""
    boxes = boxes.astype(np.float32).copy()
    scores_all = sigmoid(logits.astype(np.float32))  # (8400, 80)

    # rescale boxes from 640 frame back to original image, then clip
    boxes[:, [0, 2]] /= scale_x
    boxes[:, [1, 3]] /= scale_y
    boxes[:, [0, 2]] = boxes[:, [0, 2]].clip(0, orig_w)
    boxes[:, [1, 3]] = boxes[:, [1, 3]].clip(0, orig_h)

    box_idx, cls_idx = np.where(scores_all >= score_thr)
    if box_idx.size == 0:
        return (np.empty((0, 4), np.float32),
                np.empty((0,), np.float32),
                np.empty((0,), np.int64))
    sc = scores_all[box_idx, cls_idx]
    bx = boxes[box_idx]

    kept: list[int] = []
    for c in np.unique(cls_idx):
        m = np.where(cls_idx == c)[0]
        keep = nms(bx[m], sc[m], nms_thr)
        kept.extend(m[keep].tolist())

    kept.sort(key=lambda i: -sc[i])
    kept = kept[:max_per_img]
    return bx[kept], sc[kept], cls_idx[kept].astype(np.int64)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--model", type=Path, required=True,
                        help="RTMDet-M ONNX (cut before NMS).")
    parser.add_argument("--coco-dir", type=Path, required=True,
                        help="COCO root (annotations/ + images/val2017 or val2017/).")
    parser.add_argument("--max-images", type=int, default=100,
                        help="Max val images (0 for all, default 100).")
    parser.add_argument("--img-size", type=int, default=640)
    parser.add_argument("--score-thr", type=float, default=0.001)
    parser.add_argument("--nms-thr", type=float, default=0.65)
    parser.add_argument("--max-per-img", type=int, default=300)
    parser.add_argument("--target", default="cpu", choices=["cpu", "npu"],
                        help="Execution target: 'cpu' (CPUExecutionProvider) or "
                             "'npu' (VitisAIExecutionProvider). Default: cpu.")
    parser.add_argument("--vitis-config", type=Path,
                        default=Path("vitisai_config.json"),
                        help="VitisAI config file (npu target only).")
    parser.add_argument("--cache-dir", type=Path, default=Path("./"),
                        help="VitisAI cache directory (npu target only).")
    parser.add_argument("--cache_key", type=str, default="rtmdet_eval",
                        help="VitisAI cache key (npu target only).")
    return parser.parse_args()


def build_session(args: argparse.Namespace) -> ort.InferenceSession:
    """Build an ONNXRuntime session for the requested target.

    'cpu' uses the CPUExecutionProvider (accuracy reference); 'npu' uses the
    VitisAIExecutionProvider, passing the VitisAI config / cache dir / cache key
    through provider_options (mirrors ../../Utils/compile.py).
    """
    if args.target == "npu":
        provider_options = {
            "config_file": str(args.vitis_config),
            "cache_dir": str(args.cache_dir),
            "cache_key": args.cache_key,
            "ai_analyzer_visualization": True,
            "ai_analyzer_profiling": True,
            "log_level": "info",
            "target": "VAIML",
        }
        return ort.InferenceSession(
            str(args.model),
            providers=["VitisAIExecutionProvider"],
            provider_options=[provider_options],
        )
    return ort.InferenceSession(str(args.model), providers=["CPUExecutionProvider"])


def resolve_output_order(session: ort.InferenceSession) -> tuple[int, int]:
    """Return (boxes_idx, scores_idx) from output shapes, robust to ordering."""
    boxes_idx = scores_idx = None
    for i, o in enumerate(session.get_outputs()):
        last = o.shape[-1] if o.shape else None
        if last == 4:
            boxes_idx = i
        elif last == 80:
            scores_idx = i
    if boxes_idx is None or scores_idx is None:
        # fall back to documented order: [boxes, scores]
        boxes_idx, scores_idx = 0, 1
    return boxes_idx, scores_idx


def main() -> None:
    """Run the end-to-end COCO bbox mAP evaluation.

    Loads the COCO val2017 ground truth, then for each image: preprocess ->
    session.run -> RTMDet post-processing (sigmoid, per-class threshold+NMS,
    rescale), collecting detections in COCO result format. Finally scores them
    with pycocotools COCOeval and prints mAP@[.5:.95] / @50 / @75.
    """
    args = parse_args()

    if not args.coco_dir.is_dir():
        raise FileNotFoundError(f"COCO directory not found: {args.coco_dir}")

    print("=" * 80)
    print("RTMDet-M COCO evaluation")
    print(f"Model    : {args.model}")
    print(f"COCO dir : {args.coco_dir.resolve()}")
    print(f"Max imgs : {args.max_images if args.max_images > 0 else 'All'}")
    print(f"Provider : {'NPU (VitisAIExecutionProvider)' if args.target == 'npu' else 'CPU'}")
    print("=" * 80)

    session = build_session(args)
    print("Providers:", session.get_providers())
    input_name = session.get_inputs()[0].name
    input_type = session.get_inputs()[0].type
    boxes_idx, scores_idx = resolve_output_order(session)

    ann_file = args.coco_dir / "annotations" / "instances_val2017.json"
    if not ann_file.exists():
        raise FileNotFoundError(f"COCO annotations not found: {ann_file}")
    coco_gt = COCO(str(ann_file))
    cat_ids = sorted(coco_gt.getCatIds())  # 80 contiguous -> real COCO ids

    val_images_dir = None
    for candidate in (args.coco_dir / "images" / "val2017", args.coco_dir / "val2017"):
        if candidate.is_dir():
            val_images_dir = candidate
            break
    if not val_images_dir:
        raise FileNotFoundError(f"COCO val images not found under {args.coco_dir}")

    image_ids = sorted(coco_gt.getImgIds())
    if args.max_images > 0:
        image_ids = image_ids[:args.max_images]
    print(f"Evaluating {len(image_ids)} images...")

    results = []
    for i, img_id in enumerate(image_ids):
        if (i + 1) % 50 == 0:
            print(f"Processed {i + 1}/{len(image_ids)} images")

        img_info = coco_gt.loadImgs(img_id)[0]
        image_path = val_images_dir / img_info["file_name"]
        if not image_path.exists():
            continue

        image = Image.open(image_path)
        orig_w, orig_h = image.convert("RGB").size
        input_tensor, scale_x, scale_y = preprocess(image, args.img_size)
        if "float16" in input_type:
            input_tensor = input_tensor.astype(np.float16)

        outputs = session.run(None, {input_name: input_tensor})
        boxes = np.asarray(outputs[boxes_idx])[0]
        logits = np.asarray(outputs[scores_idx])[0]

        boxes, scores, class_ids = postprocess(
            boxes, logits, scale_x, scale_y, orig_w, orig_h,
            args.score_thr, args.nms_thr, args.max_per_img,
        )

        # COCO wants xywh boxes and *real* category ids, so convert xyxy->xywh
        # and remap the 0..79 contiguous class index back to its COCO id.
        for box, score, cls in zip(boxes, scores, class_ids):
            x1, y1, x2, y2 = box
            results.append({
                "image_id": img_id,
                "category_id": cat_ids[int(cls)],
                "bbox": [float(x1), float(y1), float(x2 - x1), float(y2 - y1)],
                "score": float(score),
            })

    if not results:
        print("No detections found!")
        return

    coco_dt = coco_gt.loadRes(results)
    coco_eval = COCOeval(coco_gt, coco_dt, "bbox")
    coco_eval.params.imgIds = image_ids
    coco_eval.evaluate()
    coco_eval.accumulate()
    coco_eval.summarize()

    print("\n===== Evaluation Result =====")
    print(f"ONNX Model: {args.model}")
    print(f"Execution Provider: {'VitisAIExecutionProvider (NPU)' if args.target == 'npu' else 'CPUExecutionProvider'}")
    print(f"Images evaluated: {len(image_ids)}")
    print("-" * 60)
    print(f"mAP@[0.50:0.95] = {coco_eval.stats[0]:.5f}")
    print(f"mAP@0.50        = {coco_eval.stats[1]:.5f}")
    print(f"mAP@0.75        = {coco_eval.stats[2]:.5f}")
    print("===== End of Evaluation Result =====")


if __name__ == "__main__":
    main()
