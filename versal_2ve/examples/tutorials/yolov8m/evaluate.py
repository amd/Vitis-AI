#!/usr/bin/env python3
"""
YOLOv8 Detection: COCO Evaluation
"""

import argparse
import time
from pathlib import Path
import json
import sys


def install_required_packages():
    """Install required Python packages using pip install --no-deps"""
    import subprocess
    import sys
    
    # List of packages required by this script
    # Note: onnxruntime-vitisai is expected to be already installed
    required_packages = [
        'tqdm',               # Progress bars
        'pycocotools',        # COCO evaluation tools (required by utils.py)
        'opencv-python==4.8.0.76',      # cv2 - required for image processing
    ]
    
    print("Installing required packages...")
    for package in required_packages:
        try:
            print(f"Installing {package}...")
            subprocess.check_call([
                sys.executable, '-m', 'pip', 'install', package, '--no-deps'
            ])
            print(f"  ✓ {package} installed successfully")
        except subprocess.CalledProcessError as e:
            print(f"  ✗ Failed to install {package}: {e}")
            print(f"  Warning: Continuing anyway. Some features may not work.")
    
    print("Package installation complete!")


def parse_args():
    parser = argparse.ArgumentParser(description="Evaluate YOLO model directly on COCO dataset")
    parser.add_argument("--install-packages", action='store_true', 
                       help="Install required Python packages and exit")
    parser.add_argument("--model", type=str, required=False, help="Path to ONNX model")
    parser.add_argument("--coco_dataset", type=str, required=False, 
                       help="Path to COCO dataset folder (e.g., datasets/coco)")
    parser.add_argument("--device", type=str, default="npu-int8-bf16", 
                       choices=["cpu-fp32", "cpu-int8-fp32", "npu-bf16", "npu-int8-bf16"],
                       help="Device to run inference on")
    parser.add_argument("--cache_dir", type=str, default="./", help="Cache directory for compiled models")
    parser.add_argument("--cache_key", type=str, default=None, help="Cache key for compiled model (can be a simple identifier or absolute/relative path)")
    parser.add_argument("--config_file", type=str, default="vitisai_config.json", help="VitisAI config file")
    parser.add_argument("--num_images", type=int, default=5000, help="Number of images to evaluate (default: 5000)")
    parser.add_argument("--conf", type=float, default=0.001, help="Confidence threshold (min_score_thres)")
    parser.add_argument("--iou", type=float, default=0.5, help="NMS IoU threshold")
    parser.add_argument("--output_dir", type=str, default=None, help="Output directory for results")
    return parser.parse_args()


def create_session(model_path, device, cache_dir, cache_key, config_file):
    """Create ONNX Runtime session based on device type"""
    import onnxruntime as ort
    import os
    
    if device == "npu-bf16":
        print("Running BF16 Model on NPU")
        if cache_key is None:
            cache_key = Path(model_path).stem

        # Normalize cache_key to handle absolute or relative paths
        if cache_key:
            if os.sep in str(cache_key) or '/' in str(cache_key) or '\\' in str(cache_key):
                cache_key_normalized = os.path.abspath(cache_key)
            else:
                cache_key_normalized = cache_key
        else:
            cache_key_normalized = Path(model_path).stem

        provider_options_dict = {
            "config_file": config_file,
            "cache_dir": cache_dir,
            "cache_key": cache_key_normalized,
            "ai_analyzer_visualization": True,
            "ai_analyzer_profiling": True,
            "target": "VAIML"
        }
        session = ort.InferenceSession(
            model_path,
            providers=["VitisAIExecutionProvider"],
            provider_options=[provider_options_dict]
        )
    elif device == "npu-int8-bf16":
        print(f"Running VINT8-BF16 Model on NPU")
        if cache_key is None:
            cache_key = Path(model_path).stem

        # Normalize cache_key to handle absolute or relative paths
        if cache_key:
            # Check if cache_key is a path (contains / or \)
            if os.sep in str(cache_key) or '/' in str(cache_key) or '\\' in str(cache_key):
                # Convert to absolute path
                cache_key_normalized = os.path.abspath(cache_key)
            else:
                # Use as-is if it's just a simple identifier
                cache_key_normalized = cache_key
        else:
            cache_key_normalized = Path(model_path).stem

        provider_options_dict = {
            "config_file": config_file,
            "cache_dir": cache_dir,
            "cache_key": cache_key_normalized,
            "ai_analyzer_visualization": True,
            "ai_analyzer_profiling": True,
            "target": "VAIML"
        }
        session = ort.InferenceSession(
            model_path,
            providers=["VitisAIExecutionProvider"],
            provider_options=[provider_options_dict]
        )
    elif device == "cpu-int8-fp32":
        print("Running VINT8-FP32 Model on CPU")
        session = ort.InferenceSession(model_path)
    else:  # cpu-fp32
        print("Running FP32 Model on CPU")
        session = ort.InferenceSession(model_path)
    
    return session


def main():
    args = parse_args()
    
    # Handle package installation request
    if args.install_packages:
        install_required_packages()
        print("\n✓ Package installation complete!")
        return
    
    # Now import required packages
    missing_packages = []
    
    try:
        import onnxruntime as ort
    except ImportError:
        missing_packages.append('onnxruntime-vitisai')
    
    try:
        from tqdm import tqdm
    except ImportError:
        missing_packages.append('tqdm')
    
    try:
        import cv2
    except ImportError:
        missing_packages.append('opencv-python')
    
    try:
        from pycocotools.coco import COCO
        from pycocotools.cocoeval import COCOeval
    except ImportError:
        missing_packages.append('pycocotools')
    
    # Report missing packages BEFORE trying to import utils.py
    # (utils.py imports pycocotools at the top level, so it will fail if missing)
    if missing_packages:
        print(f"Error: Missing required packages: {', '.join(missing_packages)}")
        print("Please run the script with --install-packages first:")
        print("  python3 evaluate_coco_direct.py --install-packages")
        sys.exit(1)
    
    # Import utils.py functions (only after all dependencies are confirmed)
    try:
        from utils import (
            load_coco_dataset, load_onnx_model,
            evaluate_model, evaluate_coco, save_detections, calc_yolo_id_to_coco_map
        )
    except ImportError as e:
        print(f"Error: Failed to import utils.py: {e}")
        print("Please ensure utils.py is in the same directory as this script.")
        print("Also ensure all required packages are installed:")
        print("  python3 evaluate_coco_direct.py --install-packages")
        sys.exit(1)
    
    # Missing packages check is now done before importing utils.py
    
    # Validate required arguments
    if not args.model:
        print("Error: --model is required (unless using --install-packages)")
        sys.exit(1)
    if not args.coco_dataset:
        print("Error: --coco_dataset is required")
        sys.exit(1)
    
    print("=" * 80)
    print("YOLOv8 Detection: COCO Evaluation")
    print("=" * 80)
    
    # Setup paths
    coco_dataset_path = Path(args.coco_dataset)
    annotations_path = coco_dataset_path / "annotations" / "instances_val2017.json"
    images_folder = coco_dataset_path / "images" / "val2017"
    
    if not annotations_path.exists():
        print(f"Error: COCO annotations file not found at {annotations_path}")
        sys.exit(1)
    
    if not images_folder.exists():
        print(f"Error: COCO images folder not found at {images_folder}")
        sys.exit(1)
    
    # Load COCO dataset - using utils.py function
    print(f"\n1. Loading COCO dataset from {annotations_path}")
    coco, img_ids, coco_id_to_cls_map = load_coco_dataset(annotations_path)
    print(f"   ✓ Loaded {len(img_ids)} images")
    print(f"   ✓ Number of classes: {len(coco_id_to_cls_map)}")
    
    # Limit number of images if specified
    if args.num_images and args.num_images < len(img_ids):
        img_ids = img_ids[:args.num_images]
        print(f"   Limited to {args.num_images} images for evaluation")
    
    # Create ONNX session
    print(f"\n2. Loading ONNX model: {args.model}")
    session = create_session(
        args.model,
        args.device,
        args.cache_dir,
        args.cache_key,
        args.config_file
    )
    
    # Load model metadata - using utils.py function
    session, input_name, yolo_id_to_cls_map = load_onnx_model(session, args.device)
    
    # Calculate YOLO to COCO ID mapping - using utils.py function
    yolo_id_to_coco_id_map = calc_yolo_id_to_coco_map(
        yolo_id_to_cls_map, coco_id_to_cls_map
    )
    
    # Get input shape
    input_shape = session.get_inputs()[0].shape
    if len(input_shape) == 4:
        _, _, img_h, img_w = input_shape
    else:
        img_h, img_w = 640, 640  # Default
    
    print(f"   ✓ Model loaded")
    print(f"   Input: {input_name}, shape: {input_shape}")
    print(f"   Image size: {img_w}x{img_h}")
    
    # Setup output directory
    if args.output_dir:
        output_root = Path(args.output_dir)
    else:
        model_name = Path(args.model).stem
        output_root = Path("runs") / "evaluation" / f"{model_name}-{args.device}"
    output_root.mkdir(parents=True, exist_ok=True)
    print(f"   Output directory: {output_root}")
    
    # Run evaluation - using utils.py function
    print(f"\n3. Running inference and evaluation on {len(img_ids)} images...")
    print(f"   Confidence threshold: {args.conf}")
    print(f"   NMS IoU threshold: {args.iou}")
    
    inference_times = []
    
    # Use utils.py evaluate_model function
    detections = evaluate_model(
        session,
        input_name,
        coco,
        images_folder,
        img_ids,
        yolo_id_to_coco_id_map,
        coco_id_to_cls_map,
        input_size=(img_w, img_h),
        min_score_thres=args.conf,
        nms_iou_thresh=args.iou,
        num_max_images=args.num_images if args.num_images else None,
        output_root=output_root,
    )
    
    if not detections:
        print('Error: Model did not generate any predictions. Unable to evaluate Model accuracy on COCO dataset')
        sys.exit(1)
    
    # Save detections - using utils.py function
    pred_json_save_path = output_root / "pred.json"
    print(f"\n4. Saving detections to {pred_json_save_path}")
    save_detections(detections, pred_json_save_path)
    print(f"   ✓ Saved {len(detections)} detections")
    
    # Evaluate COCO metrics - using utils.py function
    print(f"\n5. Computing COCO evaluation metrics...")
    coco_eval_save_path = output_root / "coco-metrics.json"
    mAP, mAP50, mAP75 = evaluate_coco(coco, pred_json_save_path, coco_eval_save_path)
    
    # Save evaluation results summary
    eval_results = {
        'map_metrics': {
            'mAP': float(mAP),
            'mAP50': float(mAP50),
            'mAP75': float(mAP75)
        },
        'conf_threshold': float(args.conf),
        'iou_threshold': float(args.iou),
        'num_images': int(len(img_ids)),
        'total_detections': int(len(detections)),
        'device': args.device,
        'model': str(args.model)
    }
    
    eval_results_json = output_root / "evaluation_results.json"
    print(f"\n6. Saving evaluation results to {eval_results_json}")
    with open(eval_results_json, 'w') as f:
        json.dump(eval_results, f, indent=2)
    
    # Print summary
    print(f"\n{'='*80}")
    print("EVALUATION SUMMARY")
    print(f"{'='*80}\n")
    
    print(f"\nmAP Metrics:")
    print(f"  {'Metric':<20} {'Value':<15}")
    print(f"  {'-'*35}")
    print(f"  {'mAP (AP@[IoU=0.50:0.95])':<20} {mAP:<15.4f}")
    print(f"  {'mAP50 (AP@IoU=0.50)':<20} {mAP50:<15.4f}")
    print(f"  {'mAP75 (AP@IoU=0.75)':<20} {mAP75:<15.4f}")
    
    print(f"\n{'='*80}")
    print("✓ Evaluation completed successfully!")
    print(f"\nResults saved to: {output_root}")
    print(f"  - Detections: {pred_json_save_path}")
    print(f"  - COCO metrics: {coco_eval_save_path}")
    print(f"  - Evaluation results: {eval_results_json}")


if __name__ == "__main__":
    main()
