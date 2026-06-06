#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

import os
import argparse
import numpy as np
from PIL import Image
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


def create_session(target,model):
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
            "cache_key":   args.cache_key,
            "ai_analyzer_visualization": True,
            "ai_analyzer_profiling": True,
            "target": 'VAIML',
        }
        session = ort.InferenceSession(
            model,
            providers=["VitisAIExecutionProvider"],
            provider_options=[provider_options_dict]
        )   
        return session
    else:
        session = ort.InferenceSession(model, providers=["CPUExecutionProvider"])
        return session
        
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
    session = create_session(args.target,args.model)

    input_name = session.get_inputs()[0].name
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
        input_type = session.get_inputs()[0].type
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
    print("\n===== Evaluation Result =====")
    print(f"Top-1 Accuracy: {100.0 * top1_correct / total:.2f}%")
    print(f"Top-5 Accuracy: {100.0 * top5_correct / total:.2f}%")
    fps = total_images / total_infer_time
    print(f"\nTotal Inference Time (pure): {total_infer_time:.4f} seconds")
    print(f"Inference FPS: {fps:.2f} images/second")
    end_2_end_fps = total_images / end_2_end_total
    print(f"\nTotal Inference Time (end-2-end): {end_2_end_total:.4f} seconds")
    print(f"Inference FPS (end-2-end): {end_2_end_fps:.2f} images/second")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=str, required=True, help="onnx model path")
    parser.add_argument("--data", type=str, required=True, help="ImageNet val directory")
    parser.add_argument("--batch_size", type=int, default=1, help="Batch size")
    parser.add_argument("--target", type=str, default="cpu",
                        help="cpu | npu (default: cpu)")
    parser.add_argument("--cache_key", type=str, default="resnet50-v1-12_quantized", help="onnx model path")
    args = parser.parse_args()
    main(args)
