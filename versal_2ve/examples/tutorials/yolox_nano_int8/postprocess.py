#!/usr/bin/env python3
"""
YOLOX Postprocessing Script
Takes model output and applies postprocessing logic to get final detections.
"""

import argparse
import numpy as np


def custom_nms(boxes, scores, nms_thr):
    """Custom implementation of single class NMS."""
    x1 = boxes[:, 0]
    y1 = boxes[:, 1]
    x2 = boxes[:, 2]
    y2 = boxes[:, 3]

    areas = (x2 - x1 + 1) * (y2 - y1 + 1)
    order = scores.argsort()[::-1]

    keep = []
    while order.size > 0:
        i = order[0]
        keep.append(i)
        xx1 = np.maximum(x1[i], x1[order[1:]])
        yy1 = np.maximum(y1[i], y1[order[1:]])
        xx2 = np.minimum(x2[i], x2[order[1:]])
        yy2 = np.minimum(y2[i], y2[order[1:]])

        w = np.maximum(0.0, xx2 - xx1 + 1)
        h = np.maximum(0.0, yy2 - yy1 + 1)
        inter = w * h
        ovr = inter / (areas[i] + areas[order[1:]] - inter)

        inds = np.where(ovr <= nms_thr)[0]
        order = order[inds + 1]

    return keep


def custom_multiclass_nms(boxes, scores, nms_thr, score_thr, class_agnostic=True):
    """Custom implementation of multiclass NMS."""
    if class_agnostic:
        cls_inds = scores.argmax(1)
        cls_scores = scores[np.arange(len(cls_inds)), cls_inds]

        valid_score_mask = cls_scores > score_thr
        if valid_score_mask.sum() == 0:
            return None
        
        valid_scores = cls_scores[valid_score_mask]
        valid_boxes = boxes[valid_score_mask]
        valid_cls_inds = cls_inds[valid_score_mask]
        keep = custom_nms(valid_boxes, valid_scores, nms_thr)
        
        if keep:
            dets = np.concatenate(
                [valid_boxes[keep], valid_scores[keep, None], valid_cls_inds[keep, None]], 1
            )
            return dets
        return None
    else:
        final_dets = []
        num_classes = scores.shape[1]
        for cls_ind in range(num_classes):
            cls_scores = scores[:, cls_ind]
            valid_score_mask = cls_scores > score_thr
            if valid_score_mask.sum() == 0:
                continue
            else:
                valid_scores = cls_scores[valid_score_mask]
                valid_boxes = boxes[valid_score_mask]
                keep = custom_nms(valid_boxes, valid_scores, nms_thr)
                if len(keep) > 0:
                    cls_inds = np.ones((len(keep), 1)) * cls_ind
                    dets = np.concatenate(
                        [valid_boxes[keep], valid_scores[keep, None], cls_inds], 1
                    )
                    final_dets.append(dets)
        if len(final_dets) == 0:
            return None
        return np.concatenate(final_dets, 0)


def custom_demo_postprocess(outputs, img_size, p6=False):
    """Custom implementation of YOLOX demo_postprocess."""
    grids = []
    expanded_strides = []
    strides = [8, 16, 32] if not p6 else [8, 16, 32, 64]

    hsizes = [img_size[0] // stride for stride in strides]
    wsizes = [img_size[1] // stride for stride in strides]

    for hsize, wsize, stride in zip(hsizes, wsizes, strides):
        xv, yv = np.meshgrid(np.arange(wsize), np.arange(hsize))
        grid = np.stack((xv, yv), 2).reshape(1, -1, 2)
        grids.append(grid)
        shape = grid.shape[:2]
        expanded_strides.append(np.full((*shape, 1), stride))

    grids = np.concatenate(grids, 1)
    expanded_strides = np.concatenate(expanded_strides, 1)
    outputs[..., :2] = (outputs[..., :2] + grids) * expanded_strides
    outputs[..., 2:4] = np.exp(outputs[..., 2:4]) * expanded_strides

    return outputs


def postprocess_predictions(predictions, input_shape, ratio, conf_thresh=0.3, nms_thresh=0.45,
                            class_agnostic=True):
    """
    Convert model predictions to valid bounding boxes.
    
    Args:
        predictions: Model output array
        input_shape: Model input shape (height, width)
        ratio: Resize ratio from preprocessing
        conf_thresh: Confidence threshold (default: 0.3)
        nms_thresh: NMS IoU threshold (default: 0.45)
    
    Returns:
        Dictionary containing:
            - boxes: Bounding boxes in (x1, y1, x2, y2) format
            - scores: Confidence scores
            - class_ids: Class IDs
    """
    predictions = custom_demo_postprocess(predictions[0], input_shape)[0]
    boxes = predictions[:, :4]
    scores = predictions[:, 4:5] * predictions[:, 5:]

    # Convert boxes to (x1, y1, x2, y2) format
    boxes_xyxy = np.ones_like(boxes)
    boxes_xyxy[:, 0] = boxes[:, 0] - boxes[:, 2] / 2.0
    boxes_xyxy[:, 1] = boxes[:, 1] - boxes[:, 3] / 2.0
    boxes_xyxy[:, 2] = boxes[:, 0] + boxes[:, 2] / 2.0
    boxes_xyxy[:, 3] = boxes[:, 1] + boxes[:, 3] / 2.0
    boxes_xyxy /= ratio  # Scale back to original image size

    # Apply NMS
    dets = custom_multiclass_nms(boxes_xyxy, scores, nms_thresh, conf_thresh, class_agnostic)
    if dets is None:
        return {
            'boxes': np.array([]),
            'scores': np.array([]),
            'class_ids': np.array([])
        }

    final_boxes, final_scores, final_class_ids = dets[:, :4], dets[:, 4], dets[:, 5]
    
    return {
        'boxes': final_boxes,
        'scores': final_scores,
        'class_ids': final_class_ids
    }


def parse_args():
    parser = argparse.ArgumentParser(description="Postprocess YOLOX model output")
    parser.add_argument("--output", type=str, required=True, 
                       help="Path to model output .npy file")
    parser.add_argument("--input-shape", type=str, required=True,
                       help="Model input shape as 'height,width' (e.g., '416,416')")
    parser.add_argument("--ratio", type=float, required=True,
                       help="Resize ratio from preprocessing")
    parser.add_argument("--conf-thresh", type=float, default=0.3,
                       help="Confidence threshold (default: 0.3)")
    parser.add_argument("--nms-thresh", type=float, default=0.45,
                       help="NMS IoU threshold (default: 0.45)")
    return parser.parse_args()


def main():
    args = parse_args()
    
    # Parse input shape
    input_shape = tuple(map(int, args.input_shape.split(',')))
    
    # Load model output
    print(f"Loading model output from: {args.output}")
    predictions = np.load(args.output)
    print(f"Output shape: {predictions.shape}")
    
    # Postprocess
    print(f"Postprocessing with conf_thresh={args.conf_thresh}, nms_thresh={args.nms_thresh}")
    result = postprocess_predictions(
        predictions, input_shape, args.ratio, args.conf_thresh, args.nms_thresh
    )
    
    print(f"\nDetections: {len(result['boxes'])}")
    if len(result['boxes']) > 0:
        print(f"Confidence range: [{result['scores'].min():.4f}, {result['scores'].max():.4f}]")
        print(f"Classes detected: {np.unique(result['class_ids'].astype(int)).tolist()}")
    
    return result


if __name__ == "__main__":
    main()
