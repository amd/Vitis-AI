#******************************************************************************
# Copyright (C) 2020-2022 Xilinx, Inc. All rights reserved.
# Copyright (C) 2022-2023 Advanced Micro Devices, Inc. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#******************************************************************************

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

xsa:
	./run_vivado.sh

.PHONY: clean
clean:
	$(RM) $(VIV_DESIGN)* .repo.yaml vivado* pspmc.hwdef .Xil *dynamic* *.log *.xpe .crashReporter/
