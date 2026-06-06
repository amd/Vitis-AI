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
#   bash script to build the entire image
#

# Create platform fixed and extensible XSAs
source create_pfm_hw.sh

# Create BOOT.bin, Image and rootfs.tar.gz using fixed XSA and Yocto build flow
source create_pfm_sw.sh

# Build Vitis application overlay PDI and DTB using extensible XSA
source create_vitis_app.sh
