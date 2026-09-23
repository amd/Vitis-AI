#******************************************************************************
# Copyright (C) 2020-2022 Xilinx, Inc. All rights reserved.
# Copyright (C) 2022-2023 Advanced Micro Devices, Inc. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#******************************************************************************

RM = rm -rf
VIVADO = $(XILINX_VIVADO)/bin/vivado
JOBS ?= $(shell nproc)

.PHONY: help
help:
	@echo 'Usage:'
	@echo ''
	@echo '  make platform'
	@echo '    Generate Vitis platform using already generated hw and sw components'
	@echo ''

.PHONY: all
all: platform

platform:
	./generate_platform.sh

.PHONY: clean
clean:
	$(RM) xpfm cmd.log
