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
#   Variant of create_pfm_sw.sh that strips the
#   vek385-reva-linux-overlay.yaml domain-file reference out of the fetched
#   multidomain machine yaml before parse-sdt, so the generated dtb only
#   contains chip/PS/PL-derived nodes, without the board-overlay nodes.
#

set +x

CUR_DIR=$(pwd)

SDT_OUTPUT="$CUR_DIR/hw/sdt_outdir"
VEK385_LAYER="meta-vek385"
MACHINE_YAML="$CUR_DIR/sw/yocto/sources/meta-amd-adaptive-socs/meta-amd-adaptive-socs-bsp/conf/machineyaml/versal-2ve-2vm-vek385-multidomain.yaml"

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

# Add vek385 layer for custom BSP configurations
if [ ! -d $CUR_DIR/sw/yocto/sources/$VEK385_LAYER ]; then
  bitbake-layers create-layer $CUR_DIR/sw/yocto/sources/$VEK385_LAYER
  rm -rf $CUR_DIR/sw/yocto/sources/$VEK385_LAYER/recipes-example
  rm -rf $CUR_DIR/sw/yocto/sources/$VEK385_LAYER/COPYING.MIT
  cp -rf  $CUR_DIR/sw/yocto/meta-vek385/* $CUR_DIR/sw/yocto/sources/$VEK385_LAYER/
  bitbake-layers add-layer $CUR_DIR/sw/yocto/sources/$VEK385_LAYER
fi

# Strip the linux-overlay domain-file reference from the fetched machine
# yaml so parse-sdt does not apply the board-overlay nodes.
if [ ! -f "$MACHINE_YAML" ]; then
  echo "ERROR: MACHINE_YAML not found at $MACHINE_YAML after repopull"
  return 1
fi

# Generate versal-2ve-2vm-vek385-multidomain machine-conf using sdt output from hw build
gen-machineconf parse-sdt \
	--hw-description $SDT_OUTPUT \
	--template $MACHINE_YAML

# xilinx-bootbin: generate boot.bin for OSPI/JTAG
if MACHINE=versal-2ve-2vm-vek385-multidomain bitbake edf-ospi; then
  echo "Yocto generated boot.bin successfully"
else
  echo "failed to generate boot.bin"
  return 1
fi

# Run Yocto script to generate linux-kernel Image and rootfs
cd $CUR_DIR/sw/yocto
source create_yocto.sh
cd $CUR_DIR
