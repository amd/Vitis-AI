#******************************************************************************
# Copyright (C) 2020-2022 Xilinx, Inc. All rights reserved.
# Copyright (C) 2022-2023 Advanced Micro Devices, Inc. All rights reserved.
# SPDX-License-Identifier: MIT
#******************************************************************************

include platform.mk

# Default to requiring BSP_PATH unless overridden in platform.mk
REQUIRES_BSP ?= yes

############################## Help Section ##############################
.PHONY: help

help::
	@echo 'Makefile Usage:'
	@echo ''
	@echo '  All the make commands install platform to "platform/xpfm/$(PROJECT_NAME)_pfm/export/$(PROJECT_NAME)_pfm/$(PROJECT_NAME)_pfm.xpfm"'
	@echo ''
	@echo '  make all'
	@echo '      Command used to generate platform hw and sw components followed by the final platform.'
	@echo ''
	@echo '  make xsa'
	@echo '      Command to generate the hardware xsa component of the platform.'
	@echo ''
	@echo '  make linux XSA=$(ABS_PATH)/sw/platform/$(PROJECT_NAME).xsa'
	@echo '      Command to generate the software component of the platform by taking xsa file as input.'
	@echo ''
	@echo '  make platform'
	@echo '      Run this command when hw and sw components of platform is already built.'
	@echo ''
	@echo '  make clean '
	@echo '      Command to remove all the generated files.'
	@echo ''

.PHONY: all xsa linux platform clean check-bsp-path

# Validate BSP_PATH if required by platform
check-bsp-path:
ifeq ($(REQUIRES_BSP),yes)
ifndef BSP_PATH
	$(error BSP_PATH is not set)
else
ifeq ("$(wildcard $(BSP_PATH))","")
	$(error "BSP_PATH($(BSP_PATH)) doesn't exist")
endif
endif
endif

xsa:
	$(MAKE) -C hw all

linux: check-bsp-path sw/platform/example_design_pfm.xsa
	$(MAKE) -C sw all BSP_PATH=$(BSP_PATH)

platform: sw/petalinux/images/linux/sdk.sh
	$(MAKE) -C platform all
	@echo 'Platform build complete'

all: check-bsp-path
ifndef NPU_IP
	$(error NPU_IP is not set, please source npu_ip/settings.sh to select the IP to generate)
endif
	#Remove existing bitstream files
	@rm -rf fpga_info_*.txt
	#Get IP specific bitstream file from npu_ip
	@cp $(VAISW_HOME)/npu_ip/$(NPU_IP)/fpga_info_*.txt .
	@[ -z "$$NPU_IP2" ] || cp $(VAISW_HOME)/npu_ip/$(NPU_IP2)/fpga_info_*.txt .

	$(MAKE) -C hw all
	$(MAKE) -C sw all BSP_PATH=$(BSP_PATH)
	$(MAKE) -C platform all

clean:
	$(MAKE) -C hw clean
	$(MAKE) -C sw clean
	rm -rf $(PLATFORM_DIR)
	rm -rf *.xsa platform/.Xil platform/xpfm platform/cmd.log
	@find . -type f -name "fpga_info_*.txt" -exec rm -f {} +
