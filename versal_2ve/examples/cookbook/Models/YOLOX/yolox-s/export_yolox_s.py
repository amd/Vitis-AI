#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

#!/usr/bin/env python3
"""
Export YOLOX-S to ONNX format with official pretrained weights (raw format, no decoder).
Source: https://github.com/Megvii-BaseDetection/YOLOX
Reference: https://github.com/Megvii-BaseDetection/YOLOX/blob/main/tools/export_onnx.py
Install: pip install yolox onnxsim
"""

import io
import torch
import onnx

WEIGHTS_URL = "https://github.com/Megvii-BaseDetection/YOLOX/releases/download/0.1.1rc0/yolox_s.pth"
INPUT_SHAPE = [1, 3, 640, 640]
OPSET_VERSION = 17
ORIGINAL_NODE_COUNT = 277


def export_yolox_s():
    from yolox.exp import get_exp

    exp = get_exp(None, "yolox_s")
    model = exp.get_model()

    print(f"Downloading pretrained weights from:\n  {WEIGHTS_URL}")
    try:
        ckpt = torch.hub.load_state_dict_from_url(WEIGHTS_URL, map_location="cpu")
        if "model" in ckpt:
            model.load_state_dict(ckpt["model"])
        else:
            model.load_state_dict(ckpt)
        print("✓ Loaded official pretrained weights")
    except Exception as e:
        print(f"⚠ Warning: Could not load pretrained weights: {e}")

    model.eval()
    model.head.decode_in_inference = False

    dummy_input = torch.randn(*INPUT_SHAPE)
    output_path = "yolox_s.onnx"

    raw_buffer = io.BytesIO()
    torch.onnx.export(
        model,
        dummy_input,
        raw_buffer,
        opset_version=OPSET_VERSION,
        input_names=["images"],
        output_names=["output"],
        dynamic_axes=None,
        do_constant_folding=True,
    )
    raw_buffer.seek(0)
    print("Raw model exported to memory buffer")

    print("Running onnx-simplifier...")
    try:
        import onnxsim
        model_onnx = onnx.load(raw_buffer)

        model_simplified, check = onnxsim.simplify(
            model_onnx,
            overwrite_input_shapes={"images": INPUT_SHAPE},
            skipped_optimizers=["fuse_consecutive_slices"],
        )

        if check:
            onnx.save(model_simplified, output_path)
            node_count = len(model_simplified.graph.node)
            print(f"✓ Simplified model saved to {output_path}")
            print(f"  Node count: {node_count} (target: {ORIGINAL_NODE_COUNT}, diff: {node_count - ORIGINAL_NODE_COUNT:+d})")
        else:
            onnx.save(model_onnx, output_path)
            print("Simplification check failed, saved raw model")

    except ImportError:
        print("onnxsim not installed. Run: pip install onnxsim")
    except Exception as e:
        print(f"Simplification failed: {e}")

    print(f"\nInput shape: {INPUT_SHAPE}")
    print(f"Output shape: [1, 8400, 85]")
    print(f"\nNote: Output contains raw predictions (no grid decoding).")
    print(f"      Grid decoding must be applied in post-processing.")


if __name__ == "__main__":
    export_yolox_s()
