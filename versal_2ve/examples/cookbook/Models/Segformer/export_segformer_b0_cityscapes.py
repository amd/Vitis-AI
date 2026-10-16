#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

#!/usr/bin/env python3
"""
Export SegFormer-B0 (Cityscapes, 19 classes) to ONNX at 256x512 input.

Produces: segformer_b0_cityscapes_256x512.onnx

The reference model `segformer_b0_cityscapes_256x512.onnx` is a HuggingFace
SegformerForSemanticSegmentation model. This script reconstructs the same
architecture from the public checkpoint and exports it with matching
input ([1,3,256,512]) / output ([1,19,64,128]) signatures.
"""

import argparse
import torch
import torch.nn as nn
from transformers import SegformerForSemanticSegmentation


class SegformerLogitsWrapper(nn.Module):
    """Wrap HF model so ONNX has a single tensor output named 'logits'."""

    def __init__(self, model: nn.Module):
        super().__init__()
        self.model = model

    def forward(self, input: torch.Tensor) -> torch.Tensor:
        return self.model(pixel_values=input).logits


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--checkpoint",
        default="nvidia/segformer-b0-finetuned-cityscapes-1024-1024",
        help="HuggingFace SegFormer-B0 Cityscapes checkpoint id",
    )
    ap.add_argument("--height", type=int, default=256)
    ap.add_argument("--width", type=int, default=512)
    ap.add_argument("--opset", type=int, default=20)
    ap.add_argument(
        "--dynamo",
        action="store_true",
        default=True,
        help="Use the TorchDynamo ONNX exporter (fused Gelu/LayerNormalization).",
    )
    ap.add_argument("--no-dynamo", dest="dynamo", action="store_false")
    ap.add_argument(
        "--output",
        default="segformer_b0_cityscapes_256x512.onnx",
    )
    args = ap.parse_args()

    print(f"[load] {args.checkpoint}")
    model = SegformerForSemanticSegmentation.from_pretrained(args.checkpoint)
    model.eval()

    wrapper = SegformerLogitsWrapper(model).eval()
    dummy = torch.randn(1, 3, args.height, args.width, dtype=torch.float32)

    with torch.no_grad():
        out = wrapper(dummy)
    print(f"[check] input {tuple(dummy.shape)} -> logits {tuple(out.shape)}")

    print(f"[export] opset={args.opset} dynamo={args.dynamo} -> {args.output}")
    torch.onnx.export(
        wrapper,
        (dummy,),
        args.output,
        input_names=["input"],
        output_names=["logits"],
        opset_version=args.opset,
        dynamo=args.dynamo,
        do_constant_folding=True,
    )
    print("[done]")


if __name__ == "__main__":
    main()
