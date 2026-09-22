#!/bin/bash

#
# Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
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
#   Variant of create_pfm_sw.sh that:
#     - refreshes sources/meta-vek385 from the meta-vek385/ template on every
#       run (so custom_npufw.dtsi/domainyaml/bbappend edits always take
#       effect, even on repeat runs where sources/meta-vek385 already exists);
#     - swaps the EDF vek385-revb-linux-overlay.yaml domain file for the local
#       vek385-revb-multidomain-memory-overlay.yaml, disables OP-TEE, and
#       drops the EDF openamp/libmetal demo domain overlays before
#       gen-machineconf parse-sdt (production uses the custom RPU-FW/RPU-APP
#       remoteproc setup in custom_npufw.dtsi instead); and
#     - patches the lopper-generated device tree's incomplete aliases{}
#       (see fix_lopper_dts_aliases.sh) directly, before Yocto/bitbake ever
#       reads it. 
set +x

CUR_DIR=$(pwd)

# Load build configuration switches (MIPI, NPU_FW) from rev-b/build.cfg
# (authoritative) / env (fallback) up front -- this is the single sourcing
# of build.cfg for the whole script, done here so the NPU_FW check below can
# bail out immediately, before the NFS/YOCTO_TMP_DIR check, before 'cd', and
# before repopull_default-edf.sh does a real repo fetch further down.
BUILD_CFG="$CUR_DIR/build.cfg"
MIPI=${MIPI:-0}
NPU_FW=${NPU_FW:-0}
if [ -f "$BUILD_CFG" ]; then
  source "$BUILD_CFG"
fi

# NPU_FW is not supported for this release.
if [ "$NPU_FW" -eq 1 ]; then
  echo "ERROR: NPU_FW=1 is not supported for this release. Exiting."
  return 1 2>/dev/null || exit 1
fi

SDT_OUTPUT="$CUR_DIR/hw/sdt_outdir"
VEK385_LAYER="meta-vek385"
MACHINE_YAML="$CUR_DIR/sw/yocto/sources/meta-amd-adaptive-socs/meta-amd-adaptive-socs-bsp/conf/machineyaml/versal-2ve-2vm-vek385-revb-multidomain.yaml"
EDF_DOMAIN_FILE_PATTERN="vek385-revb-linux-overlay.yaml"
LOCAL_DOMAIN_YAML="$CUR_DIR/sw/yocto/meta-vek385/conf/domainyaml/vek385-revb-multidomain-memory-overlay.yaml"

# Remove trailing '/' in path
if [ ! -z YOCTO_TMP_DIR ]; then
  YOCTO_TMP_DIR=${YOCTO_TMP_DIR%/}
fi

src_fs_type=$(stat -f --format=%T "${CUR_DIR}")
if [ "$src_fs_type" == "nfs" ]; then
  echo "WARNING: Source filesystem detected nfs. Need to set YOCTO_TMP_DIR"
  if [ -z "${YOCTO_TMP_DIR}" ]; then
    echo "YOCTO_TMP_DIR is not set"
    return 1
  else
    # Start with the root directory
    tmp_dir="/"

    # Iterate through each directory in the path
    for part in $(echo "$YOCTO_TMP_DIR" | tr '/' ' '); do
      tmp_dir="$tmp_dir$part/"
      if [ -d "$tmp_dir" ]; then
        yocto_tmp_dir="$tmp_dir"
      else
        break
      fi
    done

    echo "INFO: existing dir in YOCTO_TMP_DIR is $yocto_tmp_dir"

    fs_type=$(stat -f --format=%T "${yocto_tmp_dir}")
    if [ "$fs_type" = "nfs" ]; then
      echo "YOCTO_TMP_DIR=$YOCTO_TMP_DIR is NFS backed not a standard directoy"
      return 1
    else
      echo "INFO: $YOCTO_TMP_DIR is a standard directory with filesystem type"\
        ": $fs_type"
    fi
  fi
fi

cd $CUR_DIR/sw/yocto/

source $CUR_DIR/repopull_default-edf.sh
if [ $? -ne 0 ]; then
  echo repopull_default-edf.sh failed;
  return 1
fi

# Initialize Yocto build
if [ -d "sources" ]; then
  source $CUR_DIR/sw/yocto/edf-init-build-env
fi

if [[ "$src_fs_type" == "nfs" || ! -z "${YOCTO_TMP_DIR}" ]]; then
  # Patch local.conf to change the TMPDIR right after Yocto initialization.
  sed -i "s|^#TMPDIR = \"\${TOPDIR}/tmp\"|TMPDIR = \"${YOCTO_TMP_DIR}\"|" \
  $CUR_DIR/sw/yocto/build/conf/local.conf
fi

# Forward MIPI/NPU_FW (already loaded from build.cfg/env above) into
# local.conf as bitbake variables, so device-tree.bbappend's
# bb.utils.contains() calls can select the right custom*.dtsi fragment.
echo "MIPI = \"$MIPI\"" >> "$CUR_DIR/sw/yocto/build/conf/local.conf"
echo "NPU_FW = \"$NPU_FW\"" >> "$CUR_DIR/sw/yocto/build/conf/local.conf"

# Add vek385 layer for custom BSP configurations
if [ ! -d $CUR_DIR/sw/yocto/sources/$VEK385_LAYER ]; then
  bitbake-layers create-layer $CUR_DIR/sw/yocto/sources/$VEK385_LAYER
  rm -rf $CUR_DIR/sw/yocto/sources/$VEK385_LAYER/recipes-example
  rm -rf $CUR_DIR/sw/yocto/sources/$VEK385_LAYER/COPYING.MIT
  cp -rf  $CUR_DIR/sw/yocto/meta-vek385/* $CUR_DIR/sw/yocto/sources/$VEK385_LAYER/
  bitbake-layers add-layer $CUR_DIR/sw/yocto/sources/$VEK385_LAYER
fi

# Sync meta-vek385 layer from template (before domain yaml check / gen-machineconf),
# so edits under meta-vek385/ (custom_npufw.dtsi, domainyaml, bbappends, etc.)
# always take effect, even on repeat runs where sources/$VEK385_LAYER already
# exists and the one-time creation block above is skipped.
if [ -d "$CUR_DIR/sw/yocto/meta-vek385" ]; then
  mkdir -p "$CUR_DIR/sw/yocto/sources/$VEK385_LAYER"
  cp -rf "$CUR_DIR/sw/yocto/meta-vek385/"* "$CUR_DIR/sw/yocto/sources/$VEK385_LAYER/"
  echo "INFO: refreshed sources/$VEK385_LAYER from meta-vek385 template"
fi

# Patch machine yaml: swap in local memory domain overlay and disable OP-TEE.
# Only for NPU_FW=1 -- the overlay's carveouts (rpu_fw_boot, rpu_app_boot,
# rpu_gem_cma, etc.) are RPU-FW/RPU-APP-specific and only meaningful when the
# RPU manages the AIE array. For NPU_FW=0 (AIE managed by Linux kernel
# drivers), leave the EDF-fetched machine yaml untouched.
if [ "$NPU_FW" -eq 1 ]; then
  if [ ! -f "$MACHINE_YAML" ]; then
    echo "ERROR: MACHINE_YAML not found at $MACHINE_YAML after repopull"
    return 1
  fi

  if [ ! -f "$LOCAL_DOMAIN_YAML" ]; then
    echo "ERROR: LOCAL_DOMAIN_YAML not found at $LOCAL_DOMAIN_YAML"
    return 1
  fi

  if [ ! -f "${MACHINE_YAML}.orig" ]; then
    cp "$MACHINE_YAML" "${MACHINE_YAML}.orig"
  fi

  if grep -q "$EDF_DOMAIN_FILE_PATTERN" "$MACHINE_YAML"; then
    sed -i "s|\${BSPLAYERDIR_amd-adaptive-socs-bsp}/conf/domainyaml/${EDF_DOMAIN_FILE_PATTERN}|${LOCAL_DOMAIN_YAML}|" \
      "$MACHINE_YAML"
    echo "INFO: swapped $EDF_DOMAIN_FILE_PATTERN for local memory overlay in $MACHINE_YAML"
  elif ! grep -q "vek385-revb-multidomain-memory-overlay.yaml" "$MACHINE_YAML"; then
    sed -i "/versal-2ve-2vm-multidomain-base.yaml/a\\                    ${LOCAL_DOMAIN_YAML} \\" \
      "$MACHINE_YAML"
    echo "INFO: added local memory overlay to $MACHINE_YAML"
  else
    sed -i "s|.*vek385-revb-multidomain-memory-overlay.yaml.*|                    ${LOCAL_DOMAIN_YAML} \\\|" \
      "$MACHINE_YAML"
    echo "INFO: normalized local memory overlay path in $MACHINE_YAML"
  fi

  sed -i 's/CONFIG_SUBSYSTEM_OP-TEE_SERIAL_SERIAL1_SELECT: y/CONFIG_SUBSYSTEM_OP-TEE_SERIAL_SERIAL1_SELECT: n/' \
    "$MACHINE_YAML"
  sed -i 's/CONFIG_SUBSYSTEM_OPTEE: y/CONFIG_SUBSYSTEM_OPTEE: n/' "$MACHINE_YAML"
  echo "INFO: disabled OP-TEE in $MACHINE_YAML"

  # Drop EDF OpenAMP/libmetal demo domain overlays (production uses custom remoteproc).
  sed -i '/versal-2ve-2vm-openamp-overlay.yaml/d' "$MACHINE_YAML"
  sed -i '/versal-2ve-2vm-libmetal-overlay.yaml/d' "$MACHINE_YAML"
  # vek385-rpu-overlay was last non-demo entry; remove stale line continuation.
  sed -i 's|vek385-rpu-overlay.yaml \\|vek385-rpu-overlay.yaml|' "$MACHINE_YAML"
  echo "INFO: removed openamp/libmetal demo domain overlays from $MACHINE_YAML"
else
  if [ ! -f "$MACHINE_YAML" ]; then
    echo "ERROR: MACHINE_YAML not found at $MACHINE_YAML after repopull"
    return 1
  fi
  echo "INFO: NPU_FW=0; leaving EDF machine yaml domain overlays/OP-TEE untouched"
fi

# Generate versal-2ve-2vm-vek385-revb-multidomain machine-conf using sdt output from hw build
gen-machineconf parse-sdt \
	--hw-description $SDT_OUTPUT \
	--template $MACHINE_YAML

# Fix incomplete aliases{} in the lopper-generated intermediate device tree
# (CONFIG_DTFILE) BEFORE Yocto/bitbake ever reads it. This patches the
# plain-text .dts directly -- no dtc/bootgen round-trip needed here, since
# Yocto's device-tree.bb/xilinx-bootbin/edf-ospi recipes will compile and
# package the corrected tree on their own first pass below. This avoids the
# need to patch/rebuild BOOT.bin or edf-ospi.bin after the fact.

CONFIG_DTFILE_DIR="$CUR_DIR/sw/yocto/build/conf/dts/versal-2ve-2vm-vek385-revb-multidomain"
if bash "$CUR_DIR/fix_lopper_dts_aliases.sh" \
    --dts-file "$CONFIG_DTFILE_DIR/cortexa78-linux.dts"; then
  echo "lopper-generated device tree aliases fixed successfully"
else
  echo "failed to fix lopper-generated device tree aliases"
  return 1
fi

# Generate boot.bin for OSPI/JTAG
if MACHINE=versal-2ve-2vm-vek385-revb-multidomain bitbake edf-ospi; then
  echo "Yocto generated boot.bin successfully"
else
  echo "failed to generate boot.bin"
  return 1
fi

# Run Yocto script to generate linux-kernel Image and rootfs
cd $CUR_DIR/sw/yocto
source create_yocto.sh
cd $CUR_DIR
