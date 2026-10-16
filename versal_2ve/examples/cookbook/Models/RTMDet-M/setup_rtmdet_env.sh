#Copyright (C) 2026, Advanced Micro Devices, Inc. All rights reserved.
#SPDX-License-Identifier: MIT

#!/usr/bin/env bash
# =============================================================================
# setup_rtmdet_env.sh
#
# Recreate the isolated `rtmdet` conda environment used by export_rtmdet_m.py
# to export RTMDet-M (mmdetection) to ONNX via mmdeploy.
#
# Reproduces (CPU-only torch build):
#   python 3.11  (reference env was 3.12.3; see PY_VERSION note below)
#   torch 2.0.1+cpu / torchvision 0.15.2+cpu
#   mmengine 0.10.4 / mmcv 2.1.0 (ops-less) / mmdet 3.3.0 / mmdeploy 1.3.1
#   onnx 1.15.0 / onnxruntime 1.16.3 / onnxsim 0.7.0
#
# Usage:
#   bash setup_rtmdet_env.sh                # create at default prefix
#   CONDA_ROOT=/path/to/miniconda3 bash setup_rtmdet_env.sh
#   ENV_PREFIX=/path/to/envs/rtmdet bash setup_rtmdet_env.sh
#   FORCE=1 bash setup_rtmdet_env.sh        # remove & recreate if it exists
# =============================================================================
set -euo pipefail

# ---- Configurable knobs (override via environment) --------------------------
# Auto-detect the conda install root from $CONDA_EXE or `conda` on PATH,
# deriving <root> from <root>/bin/conda. Falls back to a known default when
# conda is not on PATH (common on compute nodes). Override with CONDA_ROOT=...
if [[ -z "${CONDA_ROOT:-}" ]]; then
    _conda_exe="${CONDA_EXE:-$(command -v conda || true)}"
    if [[ -n "${_conda_exe}" ]]; then
        # <root>/bin/conda -> <root> (no readlink, keep the path conda was found at)
        CONDA_ROOT="$(dirname "$(dirname "${_conda_exe}")")"
    else
        echo "[error] conda not found on PATH. Set CONDA_ROOT to your miniconda/anaconda install." >&2
        exit 1
    fi
fi
ENV_PREFIX="${ENV_PREFIX:-${CONDA_ROOT}/envs/rtmdet}"
# NOTE: pinned to Python 3.11 (not the reference env's 3.12.3): torch 2.0.1+cpu
# ships no cp312 wheel on the PyTorch index, so 3.12 + torch 2.0.1 is not
# pip-reproducible. torch version is kept at 2.0.1 (it drives ONNX tracing /
# bit-exactness); Python is dropped to 3.11 where 2.0.1+cpu wheels exist.
PY_VERSION="${PY_VERSION:-3.11}"
FORCE="${FORCE:-0}"

# Pinned versions (match the reference environment exactly)
TORCH_VER="2.0.1"
TV_VER="0.15.2"
TORCH_CPU_INDEX="https://download.pytorch.org/whl/cpu"

MMENGINE_VER="0.10.4"
MMCV_VER="2.1.0"
MMDET_VER="3.3.0"
MMDEPLOY_VER="1.3.1"

ONNX_VER="1.15.0"
ORT_VER="1.16.3"
ONNXSIM_VER="0.7.0"

echo "=============================================================="
echo " rtmdet env setup"
echo "   conda root : ${CONDA_ROOT}"
echo "   env prefix : ${ENV_PREFIX}"
echo "   python     : ${PY_VERSION}"
echo "=============================================================="

# ---- Locate conda -----------------------------------------------------------
if [[ ! -x "${CONDA_ROOT}/bin/conda" ]]; then
    echo "[error] conda not found at ${CONDA_ROOT}/bin/conda" >&2
    echo "        Set CONDA_ROOT to your miniconda/anaconda install." >&2
    exit 1
fi
# shellcheck disable=SC1091
source "${CONDA_ROOT}/etc/profile.d/conda.sh"

# ---- Create env -------------------------------------------------------------
if [[ -d "${ENV_PREFIX}" ]]; then
    if [[ "${FORCE}" == "1" ]]; then
        echo "[env] FORCE=1 -> removing existing ${ENV_PREFIX}"
        conda env remove -p "${ENV_PREFIX}" -y
    else
        echo "[env] ${ENV_PREFIX} already exists (set FORCE=1 to recreate)."
        echo "      Continuing into it and (re)installing pinned packages."
    fi
fi

if [[ ! -d "${ENV_PREFIX}" ]]; then
    echo "[env] creating python=${PY_VERSION} at ${ENV_PREFIX}"
    conda create -p "${ENV_PREFIX}" "python=${PY_VERSION}" -y
fi

conda activate "${ENV_PREFIX}"
PY="${ENV_PREFIX}/bin/python"

echo "[pip] upgrading pip/setuptools/wheel"
"${PY}" -m pip install --upgrade pip setuptools wheel

# ---- 1. PyTorch (CPU) -------------------------------------------------------
echo "[torch] installing torch==${TORCH_VER}+cpu torchvision==${TV_VER}+cpu"
"${PY}" -m pip install \
    "torch==${TORCH_VER}" "torchvision==${TV_VER}" \
    --index-url "${TORCH_CPU_INDEX}"

# ---- 2. OpenCV (headless) ---------------------------------------------------
# Install headless OpenCV before mmdet to prevent GUI opencv-python from being
# pulled in as a transitive dependency.
echo "[opencv] installing opencv-python-headless"
"${PY}" -m pip install opencv-python-headless

# ---- 3. OpenMMLab stack via openmim -----------------------------------------
# mim resolves the correct mmcv build for this torch/python combo. mmcv here is
# ops-less (no compiled _ext) which is sufficient for CPU ONNX export.
echo "[mim] installing openmim + mmengine/mmcv/mmdet"
"${PY}" -m pip install "openmim==0.3.9"
"${ENV_PREFIX}/bin/mim" install \
    "mmengine==${MMENGINE_VER}" \
    "mmcv==${MMCV_VER}" \
    "mmdet==${MMDET_VER}"

# ---- 4. mmdeploy ------------------------------------------------------------
echo "[mmdeploy] installing mmdeploy==${MMDEPLOY_VER}"
"${PY}" -m pip install "mmdeploy==${MMDEPLOY_VER}"

# ---- 5. ONNX toolchain ------------------------------------------------------
echo "[onnx] installing onnx/onnxruntime/onnxsim"
"${PY}" -m pip install \
    "onnx==${ONNX_VER}" \
    "onnxruntime==${ORT_VER}" \
    "onnxsim==${ONNXSIM_VER}"

# numpy pin (mmdet/torch 2.0.1 need numpy<2)
"${PY}" -m pip install "numpy==1.26.4"

# ---- 6. Verify --------------------------------------------------------------
echo "[verify] importing the full toolchain ..."
"${PY}" - <<'PYEOF'
import importlib
mods = [
    ("torch", "torch"),
    ("torchvision", "torchvision"),
    ("mmengine", "mmengine"),
    ("mmcv", "mmcv"),
    ("mmdet", "mmdet"),
    ("mmdeploy", "mmdeploy"),
    ("onnx", "onnx"),
    ("onnxruntime", "onnxruntime"),
    ("onnxsim", "onnxsim"),
    ("numpy", "numpy"),
]
print(f"{'package':14s} version")
print("-" * 30)
ok = True
for name, mod in mods:
    try:
        m = importlib.import_module(mod)
        print(f"{name:14s} {getattr(m, '__version__', '?')}")
    except Exception as e:  # noqa: BLE001
        ok = False
        print(f"{name:14s} FAILED: {e}")
if not ok:
    raise SystemExit("[verify] one or more imports failed")
print("[verify] OK")
PYEOF

echo "=============================================================="
echo " Done. Activate with:"
echo "   source ${CONDA_ROOT}/etc/profile.d/conda.sh"
echo "   conda activate ${ENV_PREFIX}"
echo ""
echo " Then export RTMDet-M:"
echo "   cd $(dirname "$(readlink -f "$0")")"
echo "   python export_rtmdet_m.py"
echo "=============================================================="
