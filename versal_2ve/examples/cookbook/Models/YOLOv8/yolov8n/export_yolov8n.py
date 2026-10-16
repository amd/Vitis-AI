#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

"""
Download YOLOv8n from Ultralytics and export to ONNX format.

Requirements:
    pip install ultralytics

Usage:
    python export_yolov8n.py
    python export_yolov8n.py --output yolov8n.onnx --imgsz 640 --opset 19
"""

import os
import argparse


def patch_ultralytics_onnxruntime_check():
    """
    Patch ultralytics to skip onnxruntime installation if any onnxruntime
    variant is already installed (e.g., onnxruntime-vitisai, onnxruntime-gpu).
    """
    try:
        import importlib.util
        ort_installed = importlib.util.find_spec("onnxruntime") is not None
        if ort_installed:
            try:
                from ultralytics.utils import checks
                original_check_requirements = checks.check_requirements

                def patched_check_requirements(requirements, *args, **kwargs):
                    if isinstance(requirements, str):
                        requirements = [requirements]
                    elif isinstance(requirements, tuple):
                        requirements = list(requirements)

                    def is_onnxruntime_req(r):
                        if isinstance(r, str):
                            return "onnxruntime" in r.lower()
                        elif isinstance(r, (list, tuple)):
                            return any("onnxruntime" in str(x).lower() for x in r)
                        return False

                    filtered = [r for r in requirements if not is_onnxruntime_req(r)]
                    if filtered:
                        return original_check_requirements(filtered, *args, **kwargs)
                    return True

                checks.check_requirements = patched_check_requirements
                print("Patched ultralytics to skip onnxruntime installation (already available)")
            except (ImportError, AttributeError):
                pass
    except Exception:
        pass


patch_ultralytics_onnxruntime_check()

from ultralytics import YOLO


def main():
    parser = argparse.ArgumentParser(description='Export YOLOv8n to ONNX')
    parser.add_argument('--output', type=str, default='yolov8n.onnx',
                        help='Output ONNX model path')
    parser.add_argument('--imgsz', type=int, default=640,
                        help='Input image size')
    parser.add_argument('--opset', type=int, default=19,
                        help='ONNX opset version')
    parser.add_argument('--simplify', action='store_true', default=True,
                        help='Simplify ONNX model')
    parser.add_argument('--no-simplify', action='store_false', dest='simplify',
                        help='Do not simplify ONNX model')
    args = parser.parse_args()

    print("Downloading YOLOv8n pretrained model...")
    model = YOLO("yolov8n.pt")

    print(f"Exporting to ONNX (opset={args.opset}, imgsz={args.imgsz})...")
    model.export(
        format="onnx",
        opset=args.opset,
        simplify=args.simplify,
        dynamic=False,
        imgsz=args.imgsz,
    )

    exported_file = "yolov8n.onnx"
    if os.path.exists(exported_file):
        if exported_file != args.output:
            os.rename(exported_file, args.output)
        print(f"Export complete: {args.output}")
    else:
        print(f"Error: Expected output file {exported_file} not found")

    if os.path.exists("yolov8n.pt"):
        os.remove("yolov8n.pt")
        print("Cleaned up yolov8n.pt")


if __name__ == "__main__":
    main()
