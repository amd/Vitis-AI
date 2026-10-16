#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

#!/usr/bin/env python3
"""
Export RTMDet-M (COCO, 80 classes) to ONNX at 640x640, cut before NMS.

Produces: rtmdet_m_8xb32-300e_coco_1x3x640x640_without_smaller_tail.onnx

The reference model `rtmdet_m_8xb32-300e_coco_1x3x640x640_without_smaller_tail.onnx`
is a subgraph of the standard mmdeploy ONNX export of mmdetection's RTMDet-M. Its
producer is `onnx.utils.extract_model`, its inputs/outputs are:
    input               [1, 3, 640, 640]  FLOAT
    /Concat_6_output_0  [1, 8400, 4]      FLOAT   (distance2bbox-decoded xyxy)
    /Concat_3_output_0  [1, 8400, 80]     FLOAT   (per-class scores/logits)
i.e. the RTMDet head's concatenated boxes/scores *before* the multiclass NMS tail
("without_smaller_tail").

This script reproduces the same file by:
  1. Running mmdeploy's `torch2onnx` (end2end, onnxruntime, static 640x640, opset 11)
     from the public rtmdet_m_8xb32-300e_coco config + checkpoint. mmdeploy's RTMDet
     head rewriter emits exactly the `/Concat_3` (scores) and `/Concat_6` (boxes)
     nodes, followed by the NMSop tail.
  2. Simplifying with onnx-simplifier (constant folding), which mmdeploy's own
     optimize pass would normally do but which is skipped when its optional
     torchscript extension is not built. This folds away ~59 Constant/Cast/
     Expand/Gather/Reshape/Unsqueeze/Concat nodes, matching the reference (which
     has 0 Constant nodes / all-inline initializers).
  3. Extracting the subgraph [input] -> [/Concat_6_output_0, /Concat_3_output_0]
     with `onnx.utils.extract_model`, dropping the NMS tail. Extraction is the
     last step, reproducing the reference's `onnx.utils.extract_model` producer.

Requires the isolated `rtmdet` conda env (torch 2.0.1 CPU, mmengine 0.10.4,
mmcv 2.1.0, mmdet 3.3.0, mmdeploy 1.3.1, onnx 1.15.0, onnxruntime 1.16.3).
"""

import argparse
import os

import numpy as np
import onnx
import mmengine


def build_deploy_cfg(opset: int, height: int, width: int) -> mmengine.Config:
    """Self-contained equivalent of mmdeploy's
    configs/mmdet/detection/detection_onnxruntime_static.py (end2end)."""
    return mmengine.Config(
        dict(
            onnx_config=dict(
                type="onnx",
                export_params=True,
                keep_initializers_as_inputs=False,
                opset_version=opset,
                save_file="end2end.onnx",
                input_names=["input"],
                output_names=["dets", "labels"],
                # (W, H) -- static shape, matches the reference [1,3,640,640]
                input_shape=(width, height),
                optimize=True,
            ),
            codebase_config=dict(
                type="mmdet",
                task="ObjectDetection",
                model_type="end2end",
                post_processing=dict(
                    score_threshold=0.05,
                    confidence_threshold=0.005,
                    iou_threshold=0.5,
                    max_output_boxes_per_class=200,
                    pre_top_k=5000,
                    keep_top_k=100,
                    background_label_id=-1,
                ),
            ),
            backend_config=dict(type="onnxruntime"),
        )
    )


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--model-cfg", default="ckpt/rtmdet_m_8xb32-300e_coco.py")
    ap.add_argument(
        "--checkpoint",
        default="ckpt/rtmdet_m_8xb32-300e_coco_20220719_112220-229f527c.pth",
    )
    ap.add_argument("--height", type=int, default=640)
    ap.add_argument("--width", type=int, default=640)
    ap.add_argument("--opset", type=int, default=11)
    ap.add_argument("--work-dir", default="work_dir")
    ap.add_argument("--device", default="cpu")
    ap.add_argument(
        "--no-simplify", dest="simplify", action="store_false", default=True,
        help="Skip onnx-simplifier constant folding (leaves ~59 extra nodes).",
    )
    ap.add_argument(
        "--boxes-out", default="/Concat_6_output_0",
        help="ONNX tensor name of decoded xyxy boxes [1,8400,4] to cut at.",
    )
    ap.add_argument(
        "--scores-out", default="/Concat_3_output_0",
        help="ONNX tensor name of class scores [1,8400,80] to cut at.",
    )
    ap.add_argument(
        "--output",
        default=("rtmdet_m_8xb32-300e_coco_1x3x640x640"
                 "_without_smaller_tail.onnx"),
    )
    args = ap.parse_args()

    from mmdeploy.apis import torch2onnx

    deploy_cfg = build_deploy_cfg(args.opset, args.height, args.width)
    os.makedirs(args.work_dir, exist_ok=True)

    # mmdeploy uses the image only to trace preprocessing; a blank frame suffices.
    dummy_img = np.zeros((args.height, args.width, 3), dtype=np.uint8)

    print(f"[export] mmdeploy torch2onnx end2end opset={args.opset} "
          f"{args.width}x{args.height} device={args.device}")
    torch2onnx(
        dummy_img,
        args.work_dir,
        "end2end.onnx",
        deploy_cfg=deploy_cfg,
        model_cfg=args.model_cfg,
        model_checkpoint=args.checkpoint,
        device=args.device,
    )
    full = os.path.join(args.work_dir, "end2end.onnx")
    print(f"[full] {full}")

    # Constant-folding pass (mmdeploy's optimize equivalent).
    if args.simplify:
        from onnxsim import simplify
        model, ok = simplify(onnx.load(full))
        if not ok:
            raise SystemExit("[error] onnx-simplifier validation failed")
        full = os.path.join(args.work_dir, "end2end_sim.onnx")
        onnx.save(model, full)
        print(f"[simplify] -> {full} ({len(model.graph.node)} nodes)")

    # Sanity: confirm the requested cut tensors exist in the (simplified) graph.
    m = onnx.load(full, load_external_data=False)
    produced = {o for n in m.graph.node for o in n.output}
    for t in (args.boxes_out, args.scores_out):
        if t not in produced:
            cands = sorted(x for x in produced if "Concat" in x)
            raise SystemExit(
                f"[error] cut tensor '{t}' not found in full graph.\n"
                f"        Concat tensors present: {cands}")

    print(f"[extract] {full} -> {args.output}\n"
          f"          inputs=['input'] "
          f"outputs=['{args.boxes_out}', '{args.scores_out}']")
    onnx.utils.extract_model(
        full,
        args.output,
        input_names=["input"],
        output_names=[args.boxes_out, args.scores_out],
    )
    print("[done]")


if __name__ == "__main__":
    main()
