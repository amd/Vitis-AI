#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

from pathlib import Path


def get_directories():
    current_dir = Path(__file__).resolve().parent

    # models directory for resnet sample
    models_dir = current_dir / "models"
    models_dir.mkdir(parents=True, exist_ok=True)

    # data directory for resnet sample
    data_dir = current_dir / "data"
    data_dir.mkdir(parents=True, exist_ok=True)

    # cache directory for resnet sample
    cache_dir = current_dir / "my_cache_dir"
    cache_dir.mkdir(parents=True, exist_ok=True)

    return current_dir, models_dir, data_dir, cache_dir


def load_resnet_model():
    """ResNet-50 backbone with the 10-class CIFAR-10 head."""
    import torch
    from torchvision.models import resnet50

    resnet = resnet50(weights=None)
    resnet.fc = torch.nn.Sequential(
        torch.nn.Linear(2048, 64), torch.nn.ReLU(inplace=True), torch.nn.Linear(64, 10)
    )
    return resnet
