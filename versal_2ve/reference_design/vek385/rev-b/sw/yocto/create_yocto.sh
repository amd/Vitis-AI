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
#   bash script to build rootfs, kernel Image and sysroot form
#   released edf yocto sources with custom configuration
#

# Function to display usage
usage() {
  echo "Usage: $0 [-i] [-s] [-c] [-h]"
  echo "  -i    Build Image (rootfs and kernel)"
  echo "  -s    Build SDK"
  echo "  -c    Clean (remove build/, sources/, .repo/, edf-init-build-env)"
  echo "  -h    Display this help message"
  echo ""
  echo "  If no options are provided, both Image and SDK will be built"
  exit 1
}

ABS_PATH=$(pwd)

# Load build configuration switches (MIPI, NPU_FW) from rev-b/build.cfg.
# build.cfg is authoritative: it is sourced last so its values win over any
# inherited/exported env values. Env is used only as a fallback when build.cfg
# is missing (or omits a key); defaults apply otherwise.
BUILD_CFG="$ABS_PATH/../../build.cfg"
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

VITIS_AI_LAYER="meta-vitis-ai"
VEK385_LAYER="meta-vek385"
build_image=0
build_sdk=0
clean_build=0

OPTIND=1

while getopts "isch" opt; do
  case ${opt} in
    i )
      build_image=1
      ;;
    s )
      build_sdk=1
      ;;
    c )
      clean_build=1
      ;;
    h )
      usage
      ;;
    \? )
      usage
      ;;
  esac
done

# If clean option is specified, clean and exit
if [ $clean_build -eq 1 ]; then
  echo "Cleaning build artifacts..."
  rm -rf build/ sources/ .repo/ edf-init-build-env
  echo "Clean completed."
  return 1 2>/dev/null || exit 1
fi

# If no build options specified, build both
if [ $build_image -eq 0 ] && [ $build_sdk -eq 0 ]; then
  build_image=1
  build_sdk=1
fi

set --
if [ ! -d "source" ]; then
  source ../../repopull_default-edf.sh
  if [ $? -ne 0 ]; then
    echo repopull_default-edf.sh failed;
    return 1
  fi
fi

if [ -d "sources" ]; then
  if [ -d "build" ]; then
    rm -rf $ABS_PATH/build/conf/local.conf
  fi
fi

# Initialize Yocto
source edf-init-build-env

LOCAL_CONF_PATH="$ABS_PATH/build/conf/local.conf"

if [[ "$src_fs_type" == "nfs" || ! -z "${YOCTO_TMP_DIR}" ]]; then
  # Patch local.conf to change the TMPDIR right after Yocto initialization.
  sed -i "s|^#TMPDIR = \"\${TOPDIR}/tmp\"|TMPDIR = \"${YOCTO_TMP_DIR}\"|" \
  ${LOCAL_CONF_PATH}
fi

# Preserve the MIPI/NPU_FW platform selection after local.conf regeneration.
echo "MIPI = \"$MIPI\"" >> "$LOCAL_CONF_PATH"
echo "NPU_FW = \"$NPU_FW\"" >> "$LOCAL_CONF_PATH"

# Add vitis-ai layer
if [ ! -d $ABS_PATH/sources/$VITIS_AI_LAYER ]; then
  bitbake-layers create-layer $ABS_PATH/sources/$VITIS_AI_LAYER
  rm -rf $ABS_PATH/sources/$VITIS_AI_LAYER/recipes-example
  rm -rf $ABS_PATH/sources/$VITIS_AI_LAYER/COPYING.MIT

  # Single source layer now (meta-vitis-ai). NPU_FW-specific recipe variants
  # live alongside their base counterparts as *-npufw sibling folders
  # (recipes-amdrnpu-npufw, recipes-core-npufw, recipes-kernel-npufw,
  # recipes-xrt-npufw). Select which set applies after copying.
  cp -rf $ABS_PATH/meta-vitis-ai/* $ABS_PATH/sources/$VITIS_AI_LAYER/

  NPUFW_RECIPE_DIRS="recipes-amdrnpu recipes-core recipes-kernel recipes-xrt"
  for d in $NPUFW_RECIPE_DIRS; do
    NPUFW_DIR="$ABS_PATH/sources/$VITIS_AI_LAYER/${d}-npufw"
    BASE_DIR="$ABS_PATH/sources/$VITIS_AI_LAYER/${d}"
    if [ "$NPU_FW" -eq 1 ]; then
      # Swap in the NPU_FW variant in place of the base recipes. recipes-amdrnpu
      # has no base counterpart, so BASE_DIR is a harmless no-op to remove.
      rm -rf "$BASE_DIR"
      mv "$NPUFW_DIR" "$BASE_DIR"
    else
      # Not building NPU_FW: drop the unused variant folder.
      rm -rf "$NPUFW_DIR"
    fi
  done

  bitbake-layers add-layer $ABS_PATH/sources/$VITIS_AI_LAYER
fi

# Add vek385 board-specific layer
if [ ! -d $ABS_PATH/sources/$VEK385_LAYER ]; then
  bitbake-layers create-layer $ABS_PATH/sources/$VEK385_LAYER
  rm -rf $ABS_PATH/sources/$VEK385_LAYER/recipes-example
  rm -rf $ABS_PATH/sources/$VEK385_LAYER/COPYING.MIT
  cp -rf $ABS_PATH/meta-vek385/* $ABS_PATH/sources/$VEK385_LAYER/
  bitbake-layers add-layer $ABS_PATH/sources/$VEK385_LAYER
fi

cat << 'EOF' >> "$LOCAL_CONF_PATH"

VEK385_AIE_VARIANT ?= "TessAI"
IMAGE_INSTALL:append = " packagegroup-vaiml vek385-board-setup"
IMAGE_INSTALL:append = " kernel-module-hdmi21 v4l-utils packagegroup-xilinx-gstreamer libdrm libdrm-tests media-ctl dfx-mgr"
IMAGE_INSTALL:append = " isp-firmware"
IMAGE_INSTALL:append = " isp-media-server"
PACKAGECONFIG:append:pn-gdb = " tui"
TOOLCHAIN_HOST_TASK:append = " nativesdk-python3-pip nativesdk-python3-numpy nativesdk-python3-setuptools nativesdk-python3-build nativesdk-python3-wheel nativesdk-python3-protobuf nativesdk-python3-pybind11 nativesdk-protobuf "
EOF

if [ "$NPU_FW" -eq 1 ]; then
  echo 'TOOLCHAIN_TARGET_TASK:append = " ryzenai-wheels-dev opencv-dev jansson-dev vart-ml-dev vvas-utils-dev vvas-gst-plugins-dev vart-x-dev hip-dev amdrnpu-dev"' >> "$LOCAL_CONF_PATH"
else
  echo 'TOOLCHAIN_TARGET_TASK:append = " ryzenai-wheels-dev opencv-dev jansson-dev vart-ml-dev vvas-utils-dev vvas-gst-plugins-dev vart-x-dev"' >> "$LOCAL_CONF_PATH"
fi

if [ $build_image -eq 1 ]; then
  echo "Building rootfs and kernel Image..."
  # Generate rootfs and kernel Image
  if MACHINE=amd-cortexa78-mali-common bitbake edf-linux-disk-image; then
    echo "Rootfs and Image Build successfully"
  else
    echo "Rootfs and Image Build failed"
    return 1
  fi

  #copy Rootfs and Kernel image to output build directory
  if [ -z $YOCTO_TMP_DIR ]; then
  YOCTO_deploy="$ABS_PATH/build/tmp/deploy"
  else
  YOCTO_deploy="$YOCTO_TMP_DIR/deploy"
  fi

  BOOTBIN_IMAGE_PATH="$YOCTO_deploy/images/versal-2ve-2vm-vek385-revb-multidomain"
  BUILD_OUTPUT_DIR="$ABS_PATH/../../artifact/amd/boot_images"
  if [ ! -d "$BUILD_OUTPUT_DIR" ]; then
    echo "Build directory does not exist. Creating: $BUILD_OUTPUT_DIR"
    mkdir -p "$BUILD_OUTPUT_DIR" || {
      echo " Failed to create build directory."
      exit 1
    }
  fi

  if [ -d "$BOOTBIN_IMAGE_PATH" ]; then
    # Copy BOOT Image
    IMAGE_FILE=$(find "$BOOTBIN_IMAGE_PATH" \
        -name "BOOT-versal-2ve-2vm-vek385-revb-multidomain.bin")
    if [ -f "$IMAGE_FILE" ]; then
      cp -Lf "$IMAGE_FILE" "$BUILD_OUTPUT_DIR/BOOT.bin"
    else
      echo "No BOOT-versal-2ve-2vm-vek385-revb-multidomain.bin image found."
    fi
    # Copy OSPI BOOT Image
    OSPI_FILE=$(find "$BOOTBIN_IMAGE_PATH" \
        -name "edf-ospi-versal-2ve-2vm-vek385-revb-multidomain.bin")
    if [ -f "$OSPI_FILE" ]; then
      cp -Lf "$OSPI_FILE" \
        "$BUILD_OUTPUT_DIR/edf-ospi-versal-2ve-2vm-vek385-revb-multidomain.bin"
    else
      echo "No edf-ospi-versal-2ve-2vm-vek385-revb-multidomain.bin image found."
    fi

  else
    echo "BOOTBIN directory not found: $BOOTBIN_IMAGE_PATH"
  fi

  IMAGE_PATH="$YOCTO_deploy/images/amd-cortexa78-mali-common"
  if [ -d "$IMAGE_PATH" ]; then
    # Copy rootfs image
    ROOTFS_FILE=$(find "$IMAGE_PATH" -name "*.rootfs.tar.gz")
    if [ -f "$ROOTFS_FILE" ]; then
      cp -Lf "$ROOTFS_FILE" "$BUILD_OUTPUT_DIR/rootfs.tar.gz"
    else
      echo "No rootfs.tar.gz image found."
    fi

    # Copy rootfs.wic.xz image
    ROOTFS_FILE=$(find "$IMAGE_PATH" -name "edf-linux-disk-*.rootfs.wic.xz")
    if [ -f "$ROOTFS_FILE" ]; then
      cp -Lf "$ROOTFS_FILE" "$BUILD_OUTPUT_DIR/rootfs.wic.xz"
    else
      echo "No edf-linux-disk-*.rootfs.wic.xz image found."
    fi

    # Copy rootfs.wic.ufs image
    ROOTUFS_FILE=$(find "$IMAGE_PATH" -name "edf-linux-disk-*.rootfs.wic.ufs")
    if [ -f "$ROOTUFS_FILE" ]; then
      cp -Lf "$ROOTUFS_FILE" "$BUILD_OUTPUT_DIR/rootfs.wic.ufs"
    else
      echo "No edf-linux-disk-*.rootfs.wic.ufs image found."
    fi

    # Copy rootfs.wic.bmap image
    ROOTFS_FILE=$(find "$IMAGE_PATH" -name "edf-linux-disk-image*rootfs.wic.bmap")
    if [ -f "$ROOTFS_FILE" ]; then
      cp -Lf "$ROOTFS_FILE" "$BUILD_OUTPUT_DIR/rootfs.wic.bmap"
    else
      echo "No edf-linux-disk-image*rootfs.wic.bmap image found."
    fi

    # Copy kernel Image
    IMAGE_FILE=$(find "$IMAGE_PATH" -name "Image")
    if [ -f "$IMAGE_FILE" ]; then
      cp -Lf "$IMAGE_FILE" "$BUILD_OUTPUT_DIR/Image"
    else
      echo "No kernel Image found."
    fi
  else
    echo "Rootfs, Image and BOOT directory not found: $IMAGE_PATH"
  fi
fi

if [ $build_sdk -eq 1 ]; then
  echo "Building SDK..."
  #build sdk
  if MACHINE=amd-cortexa78-mali-common bitbake meta-edf-app-sdk; then
    echo "Yocto SDK Build successfully"
  else
    echo "Yocto SDK Build failed"
    return 1
  fi

  # In case only SDK is being built set YOCTO_deploy variable
  if [ -z "$YOCTO_deploy" ]; then
    if [ -z $YOCTO_TMP_DIR ]; then
      YOCTO_deploy="$ABS_PATH/build/tmp/deploy"
    else
      YOCTO_deploy="$YOCTO_TMP_DIR/deploy"
    fi
  fi

  # Set BUILD_OUTPUT_DIR if not already set
  if [ -z "$BUILD_OUTPUT_DIR" ]; then
    BUILD_OUTPUT_DIR="$ABS_PATH/../../artifact/amd/boot_images"
    if [ ! -d "$BUILD_OUTPUT_DIR" ]; then
      echo "Build directory does not exist. Creating: $BUILD_OUTPUT_DIR"
      mkdir -p "$BUILD_OUTPUT_DIR" || {
        echo " Failed to create build directory."
        exit 1
      }
    fi
  fi

  SDK_PATH="$YOCTO_deploy/sdk"
  if [ -d "$SDK_PATH" ]; then
    # Copy sdk
    SDK_FILE=$(find "$SDK_PATH" -name "*.sh" | head -n 1)
    if [ -f "$SDK_FILE" ]; then
      cp -f "$SDK_FILE" "$BUILD_OUTPUT_DIR/sdk.sh"
    fi
  else
    echo "sdk file not generated"
  fi
fi

cd $ABS_PATH
