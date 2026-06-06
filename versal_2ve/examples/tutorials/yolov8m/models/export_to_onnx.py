#!/bin/bash 

# ===========================================================
# Copyright © 2025 Advanced Micro Devices, Inc. All rights reserved.
# MIT License
# ===========================================================

import os
import sys

# ============================================================
# STEP 1: Disable ultralytics auto-install BEFORE importing it
# ============================================================
os.environ["YOLO_AUTOINSTALL"] = "false"

# ============================================================
# STEP 2: Verify onnxruntime-vitisai is available
# ============================================================
try:
    import onnxruntime
    print(f"✅ onnxruntime already available: {onnxruntime.__version__}")
    
    # Check if it's the Vitis AI variant
    ort_providers = onnxruntime.get_available_providers()
    print(f"✅ Available providers: {ort_providers}")
    
    if "VitisAIExecutionProvider" in ort_providers:
        print("✅ onnxruntime-vitisai detected (VitisAI EP available)")
    else:
        print("⚠️  onnxruntime is available but VitisAIExecutionProvider not found.")
        print("   Make sure onnxruntime-vitisai is properly installed.")
except ImportError:
    print("❌ onnxruntime is NOT installed.")
    print("   Please install onnxruntime-vitisai before running this script.")
    print("   Example: pip install onnxruntime-vitisai")
    sys.exit(1)

# ============================================================
# STEP 3: Patch ultralytics to prevent it from installing onnxruntime
# ============================================================
import subprocess

# Save the original subprocess.run to prevent ultralytics from calling pip install
_original_subprocess_run = subprocess.run

def _patched_subprocess_run(*args, **kwargs):
    """Block any subprocess call that tries to pip install onnxruntime."""
    cmd = args[0] if args else kwargs.get("args", [])
    
    # Convert to string for easier matching
    cmd_str = " ".join(cmd) if isinstance(cmd, (list, tuple)) else str(cmd)
    
    if "pip" in cmd_str and "onnxruntime" in cmd_str:
        print(f"🚫 Blocked auto-install attempt: {cmd_str}")
        print("   Using existing onnxruntime-vitisai instead.")
        # Return a fake successful result
        return subprocess.CompletedProcess(args=cmd, returncode=0, stdout=b"", stderr=b"")
    
    return _original_subprocess_run(*args, **kwargs)

subprocess.run = _patched_subprocess_run

# ============================================================
# STEP 4: Also patch subprocess.check_call (some versions use this)
# ============================================================
_original_check_call = subprocess.check_call

def _patched_check_call(*args, **kwargs):
    """Block any check_call that tries to pip install onnxruntime."""
    cmd = args[0] if args else kwargs.get("args", [])
    cmd_str = " ".join(cmd) if isinstance(cmd, (list, tuple)) else str(cmd)
    
    if "pip" in cmd_str and "onnxruntime" in cmd_str:
        print(f"🚫 Blocked auto-install attempt: {cmd_str}")
        print("   Using existing onnxruntime-vitisai instead.")
        return 0
    
    return _original_check_call(*args, **kwargs)

subprocess.check_call = _patched_check_call

# ============================================================
# STEP 5: Now safely import and use ultralytics
# ============================================================
from ultralytics import YOLO


def export_yolov8m_to_onnx():
    model = YOLO("yolov8m.pt")
    print(f"Number of classes: {model.model.nc}")
    model.export(format="onnx", opset=17)  # Exports to yolov8m.onnx
    print("✅ YOLOv8m exported to yolov8m.onnx")


if __name__ == "__main__":
    export_yolov8m_to_onnx()
