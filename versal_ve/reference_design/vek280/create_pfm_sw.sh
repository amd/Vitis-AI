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
# create_pfm_sw.sh  (VEK280 PetaLinux-free EDF flow — SW/EDF stage)
#
# Replaces create_petalinux.sh. Builds the EDF SOFTWARE stage fully
# PetaLinux-free against the committed meta-vek280/meta-vitis-ai board layers.
# Sourced by create_image.sh after the hw stage exports HW_XSA and NPU settings.
#
# Stage order:
#   1  repo init + sync (yocto-manifests rel-v2026.1 default-edf.xml)
#   2  source edf-init-build-env -> build/ + host-tools check
#   3  write build/conf/local.conf (MACHINE, sstate, DL_DIR, WKS, IMAGE_INSTALL)
#   4  fold committed sw/yocto/meta-vek280 + meta-vitis-ai into sources/
#   5  NPU dtsi variant selection -> single active npu_versal.dtsi in layer files/
#   6  sdtgen (with -zocl enable) on HW_XSA -> SDT  [must precede any bitbake]
#   7  gen-machine-conf parse-sdt -> versal-vek280-sdt-seg.conf  [must precede any bitbake]
#   8  MACHINE=versal-vek280-sdt-seg bitbake device-tree  (fast gate + ZOCL_DTB)
#   9  MACHINE=versal-vek280-sdt-seg bitbake xilinx-bootbin  (W4 dtsi stamp guard)
#  10  MACHINE=amd-cortexa72-common bitbake edf-linux-disk-image
#  11  MACHINE=amd-cortexa72-common bitbake meta-edf-app-sdk  (optional SDK tarball)
#
# Uses HW_XSA (not LINK_XSA) for sdtgen — PS .conf is byte-identical in both;
# allows SW stage to run before v++ link, mirroring PetaLinux order.
#
# Sourced by create_image.sh; also runnable standalone after sourcing npu_ip/settings.sh.
#
# Required env (from npu_ip/settings.sh + hw stage):
#   NB_DDRS, NPU_IP2, ENABLE_NPU_TAIL, NPU_TAIL  (dtsi variant selection)
#   HW_XSA                                        (base HW xsa from make xsa)
# Optional overrides:
#   EDF_WORK_DIR   workspace (default: vek280_root/edf_build)
#   VITIS_SETTINGS Vitis settings64.sh path
###############################################################################

VEK280_ROOT="${VEK280_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}"  # run only in script directory
VEK280_PLATFORM="${VEK280_PLATFORM:-${VEK280_ROOT}}"
PFMSW_DIR="${VEK280_PLATFORM}/sw"                                        # .../sw
VITIS_SETTINGS="${VITIS_SETTINGS:-}"

# EDF constants
MANIFEST_REPO="https://github.com/Xilinx/yocto-manifests"
MANIFEST_BRANCH="rel-v2026.1"
MANIFEST_FILE="default-edf.xml"
BOARD_MACHINE="versal-vek280-sdt-seg"
SOC_MACHINE="amd-cortexa72-common"

# Workspace + committed layers
EDF_WORK_DIR="${EDF_WORK_DIR:-${VEK280_ROOT}/edf_build}"
BUILD_DIR="${EDF_WORK_DIR}/build"
SOURCES_DIR="${EDF_WORK_DIR}/sources"

# True when $1 is on an NFS filesystem. Creates the dir first: `stat -f` on a missing path
# returns empty (not "nfs"), so on a fresh clone the test must mkdir before probing.
_edf_is_nfs() {
    mkdir -p "$1" 2>/dev/null || true
    [ "$(stat -f -c %T "$1" 2>/dev/null)" = "nfs" ]
}
SW_DIR="${PFMSW_DIR}"                                             # dtsi source + committed layers root (= sw/)
COMMITTED_LAYER="${SW_DIR}/yocto/meta-vek280"
META_VEK280="${SOURCES_DIR}/meta-vek280"
COMMITTED_VITIS_AI="${SW_DIR}/yocto/meta-vitis-ai"
META_VITIS_AI="${SOURCES_DIR}/meta-vitis-ai"


# HW_XSA — recompute default for standalone sourcing; hw stage export takes precedence
HW_XSA="${HW_XSA:-${VEK280_PLATFORM}/hw/example_design_pfm.xsa}"

# Custom-recipe fingerprint files (set by hw stage fpga_info copy)
FPGA_INFO_GLOB="${VEK280_PLATFORM}/fpga_info_"*.txt

# Shared caches (read-only ACCELERATOR mirrors -> SSTATE_MIRRORS / SOURCE_MIRROR_URL).
# SHARED_* is the preferred (pinned) mirror. ROLLING_* is the non-dated rolling
# rsync mirror used as an automatic fallback when the pinned snapshot has rotated
# away (see _sw_write_localconf). Override SHARED_SSTATE/SHARED_DOWNLOADS in the
# environment to force a specific mirror.
SHARED_SSTATE="${SHARED_SSTATE:-/proj/yocto/edf/2026.1/stable/Yocto_edf_2026.1_05101844/sstate-cache}"
SHARED_DOWNLOADS="${SHARED_DOWNLOADS:-/proj/yocto/edf/2026.1/stable/Yocto_edf_2026.1_05101844/downloads}"
ROLLING_SSTATE="/proj/yocto/edf/2026.1/stable/rsync-sstatecache_2026.1"
ROLLING_DOWNLOADS="/proj/yocto/edf/2026.1/stable/rsync-downloads_2026.1"
LOCAL_SSTATE="${EDF_WORK_DIR}/sstate-cache"
LOCAL_DOWNLOADS="${EDF_WORK_DIR}/downloads"

# SDT directory (gen-machine-conf input)
SDT_DIR="${EDF_WORK_DIR}/vek280_sdt"

_sw_info() { printf '\033[0;34m[sw ]\033[0m %s\n' "$*"; }
_sw_ok()   { printf '\033[0;32m[sw ]\033[0m %s\n' "$*"; }
_sw_warn() { printf '\033[1;33m[sw ]\033[0m %s\n' "$*"; }
_sw_err()  { printf '\033[0;31m[sw ]\033[0m %s\n' "$*" >&2; }

###############################################################################
# 1. repo init + sync
###############################################################################
_sw_repo_sync() {
    _sw_info "EDF workspace: ${EDF_WORK_DIR}"

    # --- NFS: relocate the bitbake build dir to local disk ---
    # bitbake's pseudo TMPDIR and gen-machine-conf's .hw-description unpack cannot run on
    # NFS (pseudo needs local ownership DB/hardlinks/xattrs; the .hw-description recursive
    # copy hits NFS rename-over-dir ENOTEMPTY races). Symlink build/ -> a per-build mktemp
    # dir on local /scratch so everything under build/ (tmp, conf, .hw-description) is local.
    # The symlink is the only record of the target — clean_image.sh/image_assemble.sh
    # readlink it (no marker file). [ ! -L ] guards reruns so incremental rebuilds reuse the
    # same local build/. On a local-disk workspace this is skipped and behaviour is unchanged.
    if _edf_is_nfs "${EDF_WORK_DIR}" && [ ! -L "${BUILD_DIR}" ]; then
        mkdir -p "${EDF_LOCAL_TMPROOT:-/scratch/$USER}" || {
            _sw_err "cannot create local scratch root: ${EDF_LOCAL_TMPROOT:-/scratch/$USER}"; return 1; }
        local _edf_scr
        _edf_scr="$(mktemp -d -p "${EDF_LOCAL_TMPROOT:-/scratch/$USER}" edf_build.XXXXXXXX)" || {
            _sw_err "mktemp on local scratch failed"; return 1; }
        rm -rf "${BUILD_DIR}"
        ln -s "${_edf_scr}" "${BUILD_DIR}"
        _sw_warn "workspace on NFS — bitbake build/ relocated to local disk: ${_edf_scr}"
    fi

    mkdir -p "${EDF_WORK_DIR}" "${LOCAL_SSTATE}" "${LOCAL_DOWNLOADS}" || return 1

    if ! command -v repo >/dev/null 2>&1; then
        _sw_info "repo tool not found — installing to ~/bin/repo"
        mkdir -p ~/bin
        curl -fsSL https://storage.googleapis.com/git-repo-downloads/repo -o ~/bin/repo || {
            _sw_err "repo download failed"; return 1; }
        chmod a+x ~/bin/repo
        export PATH="$HOME/bin:$PATH"
    fi
    _sw_ok "repo: $(repo --version 2>&1 | head -1)"

    ( cd "${EDF_WORK_DIR}" || exit 1
      if [ ! -d .repo ]; then
          _sw_info "repo init  (-b ${MANIFEST_BRANCH} -m ${MANIFEST_FILE})"
          repo init -u "${MANIFEST_REPO}" -b "${MANIFEST_BRANCH}" \
                    -m "${MANIFEST_FILE}" --no-clone-bundle || exit 1
      else
          _sw_info ".repo exists — skipping init"
      fi
      if [ -d "${EDF_WORK_DIR}/sources/poky" ]; then
          _sw_info "sources present — skipping repo sync"
      else
          _sw_info "repo sync"
          repo sync --no-clone-bundle -j8 || exit 1
      fi
    ) || { _sw_err "repo init/sync failed"; return 1; }

    [ -d "${SOURCES_DIR}/poky" ] || { _sw_err "sources/poky missing after sync"; return 1; }
    [ -f "${EDF_WORK_DIR}/edf-init-build-env" ] || [ -f "${EDF_WORK_DIR}/setupsdk" ] || {
        _sw_err "edf-init-build-env not placed by manifest"; return 1; }
    _sw_ok "EDF sources synced."
}

###############################################################################
# 2. source EDF env -> build/
###############################################################################
_sw_source_edf_env() {
    local env_script=""
    [ -f "${EDF_WORK_DIR}/edf-init-build-env" ] && env_script="${EDF_WORK_DIR}/edf-init-build-env"
    [ -f "${EDF_WORK_DIR}/setupsdk" ]           && env_script="${EDF_WORK_DIR}/setupsdk"
    [ -n "${env_script}" ] || { _sw_err "no EDF env script in ${EDF_WORK_DIR}"; return 1; }

    local _t _missing=""
    for _t in chrpath diffstat lz4c bmaptool; do
        command -v "${_t}" >/dev/null 2>&1 || _missing="${_missing} ${_t}"
    done
    [ -z "${_missing}" ] || {
        _sw_err "missing host build tools:${_missing} — run ${SW_DIR}/setup_yocto_host.sh first"
        return 1
    }
    _sw_ok "host tools present"

    [ -n "${TEMPLATECONF:-}" ] && { _sw_warn "unsetting stale TEMPLATECONF"; unset TEMPLATECONF; }

    _sw_info "source ${env_script} ${BUILD_DIR}"
    set +u
    source "${env_script}" "${BUILD_DIR}" || {
        set -u 2>/dev/null || true; _sw_err "EDF env source failed"; return 1; }
    set -u 2>/dev/null || true
    [ -d "${BUILD_DIR}/conf" ] || { _sw_err "build/conf not created"; return 1; }
    _sw_ok "EDF build env active: ${BUILD_DIR}"
}

###############################################################################
# 3. local.conf  (MACHINE, caches, WKS override, IMAGE_INSTALL)
###############################################################################
_sw_write_localconf() {
    local lc="${BUILD_DIR}/conf/local.conf"
    if ! grep -q "VEK280 EDF self-contained" "${lc}" 2>/dev/null; then
        _sw_info "writing local.conf"
        cat >> "${lc}" <<LOCALCONF

# === VEK280 EDF self-contained config ===
MACHINE ?= "${BOARD_MACHINE}"

SSTATE_DIR = "${LOCAL_SSTATE}"
DL_DIR     = "${LOCAL_DOWNLOADS}"

BB_NUMBER_THREADS = "16"
PARALLEL_MAKE     = "-j16"

# Delete each recipe's WORKDIR after it builds — cuts peak disk/memory use.
INHERIT += "rm_work"

# 2-partition MBR wks — Versal BootROM cannot parse GPT.
WKS_FILES:forcevariable = "vek280-sd.wks"

IMAGE_INSTALL:append = " onnxruntime ai-engine-driver jansson python3-pybind11 libdfx e2fsprogs-resize2fs e2fsprogs-tune2fs gdb gdbserver strace lsof curl rsync bash-completion u-boot-tools packagegroup-self-hosted valgrind git vartml-sw vart x-plus-ml uio-npu-conf mipso-udev"
# SDK sysroot: xrt, xrt-dev, onnxruntime, already present; add vartml-sw-dev, vart-dev, onnxruntime-dev, libyaml-dev, libevent-2.1-dev
TOOLCHAIN_TARGET_TASK:append:aarch64 = " vartml-sw-dev vart-dev onnxruntime-dev libyaml-dev libevent-2.1-dev"

LOCALCONF

        # When build/ is relocated to /scratch (NFS case), it is a symlink and bitbake resolves
        # TOPDIR to the physical /scratch path. The vart/vartml-sw/x-plus-ml recipes locate their
        # sibling sources via LOCAL_DIR ?= "${TOPDIR}/../../../../{src,examples}", which then points
        # outside the repo and fails "file could not be found". Pin LOCAL_DIR to absolute repo paths
        # (derived from VEK280_ROOT, a real NFS path) so the anchor is independent of TOPDIR. Only
        # needed when relocated — on a real build/ the baseline relative anchor already resolves.
        if [ -L "${BUILD_DIR}" ]; then
            local _src_dir _ex_dir
            _src_dir="$(cd "${VEK280_ROOT}/../../src" && pwd)"
            _ex_dir="$(cd "${VEK280_ROOT}/../../examples" && pwd)"
            cat >> "${lc}" <<LOCALDIRCONF

# build/ relocated to local disk (symlink) — pin recipe source anchors to absolute repo paths.
LOCAL_DIR:pn-vart = "${_src_dir}"
LOCAL_DIR:pn-vartml-sw = "${_src_dir}"
LOCAL_DIR:pn-x-plus-ml = "${_ex_dir}"
LOCALDIRCONF
            _sw_ok "pinned LOCAL_DIR (relocated build): src=${_src_dir} examples=${_ex_dir}"
        fi

        # Set up SSTATE_MIRRORS + SOURCE_MIRROR_URL for shared accelerator mirrors. If the pinned
        # mirror is missing, fall back to the rolling rsync mirror. If both are missing, warn
        # and continue without a mirror download.
        local eff_sstate="" eff_downloads="" mirror_src=""
        if [ -d "${SHARED_SSTATE}" ] && [ -d "${SHARED_DOWNLOADS}" ]; then
            eff_sstate="${SHARED_SSTATE}"; eff_downloads="${SHARED_DOWNLOADS}"; mirror_src="pinned SHARED_*"
        elif [ -d "${ROLLING_SSTATE}" ] && [ -d "${ROLLING_DOWNLOADS}" ]; then
            eff_sstate="${ROLLING_SSTATE}"; eff_downloads="${ROLLING_DOWNLOADS}"; mirror_src="rolling rsync fallback"
            _sw_warn "pinned mirror missing (${SHARED_SSTATE}) — using rolling rsync fallback"
        fi
        if [ -n "${eff_sstate}" ]; then
            cat >> "${lc}" <<MIRRORCONF

SSTATE_MIRRORS += "file://.* file://${eff_sstate}/PATH"
SOURCE_MIRROR_URL = "file://${eff_downloads}"
INHERIT += "own-mirrors"
MIRRORCONF
            _sw_ok "accelerator mirrors enabled (${mirror_src}): sstate=${eff_sstate}"
        else
            _sw_warn "no shared mirror found (pinned + rolling both missing) — building WITHOUT"
            _sw_warn "  sstate/download accelerator. OK if upstream source URLs are reachable"
            _sw_warn "  (slower first build); FATAL on an air-gapped host. Point SHARED_SSTATE/"
            _sw_warn "  SHARED_DOWNLOADS at a current mirror under /proj/yocto/edf/2026.1/stable/."
        fi
        _sw_ok "local.conf written"
    else
        _sw_ok "local.conf already configured"
    fi
}

###############################################################################
# 4. fold committed layers (cp -rf + add-layer; no create-layer needed)
###############################################################################
_sw_fold_layer() {
    _sw_info "folding meta-vek280 -> ${META_VEK280}"
    mkdir -p "${META_VEK280}"
    cp -rf "${COMMITTED_LAYER}/." "${META_VEK280}/" || { _sw_err "copy meta-vek280 failed"; return 1; }
    if ! grep -q "meta-vek280" "${BUILD_DIR}/conf/bblayers.conf" 2>/dev/null; then
        ( cd "${EDF_WORK_DIR}" && bitbake-layers add-layer "${META_VEK280}" ) || {
            _sw_err "add-layer meta-vek280 failed"; return 1; }
    fi
    _sw_ok "meta-vek280 folded"

    mkdir -p "${META_VITIS_AI}"
    cp -rf "${COMMITTED_VITIS_AI}/." "${META_VITIS_AI}/" || { _sw_err "copy meta-vitis-ai failed"; return 1; }
    if ! grep -q "meta-vitis-ai" "${BUILD_DIR}/conf/bblayers.conf" 2>/dev/null; then
        ( cd "${EDF_WORK_DIR}" && bitbake-layers add-layer "${META_VITIS_AI}" ) || {
            _sw_err "add-layer meta-vitis-ai failed"; return 1; }
    fi
    _sw_ok "meta-vitis-ai folded"

    # Copy fpga_info fingerprint files into vartml-sw recipe dir
    local fi
    for fi in ${FPGA_INFO_GLOB}; do
        [ -f "${fi}" ] || continue
        local dest="${META_VITIS_AI}/recipes-vitis-ai/vartml-sw/vartml-sw"
        mkdir -p "${dest}" || { _sw_err "mkdir failed: ${dest}"; return 1; }
        cp "${fi}" "${dest}/" || { _sw_err "fpga_info copy failed: ${fi}"; return 1; }
        _sw_ok "fpga_info: $(basename "${fi}") staged"
    done
}

###############################################################################
# 5. NPU dtsi variant selection  (verbatim parity with create_petalinux.sh:86-89)
#    Source: ${SW_DIR}/npu_versal_*.dtsi (W1 UIO root-level fix already applied by rdegrave)
#    Only the selected variant is copied as the active npu_versal.dtsi — not all 4.
###############################################################################
_sw_select_dtsi() {
    local dest_dir="${META_VEK280}/recipes-bsp/device-tree/files"
    local act="${dest_dir}/npu_versal.dtsi"
    mkdir -p "${dest_dir}"
    : "${NB_DDRS:?NB_DDRS not set — source npu_ip/settings.sh first}"

    # Later matching line overwrites earlier — exact create_petalinux.sh logic.
    local SEL=""
    [ "$NB_DDRS" == 3 ] || { cp -f "${SW_DIR}/npu_versal_one_npu_il_ddr.dtsi"  "${act}"; SEL=one_npu_il_ddr; }
    [ "$NB_DDRS" != 3 ] || [ -n "${NPU_IP2:-}" ] || [ "${ENABLE_NPU_TAIL:-}" != "True" ] || \
        { cp -f "${SW_DIR}/npu_versal_one_npu_one_pp.dtsi" "${act}"; SEL=one_npu_one_pp; }
    [ "$NB_DDRS" == 1 ] || [ -z "${NPU_IP2:-}" ] || [ "${ENABLE_NPU_TAIL:-}" == "True" ] || \
        { cp -f "${SW_DIR}/npu_versal_two_npus.dtsi"       "${act}"; SEL=two_npus; }
    [ "$NB_DDRS" == 1 ] || [ -n "${NPU_IP2:-}" ] || [ "${ENABLE_NPU_TAIL:-}" == "True" ] || \
        { cp -f "${SW_DIR}/npu_versal_one_npu.dtsi"        "${act}"; SEL=one_npu; }

    [ -f "${act}" ] || {
        _sw_err "no dtsi variant matched (NB_DDRS=${NB_DDRS} NPU_IP2='${NPU_IP2:-}' ENABLE_NPU_TAIL='${ENABLE_NPU_TAIL:-}')"
        return 1
    }
    _sw_ok "npu_versal.dtsi <- npu_versal_${SEL}.dtsi (NB_DDRS=${NB_DDRS})"
    export ACTIVE_DTSI="${act}"
}

###############################################################################
# 6. sdtgen on HW_XSA -> SDT  [prerequisite for gen-machine-conf]
###############################################################################
_sw_sdtgen() {
    : "${HW_XSA:?HW_XSA not set — run make xsa first}"
    [ -f "${HW_XSA}" ] || { _sw_err "base HW xsa missing: ${HW_XSA}"; return 1; }

    if [ -n "${VITIS_SETTINGS}" ]; then
        [ -f "${VITIS_SETTINGS}" ] || { _sw_err "Vitis settings not found: ${VITIS_SETTINGS}"; return 1; }
    elif ! command -v sdtgen >/dev/null 2>&1; then
        _sw_err "Vitis not on PATH — source <Vitis>/settings64.sh or set VITIS_SETTINGS"; return 1
    fi

    mkdir -p "${SDT_DIR}"
    _sw_info "sdtgen on HW_XSA -> ${SDT_DIR}"
    # Run in a clean subshell — EDF bitbake env rejects Vitis env vars.
    bash -c "
        set -e
        [ -z '${VITIS_SETTINGS}' ] || source '${VITIS_SETTINGS}'
        sdtgen <<'SDTEOF'
set_dt_param -dir ${SDT_DIR}
set_dt_param -xsa ${HW_XSA}
set_dt_param -board_dts versal-vek280-revb
set_dt_param -zocl "enable"
generate_sdt
exit
SDTEOF
    " || { _sw_err "sdtgen failed"; return 1; }

    [ -f "${SDT_DIR}/system-top.dts" ] || { _sw_err "sdtgen: no system-top.dts produced"; return 1; }
    _sw_ok "SDT generated: ${SDT_DIR}"
}

###############################################################################
# 7. gen-machine-conf parse-sdt -> versal-vek280-sdt-seg.conf
#    Required before any bitbake — no pre-built conf exists in EDF sources for VEK280.
###############################################################################
_sw_gen_machine_conf() {
    command -v gen-machine-conf >/dev/null 2>&1 || {
        _sw_err "gen-machine-conf not in PATH (EDF env not sourced?)"; return 1; }

    local mc_dir="${META_VEK280}/conf/machine"
    mkdir -p "${mc_dir}"

    if [ -f "${mc_dir}/versal-vek280-sdt-seg.conf" ]; then
        _sw_ok "machine conf already present — skipping regen"
        return 0
    fi

    _sw_info "gen-machine-conf parse-sdt"
    gen-machine-conf parse-sdt \
        --hw-description "${SDT_DIR}" \
        --machine-name "${BOARD_MACHINE}" \
        -O "${mc_dir}" || { _sw_err "gen-machine-conf failed"; return 1; }

    # gen-machine-conf may write to build/conf/machine on name clash; mirror into layer.
    if [ ! -f "${mc_dir}/versal-vek280-sdt-seg.conf" ] && \
       [ -f "${BUILD_DIR}/conf/machine/versal-vek280-sdt-seg.conf" ]; then
        cp "${BUILD_DIR}/conf/machine/versal-vek280-sdt-seg.conf" "${mc_dir}/"
        cp -r "${BUILD_DIR}/conf/machine/include" "${mc_dir}/" 2>/dev/null || true
    fi
    [ -f "${mc_dir}/versal-vek280-sdt-seg.conf" ] || { _sw_err "no machine conf produced"; return 1; }
    _sw_ok "machine conf: ${mc_dir}/versal-vek280-sdt-seg.conf"

}
###############################################################################
# 8. One-pass device-tree build  (fast verification gate + ZOCL_DTB)
#    Machine conf from step 7 must exist before this runs.
#    Produces cortexa72-linux.dtb = runtime --package.dtb for v++ -p.
#    W1 regression guard: checks linux,uio-name present at DT root.
###############################################################################
_sw_build_device_tree() {
    _sw_info "MACHINE=${BOARD_MACHINE} bitbake device-tree"
    ( cd "${EDF_WORK_DIR}" && MACHINE="${BOARD_MACHINE}" bitbake device-tree ) || {
        _sw_err "device-tree bitbake failed"; return 1; }

    local dtb_path="${BUILD_DIR}/tmp/deploy/images/${BOARD_MACHINE}/devicetree/cortexa72-linux.dtb"
    [ -f "${dtb_path}" ] || { _sw_err "cortexa72-linux.dtb not found at ${dtb_path}"; return 1; }
    ZOCL_DTB="${dtb_path}"

    if command -v dtc >/dev/null 2>&1; then
        dtc -I dtb -O dts "${ZOCL_DTB}" 2>/dev/null | grep -q "linux,uio-name" \
            || _sw_warn "linux,uio-name not in dtb — W1 UIO root-level fix may not be applied"
    fi

    export ZOCL_DTB
    _sw_ok "device-tree built; ZOCL_DTB=${ZOCL_DTB}"
}

###############################################################################
# 9. EDF boot assets  (xilinx-bootbin -> boot.bin-extracted)
#    W4: dtsi stamp guard — forces cleansstate when npu_versal.dtsi changes
#    so sstate does not replay a stale BOOT.BIN with the old embedded dtb.
###############################################################################
_sw_build_boot_assets() {
    local _stamp="${EDF_WORK_DIR}/.dtsi_hash"
    local _hash
    _hash=$(md5sum "${META_VEK280}/recipes-bsp/device-tree/files/npu_versal.dtsi" \
            2>/dev/null | awk '{print $1}')
    if [ -f "${_stamp}" ] && [ "$(cat "${_stamp}")" != "${_hash}" ]; then
        _sw_warn "dtsi changed — forcing xilinx-bootbin cleansstate"
        ( cd "${EDF_WORK_DIR}" && MACHINE="${BOARD_MACHINE}" bitbake -c cleansstate xilinx-bootbin ) \
            || _sw_warn "cleansstate failed -- build may use stale sstate for xilinx-bootbin"
    fi
    echo "${_hash}" > "${_stamp}"

    _sw_info "MACHINE=${BOARD_MACHINE} bitbake xilinx-bootbin"
    ( cd "${EDF_WORK_DIR}" && MACHINE="${BOARD_MACHINE}" bitbake xilinx-bootbin ) || {
        _sw_err "xilinx-bootbin failed"; return 1; }

    local ext="${BUILD_DIR}/tmp/deploy/images/${BOARD_MACHINE}/boot.bin-extracted"
    EDF_UBOOT="${ext}/u-boot.elf"
    EDF_ATF="${ext}/arm-trusted-firmware.elf"
    [ -f "${EDF_UBOOT}" ] || { _sw_err "u-boot.elf missing in ${ext}"; return 1; }
    [ -f "${EDF_ATF}" ]   || { _sw_err "arm-trusted-firmware.elf missing in ${ext}"; return 1; }
    _sw_ok "EDF_UBOOT: ${EDF_UBOOT}"
    _sw_ok "EDF_ATF  : ${EDF_ATF}"
}

###############################################################################
# 10. rootfs WIC  (IMAGE_INSTALL from local.conf)
###############################################################################
_sw_build_rootfs() {
    _sw_info "MACHINE=${SOC_MACHINE} bitbake edf-linux-disk-image"
    ( cd "${EDF_WORK_DIR}" && MACHINE="${SOC_MACHINE}" bitbake edf-linux-disk-image ) || {
        _sw_err "edf-linux-disk-image failed"; return 1; }

    local img="${BUILD_DIR}/tmp/deploy/images/${SOC_MACHINE}"
    EDF_WIC="${img}/edf-linux-disk-image-${SOC_MACHINE}.rootfs.wic"
    [ -n "${EDF_WIC}" ] && [ -f "${EDF_WIC}" ] || { _sw_err "WIC not found in ${img}"; return 1; }
    _sw_ok "EDF_WIC: ${EDF_WIC}"
}

###############################################################################
# 11. EDF SDK Generate (meta-edf-app-sdk), skipped if NO_SDK_BUILD is set.
###############################################################################
_sw_build_sdk() {
    if [ -n "${NO_SDK_BUILD:-}" ]; then
        _sw_warn "NO_SDK_BUILD set — skipping EDF SDK (meta-edf-app-sdk) build"
    else
    _sw_info "MACHINE=${SOC_MACHINE} bitbake meta-edf-app-sdk"
    ( cd "${EDF_WORK_DIR}" && MACHINE="${SOC_MACHINE}" bitbake meta-edf-app-sdk ) || {
        _sw_err "meta-edf-app-sdk build failed"; return 1; }
    SDK_SH="$(find "${BUILD_DIR}/tmp/deploy/sdk" -maxdepth 1 -name '*.sh' 2>/dev/null | head -1)"
    [ -n "${SDK_SH}" ] && [ -f "${SDK_SH}" ] || { _sw_err "SDK .sh not found in ${BUILD_DIR}/tmp/deploy/sdk"; return 1; }
    export SDK_SH
    _sw_ok "EDF SDK: ${SDK_SH}"
    fi
    [ -f "${ZOCL_DTB:-}" ] || { _sw_err "ZOCL_DTB not set or missing: '${ZOCL_DTB:-}'"; return 1; }
    export ZOCL_DTB
    _sw_ok "runtime --package.dtb: ${ZOCL_DTB}"
}

main_sw() {
    _sw_repo_sync           || return 1
    _sw_source_edf_env      || return 1
    _sw_write_localconf     || return 1
    _sw_fold_layer          || return 1
    _sw_select_dtsi         || return 1
    _sw_sdtgen              || return 1
    _sw_gen_machine_conf    || return 1
    _sw_build_device_tree   || return 1
    _sw_build_boot_assets   || return 1
    _sw_build_rootfs        || return 1
    _sw_build_sdk           || return 1

    export ZOCL_DTB EDF_UBOOT EDF_ATF

    _sw_ok "SW/EDF stage complete."
    _sw_info "  ZOCL_DTB = ${ZOCL_DTB}"
    _sw_info "  EDF_UBOOT= ${EDF_UBOOT}"
    _sw_info "  EDF_ATF  = ${EDF_ATF}"
    _sw_info "  EDF_WIC  = ${EDF_WIC}"
}

main_sw
