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
#   bash script to collect XSA and extracted boot.bin into project_files
#

ABS_PATH=$PWD
PROJECT_FILES_PATH=$ABS_PATH/artifact/amd/project_files

YOCTO_deploy="$YOCTO_TMP_DIR/deploy/images/versal2-vek385-sdt-full"

if [ ! -d "$PROJECT_FILES_PATH" ]; then
    echo -e "\nproject_files directory does not exist. Creating: $PROJECT_FILES_PATH\n"
    mkdir -p "$PROJECT_FILES_PATH" || {
      echo " Failed to create build directory."
      exit 1
    }
fi

# Copy HW platform XSA files
for xsa in \
    "$ABS_PATH/hw/example_design_pfm_extensible.xsa" \
    "$ABS_PATH/hw/example_design_pfm_fixed.xsa"; do
  if [ -f "$xsa" ]; then
    cp -f "$xsa" "$PROJECT_FILES_PATH/"
  else
    echo "Not found: $xsa"
  fi
done

# Copy Vitis link XSA
if [ -f "$ABS_PATH/vitis_prj/link/example_design_link.xsa" ]; then
  cp -f "$ABS_PATH/vitis_prj/link/example_design_link.xsa" \
    "$PROJECT_FILES_PATH/"
else
  echo "Not found: $ABS_PATH/vitis_prj/link/example_design_link.xsa"
fi

# Copy extracted boot.bin components from Yocto deploy
if [ -d "$YOCTO_deploy" ]; then
  BOOTBIN_EXTRACTED="$YOCTO_deploy/boot.bin-extracted"
  if [ -d "$BOOTBIN_EXTRACTED" ]; then
    cp -rf "$BOOTBIN_EXTRACTED" "$PROJECT_FILES_PATH/"
  else
    echo "No boot.bin-extracted directory found in $YOCTO_deploy"
  fi
else
  echo "Yocto deploy directory not found: $YOCTO_deploy"
fi
