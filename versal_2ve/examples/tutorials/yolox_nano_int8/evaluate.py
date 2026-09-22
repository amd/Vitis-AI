#!/usr/bin/env python3
"""
YOLOX-Nano ONNX COCO Evaluation Script

Evaluates an ONNX YOLOX-Nano model (onnx_model/yolox_nano.onnx) on the COCO
val2017 dataset and reports standard COCO mAP metrics (mAP@[.5:.95] and mAP@.5).

Reuses the preprocessing / postprocessing logic from preprocess.py and
postprocess.py to stay consistent with the rest of the pipeline.
"""

import argparse
import json
import os
import sys
import time

# Make locally-installed packages (e.g. pycocotools in ./pylibs) importable.
_PYLIBS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pylibs")
if os.path.isdir(_PYLIBS) and _PYLIBS not in sys.path:
    sys.path.insert(0, _PYLIBS)

import numpy as np
import onnxruntime as ort
from tqdm import tqdm

# NOTE: pycocotools is imported lazily inside compute_map() only. Its native
# extension (_mask) is architecture-specific and may be unavailable on the NPU
# board, so the inference/dump path must not depend on it.

from preprocess import preprocess_image
from postprocess import postprocess_predictions


COCO_ROOT = "../datasets/coco"


def _sigmoid(x):
    return 1.0 / (1.0 + np.exp(-x))


def to_postprocess_input(outputs):
    """Normalize raw ONNX outputs into the (1, 1, N, 85) layout that
    postprocess_predictions expects.

    Handles two export formats:
      * Decoded float model: single output (1, N, 85) with obj/cls already
        sigmoid-activated. Box channels are still in raw (grid) form.
      * QAT model: three raw head feature maps (1, 85, H, W) with obj/cls as
        logits. We transpose/reshape each head, apply sigmoid to obj/cls,
        and concatenate in stride order (8, 16, 32) to match the float layout.
    """
    if len(outputs) == 1 and outputs[0].ndim == 3:
        return np.asarray(outputs)

    # Raw multi-head format: sort heads by spatial size (largest first =
    # stride 8, 16, 32) so the concat order matches the grid generation in
    # custom_demo_postprocess.
    heads = sorted(outputs, key=lambda a: a.shape[-1], reverse=True)
    flat = []
    for h in heads:
        # h: (1, 85, H, W) -> (1, H*W, 85)
        b, c, hh, ww = h.shape
        f = h.transpose(0, 2, 3, 1).reshape(b, hh * ww, c).astype(np.float32)
        f[..., 4:] = _sigmoid(f[..., 4:])  # obj + class logits -> probs
        flat.append(f)
    preds = np.concatenate(flat, axis=1)  # (1, N, 85)
    return preds[np.newaxis, ...]         # (1, 1, N, 85)


def build_session(model_path, provider, target="cpu", cache_key="yolox_nano_onnx_pt"):
    """Create an ONNXRuntime inference session on the requested provider/target."""
    if target == "NPU":
        # Run inference on the NPU via the VitisAI execution provider
        # (mirrors run_infer.py).
        provider_options_dict = {
            "config_file": 'vitisai_config.json',
            "cache_dir":   './',
            "cache_key":   cache_key,
            "log_level":   'info',
            "ai_analyzer_visualization": True,
            "ai_analyzer_profiling": True,
            "target": 'VAIML'
        }
        session = ort.InferenceSession(
            model_path,
            providers=["VitisAIExecutionProvider"],
            provider_options=[provider_options_dict],
        )
        return session

    if provider == "cpu":
        providers = ["CPUExecutionProvider"]
    elif provider == "cuda":
        providers = ["CUDAExecutionProvider", "CPUExecutionProvider"]
    else:
        raise ValueError(f"Unknown provider: {provider}")

    so = ort.SessionOptions()
    so.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
    session = ort.InferenceSession(model_path, sess_options=so, providers=providers)
    return session


def output_dequant_params(model_path):
    """Return {graph_output_name: (scale, zero_point)} for every graph output
    that is produced directly by a QuantizeLinear node.

    The ..._Last_Dequant_bad_ofms_erased model drops the final DequantizeLinear
    on two of its three heads, so those outputs come out as quantized int8.
    These params let us compensate by dequantizing them at runtime
    (float = (q - zero_point) * scale).
    """
    import onnx
    from onnx import numpy_helper

    g = onnx.load(model_path).graph
    consts = {i.name: numpy_helper.to_array(i) for i in g.initializer}
    # Pick up any unfolded Constant nodes too.
    for n in g.node:
        if n.op_type == "Constant":
            for attr in n.attribute:
                if attr.name == "value":
                    consts[n.output[0]] = numpy_helper.to_array(attr.t)

    out_names = {o.name for o in g.output}
    params = {}
    for n in g.node:
        if n.op_type == "QuantizeLinear" and n.output[0] in out_names:
            scale = consts.get(n.input[1])
            zp = consts.get(n.input[2]) if len(n.input) > 2 else 0
            if scale is None:
                continue
            params[n.output[0]] = (float(np.asarray(scale).item()),
                                   float(np.asarray(zp).item()))
    return params


def _load_coco_meta(ann_file):
    """Read the COCO annotation JSON directly (no pycocotools) to obtain the
    image id list, image-id -> file_name map, and sorted category ids."""
    with open(ann_file) as f:
        data = json.load(f)
    img_ids = sorted(img["id"] for img in data["images"])
    id_to_file = {img["id"]: img["file_name"] for img in data["images"]}
    cat_ids = sorted(c["id"] for c in data["categories"])
    return img_ids, id_to_file, cat_ids


def run_inference_collect(model_path, ann_file, img_dir, input_shape,
                          conf_thresh, nms_thresh, provider="cpu", limit=None,
                          target="cpu", cache_key="yolox_nano_onnx_pt",
                          dequant_compensate=False):
    """Run inference over the dataset and collect COCO-format detections.

    Does NOT depend on pycocotools, so it can run on the NPU board.
    Returns (results, img_ids).

    If dequant_compensate is True, any graph output that comes out quantized
    (the erased final DequantizeLinear heads) is dequantized at runtime using
    the scale/zero_point stored in the model.
    """
    img_ids, id_to_file, cat_ids = _load_coco_meta(ann_file)
    if limit:
        img_ids = img_ids[:limit]

    # Map model class index (0..79) -> COCO category id
    class_to_cat = {i: cat_ids[i] for i in range(len(cat_ids))}

    session = build_session(model_path, provider, target, cache_key)
    input_name = session.get_inputs()[0].name
    output_names = [o.name for o in session.get_outputs()]

    dq_params = {}
    if dequant_compensate:
        dq_params = output_dequant_params(model_path)
        if dq_params:
            print("Dequant compensation enabled for outputs:")
            for name, (s, z) in dq_params.items():
                print(f"  {name}: scale={s}, zero_point={z}")
        else:
            print("Dequant compensation requested but no quantized outputs found.")

    results = []
    total_infer_ms = 0.0

    for img_id in tqdm(img_ids, desc="Evaluating"):
        img_path = os.path.join(img_dir, id_to_file[img_id])
        if not os.path.exists(img_path):
            continue

        pre = preprocess_image(img_path, input_shape)
        inp = pre["preprocessed_img"]
        ratio = pre["ratio"]

        t0 = time.time()
        outputs = session.run(output_names, {input_name: inp})
        total_infer_ms += (time.time() - t0) * 1000.0

        # Compensate for the erased final DequantizeLinear nodes: dequantize
        # any quantized output back to float before postprocessing.
        if dq_params:
            outputs = list(outputs)
            for i, name in enumerate(output_names):
                if name in dq_params:
                    s, z = dq_params[name]
                    outputs[i] = (outputs[i].astype(np.float32) - z) * s

        # Normalize to the (1, 1, N, 85) layout postprocess_predictions expects
        # (handles both the decoded float model and the raw-head QAT model).
        det = postprocess_predictions(
            to_postprocess_input(outputs), input_shape, ratio,
            conf_thresh=conf_thresh, nms_thresh=nms_thresh,
            class_agnostic=False,  # match YOLOX eval_onnx.py (class-aware NMS)
        )

        boxes = det["boxes"]
        scores = det["scores"]
        class_ids = det["class_ids"]
        if len(boxes) == 0:
            continue

        for box, score, cls in zip(boxes, scores, class_ids):
            x1, y1, x2, y2 = box
            results.append({
                "image_id": int(img_id),
                "category_id": class_to_cat[int(cls)],
                "bbox": [float(x1), float(y1),
                         float(x2 - x1), float(y2 - y1)],
                "score": float(score),
            })

    n = len(img_ids)
    print(f"\nProcessed {n} images, {len(results)} detections.")
    if n:
        print(f"Mean inference time: {total_infer_ms / n:.2f} ms/image")

    return results, img_ids


def compute_map(ann_file, results, img_ids):
    """Compute COCO mAP from collected detections (requires pycocotools)."""
    from pycocotools.coco import COCO
    from pycocotools.cocoeval import COCOeval

    if not results:
        print("No detections produced -- cannot compute mAP.")
        return

    coco = COCO(ann_file)
    coco_dt = coco.loadRes(results)
    coco_eval = COCOeval(coco, coco_dt, "bbox")
    coco_eval.params.imgIds = img_ids
    coco_eval.evaluate()
    coco_eval.accumulate()
    coco_eval.summarize()

    print(f"\nmAP@[.5:.95] = {coco_eval.stats[0]:.4f}")
    print(f"mAP@.5       = {coco_eval.stats[1]:.4f}")


def parse_args():
    p = argparse.ArgumentParser(description="Evaluate YOLOX-Nano ONNX on COCO val2017")
    p.add_argument("--model", default="onnx_model/yolox_nano.onnx",
                   help="Path to ONNX model")
    p.add_argument("--coco-root", default=COCO_ROOT,
                   help="COCO dataset root directory")
    p.add_argument("--ann-file", default=None,
                   help="Annotation json (default: <coco-root>/annotations/instances_val2017.json)")
    p.add_argument("--img-dir", default=None,
                   help="Image directory (default: <coco-root>/images/val2017)")
    p.add_argument("--input-shape", default="416,416",
                   help="Model input as 'height,width' (default: 416,416)")
    p.add_argument("--conf-thresh", type=float, default=0.01,
                   help="Confidence threshold for mAP eval (default: 0.01, "
                        "matches YOLOX exp.test_conf)")
    p.add_argument("--nms-thresh", type=float, default=0.65,
                   help="NMS IoU threshold (default: 0.65)")
    p.add_argument("--provider", choices=["cpu", "cuda"], default="cpu",
                   help="ONNXRuntime execution provider (default: cpu)")
    p.add_argument("--target", choices=["cpu", "NPU"], default="cpu",
                   help="Inference target. 'NPU' runs via VitisAIExecutionProvider (default: cpu)")
    p.add_argument("--cache_key", default="yolox_nano_onnx_pt",
                   help="VitisAI provider cache_key (used when --target=NPU)")
    p.add_argument("--limit", type=int, default=None,
                   help="Evaluate only the first N images (for quick checks)")
    p.add_argument("--dequant-compensate", action="store_true",
                   help="Dequantize quantized graph outputs at runtime "
                        "(compensates for the erased final DequantizeLinear "
                        "nodes in yolox_nano_qat_Last_Dequant_bad_ofms_erased.onnx).")
    p.add_argument("--dump-results", default=None,
                   help="Run inference only and save detections to this JSON file "
                        "(skips mAP). Use on the NPU board where pycocotools is unavailable.")
    p.add_argument("--from-results", default=None,
                   help="Skip inference and compute mAP from a previously saved "
                        "--dump-results JSON file (run on a host with pycocotools).")
    return p.parse_args()


def main():
    args = parse_args()

    ann_file = args.ann_file or os.path.join(
        args.coco_root, "annotations", "instances_val2017.json")
    img_dir = args.img_dir or os.path.join(args.coco_root, "images", "val2017")
    input_shape = tuple(int(x) for x in args.input_shape.split(","))

    # mAP-only path: load saved detections and compute mAP (needs pycocotools).
    if args.from_results:
        print(f"Loading detections from: {args.from_results}")
        with open(args.from_results) as f:
            saved = json.load(f)
        compute_map(ann_file, saved["results"], saved["img_ids"])
        return

    print(f"Model:       {args.model}")
    print(f"Annotations: {ann_file}")
    print(f"Images:      {img_dir}")
    print(f"Input shape: {input_shape}")
    print(f"Provider:    {args.provider}")
    print(f"Target:      {args.target}")
    if args.target == "NPU":
        print(f"cache_key:   {args.cache_key}")
    print(f"conf={args.conf_thresh}  nms={args.nms_thresh}\n")

    results, img_ids = run_inference_collect(
        args.model, ann_file, img_dir, input_shape,
        args.conf_thresh, args.nms_thresh, args.provider, args.limit,
        args.target, args.cache_key, args.dequant_compensate)

    if args.dump_results:
        with open(args.dump_results, "w") as f:
            json.dump({"img_ids": img_ids, "results": results}, f)
        print(f"\nSaved {len(results)} detections to {args.dump_results}")
        print("Compute mAP on a host with pycocotools via:")
        print(f"  python3 evaluate.py --from-results {args.dump_results}")
        return

    compute_map(ann_file, results, img_ids)


if __name__ == "__main__":
    main()
