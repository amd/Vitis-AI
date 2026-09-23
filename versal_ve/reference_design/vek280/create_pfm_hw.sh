#!/bin/bash
set -euo pipefail

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

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export VEK280_ROOT="${SCRIPT_DIR}"
VAISW_HOME="${VAISW_HOME:-$(cd "${SCRIPT_DIR}/../../.." && pwd)}"
export VAISW_HOME
PROJECT_NAME="${PROJECT_NAME:-example_design}"
export PROJECT_NAME

_hw_info() { printf '\033[0;34m[hw ]\033[0m %s\n' "$*"; }
_hw_ok()   { printf '\033[0;32m[hw ]\033[0m %s\n' "$*"; }
_hw_err()  { printf '\033[0;31m[hw ]\033[0m %s\n' "$*" >&2; }

# Step 1: NPU IP bitstream info
if [ -n "${NPU_IP:-}" ]; then
    rm -f "${SCRIPT_DIR}"/fpga_info_*.txt
    cp "${VAISW_HOME}/npu_ip/${NPU_IP}"/fpga_info_*.txt "${SCRIPT_DIR}/" \
        || { _hw_err "fpga_info copy failed for NPU_IP=${NPU_IP}"; return 1; }
    if [ -n "${NPU_IP2:-}" ]; then
        cp "${VAISW_HOME}/npu_ip/${NPU_IP2}"/fpga_info_*.txt "${SCRIPT_DIR}/" \
            || { _hw_err "fpga_info copy failed for NPU_IP2=${NPU_IP2}"; return 1; }
    fi
else
    _hw_err "NPU_IP not set — source npu_ip/settings.sh before running this stage"; return 1
fi

# Step 2: Vivado XSA (make xsa)
export HW_XSA="${SCRIPT_DIR}/hw/${PROJECT_NAME}_pfm.xsa"
if [ -f "${HW_XSA}" ]; then
    _hw_ok "HW_XSA already present — skipping Vivado build."
    _hw_info "  HW_XSA = ${HW_XSA}"
else
    _hw_info "Building platform XSA (Vivado)..."
    make -C "${SCRIPT_DIR}/hw" all || { _hw_err "Vivado XSA build failed"; return 1; }
    [ -f "${HW_XSA}" ] || { _hw_err "HW_XSA not found: ${HW_XSA}"; return 1; }
    _hw_ok "HW stage complete."
    _hw_info "  HW_XSA = ${HW_XSA}"
fi
