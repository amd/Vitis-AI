#!/bin/bash

# ===========================================================
# Copyright © 2026 Advanced Micro Devices, Inc. All rights reserved.
# MIT License
# ===========================================================

import json
from pathlib import Path
import sys
import cv2
import os
import subprocess
import numpy as np
import torch
import onnxruntime as ort
from pycocotools.coco import COCO
from pycocotools.cocoeval import COCOeval
from tqdm import tqdm
from ultralytics.utils.nms import non_max_suppression

PROJECT_DIR = Path(__file__).parent

# Default mapping from YOLOv8 class IDs to COCO class names
id_to_cls_map_default = {'0': 'person', '1': 'bicycle', '2': 'car', '3': 'motorcycle', '4': 'airplane', '5': 'bus', '6': 'train', '7': 'truck', '8': 'boat', '9': 'traffic light',
                '10': 'fire hydrant', '11': 'stop sign', '12': 'parking meter', '13': 'bench', '14': 'bird', '15': 'cat', '16': 'dog', '17': 'horse', '18': 'sheep', '19': 'cow',
                '20': 'elephant', '21': 'bear', '22': 'zebra', '23': 'giraffe', '24': 'backpack', '25': 'umbrella', '26': 'handbag', '27': 'tie', '28': 'suitcase', '29': 'frisbee',
                '30': 'skis', '31': 'snowboard', '32': 'sports ball', '33': 'kite', '34': 'baseball bat', '35': 'baseball glove', '36': 'skateboard', '37': 'surfboard', '38': 'tennis racket', '39': 'bottle',
                '40': 'wine glass', '41': 'cup', '42': 'fork', '43': 'knife', '44': 'spoon', '45': 'bowl', '46': 'banana', '47': 'apple', '48': 'sandwich', '49': 'orange',
                '50': 'broccoli', '51': 'carrot', '52': 'hot dog', '53': 'pizza', '54': 'donut', '55': 'cake', '56': 'chair', '57': 'couch', '58': 'potted plant', '59': 'bed',
                '60': 'dining table', '61': 'toilet', '62': 'tv', '63': 'laptop', '64': 'mouse', '65': 'remote', '66': 'keyboard', '67': 'cell phone', '68': 'microwave', '69': 'oven',
                '70': 'toaster', '71': 'sink', '72': 'refrigerator', '73': 'book', '74': 'clock', '75': 'vase', '76': 'scissors', '77': 'teddy bear', '78': 'hair drier', '79': 'toothbrush'}


def load_onnx_model(session, device: str):   
    input_name = session.get_inputs()[0].name
    custom_meta_map: dict = session.get_modelmeta().custom_metadata_map
    id_to_cls_json_str = custom_meta_map.get("id_to_cls", None)
    if id_to_cls_json_str is not None:
        id_to_cls_map = json.loads(id_to_cls_json_str)
    else:
        id_to_cls_map = id_to_cls_map_default

    assert id_to_cls_map is not None

    return session, input_name, id_to_cls_map


def load_coco_dataset(annotations_path):
    coco = COCO(annotations_path)
    img_ids = coco.getImgIds()

    cats = coco.loadCats(coco.getCatIds())
    id_to_name = {cat["id"]: cat["name"] for cat in cats}

    return coco, img_ids, id_to_name


def preprocess_image(img: np.ndarray, input_size, bgr2rgb=False):
    img_height, img_width = img.shape[:2]
    scale = min(input_size[0] / img_width, input_size[1] / img_height)
    new_size = int(img_width * scale), int(img_height * scale)
    img_resized = cv2.resize(img, new_size)

    top = (input_size[1] - new_size[1]) // 2
    bottom = (input_size[1] - new_size[1]) - top
    left = (input_size[0] - new_size[0]) // 2
    right = (input_size[0] - new_size[0]) - left

    img_resized = cv2.copyMakeBorder(
        img_resized,
        top,
        bottom,
        left,
        right,
        borderType=cv2.BORDER_CONSTANT,
        value=(0, 0, 0),
    )

    # cv2.imwrite("runs/resized_and_padded.png", img_resized)

    if bgr2rgb:
        img_resized = cv2.cvtColor(img_resized, cv2.COLOR_BGR2RGB)

    img_resized = np.float32(img_resized) / 255.0
    img_resized = img_resized.transpose(2, 0, 1)  # hwc --> chw
    img_resized = np.expand_dims(img_resized, axis=0)  # chw --> 1chw

    # print(f"pad top {top}, left {left}, scale {scale}")

    return img_resized, (top, left), scale


def postprocess_output_model_space(
    output: np.ndarray,
    min_score_thres: float,
    nms_iou_thres: float,
):
    """
    Postprocess YOLOv8 output using Ultralytics non_max_suppression.
    Returns predictions in MODEL INPUT SPACE (640x640)

    Args:
        output: Model output, shape (num-cls + 4, num-boxes)
        min_score_thres: Confidence threshold
        nms_iou_thres: NMS IoU threshold

    Returns:
        Tensor of detections in model input space (640x640), shape (N, 6)
        where columns are [x1, y1, x2, y2, conf, class]
    """
    # Convert output to torch tensor for Ultralytics NMS
    predictions_torch = torch.from_numpy(output)

    # Add batch dimension if needed
    if predictions_torch.dim() == 2:
        predictions_torch = predictions_torch.unsqueeze(0)  # (84, 8400) -> (1, 84, 8400)

    # Apply Ultralytics NMS 
    detections = non_max_suppression(
        predictions_torch,
        conf_thres=min_score_thres,
        iou_thres=nms_iou_thres,
        nc=0,  # For detect task
        multi_label=True,  # Multi-label NMS
        agnostic=False,    # Per-class NMS
        max_det=300        # COCO standard
    )

    # Return predictions in model input space (640x640)
    return detections[0]  # Single image (batch size = 1)


def postprocess_output(
    output: np.ndarray,
    pad_top_left: tuple,
    scale: float,
    yolo_id_to_coco_id_map: dict,
    min_score_thres: float,
    nms_iou_thres: float,
    img_width: int,
    img_height: int,
):
    """
    Postprocess YOLOv8 output using Ultralytics non_max_suppression.
    Returns detections in ORIGINAL IMAGE SPACE for COCO evaluation.

    Args:
        output: Model output, shape (num-cls + 4, num-boxes)
        pad_top_left: Letterbox padding (top, left)
        scale: Letterbox scale factor
        yolo_id_to_coco_id_map: Mapping from YOLO class IDs to COCO IDs
        min_score_thres: Confidence threshold
        nms_iou_thres: NMS IoU threshold
        img_width: Original image width
        img_height: Original image height

    Returns:
        List of detections in COCO format (original image space)
    """
    # Convert output to torch tensor for Ultralytics NMS
    # Output shape from model: (84, 8400) for single image
    # Need to add batch dimension: (1, 84, 8400)
    # non_max_suppression expects (batch, 84, 8400) - DO NOT TRANSPOSE
    predictions_torch = torch.from_numpy(output)

    # Add batch dimension if needed
    if predictions_torch.dim() == 2:
        predictions_torch = predictions_torch.unsqueeze(0)  # (84, 8400) -> (1, 84, 8400)

    # Apply Ultralytics NMS 
    # IMPORTANT: non_max_suppression expects (batch, 84, 8400) format, NOT transposed!
    # multi_label=True allows boxes to have multiple class labels
    # agnostic=False applies NMS per-class (not across all classes)
    # max_det=300 is COCO evaluation standard
    detections = non_max_suppression(
        predictions_torch,
        conf_thres=min_score_thres,
        iou_thres=nms_iou_thres,
        nc=0,  # For detect task (auto-inferred from prediction shape)
        multi_label=True,  # KEY: Multi-label NMS for better accuracy
        agnostic=False,    # KEY: Per-class NMS
        max_det=300        # COCO standard
    )

    # Process detections - detections is a list with one element per batch
    pred = detections[0]  # Single image (batch size = 1)

    if pred is None or len(pred) == 0:
        return []

    # pred shape: (num_detections, 6) where columns are [x1, y1, x2, y2, conf, class]
    # Boxes are in model input space (640x640), need to restore to original image space

    coco_detections = []

    for detection in pred:
        x1, y1, x2, y2, conf, cls = detection.tolist()

        # Convert from xyxy to cxcywh (center format)
        cx = (x1 + x2) / 2.0
        cy = (y1 + y2) / 2.0
        w = x2 - x1
        h = y2 - y1

        # Restore boxes from model input space to original image space
        # 1. Remove letterbox padding
        cx_unpadded = cx - pad_top_left[1]  # minus pad left
        cy_unpadded = cy - pad_top_left[0]  # minus pad top

        # 2. Scale back to original image size
        cx_orig = cx_unpadded / scale
        cy_orig = cy_unpadded / scale
        w_orig = w / scale
        h_orig = h / scale

        # 3. Convert back to corner format (x, y, w, h) for COCO
        x_orig = cx_orig - w_orig / 2.0
        y_orig = cy_orig - h_orig / 2.0

        # Clamp to image boundaries
        x_orig = max(0, min(x_orig, img_width))
        y_orig = max(0, min(y_orig, img_height))
        w_orig = min(w_orig, img_width - x_orig)
        h_orig = min(h_orig, img_height - y_orig)

        # Skip invalid boxes
        if w_orig <= 0 or h_orig <= 0:
            continue

        # Convert YOLO class ID to COCO class ID
        coco_class_id = yolo_id_to_coco_id_map[int(cls)]

        coco_detections.append({
            "category_id": coco_class_id,
            "bbox": (
                round(float(x_orig), 4),
                round(float(y_orig), 4),
                round(float(w_orig), 4),
                round(float(h_orig), 4)
            ),
            "score": round(float(conf), 4),
        })

    # Sort by confidence (highest first) and limit to top 300
    coco_detections = sorted(coco_detections, key=lambda d: d["score"], reverse=True)
    if len(coco_detections) > 300:
        coco_detections = coco_detections[:300]

    return coco_detections


def draw_detections(
    canvas: np.ndarray,
    img_detections: list,
    id_to_name: dict = None,
    save_path: Path = None,
):
    for pred in img_detections:
        score = pred["score"]
        if score < 0.25:
            continue
        cls_id = pred["category_id"]
        x, y, w, h = np.asarray(pred["bbox"], int)
        cv2.rectangle(canvas, (x, y), (x + w, y + h), (0, 255, 0), thickness=1)

        if id_to_name is not None:
            text = f"{id_to_name[cls_id]}: {score * 100:.1f}%"
        else:
            text = f"{cls_id}: {score * 100:.1f}%"

        cv2.putText(
            canvas,
            text=text,
            org=(x, max(y - 10, 0)),
            fontFace=cv2.FONT_HERSHEY_SIMPLEX,
            fontScale=0.75,
            color=(255, 0, 255),
            thickness=1,
            lineType=cv2.LINE_AA,
        )

    save_path = str(save_path or "runs/debug_postprocess.png")
    cv2.imwrite(save_path, canvas)


def evaluate_model_ultralytics(
    session: ort.InferenceSession,
    input_name: str,
    coco: COCO,
    images_folder: Path,
    img_ids: list,
    yolo_id_to_cls_map: dict,
    coco_id_to_cls_map: dict,
    input_size=(640, 640),
    min_score_thres=0.001,
    nms_iou_thresh=0.6,
    num_max_images=None,
):
    """
    Evaluate model using Ultralytics metrics 
    Predictions and labels stay in model input space (640x640).

    Returns:
        stats: List of (correct, conf, pcls, tcls) for ap_per_class
        num_classes: Number of classes
    """
    from ultralytics.utils import ops
    from ultralytics.utils.metrics import box_iou

    images_folder = Path(images_folder)
    assert images_folder.is_dir()

    if num_max_images is not None:
        img_ids = img_ids[:num_max_images]

    # Create mapping from COCO class ID to YOLO class ID
    # yolo_id_to_cls_map: {'0': 'person', '1': 'bicycle', ...}
    # coco_id_to_cls_map: {1: 'person', 2: 'bicycle', ...}
    # We need: {1: 0, 2: 1, ...} (COCO ID -> YOLO ID)
    coco_id_to_yolo_id = {}
    for yolo_id_str, class_name in yolo_id_to_cls_map.items():
        # Find the COCO ID for this class name
        for coco_id, coco_class_name in coco_id_to_cls_map.items():
            if coco_class_name == class_name:
                coco_id_to_yolo_id[coco_id] = int(yolo_id_str)
                break

    stats = []
    iouv = torch.linspace(0.5, 0.95, 10)  # IoU vector for mAP@0.5:0.95
    imgsz = input_size[0]  # Assuming square input

    for img_id in tqdm(img_ids):
        img_info = coco.loadImgs(img_id)[0]
        img_path = images_folder / img_info["file_name"]
        img: np.ndarray = cv2.imread(str(img_path), cv2.IMREAD_COLOR)
        img_height, img_width = img.shape[:2]

        # Preprocess image
        img_resized, pad_top_left, scale = preprocess_image(
            img, input_size, bgr2rgb=True
        )

        # Run inference
        outputs = session.run(output_names=None, input_feed={input_name: img_resized})
        outputs = outputs[0]

        # Postprocess in model space (no coordinate transformation)
        pred = postprocess_output_model_space(
            outputs[0],
            min_score_thres,
            nms_iou_thresh,
        )

        # Get ground truth labels for this image
        ann_ids = coco.getAnnIds(imgIds=img_id)
        anns = coco.loadAnns(ann_ids)

        # Convert COCO annotations to model input space
        labels_list = []
        for ann in anns:
            if ann.get('iscrowd', 0):
                continue

            # Get COCO bbox in original image space (x, y, w, h)
            x, y, w, h = ann['bbox']
            coco_class_id = ann['category_id']

            # Convert COCO class ID to YOLO class ID
            if coco_class_id not in coco_id_to_yolo_id:
                continue
            yolo_class_id = coco_id_to_yolo_id[coco_class_id]

            # Transform bbox from original image space to model input space
            # 1. Scale to resized (before padding)
            x_scaled = x * scale
            y_scaled = y * scale
            w_scaled = w * scale
            h_scaled = h * scale

            # 2. Add padding offset
            x_model = x_scaled + pad_top_left[1]  # add left padding
            y_model = y_scaled + pad_top_left[0]  # add top padding

            # 3. Convert to normalized xywh format (normalized by model input size)
            cx_norm = (x_model + w_scaled / 2) / imgsz
            cy_norm = (y_model + h_scaled / 2) / imgsz
            w_norm = w_scaled / imgsz
            h_norm = h_scaled / imgsz

            labels_list.append([yolo_class_id, cx_norm, cy_norm, w_norm, h_norm])

        # Process stats for this image
        if len(labels_list) > 0:
            labels = torch.tensor(labels_list, dtype=torch.float32)
            nl = labels.shape[0]

            # Convert labels from normalized xywh to model input space xyxy
            tbox = ops.xywh2xyxy(labels[:, 1:5])
            tbox = tbox * torch.tensor([imgsz, imgsz, imgsz, imgsz], dtype=tbox.dtype)
            labelsn = torch.cat((labels[:, 0:1], tbox), 1)

            if pred is None or len(pred) == 0:
                # No predictions but have ground truth
                stats.append((
                    torch.zeros(0, iouv.numel(), dtype=torch.bool),
                    torch.Tensor(),
                    torch.Tensor(),
                    labels[:, 0]
                ))
            else:
                # Compare predictions and labels (both in model input space)
                correct = _process_batch_ultralytics(pred, labelsn, iouv)
                stats.append((correct, pred[:, 4], pred[:, 5], labels[:, 0]))

    return stats, len(yolo_id_to_cls_map)


def _process_batch_ultralytics(detections, labels, iouv):
    from ultralytics.utils.metrics import box_iou

    iou = box_iou(labels[:, 1:], detections[:, :4])
    correct = np.zeros((detections.shape[0], iouv.shape[0])).astype(bool)
    correct_class = labels[:, 0:1] == detections[:, 5]
    for i in range(len(iouv)):
        x = torch.where((iou >= iouv[i]) & correct_class)
        if x[0].shape[0]:
            matches = torch.cat((torch.stack(x, 1), iou[x[0], x[1]][:, None]), 1).cpu().numpy()
            if x[0].shape[0] > 1:
                matches = matches[matches[:, 2].argsort()[::-1]]
                matches = matches[np.unique(matches[:, 1], return_index=True)[1]]
                matches = matches[np.unique(matches[:, 0], return_index=True)[1]]
            correct[matches[:, 1].astype(int), i] = True
    return torch.tensor(correct, dtype=torch.bool, device=detections.device)


def evaluate_model(
    session: ort.InferenceSession,
    input_name: str,
    coco: COCO,
    images_folder: Path,
    img_ids: list,
    yolo_id_to_coco_id_map: dict,
    coco_id_to_cls_map: dict,
    input_size=(640, 640),
    min_score_thres=0.001,
    nms_iou_thresh=0.5,
    num_max_images=None,
    output_root: Path = None,
):
    images_folder = Path(images_folder)
    assert images_folder.is_dir()

    if num_max_images is not None:
        img_ids = img_ids[:num_max_images]

    detections = []
    demo_saved = False
    for img_id in tqdm(img_ids):
        img_info = coco.loadImgs(img_id)[0]
        img_path = images_folder / img_info["file_name"]
        img: np.ndarray = cv2.imread(str(img_path), cv2.IMREAD_COLOR)
        img_height, img_width = img.shape[:2]

        img_resized, pad_top_left, scale = preprocess_image(
            img, input_size, bgr2rgb=True
        )

        # outputs shape: (bs=1, xyxy + num-cls, num-boxes)
        # outputs = session.run(
        #     output_names=["bbox_output", "cls_output"],
        #     input_feed={input_name: img_resized},
        # )
        # outputs = np.concat(outputs, axis=1)
        outputs = session.run(output_names=None, input_feed={input_name: img_resized})
        outputs = outputs[0]

        img_detections = postprocess_output(
            outputs[0],
            pad_top_left,
            scale,
            yolo_id_to_coco_id_map,
            min_score_thres,
            nms_iou_thresh,
            img_width,
            img_height,
        )

        if not demo_saved:
            save_path = None
            if output_root is not None:
                output_root = Path(output_root)
                output_root.mkdir(parents=True, exist_ok=True)
                save_path = output_root / f"predict_of_{img_id}.png"

            draw_detections(img.copy(), img_detections, coco_id_to_cls_map, save_path)
            demo_saved = True

        for det in img_detections:
            det["image_id"] = img_id

        detections.extend(img_detections)

    return detections


def save_detections(detections, output_path="detections.json"):
    # Sort detections by image_id first and then by category_id
    sorted_detections = sorted(detections, key=lambda x: (x['image_id'], x['category_id']))
    with open(output_path, "w") as f:
        json.dump(sorted_detections, f, indent=2)


def save_coco_eval_results(
    coco_eval: COCOeval, save_path: str = "coco_eval_results.json"
):
    # Extract summary metrics (12 values: mAP, AR, etc.)
    summary = {
        "mAP": coco_eval.stats[0],
        "mAP50": coco_eval.stats[1],
        "mAP75": coco_eval.stats[2],
        "mAP_small": coco_eval.stats[3],
        "mAP_medium": coco_eval.stats[4],
        "mAP_large": coco_eval.stats[5],
        "AR@1": coco_eval.stats[6],
        "AR@10": coco_eval.stats[7],
        "AR@100": coco_eval.stats[8],
        "AR_small": coco_eval.stats[9],
        "AR_medium": coco_eval.stats[10],
        "AR_large": coco_eval.stats[11],
    }

    with open(save_path, "w") as f:
        json.dump(summary, f, indent=2)

    print(f"COCO evaluation results saved to: {save_path}")


def evaluate_ultralytics(
    stats: list,
    names: dict,
    save_path: str = None
):
    """
    Args:
        stats: List of (correct, conf, pcls, tcls) tuples from each image
        names: Class names dict
        save_path: Optional path to save results

    Returns:
        dict with Precision, Recall, mAP50, mAP50-95
    """
    from ultralytics.utils.metrics import ap_per_class
    from pathlib import Path

    if not stats:
        return None

    stats = [torch.cat(x, 0).cpu().numpy() for x in zip(*stats)]
    if len(stats) and stats[0].any():
        # Handle both old (7 values) and new (12 values) ap_per_class return formats
        results = ap_per_class(
            *stats,
            plot=False,
            save_dir=Path('.'),
            names=names
        )
        # Extract first 7 values which are consistent across versions
        tp, fp, p, r, f1, ap, ap_class = results[:7]
        # ap shape: (num_classes, 10) where 10 IoU thresholds from 0.5 to 0.95
        # ap[:, 0] is mAP@0.5
        # ap[:, 5] is mAP@0.75 (0.5 + 5*0.05 = 0.75)
        ap50 = ap[:, 0]      # IoU=0.50
        ap75 = ap[:, 5]      # IoU=0.75
        ap_mean = ap.mean(1) # Mean across all IoU thresholds

        mp, mr = p.mean(), r.mean()
        map50 = ap50.mean()
        map75 = ap75.mean()
        map = ap_mean.mean()

        metrics = {
            'Precision': float(mp),
            'Recall': float(mr),
            'mAP50': float(map50),
            'mAP75': float(map75),
            'mAP50-95': float(map)
        }

        if save_path:
            import json
            with open(save_path, 'w') as f:
                json.dump(metrics, f, indent=2)

        return metrics

    return None


def evaluate_coco(coco_gt: COCO, detections_path: str, results_save_path: str):
    coco_dt = coco_gt.loadRes(str(detections_path))
    coco_eval = COCOeval(coco_gt, coco_dt, "bbox")

    coco_eval.evaluate()
    coco_eval.accumulate()

    # Print overall evaluation summary
    coco_eval.summarize()

    # Extract per-category AP (IoU=0.5:0.95, area=all)
    cat_ids = coco_gt.getCatIds()
    cat_id_to_name = {cat["id"]: cat["name"] for cat in coco_gt.loadCats(cat_ids)}

    # precision shape: [IoU thresholds, Recall thresholds, Categories, Area range, MaxDets]
    prec = coco_eval.eval["precision"]

    print("\nPer-category AP (IoU=0.5:0.95, area=all):")
    for idx, cat_id in enumerate(cat_ids):
        # Select metrics: IoU=0.5:0.95, area=all (index 0), maxDet=100 (index 2)
        precision = prec[:, :, idx, 0, 2]
        precision = precision[precision > -1]
        ap = np.mean(precision) if precision.size else float("nan")
        print(f"{cat_id_to_name[cat_id]:<20} AP: {ap:.3f}")

    mAP = coco_eval.stats[0] * 100
    mAP50 = coco_eval.stats[1] * 100
    mAP75 = coco_eval.stats[2] * 100

    # print("\nMain COCO Metrics:")
    # print(f"mAP     (AP@[IoU=0.50:0.95]): {mAP:.1f}")
    # print(f"mAP50   (AP@IoU=0.50)       : {mAP50:.1f}")
    # print(f"mAP75   (AP@IoU=0.75)       : {mAP75:.1f}")

    save_coco_eval_results(coco_eval, results_save_path)
    return mAP, mAP50, mAP75


def calc_yolo_id_to_coco_map(yolo_id_to_cls_map: dict, coco_id_to_cls_map: dict):
    yolo_cls_names = sorted(yolo_id_to_cls_map.values())
    coco_cls_names = sorted(coco_id_to_cls_map.values())

    assert yolo_cls_names == coco_cls_names

    coco_cls_to_id_map = {v: k for k, v in coco_id_to_cls_map.items()}

    yolo_id_to_coco_id_map = {
        int(k): coco_cls_to_id_map[v] for k, v in yolo_id_to_cls_map.items()
    }

    return yolo_id_to_coco_id_map


def evaluate_on_coco(onnx_model: str, session, coco_dataset: str, device: str = "cpu"):
    onnx_model_path = Path(onnx_model)
    print(f"Evaluating model: {onnx_model_path}")
    coco_dataset_path = Path(coco_dataset)
    anno_file_path = coco_dataset_path / "annotations/instances_val2017.json"
    coco, img_ids, coco_id_to_cls_map = load_coco_dataset(anno_file_path)

    session, input_name, yolo_id_to_cls_map = load_onnx_model(session, device)

    yolo_id_to_coco_id_map = calc_yolo_id_to_coco_map(
        yolo_id_to_cls_map, coco_id_to_cls_map
    )

    coco_val2017_images_folder = coco_dataset_path / "images/val2017"

    nms_iou_thresh = 0.5

    output_root = (
        PROJECT_DIR
        / f"runs/onnx-predict/{onnx_model_path.stem}-{anno_file_path.stem}-iou={nms_iou_thresh:.2f}"
    )
    output_root.mkdir(exist_ok=True, parents=True)

    _, _, img_w, img_h = session.get_inputs()[0].shape

    detections = evaluate_model(
        session,
        input_name,
        coco,
        coco_val2017_images_folder,
        img_ids,
        yolo_id_to_coco_id_map,
        coco_id_to_cls_map,
        input_size=(img_w, img_h),
        min_score_thres=0.25,
        nms_iou_thresh=nms_iou_thresh,
        output_root=output_root,
        # num_max_images=100,
    )
    
    if not detections:
        print('Model did not generate any predictions. Unable to evaluate Model accuracy on COCO dataset')
        sys.exit(1)

    pred_json_save_path = output_root / "pred.json"
    save_detections(detections, pred_json_save_path)
    print(f"detections saved to: {pred_json_save_path}")

    coco_eval_save_path = output_root / "coco-metrics.json"
    mAP, mAP50, mAP75 = evaluate_coco(coco, pred_json_save_path, PROJECT_DIR / coco_eval_save_path)

    return mAP, mAP50, mAP75
