#!/usr/bin/env bash
# ===========================================================
# Copyright 2026 Advanced Micro Devices Inc.
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
# ===========================================================
###############################################################################
# create_vitis_app.sh  (VEK280 EDF flow — Vitis 2026.1)
#
# Single build stage — orchestrates full Vitis workflow (make all):
#  1. check inputs (HW_XSA, ZOCL_DTB, EDF_UBOOT, EDF_ATF)
#  2. v++ link (HW_XSA -> LINK_XSA) + v++ package (LINK_XSA -> XCLBIN, BOOT.BIN)
#  3. validate artifacts (LINK_XSA, XCLBIN, BOOT.BIN)
#
# Requires (from create_pfm_hw.sh):
#   HW_XSA   — Vivado extensible XSA
# Requires (from create_pfm_sw.sh or env):
#   ZOCL_DTB, EDF_UBOOT, EDF_ATF
#
# Exports: LINK_XSA, XCLBIN, BOOT_BIN
###############################################################################

VEK280_ROOT="${VEK280_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}"  # run only in script directory
VITIS_SETTINGS="${VITIS_SETTINGS:-}"

PROJECT_NAME="${PROJECT_NAME:-example_design}"
VITIS_PRJ="${VEK280_ROOT}/vitis_prj"
PACKAGE_DIR="${VITIS_PRJ}/package"
LINK_DIR="${VITIS_PRJ}/link"
LINK_XSA="${LINK_XSA:-${LINK_DIR}/${PROJECT_NAME}_link.xsa}"
HW_XSA="${HW_XSA:-${VEK280_ROOT}/hw/${PROJECT_NAME}_pfm.xsa}"
XCLBIN="${PACKAGE_DIR}/hw_outputs/x_plus_ml.xclbin"
BOOT_BIN="${PACKAGE_DIR}/hw_outputs/BOOT.BIN"

_va_info() { printf '\033[0;34m[app]\033[0m %s\n' "$*"; }
_va_ok()   { printf '\033[0;32m[app]\033[0m %s\n' "$*"; }
_va_warn() { printf '\033[1;33m[app]\033[0m %s\n' "$*"; }
_va_err()  { printf '\033[0;31m[app]\033[0m %s\n' "$*" >&2; }

_va_check_inputs() {
    if ! command -v v++ >/dev/null 2>&1 || ! command -v xclbinutil >/dev/null 2>&1; then
        if [ -n "${VITIS_SETTINGS}" ]; then
            [ -f "${VITIS_SETTINGS}" ] || { _va_err "Vitis settings not found: ${VITIS_SETTINGS}"; return 1; }
            source "${VITIS_SETTINGS}"
        fi
    fi
    command -v v++ >/dev/null 2>&1        || { _va_err "v++ not on PATH — source <Vitis>/settings64.sh"; return 1; }
    command -v xclbinutil >/dev/null 2>&1 || { _va_err "xclbinutil not on PATH — source <Vitis>/settings64.sh"; return 1; }

    [ -f "${HW_XSA}" ] || { _va_err "HW_XSA missing: ${HW_XSA} — run create_pfm_hw.sh first"; return 1; }

    : "${ZOCL_DTB:?ZOCL_DTB not set — run create_pfm_sw.sh first}"
    : "${EDF_UBOOT:?EDF_UBOOT not set — run create_pfm_sw.sh first}"
    : "${EDF_ATF:?EDF_ATF not set — run create_pfm_sw.sh first}"
    [ -f "${ZOCL_DTB}" ]  || { _va_err "ZOCL_DTB missing: ${ZOCL_DTB}"; return 1; }
    [ -f "${EDF_UBOOT}" ] || { _va_err "EDF_UBOOT missing: ${EDF_UBOOT}"; return 1; }
    [ -f "${EDF_ATF}" ]   || { _va_err "EDF_ATF missing: ${EDF_ATF}"; return 1; }
    _va_ok "inputs OK (HW_XSA, ZOCL_DTB, EDF boot assets)"
}


# Build stage: v++ link + v++ package (make all)
_va_build_all() {
    _va_info "Running make all (v++ link + EDF package) ..."
    make -C "${VITIS_PRJ}" all \
        ZOCL_DTB="${ZOCL_DTB}" \
        EDF_UBOOT="${EDF_UBOOT}" \
        EDF_ATF="${EDF_ATF}" \
        || { _va_err "make all failed"; return 1; }
    _va_ok "make all completed successfully"
}

# Verify all artifacts exist
_va_validate_artifacts() {
    _va_info "Validating build artifacts ..."
    [ -f "${LINK_XSA}" ] && _va_ok " Linked XSA = ${LINK_XSA}" || { _va_err "Linked XSA not found: ${LINK_XSA}"; return 1; }
    [ -f "${XCLBIN}" ]   && _va_ok " XCLBIN     = ${XCLBIN}"   || { _va_err "XCLBIN not found: ${XCLBIN}"; return 1; }
    [ -f "${BOOT_BIN}" ] && _va_ok " BOOT.BIN   = ${BOOT_BIN}" || { _va_err "BOOT.BIN not found: ${BOOT_BIN}"; return 1; }
}

###############################################################################
main_app() {
    _va_check_inputs      || return 1
    _va_build_all         || return 1
    _va_validate_artifacts || return 1

    export LINK_XSA XCLBIN BOOT_BIN
    _va_ok "Vitis app stage complete."
}

main_app
