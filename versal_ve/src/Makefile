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
#Build npu stack
vart_ml:
	@cd vart_ml && \
	make -C . all && make install-sdk

vart_ml_install_tar: vart_ml
	@cd vart_ml && make install-tar

# Remove vart build target files
vart_ml-clean:
	@cd vart_ml && make clean

all: vart_ml vart_ml_install_tar

clean: vart_ml-clean

# Phony targets
.PHONY: all clean vart_ml-clean vart_ml
