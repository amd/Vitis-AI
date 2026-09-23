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
#   Clean orchestrator for the VEK280 AMD-EDF 2026.1 flow — the clean-side
#   counterpart to create_image.sh. Keeps all stage orchestration (build AND
#   clean) in scripts so the "what stages exist" knowledge lives in one place.
#   Per-directory unit cleaning stays in each Makefile's `clean` target; this
#   script only sequences them + removes the EDF workspace and top-level outputs.
#
# Usage:
#   ./clean_image.sh [TARGET ...]
#
#   TARGET (one or more; default = all):
#     all      Everything below (default when no argument is given)
#     hw       Vivado outputs        (make -C hw clean; also cleans npu_tail)
#     vitis    v++ link/package out  (make -C vitis_prj clean)
#     amd-edf      EDF/Yocto workspace   (edf_build/{build,sources,vek280_sdt,.dtsi_hash})
#              NOTE: sstate-cache + downloads are preserved (accelerators).
#     output   Collected deliverables (output/) + staged fpga_info_*.txt
#
#   Examples:
#     ./clean_image.sh            # full clean
#     ./clean_image.sh all        # full clean (explicit)
#     ./clean_image.sh hw vitis   # only the Vivado + Vitis outputs
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

_cl_info() { printf '\033[0;34m[clean]\033[0m %s\n' "$*"; }
_cl_ok()   { printf '\033[0;32m[clean]\033[0m %s\n' "$*"; }
_cl_err()  { printf '\033[0;31m[clean]\033[0m %s\n' "$*" >&2; }

_cl_usage() {
    sed -n 's/^# \{0,1\}//p' "${BASH_SOURCE[0]}" | sed -n '/^Usage:/,/^$/p'
}

# --- individual clean stages -------------------------------------------------
_clean_hw() {
    _cl_info "hw: make -C hw clean"
    make -C "${SCRIPT_DIR}/hw" clean
}
_clean_vitis() {
    _cl_info "vitis_prj: make -C vitis_prj clean"
    make -C "${SCRIPT_DIR}/vitis_prj" clean
}
# On NFS workspaces create_pfm_sw.sh symlinks edf_build/build -> a per-build dir on local
# /scratch. `rm -rf <symlink>` removes only the link, orphaning the ~50GB target (which also
# holds the Versal multiconfig tmp-microblaze-pmc/-psm siblings). Resolve + remove the target
# first, then drop the link. No-op when build/ is a real dir (local-disk workspace).
_edf_rm_build() {                       # $1 = edf_build dir
    local _b="$1/build" _t
    if [ -L "${_b}" ]; then
        _t="$(readlink -f "${_b}")"
        [ -n "${_t}" ] && [ -d "${_t}" ] && rm -rf "${_t}"
    fi
    rm -rf "${_b}"
}

_clean_edf() {
    _cl_info "edf_build: removing build/ sources/ vek280_sdt/ .dtsi_hash (sstate + downloads preserved)"
    _edf_rm_build "${SCRIPT_DIR}/edf_build"
    rm -rf "${SCRIPT_DIR}/edf_build/sources" \
           "${SCRIPT_DIR}/edf_build/vek280_sdt" \
           "${SCRIPT_DIR}/edf_build/.dtsi_hash"
}
_clean_output() {
    _cl_info "output/ + fpga_info_*.txt"
    rm -rf "${SCRIPT_DIR}/output"
    rm -f  "${SCRIPT_DIR}"/fpga_info_*.txt
}
_clean_npu_ip() {
    _cl_info "npu_ip: make -C npu_ip clean"
    make -C "${SCRIPT_DIR}/../../npu_ip" clean
}

# --- argument dispatch -------------------------------------------------------
# No args => all. Validate every requested target before running any.
_targets=("$@")
[ ${#_targets[@]} -eq 0 ] && _targets=("all")

for _t in "${_targets[@]}"; do
    case "${_t}" in
        all|hw|vitis|amd-edf|output|npu_ip) ;;
        -h|--help) _cl_usage; return 0 2>/dev/null || exit 0 ;;
        *) _cl_err "unknown clean target: '${_t}'"; _cl_usage; return 1 2>/dev/null || exit 1 ;;
    esac
done

for _t in "${_targets[@]}"; do
    case "${_t}" in
        all)    _clean_hw; _clean_vitis; _clean_edf; _clean_output; _clean_npu_ip ;;
        hw)     _clean_hw ;;
        vitis)  _clean_vitis ;;
        amd-edf)    _clean_edf ;;
        output) _clean_output ;;
        npu_ip) _clean_npu_ip ;;
    esac
done

_cl_ok "clean complete (${_targets[*]})."
