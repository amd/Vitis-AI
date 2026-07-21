#!/usr/bin/env python3
# Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.
"""Convert a YOLOv7 NMS float32 binary output to npy and draw detections."""

import argparse
from pathlib import Path
from typing import List

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from prepare_input import preprocess


COCO_CLASSES = (
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
    "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
    "hair drier", "toothbrush",
)


def normalize_predictions(output: np.ndarray) -> np.ndarray:
    predictions = np.asarray(output, dtype=np.float32)
    if predictions.ndim == 3:
        predictions = predictions[0]
    if predictions.ndim != 2:
        raise ValueError(f"Expected 2D YOLOv7 predictions, got shape {predictions.shape}")
    if predictions.shape[0] in (84, 85) and predictions.shape[1] > predictions.shape[0]:
        predictions = predictions.T
    if predictions.shape[1] < 5:
        raise ValueError(f"Expected xywh + class scores, got shape {predictions.shape}")
    return predictions


def normalize_nms_output(output: np.ndarray) -> np.ndarray:
    detections = np.asarray(output, dtype=np.float32)
    if detections.ndim == 3:
        detections = detections[0]
    if detections.ndim != 2 or detections.shape[1] != 7:
        raise ValueError(f"Expected 2D YOLOv7 NMS output with 7 columns, got shape {detections.shape}")
    return detections


def looks_like_nms_output(output: np.ndarray) -> bool:
    detections = np.asarray(output)
    if detections.ndim == 3:
        detections = detections[0]
    if detections.ndim != 2 or detections.shape[1] != 7:
        return False
    if detections.shape[0] == 0:
        return True
    batch_ids = detections[:, 0]
    class_ids = detections[:, 5]
    scores = detections[:, 6]
    # In the NMS-exported format, batch and class columns are effectively
    # integer-valued and the score column stays in the [0, 1] range.
    return (
        np.allclose(batch_ids, np.round(batch_ids), atol=1e-3)
        and np.allclose(class_ids, np.round(class_ids), atol=1e-3)
        and np.nanmin(scores) >= 0.0
        and np.nanmax(scores) <= 1.0
    )


def xywh_to_xyxy(boxes: np.ndarray) -> np.ndarray:
    converted = np.empty_like(boxes)
    converted[:, 0] = boxes[:, 0] - boxes[:, 2] / 2
    converted[:, 1] = boxes[:, 1] - boxes[:, 3] / 2
    converted[:, 2] = boxes[:, 0] + boxes[:, 2] / 2
    converted[:, 3] = boxes[:, 1] + boxes[:, 3] / 2
    return converted


def nms(boxes: np.ndarray, scores: np.ndarray, threshold: float) -> List[int]:
    if len(boxes) == 0:
        return []
    x1, y1, x2, y2 = boxes.T
    areas = np.maximum(0.0, x2 - x1) * np.maximum(0.0, y2 - y1)
    order = scores.argsort()[::-1]
    keep: List[int] = []
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
        inter_w = np.maximum(0.0, xx2 - xx1)
        inter_h = np.maximum(0.0, yy2 - yy1)
        intersection = inter_w * inter_h
        union = areas[current] + areas[rest] - intersection
        iou = intersection / np.maximum(union, 1e-6)
        order = rest[iou <= threshold]
    return keep


def project_boxes_to_image(
    boxes: np.ndarray,
    scale: float,
    padding: tuple[int, int],
    image_size: tuple[int, int],
) -> np.ndarray:
    # Undo the letterbox transform applied during preprocessing before clipping
    # boxes to the source image bounds.
    boxes = boxes.copy()
    pad_x, pad_y = padding
    boxes[:, [0, 2]] -= pad_x
    boxes[:, [1, 3]] -= pad_y
    boxes /= scale
    width, height = image_size
    boxes[:, [0, 2]] = boxes[:, [0, 2]].clip(0, width - 1)
    boxes[:, [1, 3]] = boxes[:, [1, 3]].clip(0, height - 1)
    x_min = np.minimum(boxes[:, 0], boxes[:, 2])
    y_min = np.minimum(boxes[:, 1], boxes[:, 3])
    x_max = np.maximum(boxes[:, 0], boxes[:, 2])
    y_max = np.maximum(boxes[:, 1], boxes[:, 3])
    return np.stack((x_min, y_min, x_max, y_max), axis=1)


def postprocess_raw_predictions(
    output: np.ndarray,
    scale: float,
    padding: tuple[int, int],
    image_size: tuple[int, int],
    score_thr: float,
    nms_thr: float,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    # Raw head outputs still contain per-class scores for every candidate, so
    # this path applies score filtering and class-wise NMS in Python.
    predictions = normalize_predictions(output)
    boxes = project_boxes_to_image(xywh_to_xyxy(predictions[:, :4]), scale, padding, image_size)
    class_scores = predictions[:, 4:]
    class_ids = class_scores.argmax(axis=1)
    scores = class_scores[np.arange(class_scores.shape[0]), class_ids]
    valid = scores >= score_thr
    boxes, scores, class_ids = boxes[valid], scores[valid], class_ids[valid]
    non_empty = (boxes[:, 2] > boxes[:, 0]) & (boxes[:, 3] > boxes[:, 1])
    boxes, scores, class_ids = boxes[non_empty], scores[non_empty], class_ids[non_empty]
    kept_indices: List[int] = []
    for class_id in np.unique(class_ids):
        indices = np.where(class_ids == class_id)[0]
        keep = nms(boxes[indices], scores[indices], nms_thr)
        kept_indices.extend(indices[keep].tolist())
    kept_indices = sorted(kept_indices, key=lambda idx: float(scores[idx]), reverse=True)
    return boxes[kept_indices], scores[kept_indices], class_ids[kept_indices]


def postprocess_nms_output(
    output: np.ndarray,
    scale: float,
    padding: tuple[int, int],
    image_size: tuple[int, int],
    score_thr: float,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    # NMS-formatted outputs are already reduced to final detection candidates,
    # so only thresholding, box cleanup, and sorting are needed here.
    detections = normalize_nms_output(output)
    boxes = project_boxes_to_image(detections[:, 1:5], scale, padding, image_size)
    class_ids = detections[:, 5].astype(np.int64)
    scores = detections[:, 6]
    valid = scores >= score_thr
    boxes, scores, class_ids = boxes[valid], scores[valid], class_ids[valid]
    non_empty = (boxes[:, 2] > boxes[:, 0]) & (boxes[:, 3] > boxes[:, 1])
    boxes, scores, class_ids = boxes[non_empty], scores[non_empty], class_ids[non_empty]
    order = scores.argsort()[::-1]
    return boxes[order], scores[order], class_ids[order]


def postprocess(
    output: np.ndarray,
    scale: float,
    padding: tuple[int, int],
    image_size: tuple[int, int],
    score_thr: float,
    nms_thr: float,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    # Support both raw head outputs and already-NMS-filtered outputs so the same
    # tool can be used across export variants.
    if looks_like_nms_output(output):
        return postprocess_nms_output(output, scale, padding, image_size, score_thr)
    return postprocess_raw_predictions(output, scale, padding, image_size, score_thr, nms_thr)


def draw_detections(
    image: Image.Image,
    boxes: np.ndarray,
    scores: np.ndarray,
    class_ids: np.ndarray,
) -> Image.Image:
    output = image.convert("RGB")
    draw = ImageDraw.Draw(output)
    font = ImageFont.load_default()
    for box, score, class_id in zip(boxes, scores, class_ids):
        x1, y1, x2, y2 = [int(round(value)) for value in box]
        x1, x2 = sorted((x1, x2))
        y1, y2 = sorted((y1, y2))
        if x2 <= x1 or y2 <= y1:
            continue
        color = tuple(int(value) for value in np.random.default_rng(int(class_id)).integers(64, 256, size=3))
        label = COCO_CLASSES[int(class_id)] if int(class_id) < len(COCO_CLASSES) else f"class_{int(class_id)}"
        text = f"{label} {score:.2f}"
        draw.rectangle((x1, y1, x2, y2), outline=color, width=3)
        text_box = draw.textbbox((x1, y1), text, font=font)
        text_width = text_box[2] - text_box[0]
        text_height = text_box[3] - text_box[1]
        text_y = max(0, y1 - text_height - 4)
        draw.rectangle((x1, text_y, x1 + text_width + 4, text_y + text_height + 4), fill=color)
        draw.text((x1 + 2, text_y + 2), text, fill=(0, 0, 0), font=font)
    return output


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--bin",
        type=Path,
        default=Path("infer_out0-float32_4096x7_output.bin"),
        help="Input float32 binary output with 7 columns.",
    )
    parser.add_argument(
        "--image",
        type=Path,
        default=Path("coco_indoor_hotel_room_000000029596.jpg"),
        help="Original image used for inference.",
    )
    parser.add_argument(
        "--npy",
        type=Path,
        default=None,
        help="Output npy path. Defaults to input binary name with .npy suffix.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("output_bin_yolov7_detections.jpg"),
        help="Annotated output image path.",
    )
    parser.add_argument("--img-size", type=int, default=640, help="Letterboxed model input size.")
    parser.add_argument("--score-thr", type=float, default=0.25, help="Minimum score threshold.")
    parser.add_argument("--nms-thr", type=float, default=0.45, help="NMS IoU threshold.")
    parser.add_argument(
        "--top-left",
        action="store_true",
        help="Use top-left letterbox placement instead of centered padding.",
    )
    return parser.parse_args()


def bin_to_npy(bin_path: Path, npy_path: Path) -> np.ndarray:
    """Read float32 NMS output from .bin, reshape it, and save it as .npy."""
    raw = np.fromfile(bin_path, dtype=np.float32)
    if raw.size % 7 != 0:
        raise ValueError(f"{bin_path} has {raw.size} float32 values, not divisible by 7")
    output = raw.reshape(-1, 7)
    np.save(npy_path, output)
    return output


def main() -> None:
    args = parse_args()
    npy_path = args.npy or args.bin.with_suffix(".npy")

    # Saving an intermediate .npy makes it easier to inspect the same tensor in
    # numpy-based debugging workflows without re-reading the raw binary file.
    bin_to_npy(args.bin, npy_path)
    output = np.load(npy_path)

    image = Image.open(args.image)
    _, scale, padding = preprocess(
        image,
        args.img_size,
        input_format="rgb",
        normalize=True,
        center=not args.top_left,
    )
    boxes, scores, class_ids = postprocess(
        output,
        scale,
        padding,
        image.size,
        args.score_thr,
        args.nms_thr,
    )

    annotated = draw_detections(image, boxes, scores, class_ids)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    annotated.save(args.output)

    print(f"Wrote {npy_path} with shape {output.shape}")
    print(f"Wrote {args.output} with {len(boxes)} detections")
    for box, score, class_id in zip(boxes, scores, class_ids):
        label = COCO_CLASSES[int(class_id)] if int(class_id) < len(COCO_CLASSES) else f"class_{int(class_id)}"
        print(f"{label}: score={float(score):.4f}, box={box.round(1).tolist()}")


if __name__ == "__main__":
    main()
