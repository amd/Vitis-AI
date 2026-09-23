#******************************************************************************
# Copyright (C) 2020-2022 Xilinx, Inc. All rights reserved.
# Copyright (C) 2022-2025 Advanced Micro Devices, Inc. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#******************************************************************************

ABS_PATH := $(shell pwd -P)
RM = rm -rf
VIVADO = $(XILINX_VIVADO)/bin/vivado
JOBS ?= $(shell nproc)

VIV_DESIGN = example_design

.PHONY: help
help:
	@echo 'Usage:'
	@echo ''
	@echo '  make xsa'
	@echo '    Generate extensible xsa for platform generation'
	@echo ''

.PHONY: all
all: xsa

xsa: ips
	./run_vivado.sh

ips:
	@cd $(ABS_PATH)/../vitis_prj/kernels/npu_tail && make all

.PHONY: clean
clean:
	$(RM) $(VIV_DESIGN)* vivado* pspmc.hwdef .Xil *dynamic* *.log *.xpe .crashReporter/
	@make -C  $(ABS_PATH)/../vitis_prj/kernels/npu_tail clean
