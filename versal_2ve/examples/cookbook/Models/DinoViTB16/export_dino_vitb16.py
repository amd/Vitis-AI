#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

#!/usr/bin/env python3
"""
Export facebook/dino-vitb16 (ViT-B/16) to ONNX.

Produces: dino-vitb16_last_hidden_state.onnx

The reference model `dino-vitb16_last_hidden_state.onnx` is a
HuggingFace ViTModel. This script reconstructs the same architecture from the
public checkpoint and exports it with matching input (pixel_values
[1,3,224,224]) / output (last_hidden_state [1,197,768]) signatures.

The original was exported with the classic TorchScript exporter at opset 17
(GELU shows up as Erf, i.e. unfused), so this script defaults to dynamo=False
and opset 17.
"""

import argparse
import torch
import torch.nn as nn
from transformers import ViTModel


class ViTLastHiddenStateWrapper(nn.Module):
    """Wrap ViTModel so ONNX has a single output named 'last_hidden_state'.

    The submodule is named `self.model` to reproduce the original's
    `model.` weight-name prefix.
    """

    def __init__(self, model: nn.Module):
        super().__init__()
        self.model = model

    def forward(self, pixel_values: torch.Tensor) -> torch.Tensor:
        return self.model(pixel_values=pixel_values).last_hidden_state


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--checkpoint", default="facebook/dino-vitb16")
    ap.add_argument("--height", type=int, default=224)
    ap.add_argument("--width", type=int, default=224)
    ap.add_argument("--opset", type=int, default=17)
    ap.add_argument(
        "--dynamo",
        action="store_true",
        default=False,
        help="Use the TorchDynamo ONNX exporter (fuses Gelu/LayerNormalization).",
    )
    ap.add_argument(
        "--attn",
        default="eager",
        choices=["eager", "sdpa"],
        help="Attention implementation; 'eager' yields explicit Softmax like the original.",
    )
    ap.add_argument(
        "--output",
        default="dino-vitb16_last_hidden_state.onnx",
    )
    args = ap.parse_args()

    print(f"[load] {args.checkpoint}")
    model = ViTModel.from_pretrained(
        args.checkpoint,
        add_pooling_layer=False,
        attn_implementation=args.attn,
    )
    model.eval()

    wrapper = ViTLastHiddenStateWrapper(model).eval()
    dummy = torch.randn(1, 3, args.height, args.width, dtype=torch.float32)

    with torch.no_grad():
        out = wrapper(dummy)
    print(f"[check] pixel_values {tuple(dummy.shape)} -> "
          f"last_hidden_state {tuple(out.shape)}")

    print(f"[export] opset={args.opset} dynamo={args.dynamo} -> {args.output}")
    torch.onnx.export(
        wrapper,
        (dummy,),
        args.output,
        input_names=["pixel_values"],
        output_names=["last_hidden_state"],
        opset_version=args.opset,
        dynamo=args.dynamo,
        do_constant_folding=True,
    )
    print("[done]")


if __name__ == "__main__":
    main()
