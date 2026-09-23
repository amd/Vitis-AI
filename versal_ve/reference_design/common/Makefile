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
ABS_PATH := $(shell pwd -P)
PLATFORM_NAME := $(notdir $(ABS_PATH))

TARGETS := $(NPU_IP) $(NPU_IP2)

PLATFORM_OUTPUT := $(ABS_PATH)/$(PLATFORM_NAME)_platform/platform/xpfm
SKIP_SNAPSHOT :=

# Default target
.PHONY: all
all: vitis-package

# Platform target
.PHONY: platform

platform:
	@if [ -d "$(PLATFORM_OUTPUT)" ]; then \
	    echo "Platform available. Skipping platform build."; \
	else \
	    echo "Building platform..."; \
	    cd $(PLATFORM_NAME)_platform && $(MAKE) -C . all BSP_PATH=$(BSP_PATH); \
	fi

# ResNet50 snapshot target
.PHONY: resnet50-snapshot

resnet50-snapshot:
	@for target in $(TARGETS); do \
	  SNAPSHOT_OUTPUT_50=$(ABS_PATH)/snapshot.$${target}.resnet50.TF; \
    echo -e "\n\n PATH $${SNAPSHOT_OUTPUT_50} \n target $${target} \n\n\n\n\n"; \
	  if [ ! -d $${SNAPSHOT_OUTPUT_50} ] && [ -z "$(SKIP_SNAPSHOT)" ]; then \
	      echo "Generate ResNet50 TF2 snapshot if docker available,else install docker and generates snapshot...."; \
	      cd $(ABS_PATH)/../../ && ./docker/run.bash --acceptLicense -- /bin/bash -c "source npu_ip/settings.sh $${target} && cd examples/python_examples/batcher && VAISW_SNAPSHOT_DIRECTORY=$${SNAPSHOT_OUTPUT_50} ./run_classification.sh -f tensorflow2 -n resnet50 --batchSizePerCore 1"; \
	  else \
	      echo "Snapshot already generated. Skipping snapshot generation."; \
	  fi; \
	done

# Vitis package target
.PHONY: vitis-package
vitis-package: resnet50-snapshot platform
	@$(MAKE) -C vitis_prj all
	@rm -rf output
	@mkdir -p output
	mv vitis_prj/package/hw_outputs/* output/
	[ "$(NO_SDK_BUILD)" != "" ] || mv $(PLATFORM_NAME)_platform/sw/petalinux/images/linux/sdk.sh output
	cp -f $(PLATFORM_NAME)_platform/sw/petalinux/project-spec/meta-user/recipes-core/vart/files/wheels/vart_x*.whl output/ 2>/dev/null || true

# Clean targets
.PHONY: clean vitis-clean platform-clean npu-ip-clean snapshot-clean
clean: vitis-clean npu-ip-clean platform-clean snapshot-clean
	rm -rf output snapshot.*

vitis-clean:
	@$(MAKE) -C vitis_prj clean &> /dev/null
	rm -rf output

npu-ip-clean:
	@$(MAKE) -C $(ABS_PATH)/../../npu_ip clean

platform-clean:
	@$(MAKE) -C $(PLATFORM_NAME)_platform clean &> /dev/null

snapshot-clean:
	@$(MAKE) -C $(ABS_PATH)/../../python_examples/ssdResnet34 clean
	@$(MAKE) -C $(ABS_PATH)/../../samples clean

# Phony targets
.PHONY: all platform vitis-package resnet50-snapshot clean platform-clean vitis-clean npu-ip-clean snapshot-clean

