#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

import os
import argparse
import numpy as np
from tqdm import tqdm
import onnxruntime as ort
import time

import torch
from torchvision import transforms, datasets


# ----------------------------
# ImageNet Preprocessing
# ----------------------------
preprocess = transforms.Compose([
    transforms.Resize(256),
    transforms.CenterCrop(224),
    transforms.ToTensor(),
    transforms.Normalize(
        mean=[0.485, 0.456, 0.406],
        std=[0.229, 0.224, 0.225],
    ),
])

def topk_accuracy(output, target, topk=(1, 5)):
    maxk = max(topk)
    batch_size = target.size(0)
    _, pred = output.topk(maxk, dim=1, largest=True, sorted=True)
    pred = pred.t()
    correct = pred.eq(target.view(1, -1).expand_as(pred))
    res = []
    for k in topk:
        correct_k = correct[:k].reshape(-1).float().sum(0, keepdim=True)
        res.append(correct_k.mul_(100.0 / batch_size))
    return res


def create_session(target,model,cache_key,enable_ai_analyzer=False):
    """
    Map target to ONNX Runtime provider list
    """
    target = target.lower()
    if target == "cpu":
        session = ort.InferenceSession(model, providers=["CPUExecutionProvider"])
        return session
    elif target == "npu":
        provider_options_dict = {
            "config_file": 'vitisai_config.json',
            "cache_dir":   './',
            "cache_key":   cache_key,
            "ai_analyzer_visualization": enable_ai_analyzer,
            "ai_analyzer_profiling": enable_ai_analyzer,
            "target": 'VAIML',
        }
        session = ort.InferenceSession(
            model,
            providers=["VitisAIExecutionProvider"],
            provider_options=[provider_options_dict]
        )   
        return session
    else:
        raise ValueError(f"Unsupported target '{target}'. Expected 'cpu' or 'npu'.")

def main(args):
    # -------------------------------
    # Load ImageNet Validation Set
    # -------------------------------
    dataset = datasets.ImageFolder(args.data, preprocess)
    dataloader = torch.utils.data.DataLoader(
        dataset,
        batch_size=args.batch_size,
        shuffle=False,
        num_workers=4,
        pin_memory=False
    )

    # -------------------------------
    # Load ONNX model
    # -------------------------------
    print(f"[INFO] Using {args.target}")
    session = create_session(args.target,args.model,args.cache_key,args.enable_ai_analyzer)

    input_name = session.get_inputs()[0].name
    input_type = session.get_inputs()[0].type
    print(f"[INFO] Detected input name: {input_name}")

    top1_correct, top5_correct, total = 0, 0, 0
    total_infer_time = 0.0 
    total_images = 0
    end_2_end_start, end_2_end_end, end_2_end_total = 0.0, 0.0, 0.0

    # -------------------------------
    # Eval Loop
    # -------------------------------
    end_2_end_start=time.perf_counter()
    for images, labels in tqdm(dataloader, total=len(dataloader), disable=False, dynamic_ncols=True):
        if input_type == "tensor(float16)":
            np_images = images.numpy().astype(np.float16)
        else:
            np_images = images.numpy().astype(np.float32)
    
        # ---- inference timing begin ----
        start = time.perf_counter()
        outputs = session.run(None, {input_name: np_images})[0]
        end = time.perf_counter()
        # ---- inference timing end ----
    
        infer_time = end - start
        total_infer_time += infer_time
        total_images += images.size(0)
    
        if outputs.dtype == np.float16:
            outputs = outputs.astype(np.float32)
        outputs = torch.from_numpy(outputs)
        top1, top5 = topk_accuracy(outputs, labels, topk=(1, 5))
        top1_correct += top1.item() * images.size(0) / 100
        top5_correct += top5.item() * images.size(0) / 100
        total += images.size(0)
    end_2_end_end=time.perf_counter()
    end_2_end_total=end_2_end_end -  end_2_end_start
    
    # -------------------------------
    # Final Results
    # -------------------------------
    if total == 0:
        print("[WARN] No images were processed; the dataloader yielded no batches.")
        return

    top1_acc = top1_correct / total
    top5_acc = top5_correct / total
    fps = total_images / total_infer_time if total_infer_time > 0 else 0.0
    end_2_end_fps = total_images / end_2_end_total if end_2_end_total > 0 else 0.0

    print("\n===== Evaluation Result =====")
    print(f"Top-1 Accuracy: {top1_acc:.4f}")
    print(f"Top-5 Accuracy: {top5_acc:.4f}")
    print(f"Total Inference Time (pure): {total_infer_time:.4f} seconds")
    print(f"Inference FPS: {fps:.2f} images/second")
    print(f"Total Inference Time (end-2-end): {end_2_end_total:.4f} seconds")
    print(f"Inference FPS (end-2-end): {end_2_end_fps:.2f} images/second")
    print("===== End of Evaluation Result =====")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=str, required=True, help="onnx model path")
    parser.add_argument("--data", type=str, required=True, help="ImageNet val directory")
    parser.add_argument("--batch_size", type=int, default=1, help="Batch size")
    parser.add_argument("--target", type=str, default="cpu",
                        help="cpu | npu (default: cpu)")
    parser.add_argument("--cache_key", type=str, default="resnet50-v1-12_quantized", help="VitisAI EP cache key")
    parser.add_argument("--enable-ai-analyzer", dest="enable_ai_analyzer", action="store_true",
                        help="Enable AI Analyzer visualization and profiling for NPU runs (default: off)")
    args = parser.parse_args()
    main(args)
