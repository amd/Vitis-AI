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
set -euo pipefail
###############################################################################
# create_assemble.sh  (VEK280 EDF flow — assemble stage)
#
# Copies Vitis HW outputs and reference data onto the EDF WIC image:
#   BOOT.BIN        -> WIC p1 (FAT32 /boot)   replaces EDF xilinx-bootbin one
#   x_plus_ml.xclbin -> WIC p1                loaded by x_plus_ml_app at runtime
#   image_processing.cfg -> WIC p1            VVAS kernel config (if img_proc enabled)
#   resnet50 snapshots -> WIC p1/snapshot.*   one per NPU_IP / NPU_IP2
#
# Expects (from prior stages):
#   EDF_WIC    — path to EDF WIC image (from create_pfm_sw.sh)
#   BOOT_BIN   — path to Vitis-assembled BOOT.BIN (from create_vitis_app.sh)
#   XCLBIN     — path to patched x_plus_ml.xclbin (from create_vitis_app.sh)
#   NPU_IP     — primary NPU IP name (from npu_ip/settings.sh)
#   NPU_IP2    — secondary NPU IP (optional)
#
# Output: ${OUTPUT_WIC}  (default: beside EDF_WIC as *_vitis_assembled.wic)
# Sourced by create_image.sh as the final stage.
###############################################################################

VEK280_ROOT="${VEK280_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}"  # run only in script directory

_as_info() { printf '\033[0;34m[asm]\033[0m %s\n' "$*"; }
_as_ok()   { printf '\033[0;32m[asm]\033[0m %s\n' "$*"; }
_as_warn() { printf '\033[1;33m[asm]\033[0m %s\n' "$*"; }
_as_err()  { printf '\033[0;31m[asm]\033[0m %s\n' "$*" >&2; }

_as_check_inputs() {
    : "${EDF_WIC:?EDF_WIC not set — run create_pfm_sw.sh first}"
    : "${BOOT_BIN:?BOOT_BIN not set — run create_vitis_app.sh first}"
    : "${XCLBIN:?XCLBIN not set — run create_vitis_app.sh first}"
    [ -f "${EDF_WIC}" ]  || { _as_err "EDF_WIC not found: ${EDF_WIC}"; return 1; }
    [ -f "${BOOT_BIN}" ] || { _as_err "BOOT_BIN not found: ${BOOT_BIN}"; return 1; }
    [ -f "${XCLBIN}" ]   || { _as_err "XCLBIN not found: ${XCLBIN}"; return 1; }
    command -v bmaptool >/dev/null 2>&1 || {
        _as_err "bmaptool not on PATH — run sw/setup_yocto_host.sh first"; return 1; }
    command -v wic >/dev/null 2>&1 || {
        _as_err "wic not on PATH — source EDF build env (edf-init-build-env) first"; return 1; }
    _as_ok "inputs OK"
}

# Stage src under a per-PID tmpdir so wic cp writes the exact filename.
# wic cp preserves the source basename — this is critical for BOOT.BIN (8.3 literal).
_as_wic_cp() {
    local src="$1" wic="$2" part="$3" destname="${4:-$(basename "$1")}"
    local tmpdir; tmpdir="$(mktemp -d /tmp/vek280-asm.XXXXXX)"
    cp "${src}" "${tmpdir}/${destname}"
    wic cp "${tmpdir}/${destname}" "${wic}:${part}"
    rm -rf "${tmpdir}"
}

# version.txt — provenance record (git hash, NPU IP(s), snapshot timestamp(s), tail).
_as_version_txt() {
    local vt; vt="$(mktemp -d /tmp/vek280-ver.XXXXXX)/version.txt"
    echo "hash=$(git -C "${VEK280_ROOT}" rev-parse HEAD 2>/dev/null)"     > "${vt}"
    echo "NPU_IP=${NPU_IP:-}"                                            >> "${vt}"
    echo "NPU_TIMESTAMP=${VAISW_SNAPSHOT_TIMESTAMP:-}"                   >> "${vt}"
    [ -z "${NPU_IP2:-}" ] || echo "NPU_IP2=${NPU_IP2}"                   >> "${vt}"
    [ -z "${NPU_IP2:-}" ] || echo "NPU_TIMESTAMP2=${VAISW_SNAPSHOT_TIMESTAMP2:-}" >> "${vt}"
    [ -z "${NPU_TAIL:-}" ] || echo "NPU_TAIL=${NPU_TAIL}"               >> "${vt}"
    _as_info "wic cp version.txt -> p1"
    _as_wic_cp "${vt}" "${OUTPUT_WIC}" 1 "version.txt"
    _as_ok "version.txt injected"
    VERSION_TXT="${vt}"; export VERSION_TXT   # reused by the output/ collection (Issue D+F)
}

_as_assemble() {
    # Work on a copy — never mutate the bitbake deploy WIC directly.
    _npu_suffix="${NPU_IP:-unknown}"
    [ -z "${NPU_IP2:-}" ] || _npu_suffix="${_npu_suffix}__${NPU_IP2#VE2802_NPU_IP_}"
    [ -z "${NPU_TAIL:-}" ] || _npu_suffix="${_npu_suffix}__${NPU_TAIL#NPU_TAIL_}"
    OUTPUT_WIC="${OUTPUT_WIC:-$(dirname "${EDF_WIC}")/${_npu_suffix}_sd_card.wic}"

    _as_info "Copying EDF WIC to output: $(basename "${OUTPUT_WIC}")"
    cp "${EDF_WIC}" "${OUTPUT_WIC}"

    # BOOT.BIN -> p1 (FAT32 boot, replaces EDF-built one)
    _as_info "wic cp BOOT.BIN -> p1"
    _as_wic_cp "${BOOT_BIN}" "${OUTPUT_WIC}" 1 "BOOT.BIN"
    _as_ok "BOOT.BIN injected ($(du -h "${BOOT_BIN}" | awk '{print $1}'))"

    # x_plus_ml.xclbin -> p1
    _as_info "wic cp x_plus_ml.xclbin -> p1"
    _as_wic_cp "${XCLBIN}" "${OUTPUT_WIC}" 1 "x_plus_ml.xclbin"
    _as_ok "xclbin injected ($(du -h "${XCLBIN}" | awk '{print $1}'))"

    # image_processing.cfg -> p1 (only when image_processing IP is enabled)
    IMG_PROC_CFG="${VEK280_ROOT}/vitis_prj/kernels/image_processing/image_processing.cfg"
    local _dis; _dis="$(echo "${DISABLE_IMG_PROCE:-False}" | tr '[:upper:]' '[:lower:]')"
    if [[ "${_dis}" == "false" || "${_dis}" == "0" ]] && [ -f "${IMG_PROC_CFG}" ]; then
        _as_info "wic cp image_processing.cfg -> p1"
        _as_wic_cp "${IMG_PROC_CFG}" "${OUTPUT_WIC}" 1 "image_processing.cfg"
        _as_ok "image_processing.cfg injected"
    else
        _as_info "image_processing disabled or cfg missing — skipping"
    fi

    # resnet50 TF2 snapshots -> p1 (one directory per NPU target)
    local _npu
    for _npu in ${NPU_IP:-} ${NPU_IP2:-}; do
        [ -z "${_npu}" ] && continue
        local snap_dir="${VEK280_ROOT}/snapshot.${_npu}.resnet50.TF"
        if [ -d "${snap_dir}" ]; then
            _as_info "wic cp snapshot.${_npu}.resnet50.TF -> p1"
            # wic cp copies a directory recursively when src ends with /
            wic cp "${snap_dir}" "${OUTPUT_WIC}:1"
            _as_ok "resnet50 snapshot (${_npu}) injected"
        else
            _as_warn "resnet50 snapshot not found for ${_npu}: ${snap_dir} — skipping"
        fi
    done

    # version.txt provenance onto the boot partition
    _as_version_txt

    _as_ok "Assemble complete: ${OUTPUT_WIC}"
    _as_info "  Size: $(du -h "${OUTPUT_WIC}" | awk '{print $1}')"
    _as_info "  FAT32 contents:"
    wic ls "${OUTPUT_WIC}:1" 2>/dev/null || true
    export OUTPUT_WIC
}

# Collect deliverables into vek280/output/ and report to user.
# Includes BOOT.BIN, xclbin, version.txt, and optionally SDK tarball.
_as_collect_output() {
    local out="${VEK280_ROOT}/output"
    local hw="${VEK280_ROOT}/vitis_prj/package/hw_outputs"
    local deploy_dir; deploy_dir="$(dirname "${EDF_WIC}")"
    local img_proc_cfg="${VEK280_ROOT}/vitis_prj/kernels/image_processing/image_processing.cfg"
    _as_info "Collecting deliverables -> ${out}"
    rm -rf "${out}"; mkdir -p "${out}" "${out}/sd_card"

    # output/ top-level: WIC + utilization reports + sdk
    [ -f "${OUTPUT_WIC}" ]       && cp -f "${OUTPUT_WIC}"    "${out}/"
    [ -f "${OUTPUT_WIC}" ]       && bmaptool create "${OUTPUT_WIC}" -o "${out}/$(basename "${OUTPUT_WIC}").bmap"
    cp -f "${hw}"/npu_*_utilization.rpt "${out}/" 2>/dev/null || true
    cp -fr "${hw}"/reports "${out}/" 2>/dev/null || true
    [ -n "${SDK_SH:-}" ] && [ -f "${SDK_SH}" ] && cp -f "${SDK_SH}" "${out}/sdk.sh"

    # vart_x wheel: vart.bb do_deploy lands it in deploy/images/<machine>/ = ${deploy_dir}.
    if ls "${deploy_dir}"/vart_x*.whl >/dev/null 2>&1; then
        cp -f "${deploy_dir}"/vart_x*.whl "${out}/"
    else
        _as_warn "no vart_x wheel in ${deploy_dir} — omitted from deliverables"
    fi

    # sd_card/: boot artifacts
    [ -d "${hw}" ]               && cp -f "${hw}/BOOT.BIN"           "${out}/sd_card/" 2>/dev/null || true
    [ -d "${hw}" ]               && cp -f "${hw}/x_plus_ml.xclbin"   "${out}/sd_card/" 2>/dev/null || true
    [ -d "${hw}" ]               && cp -f "${hw}/boot.bif"           "${out}/sd_card/" 2>/dev/null || true
    [ -f "${deploy_dir}/Image" ] && cp -f "${deploy_dir}/Image"      "${out}/sd_card/"
    [ -f "${img_proc_cfg}" ]     && cp -f "${img_proc_cfg}"          "${out}/sd_card/image_processing.cfg"
    [ -n "${VERSION_TXT:-}" ]    && [ -f "${VERSION_TXT}" ] && cp -f "${VERSION_TXT}" "${out}/sd_card/version.txt"

    _as_ok "Deliverables in ${out}:"
    ls -la "${out}" 2>/dev/null | sed 's/^/    /' || true
    _as_ok "sd_card/ contents:"
    ls -la "${out}/sd_card" 2>/dev/null | sed 's/^/    /' || true
}

# On NFS workspaces create_pfm_sw.sh symlinks edf_build/build -> a per-build dir on local
# /scratch. `rm -rf <symlink>` removes only the link, orphaning the ~50GB target. Resolve +
# remove the target first, then drop the link. No-op when build/ is a real dir (local disk).
_edf_rm_build() {                       # $1 = edf_build dir
    local _b="$1/build" _t
    if [ -L "${_b}" ]; then
        _t="$(readlink -f "${_b}")"
        [ -n "${_t}" ] && [ -d "${_t}" ] && rm -rf "${_t}"
    fi
    rm -rf "${_b}"
}

_clean() {
  _edf_rm_build "${VEK280_ROOT}/edf_build"
  rm -rf ${VEK280_ROOT}/edf_build/downloads || true
}

###############################################################################
main_assemble() {
    _as_check_inputs   || return 1
    _as_assemble       || return 1
    _as_collect_output || return 1
    _clean
}

main_assemble
