########################################################################
#
# Copyright (C) 2024 Advanced Micro Devices, Inc.
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
#########################################################################
SHELL := /bin/bash

export BB_ENV_PASSTHROUGH_ADDITIONS += VART_X_WHL
export VART_X_WHL

.PHONY: all clean

## Build everything: hw XSA -> EDF Yocto SW -> Vitis app
all:
	source ./create_image.sh

## Clean (default = all). Granular: make clean CLEAN="hw vitis amd-edf output npu_ip"
clean:
	source ./clean_image.sh $(CLEAN)
