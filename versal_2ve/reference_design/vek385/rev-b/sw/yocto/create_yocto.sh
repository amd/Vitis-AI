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

# Set it to 1 to disable amd-edf user
disable_amd_edf=0
VITIS_AI_LAYER="meta-vitis-ai"
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


### This is to enter as root user, will remove this patch in release #######
if [ $disable_amd_edf -eq 1 ] ; then
  # Check if EXTRA_IMAGE_FEATURES is already defined
  if grep -q '^#*EXTRA_IMAGE_FEATURES' "$LOCAL_CONF_PATH"; then
    # Replace existing line (whether commented or not)
    sed -i \
      's|^#*EXTRA_IMAGE_FEATURES.*|EXTRA_IMAGE_FEATURES ?= "debug-tweaks"|' \
      "$LOCAL_CONF_PATH"
  else
    # Append if not found
    echo 'EXTRA_IMAGE_FEATURES ?= "debug-tweaks"' >> "$LOCAL_CONF_PATH"
  fi
fi

# Add vitis-ai layer
if [ ! -d $ABS_PATH/sources/$VITIS_AI_LAYER ]; then
  bitbake-layers create-layer $ABS_PATH/sources/$VITIS_AI_LAYER
  rm -rf $ABS_PATH/sources/$VITIS_AI_LAYER/recipes-example
  rm -rf $ABS_PATH/sources/$VITIS_AI_LAYER/COPYING.MIT
  cp -rf $ABS_PATH/meta-vitis-ai/* $ABS_PATH/sources/$VITIS_AI_LAYER/
  bitbake-layers add-layer $ABS_PATH/sources/$VITIS_AI_LAYER
fi

cat << 'EOF' >> "$LOCAL_CONF_PATH"

IMAGE_INSTALL:append = "packagegroup-vaiml"
PACKAGECONFIG:append:pn-gdb = " tui"
TOOLCHAIN_HOST_TASK:append = " nativesdk-python3-pip nativesdk-python3-numpy nativesdk-python3-setuptools nativesdk-python3-build nativesdk-python3-wheel nativesdk-python3-protobuf nativesdk-python3-pybind11 nativesdk-protobuf "
TOOLCHAIN_TARGET_TASK:append = " ryzenai-wheels-dev opencv-dev jansson-dev vart-ml-dev vvas-utils-dev vvas-gst-plugins-dev vart-x-dev hip-dev"
EOF

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

  BOOTBIN_IMAGE_PATH="$YOCTO_deploy/images/versal2-vek385-sdt-full"
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
        -name "BOOT-versal2-vek385-sdt-full.bin")
    if [ -f "$IMAGE_FILE" ]; then
      cp -Lf "$IMAGE_FILE" "$BUILD_OUTPUT_DIR/BOOT.bin"
      cp -Lf "$IMAGE_FILE" \
        "$BUILD_OUTPUT_DIR/edf-ospi-versal2-vek385-sdt-full.bin"
    else
      echo "No BOOT-versal2-vek385-sdt-full.bin image found."
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
  #if MACHINE=versal2-vek385-sdt-full bitbake meta-edf-app-sdk; then
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
