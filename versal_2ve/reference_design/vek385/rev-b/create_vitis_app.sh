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
#   bash script to build the vitis application
#

echo "Start building the gmio_train Vitis applicaiton..."
make -C vitis_prj clean
if make -C vitis_prj all; then
  echo "Vitis build for gmio_train AIE kernel is successful."
else
  echo "Vitis build for gmio_train AIE kernel is failed."
  exit 1
fi
