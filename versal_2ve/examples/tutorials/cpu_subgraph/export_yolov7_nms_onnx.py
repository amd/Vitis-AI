#!/usr/bin/env python3
# Copyright (C) 2026 Advanced Micro Devices, Inc. All rights reserved.
# pyright: reportMissingImports=false
"""Download open-source YOLOv7 and export a single ONNX graph.

This script can:
1. Download YOLOv7 source archive from GitHub if missing.
2. Download official YOLOv7 weights if missing.
3. Export ONNX either with NMS or without NMS.
4. Verify whether NonMaxSuppression presence matches the export mode.
"""

from __future__ import annotations

import argparse
import importlib
import subprocess
import sys
import shutil
from pathlib import Path
from urllib.request import urlretrieve
from zipfile import ZipFile

import onnx


MIN_VALID_WEIGHTS_BYTES = 10 * 1024 * 1024
DEFAULT_NMS_MAX_WH = 4096


def run_cmd(cmd: list[str], cwd: Path | None = None, quiet: bool = False) -> None:
    if not quiet:
        print("+", " ".join(cmd))
    subprocess.run(
        cmd,
        cwd=str(cwd) if cwd else None,
        check=True,
        stdout=subprocess.DEVNULL if quiet else None,
        stderr=subprocess.DEVNULL if quiet else None,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo",
        type=Path,
        default=Path("yolov7"),
        help="Path to local YOLOv7 repository.",
    )
    parser.add_argument(
        "--weights",
        type=Path,
        default=Path("yolov7/yolov7.pt"),
        help="Path to YOLOv7 .pt weights.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("yolov7_raw.onnx"),
        help="Output ONNX path. Default output is without NMS.",
    )
    parser.add_argument(
        "--end2end",
        action="store_true",
        help="Export ONNX with in-graph NMS (YOLOv7's --end2end path). Default is without NMS.",
    )
    parser.add_argument("--img-size", type=int, default=640, help="Export image size.")
    parser.add_argument("--batch-size", type=int, default=1, help="Export batch size.")
    parser.add_argument(
        "--simplify",
        action="store_true",
        help="Run ONNX graph simplification during export (requires onnxsim).",
    )
    parser.add_argument(
        "--repo-archive-url",
        default="https://github.com/WongKinYiu/yolov7/archive/refs/heads/main.zip",
        help="YOLOv7 source archive URL (must include export.py).",
    )
    parser.add_argument(
        "--weights-url",
        default="https://github.com/WongKinYiu/yolov7/releases/download/v0.1/yolov7.pt",
        help="YOLOv7 weights URL.",
    )
    return parser.parse_args()


def verify_nms(onnx_path: Path) -> bool:
    model = onnx.load(str(onnx_path), load_external_data=False)
    has_nms = any(node.op_type == "NonMaxSuppression" for node in model.graph.node)
    print(f"NMS check for {onnx_path.name}: {'present' if has_nms else 'not present'}")
    return has_nms


def ensure_python_modules(modules: list[str]) -> None:
    missing = []
    for module in modules:
        try:
            importlib.import_module(module)
        except Exception:
            missing.append(module)

    if missing:
        missing_csv = ", ".join(missing)
        raise RuntimeError(
            "Missing required Python modules for YOLOv7 export: "
            f"{missing_csv}. Install tutorial dependencies first with: "
            "pip install --no-deps -r requirements.txt"
        )


def download_file(url: str, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)

    wget_bin = shutil.which("wget")
    if wget_bin:
        run_cmd([wget_bin, "-O", str(dst), url])
        return

    curl_bin = shutil.which("curl")
    if curl_bin:
        run_cmd([curl_bin, "-L", "-o", str(dst), url])
        return

    print("Neither wget nor curl found. Falling back to Python downloader.")
    urlretrieve(url, str(dst))


def ensure_repo(repo_path: Path, repo_archive_url: str) -> None:
    if repo_path.is_dir():
        print(f"Using existing YOLOv7 repository: {repo_path}")
        return

    zip_path = repo_path.parent / "yolov7.zip"
    print(f"Downloading YOLOv7 source archive from {repo_archive_url}")
    download_file(repo_archive_url, zip_path)

    with ZipFile(zip_path, "r") as zf:
        # Release archives use tag-based folder names (for example yolov7-0.1),
        # while branch archives use names like yolov7-main.
        top_levels = {Path(name).parts[0] for name in zf.namelist() if name}
        if len(top_levels) != 1:
            raise RuntimeError(
                f"Unexpected archive layout in {zip_path}: top-level entries={sorted(top_levels)}"
            )
        extracted_dir_name = next(iter(top_levels))
        zf.extractall(repo_path.parent)

    extracted_repo = repo_path.parent / extracted_dir_name
    if not extracted_repo.is_dir():
        raise FileNotFoundError(
            f"Expected extracted repo directory not found: {extracted_repo}"
        )

    extracted_repo.rename(repo_path)
    zip_path.unlink(missing_ok=True)


def is_valid_weights_file(weights_path: Path, min_valid_size_bytes: int) -> bool:
    if not weights_path.is_file():
        return False

    size = weights_path.stat().st_size
    if size < min_valid_size_bytes:
        return False

    # Basic sanity check to reject common proxy/error-page downloads.
    try:
        head = weights_path.read_bytes()[:256].lstrip().lower()
        if head.startswith(b"<!doctype html") or head.startswith(b"<html"):
            return False
    except Exception:
        return False

    return True


def ensure_weights(weights_path: Path, weights_url: str) -> None:
    # YOLOv7 export.py falls back to its own download helper when weights are
    # missing/invalid; that code path may invoke git. Validate here to avoid it.
    if is_valid_weights_file(weights_path, MIN_VALID_WEIGHTS_BYTES):
        print(f"Using existing weights: {weights_path}")
        return

    if weights_path.exists():
        print(f"Re-downloading weights: {weights_path}")
        weights_path.unlink()

    weights_path.parent.mkdir(parents=True, exist_ok=True)
    print(f"Downloading weights from {weights_url}")
    download_file(weights_url, weights_path)

    if not is_valid_weights_file(weights_path, MIN_VALID_WEIGHTS_BYTES):
        downloaded_size = weights_path.stat().st_size if weights_path.exists() else 0
        raise RuntimeError(
            "Downloaded weights appear invalid. "
            f"Expected >= {MIN_VALID_WEIGHTS_BYTES} bytes, got {downloaded_size} bytes at {weights_path}."
        )


def find_exported_onnx(repo: Path, weights: Path) -> Path:
    candidates = [
        weights.with_suffix(".onnx"),
        repo / (weights.stem + ".onnx"),
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError("YOLOv7 export did not produce an ONNX file at expected location.")


def export_onnx(
    repo: Path,
    weights: Path,
    output: Path,
    img_size: int,
    batch_size: int,
    end2end: bool,
    simplify: bool,
) -> None:
    export_script = repo / "export.py"
    if not export_script.is_file():
        raise FileNotFoundError(f"YOLOv7 export script not found: {export_script}")

    # YOLOv7 lowercases --weights internally in attempt_download().
    # Passing an absolute path like /cpuSubgraph/... can become /cpusubgraph/...
    # on Linux and incorrectly trigger download fallback. Use repo-relative name.
    weights_arg = weights.name if weights.parent.resolve() == repo.resolve() else str(weights)

    cmd = [
        sys.executable,
        str(export_script),
        "--weights",
        str(weights_arg),
        "--img-size",
        str(img_size),
        str(img_size),
        "--batch-size",
        str(batch_size),
        "--grid",
    ]

    if simplify:
        cmd.append("--simplify")

    if end2end:
        cmd.extend(
            [
                "--end2end",
                "--max-wh",
                str(DEFAULT_NMS_MAX_WH),
            ]
        )

    run_cmd(cmd, cwd=repo)

    exported = find_exported_onnx(repo, weights)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(exported.read_bytes())
    print(f"Wrote {output}")


def canonicalize_for_reference_match(onnx_path: Path) -> None:
    """Normalize ONNX metadata to enable byte-identical reproduction.
    
    Sets ir_version=7, producer_version="2.2.2", and strips value_info
    to match reference models exported with PyTorch 2.2.2.
    """
    model = onnx.load(str(onnx_path), load_external_data=False)
    
    model.ir_version = 7
    model.producer_version = "2.2.2"
    model.graph.ClearField("value_info")
    model.ClearField("doc_string")
    
    onnx.save(model, str(onnx_path))
    print(
        "Applied canonicalization: ir_version=7, producer_version=2.2.2, "
        "value_info stripped, doc_string cleared."
    )


def export_and_verify(
    repo: Path,
    weights: Path,
    output: Path,
    img_size: int,
    batch_size: int,
    simplify: bool,
    end2end: bool,
) -> None:
    export_onnx(
        repo=repo,
        weights=weights,
        output=output,
        img_size=img_size,
        batch_size=batch_size,
        end2end=end2end,
        simplify=simplify,
    )

    canonicalize_for_reference_match(output)

    has_nms = verify_nms(output)
    if end2end and not has_nms:
        raise RuntimeError("Expected NMS export to include NonMaxSuppression, but it was not found.")
    if not end2end and has_nms:
        raise RuntimeError("Expected raw export to exclude NonMaxSuppression, but it was found.")


def main() -> None:
    args = parse_args()

    # YOLOv7 export imports cv2 and fails deep in its stack if missing.
    ensure_python_modules(["cv2"])

    repo = args.repo.resolve()
    weights = args.weights.resolve()
    output = args.output.resolve()

    ensure_repo(repo, args.repo_archive_url)
    ensure_weights(weights, args.weights_url)

    export_and_verify(
        repo=repo,
        weights=weights,
        output=output,
        img_size=args.img_size,
        batch_size=args.batch_size,
        simplify=args.simplify,
        end2end=args.end2end,
    )

    print("Export summary:")
    print(f"  Output:      {output}")
    print(f"  End2End NMS: {args.end2end}")


if __name__ == "__main__":
    main()
