#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

"""Download the CIFAR-10 dataset and export the CIFAR-10 ResNet-50 to ONNX.

The dataset is fetched from https://www.cs.toronto.edu/~kriz/cifar.html and
extracted into ``data/``. The fine-tuned weights shipped with this tutorial
(``models/resnet_trained_for_cifar10.pt``) are loaded into the ResNet-50
architecture and exported to ``models/resnet_trained_for_cifar10.onnx``.

Pass ``--train`` (optionally with ``--num_epochs``) to fine-tune the model
yourself instead of using the provided weights.
"""

import argparse
import random
import sys
import tarfile
import urllib.request

import torch
import torch.nn as nn
import torchvision
from torchvision import transforms

from resnet_utils import get_directories, load_resnet_model

CIFAR10_PYTHON_URL = "https://www.cs.toronto.edu/~kriz/cifar-10-python.tar.gz"
CIFAR10_BINARY_URL = "https://www.cs.toronto.edu/~kriz/cifar-10-binary.tar.gz"


def get_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--num_epochs", type=int, default=0)
    parser.add_argument("--train", action="store_true")
    parser.add_argument(
        "--binary-data",
        action="store_true",
        help="also download the CIFAR-10 binary archive (used by C++ flows)",
    )
    return parser.parse_args()


def download_progress(block_num, block_size, total_size):
    downloaded = block_num * block_size
    if total_size > 0:
        percent = min(100.0, downloaded * 100.0 / total_size)
        sys.stdout.write(f"\r  {percent:5.1f}% ({downloaded // (1024 * 1024)} MB)")
        sys.stdout.flush()
        if downloaded >= total_size:
            sys.stdout.write("\n")


def download_data(data_dir, binary_data=False):
    cifar_extracted = data_dir / "cifar-10-batches-py"
    if cifar_extracted.exists():
        print("CIFAR-10 data already exists, skipping download.")
        return

    archives = [(CIFAR10_PYTHON_URL, data_dir / "cifar-10-python.tar.gz")]
    if binary_data:
        archives.append((CIFAR10_BINARY_URL, data_dir / "cifar-10-binary.tar.gz"))

    for url, path in archives:
        if path.exists() and not tarfile.is_tarfile(path):
            print(f"{path.name} is corrupted, re-downloading...")
            path.unlink()
        if not path.exists():
            print(f"Downloading {path.name}...")
            urllib.request.urlretrieve(url, path, reporthook=download_progress)
        print(f"Extracting {path.name}...")
        with tarfile.open(path) as f:
            f.extractall(data_dir, filter="data")


# For updating learning rate
def update_lr(optimizer, lr):
    for param_group in optimizer.param_groups:
        param_group["lr"] = lr


def train_model(num_epochs, models_dir, data_dir):
    # seed everything to 0
    random.seed(0)
    torch.manual_seed(0)
    torch.cuda.manual_seed(0)

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    learning_rate = 0.001

    # Image preprocessing modules
    transform = transforms.Compose(
        [
            transforms.Pad(4),
            transforms.RandomHorizontalFlip(),
            transforms.RandomCrop(32),
            transforms.ToTensor(),
        ]
    )

    # CIFAR-10 dataset
    train_dataset = torchvision.datasets.CIFAR10(
        root=str(data_dir), train=True, transform=transform, download=False
    )
    test_dataset = torchvision.datasets.CIFAR10(
        root=str(data_dir), train=False, transform=transforms.ToTensor(), download=False
    )

    # Data loader
    train_loader = torch.utils.data.DataLoader(
        dataset=train_dataset, batch_size=100, shuffle=True
    )
    test_loader = torch.utils.data.DataLoader(
        dataset=test_dataset, batch_size=100, shuffle=False
    )

    model = load_resnet_model().to(device)

    # Loss and optimizer
    criterion = nn.CrossEntropyLoss()
    optimizer = torch.optim.Adam(model.parameters(), lr=learning_rate)

    # Train the model
    total_step = len(train_loader)
    curr_lr = learning_rate
    for epoch in range(num_epochs):
        for i, (images, labels) in enumerate(train_loader):
            images = images.to(device)
            labels = labels.to(device)
            # Forward pass
            outputs = model(images)
            loss = criterion(outputs, labels)
            # Backward and optimize
            optimizer.zero_grad()
            loss.backward()
            optimizer.step()
            if (i + 1) % 100 == 0:
                print(
                    "Epoch [{}/{}], Step [{}/{}] Loss: {:.4f}".format(
                        epoch + 1, num_epochs, i + 1, total_step, loss.item()
                    )
                )
        # Decay learning rate
        if (epoch + 1) % 20 == 0:
            curr_lr /= 3
            update_lr(optimizer, curr_lr)

    # Test the model
    model.eval()
    if num_epochs:
        with torch.no_grad():
            correct = 0
            total = 0
            for images, labels in test_loader:
                images = images.to(device)
                labels = labels.to(device)
                outputs = model(images)
                _, predicted = torch.max(outputs.data, 1)
                total += labels.size(0)
                correct += (predicted == labels).sum().item()
            print(
                "Accuracy of the model on the test images: {} %".format(
                    100 * correct / total
                )
            )

    # Save the model weights
    model.to("cpu")
    torch.save(model.state_dict(), str(models_dir / "resnet_trained_for_cifar10.pt"))
    return model


def load_trained_model(models_dir):
    model = load_resnet_model()
    weights = models_dir / "resnet_trained_for_cifar10.pt"
    # weights_only=True: the checkpoint is a plain state_dict, no pickled code.
    model.load_state_dict(torch.load(str(weights), map_location="cpu", weights_only=True))
    return model


def export_to_onnx(model, models_dir):
    model.to("cpu")
    model.eval()
    dummy_inputs = torch.randn(1, 3, 32, 32)
    onnx_model_path = str(models_dir / "resnet_trained_for_cifar10.onnx")
    torch.onnx.export(
        model,
        dummy_inputs,
        onnx_model_path,
        export_params=True,
        opset_version=17,
        input_names=["input"],
        output_names=["output"],
        dynamic_axes={"input": {0: "batch_size"}, "output": {0: "batch_size"}},
    )
    return onnx_model_path


def main():
    _, models_dir, data_dir, _ = get_directories()
    args = get_args()

    download_data(data_dir, args.binary_data)

    if args.train:
        model = train_model(args.num_epochs, models_dir, data_dir)
    else:
        model = load_trained_model(models_dir)

    onnx_model_path = export_to_onnx(model, models_dir)
    print(f"Model exported successfully to: {onnx_model_path}")


if __name__ == "__main__":
    main()
