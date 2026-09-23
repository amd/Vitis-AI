#!/bin/bash

#
# Copyright (C) 2024, Advanced Micro Devices, Inc. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Description:
#   Top-level build orchestrator for VEK280 EDF 2026.1 image.
#
#   Stage order:
#     0. ResNet50 snapshot generation via docker
#     1. create_pfm_hw.sh   — Vivado XSA -> XPFM -> v++ link
#     2. create_pfm_sw.sh   — EDF/Yocto: device-tree, BOOT.BIN, WIC
#     3. create_vitis_app.sh — v++ -p (libadf) -> mem-patch xclbin -> bootgen BOOT.BIN
#     4. image_assemble.sh   — wic cp: BOOT.BIN + xclbin + cfg + snapshots -> OUTPUT_WIC
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Source NPU IP settings (sets NPU_IP, NPU_IP2, NB_DDRS, ENABLE_NPU_TAIL, NPU_TAIL)
NPU_SETTINGS="${SCRIPT_DIR}/../../../npu_ip/settings.sh"
[ -f "${NPU_SETTINGS}" ] && source "${NPU_SETTINGS}"

# Stage 0: ResNet50 TF2 snapshot generation (docker) — one per NPU target
# SKIP_SNAPSHOT: set non-empty (e.g. SKIP_SNAPSHOT=1) to bypass generation,
VITISAI_ROOT="${VITISAI_ROOT:-$(cd "${SCRIPT_DIR}/../../.." && pwd)}"
if [ -n "${SKIP_SNAPSHOT:-}" ]; then
    echo "[image] SKIP_SNAPSHOT set — skipping ResNet50 snapshot generation"
fi
for _npu_target in ${NPU_IP:-} ${NPU_IP2:-}; do
    [ -n "${SKIP_SNAPSHOT:-}" ] && continue
    [ -z "${_npu_target}" ] && continue
    _snapshot_dir="${SCRIPT_DIR}/snapshot.${_npu_target}.resnet50.TF"
    if [ -d "${_snapshot_dir}" ]; then
        echo "[image] ResNet50 snapshot already exists for ${_npu_target}: ${_snapshot_dir}"
        continue
    fi
    echo "[image] Generating ResNet50 TF2 snapshot for ${_npu_target} ..."
    (
        cd "${SCRIPT_DIR}/../.."
        ./docker/run.bash --acceptLicense -- /bin/bash -c \
            "source npu_ip/settings.sh ${_npu_target} && \
             cd examples/python_examples/batcher && \
             VAISW_SNAPSHOT_DIRECTORY=${_snapshot_dir} \
             ./run_classification.sh -f tensorflow2 -n resnet50 --batchSizePerCore 1"
    ) || { echo "[image] ERROR: ResNet50 snapshot generation failed for ${_npu_target}"; return 1; }
    echo "[image] ResNet50 snapshot done: ${_snapshot_dir}"
done

# Stage 1: Vivado XSA + Vitis XPFM + v++ link
source "${SCRIPT_DIR}/create_pfm_hw.sh" || return 1

# Stage 2: EDF/Yocto SW — device-tree (ZOCL_DTB), BOOT assets, WIC
source "${SCRIPT_DIR}/create_pfm_sw.sh" || return 1

# Stage 3: v++ -p with libadf -> mem-patched xclbin + EDF BOOT.BIN
source "${SCRIPT_DIR}/create_vitis_app.sh" || return 1

# Stage 4: Assemble — wic cp BOOT.BIN + xclbin + cfg + snapshots -> OUTPUT_WIC
source "${SCRIPT_DIR}/image_assemble.sh" || return 1
