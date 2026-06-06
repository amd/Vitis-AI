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
#   bash script to build the host/demo application
#

ABS_PATH=$PWD
echo $ABS_PATH

BOOT_IMAGES_PATH=$ABS_PATH/artifact/amd/boot_images/

# inject BOOT.BIN file into partition 1 of the .wic.ufs image
wic cp --sector-size 4096 $BOOT_IMAGES_PATH/BOOT.bin $BOOT_IMAGES_PATH/rootfs.wic.ufs:1
wic ls --sector-size 4096 $BOOT_IMAGES_PATH/rootfs.wic.ufs:1

# Generate a block map (.bmap) file for the updated .wic.ufs image .
bmaptool create -o $BOOT_IMAGES_PATH/rootfs.wic.ufs.bmap $BOOT_IMAGES_PATH/rootfs.wic.ufs

echo "Creating rootfs.wic.ufs.xz. This may take some time..."
xz -c $BOOT_IMAGES_PATH/rootfs.wic.ufs > $BOOT_IMAGES_PATH/rootfs.wic.ufs.xz

if [ $? -eq 0 ]; then
    echo "Compression successful. Removing original file..."
    rm -rf "$BOOT_IMAGES_PATH/rootfs.wic.ufs"
else
    echo "Compression failed. Keeping original file."
fi

