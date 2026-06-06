#!/usr/bin/env python3

# ===========================================================
# Copyright © 2025 Advanced Micro Devices, Inc. All rights reserved.
# MIT License
# ===========================================================

import numpy as np
import onnxruntime as ort
import os
import argparse
import sys
from pathlib import Path
import colorsys
import tempfile
import shutil

# Import cv2 with error handling
try:
    import cv2
    # Verify cv2 has required functions
    if not hasattr(cv2, 'imread'):
        raise AttributeError("cv2.imread not available")
except (ImportError, AttributeError) as e:
    print(f"Error: OpenCV (cv2) is not properly installed: {e}")
    print("Please install opencv-python:")
    print("  pip install opencv-python --no-deps")
    sys.exit(1)

# Try to import yolov8_utils if available (after cv2 import to avoid conflicts)
try:
    from yolov8_utils import *
    # Re-import cv2 after yolov8_utils in case it was shadowed
    import cv2
except ImportError:
    pass

# Load COCO class labels
COCO_CLASSES = [  # 80 classes
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck",
    "boat", "traffic light", "fire hydrant", "stop sign", "parking meter", "bench",
    "bird", "cat", "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe",
    "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard",
    "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard",
    "tennis racket", "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl",
    "banana", "apple", "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza",
    "donut", "cake", "chair", "couch", "potted plant", "bed", "dining table", "toilet",
    "TV", "laptop", "mouse", "remote", "keyboard", "cell phone", "microwave", "oven",
    "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
    "hair drier", "toothbrush"
]

def get_color_for_class(class_id, num_classes=80):
    """
    Generate a distinct color for each class using HSV color space.
    Returns BGR color tuple for OpenCV.
    """
    # Use golden ratio conjugate for better color distribution
    golden_ratio = 0.618033988749895
    hue = (class_id * golden_ratio) % 1.0
    
    # Use high saturation and value for vibrant colors
    saturation = 0.8 + (class_id % 3) * 0.1  # Vary between 0.8-1.0
    value = 0.9 + (class_id % 2) * 0.1       # Vary between 0.9-1.0
    
    # Convert HSV to RGB
    rgb = colorsys.hsv_to_rgb(hue, saturation, value)
    
    # Convert to BGR (OpenCV format) and scale to 0-255
    bgr = (int(rgb[2] * 255), int(rgb[1] * 255), int(rgb[0] * 255))
    
    return bgr

def preprocess_image(image_path, img_size=640):
    """
    Preprocess a single image: resize, normalize, convert to CHW.
    Returns preprocessed numpy array and original image.
    """
    # Verify cv2 is available and has imread
    if not hasattr(cv2, 'imread'):
        raise RuntimeError("cv2.imread is not available. Please install opencv-python: pip install opencv-python --no-deps")
    
    img = cv2.imread(image_path)
    if img is None:
        raise ValueError(f"Failed to load image from {image_path}")
    
    orig = img.copy()
    img = cv2.resize(img, (img_size, img_size))
    img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    img = img.astype(np.float32) / 255.0
    img = np.transpose(img, (2, 0, 1))  # HWC to CHW
    img = np.expand_dims(img, axis=0)
    
    return img, orig

def postprocess(outputs, img, conf_thres=0.05, nms_thres=0.7, max_per_class=2):
    """
    Post-process YOLOv8 outputs with adjustable thresholds.
    Lower confidence threshold helps detect objects like person and clock.
    Limits detections to max_per_class instances per object category.
    """
    predictions = np.transpose(np.squeeze(outputs[0]))
    
    img_height, img_width = img.shape[:2]
    print(f"Processing {predictions.shape[0]} predictions for image size {img_width}x{img_height}")

    boxes = []
    confidences = []
    class_ids = []

    for pred in predictions:
        x_center, y_center, width, height = pred[0:4]
        class_scores = pred[4:]  # 80 class scores

        class_id = np.argmax(class_scores)
        confidence = class_scores[class_id]

        # Use lower threshold for person class (class_id 0)
        threshold = conf_thres * 0.5 if class_id == 0 else conf_thres
        
        if confidence < threshold:
            continue

        # Convert xywh (center) to xyxy (corners) with proper scaling
        x1 = int((x_center - width / 2) * img_width / 640)
        y1 = int((y_center - height / 2) * img_height / 640)
        x2 = int((x_center + width / 2) * img_width / 640)
        y2 = int((y_center + height / 2) * img_height / 640)
        
        # Clamp to image boundaries
        x1 = max(0, min(x1, img_width - 1))
        y1 = max(0, min(y1, img_height - 1))
        x2 = max(0, min(x2, img_width - 1))
        y2 = max(0, min(y2, img_height - 1))
        
        # Skip invalid boxes (too small or malformed)
        box_width = x2 - x1
        box_height = y2 - y1
        
        # More lenient for person class
        min_size = 5 if class_id == 0 else 10
        if box_width < min_size or box_height < min_size:
            continue
        
        # Skip boxes that are unreasonably large (>98% of image)
        if box_width > img_width * 0.98 or box_height > img_height * 0.98:
            continue

        boxes.append([x1, y1, x2, y2])
        confidences.append(float(confidence))
        class_ids.append(class_id)

    print(f"Found {len(boxes)} detections above confidence threshold")
    
    if len(boxes) == 0:
        print("Warning: No objects detected! Try lowering --conf_threshold")
        return img

    # Apply Non-Maximum Suppression (NMS)
    indices = cv2.dnn.NMSBoxes(boxes, confidences, conf_thres * 0.5, nms_threshold=nms_thres)
    
    # Handle different return formats from NMSBoxes
    if len(indices) == 0:
        print("No objects remaining after NMS")
        return img
    
    # Convert to flat list of indices
    if isinstance(indices, np.ndarray):
        indices = indices.flatten().tolist()
    elif isinstance(indices, (list, tuple)):
        indices = list(indices)
    else:
        indices = [int(indices)]
    
    print(f"After NMS: {len(indices)} objects detected")
    
    # Limit to max_per_class detections per class (keep highest confidence)
    class_detections = {}
    for i in indices:
        class_id = class_ids[i]
        if class_id not in class_detections:
            class_detections[class_id] = []
        class_detections[class_id].append((i, confidences[i]))
    
    # Sort by confidence and keep only top max_per_class per class
    filtered_indices = []
    removed_count = 0
    for class_id, detections in class_detections.items():
        # Sort by confidence (highest first)
        detections.sort(key=lambda x: x[1], reverse=True)
        kept = detections[:max_per_class]
        removed = len(detections) - len(kept)
        if removed > 0:
            removed_count += removed
            print(f"  - Limited {COCO_CLASSES[class_id]} from {len(detections)} to {len(kept)} instances")
        filtered_indices.extend([idx for idx, conf in kept])
    
    indices = filtered_indices
    if removed_count > 0:
        print(f"Filtered to top {max_per_class} per class: {len(indices)} objects remaining")
    
    # Count detections by class
    class_counts = {}
    for i in indices:
        class_id = class_ids[i]
        class_name = COCO_CLASSES[class_id]
        class_counts[class_name] = class_counts.get(class_name, 0) + 1

    # Calculate total detections and percentages
    total_detections = len(indices)
    class_percentages = {}
    for class_name, count in class_counts.items():
        class_percentages[class_name] = (count / total_detections) * 100

    # Draw boxes with improved labels and unique colors per class
    for i in indices:
        x1, y1, x2, y2 = boxes[i]
        conf = confidences[i]
        class_id = class_ids[i]
        class_name = COCO_CLASSES[class_id]
        
        # Clean, professional label format with confidence and class percentage
        class_pct = class_percentages[class_name]
        label = f"{class_name} {conf:.1%} ({class_pct:.1f}%)"  # e.g., "person 95.3% (40.0%)"
        
        # Get unique color for this class
        color = get_color_for_class(class_id)
        
        # Draw bounding box with fine line thickness (2px for clarity)
        cv2.rectangle(img, (x1, y1), (x2, y2), color, 2)
        
        # Use HERSHEY_DUPLEX for cleaner, more refined text
        font = cv2.FONT_HERSHEY_DUPLEX
        font_scale = 0.5  # Compact but readable
        font_thickness = 1  # Fine, clean lines
        (text_width, text_height), baseline = cv2.getTextSize(label, font, font_scale, font_thickness)
        
        # Minimal padding for compact, clean appearance
        padding_x = 6
        padding_y = 3
        
        # Determine label position (above box if space available, otherwise inside top)
        if y1 - text_height - padding_y * 2 - 2 > 0:
            # Place above the box with minimal clearance
            label_y = y1 - padding_y - 2
            rect_y1 = y1 - text_height - padding_y * 2 - 2
            rect_y2 = y1 - 2
        else:
            # Place inside top of the box
            label_y = y1 + text_height + padding_y
            rect_y1 = y1 + 2
            rect_y2 = y1 + text_height + padding_y * 2 + 2
        
        # Ensure label fits within image width
        rect_x2 = min(x1 + text_width + padding_x * 2, img_width)
        
        # Draw semi-transparent background for modern look
        overlay = img.copy()
        cv2.rectangle(overlay, (x1, rect_y1), (rect_x2, rect_y2), color, -1)
        cv2.addWeighted(overlay, 0.7, img, 0.3, 0, img)
        
        # Draw clean border around label
        cv2.rectangle(img, (x1, rect_y1), (rect_x2, rect_y2), color, 1)
        
        # Draw text label with white color and shadow for depth
        # Shadow for better readability
        cv2.putText(img, label, (x1 + padding_x + 1, label_y + 1), font, font_scale, (0, 0, 0), font_thickness, cv2.LINE_AA)
        # Main text
        cv2.putText(img, label, (x1 + padding_x, label_y), font, font_scale, (255, 255, 255), font_thickness, cv2.LINE_AA)
    
    # Organize detections by class for console output
    detections_by_class = {}
    for i in indices:
        class_id = class_ids[i]
        class_name = COCO_CLASSES[class_id]
        conf = confidences[i]
        if class_name not in detections_by_class:
            detections_by_class[class_name] = []
        detections_by_class[class_name].append(conf)
    
    # Print detected objects with same format as bounding box labels
    print(f"\nDetected objects (Total: {total_detections}):")
    for class_name in sorted(detections_by_class.keys()):
        confs = detections_by_class[class_name]
        count = len(confs)
        percentage = class_percentages[class_name]
        print(f"\n  {class_name}: {count} detection(s) ({percentage:.1f}%)")
        for idx, conf in enumerate(confs, 1):
            print(f"    #{idx}: {class_name} {conf:.1%} ({percentage:.1f}%)")
    
    return img

def run_inference(session, preprocessed_input):
    """
    Run inference on a preprocessed input.
    Returns the output numpy array.
    """
    input_name = session.get_inputs()[0].name
    outputs = session.run(None, {input_name: preprocessed_input})
    return outputs

def main():
    parser = argparse.ArgumentParser(description="YOLOv8m Complete Inference Pipeline - Preprocess, Run Inference, Postprocess")
    
    # Model arguments
    parser.add_argument(
        "--model_path",
        type=str,
        default="models/yolov8m_VINT8_skipNodes.onnx",
        help="Path to the quantized ONNX model"
    )
    parser.add_argument(
        "--cache_dir",
        type=str,
        default=os.getcwd(),
        help="Directory to store cache files (default: present working directory)"
    )
    parser.add_argument(
        "--cache_key",
        type=str,
        default="yolov8m_VINT8_skipNodes",
        help="Cache key for the compiled model (can be a simple identifier or absolute/relative path)"
    )
    parser.add_argument(
        "--device",
        type=str,
        default="npu",
        choices=["npu", "cpu"],
        help="Device to run inference on: 'npu' for VEK385 NPU or 'cpu' for CPU (default: npu)"
    )
    
    # Input arguments
    parser.add_argument(
        "--image_path",
        type=str,
        required=True,
        help="Path to input image file or folder containing images"
    )
    
    # Output arguments
    parser.add_argument(
        "--output_dir",
        type=str,
        default="output",
        help="Directory to save output images with detections"
    )
    
    # Postprocessing arguments
    parser.add_argument(
        "--conf_threshold",
        type=float,
        default=0.05,
        help="Confidence threshold (lower=more objects, try 0.01-0.1, default: 0.025)"
    )
    parser.add_argument(
        "--nms_threshold",
        type=float,
        default=0.7,
        help="NMS IoU threshold (higher=more duplicates, default: 0.7)"
    )
    parser.add_argument(
        "--max_per_class",
        type=int,
        default=2,
        help="Maximum number of detections per class (default: 2)"
    )
    parser.add_argument(
        "--num_runs",
        type=int,
        default=1,
        help="Number of inference runs per image (default: 1, useful for benchmarking)"
    )
    
    # Optional: keep intermediate files
    parser.add_argument(
        "--keep_intermediate",
        action="store_true",
        help="Keep intermediate numpy files (input and output)"
    )
    parser.add_argument(
        "--intermediate_dir",
        type=str,
        default=None,
        help="Directory to save intermediate files (default: temporary directory)"
    )
    
    args = parser.parse_args()
    
    # Verify model exists
    if not os.path.exists(args.model_path):
        raise FileNotFoundError(f"Model not found: {args.model_path}")
    
    # Verify image path exists
    if not os.path.exists(args.image_path):
        raise FileNotFoundError(f"Image path not found: {args.image_path}")
    
    # Determine if input is a file or folder
    if os.path.isfile(args.image_path):
        image_files = [args.image_path]
    else:
        # It's a folder, get all image files
        image_files = [os.path.join(args.image_path, f) for f in os.listdir(args.image_path) 
                      if f.lower().endswith(('.png', '.jpg', '.jpeg'))]
        if not image_files:
            raise ValueError(f"No image files found in {args.image_path}")
    
    # Create cache directory if it doesn't exist
    cache_dir_abs = os.path.abspath(args.cache_dir)
    os.makedirs(cache_dir_abs, exist_ok=True)
    
    # Create output directory
    output_dir = os.path.abspath(args.output_dir)
    os.makedirs(output_dir, exist_ok=True)
    
    # Create intermediate directory (temporary or specified)
    if args.intermediate_dir:
        intermediate_dir = os.path.abspath(args.intermediate_dir)
        os.makedirs(intermediate_dir, exist_ok=True)
        temp_intermediate = False
    else:
        intermediate_dir = tempfile.mkdtemp(prefix="yolov8_inference_")
        temp_intermediate = True
    
    input_folder = os.path.join(intermediate_dir, "input")
    output_npy_folder = os.path.join(intermediate_dir, "output_npy")
    os.makedirs(input_folder, exist_ok=True)
    os.makedirs(output_npy_folder, exist_ok=True)
    
    print("="*70)
    print("YOLOv8m Complete Inference Pipeline")
    print("="*70)
    print(f"Model path:        {os.path.abspath(args.model_path)}")
    # Normalize cache_key to handle absolute or relative paths
    if args.cache_key:
        # Check if cache_key is a path (contains / or \)
        if os.sep in args.cache_key or '/' in args.cache_key or '\\' in args.cache_key:
            # Convert to absolute path
            cache_key_abs = os.path.abspath(args.cache_key)
            cache_key_normalized = cache_key_abs
        else:
            # Use as-is if it's just a simple identifier
            cache_key_normalized = args.cache_key
    else:
        cache_key_normalized = "yolov8m_VINT8_skipNodes"
    
    print(f"Cache dir:         {cache_dir_abs}")
    print(f"Cache key:         {cache_key_normalized}")
    print(f"Device:            {args.device.upper()}")
    print(f"Input image(s):    {len(image_files)} file(s)")
    print(f"Output dir:        {output_dir}")
    print(f"Intermediate dir:  {intermediate_dir}")
    print(f"Conf threshold:    {args.conf_threshold}")
    print(f"NMS threshold:     {args.nms_threshold}")
    print(f"Max per class:     {args.max_per_class}")
    print(f"Number of runs:    {args.num_runs}")
    print("="*70)
    
    # Create inference session based on device selection
    print(f"\nLoading model and creating inference session ({args.device.upper()})...")
    
    if args.device == "npu":
        # Provider options (using absolute path for cache_dir)
        provider_options_dict = {
            "config_file": "vitisai_config.json",
            "cache_dir": cache_dir_abs,
            "cache_key": cache_key_normalized,
            "ai_analyzer_visualization": True,
            "ai_analyzer_profiling": True,
            "target": "VAIML"
        }
        provider_options_dict["log_level"] = "info"
        
        session = ort.InferenceSession(
            args.model_path,
            providers=["VitisAIExecutionProvider"],
            provider_options=[provider_options_dict]
        )
    else:  # cpu
        session = ort.InferenceSession(
            args.model_path,
            providers=["CPUExecutionProvider"]
        )
    
    print("✓ Model loaded successfully")
    
    # Process each image
    total_images = len(image_files)
    
    for img_idx, image_path in enumerate(image_files):
        print(f"\n{'='*70}")
        print(f"Processing image {img_idx + 1}/{total_images}: {os.path.basename(image_path)}")
        print(f"{'='*70}")
        
        # Step 1: Preprocess
        print("\n[Step 1/3] Preprocessing image...")
        try:
            preprocessed_img, original_img = preprocess_image(image_path)
            print(f"✓ Preprocessing complete. Input shape: {preprocessed_img.shape}")
        except Exception as e:
            print(f"✗ Error during preprocessing: {e}")
            continue
        
        # Save preprocessed input (optional, for debugging)
        if args.keep_intermediate:
            input_npy_path = os.path.join(input_folder, f"input_{img_idx}.npy")
            np.save(input_npy_path, preprocessed_img)
            print(f"  Saved preprocessed input to: {input_npy_path}")
        
        # Step 2: Run inference
        print(f"\n[Step 2/3] Running inference ({args.num_runs} run(s))...")
        outputs = None
        
        try:
            for run_idx in range(args.num_runs):
                outputs = run_inference(session, preprocessed_img)
                if args.num_runs > 1:
                    print(f"  Run {run_idx + 1}/{args.num_runs} complete")
            
            if args.num_runs > 1:
                print(f"✓ Inference complete ({args.num_runs} runs)")
            else:
                print(f"✓ Inference complete")
            
            print(f"  Output shape: {outputs[0].shape}")
        except Exception as e:
            print(f"✗ Error during inference: {e}")
            continue
        
        # Save output numpy (optional, for debugging)
        if args.keep_intermediate:
            output_npy_path = os.path.join(output_npy_folder, f"output_{img_idx}.npy")
            np.save(output_npy_path, outputs[0])
            print(f"  Saved output numpy to: {output_npy_path}")
        
        # Step 3: Postprocess
        print("\n[Step 3/3] Postprocessing...")
        try:
            result_img = postprocess(
                outputs,
                original_img.copy(),
                conf_thres=args.conf_threshold,
                nms_thres=args.nms_threshold,
                max_per_class=args.max_per_class
            )
            print("✓ Postprocessing complete")
        except Exception as e:
            print(f"✗ Error during postprocessing: {e}")
            continue
        
        # Save output image
        image_name = os.path.splitext(os.path.basename(image_path))[0]
        output_image_path = os.path.join(output_dir, f"{image_name}_detected.jpg")
        cv2.imwrite(output_image_path, result_img)
        print(f"✓ Output image saved to: {output_image_path}")
    
    # Summary
    print(f"\n{'='*70}")
    print("Summary")
    print(f"{'='*70}")
    print(f"Total images processed: {total_images}")
    print(f"Output images saved to: {output_dir}")
    if args.keep_intermediate:
        print(f"Intermediate files saved to: {intermediate_dir}")
    elif temp_intermediate:
        shutil.rmtree(intermediate_dir, ignore_errors=True)
    print("="*70)

if __name__ == "__main__":
    main()