#******************************************************************************
# Copyright (C) 2020-2022 Xilinx, Inc. All rights reserved.
# Copyright (C) 2022-2023 Advanced Micro Devices, Inc. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#******************************************************************************

RM = rm -rf
VIVADO = $(XILINX_VIVADO)/bin/vivado
JOBS ?= $(shell nproc)

# FMC card / build switches come from the shared build.cfg (rev-b/build.cfg),
# which is authoritative: values defined there win. The '-include' pulls them in
# as make variables, and the '?=' defaults below apply only when build.cfg is
# missing (or omits a key). Note run_vivado.sh also re-reads build.cfg, so it is
# the single source of truth for the platform build.
-include ../build.cfg
FMC_CARD ?= 96716A
NPU_FW ?= 0
MIPI ?= 0

# NPU_FW is not supported for this release.
ifeq ($(NPU_FW),1)
$(error NPU_FW=1 is not supported for this release. Exiting.)
endif

VIV_DESIGN = example_design

SDTGEN_TCL       = sdtgen.tcl
PFM_FIXED_XSA    = example_design_pfm_fixed.xsa
PFM_EXT_XSA      = example_design_pfm_extensible.xsa
SDT_OUTPUT       = sdt_outdir
BOARD_DTSI       = versal2-vek385-revb

export FMC_CARD
export NPU_FW
export MIPI

.PHONY: help
help:
	@echo 'Usage:'
	@echo ''
	@echo '  make xsa'
	@echo '    Generate xsa files (skipped if both already exist)'
	@echo '    FMC_CARD is read from rev-b/build.cfg (authoritative) and selects'
	@echo '    the FMC daughter card pinout:'
	@echo '      96716A - Xylon FMC-12 logiFMC-96716A (MIPI2 + MIPI3) [default]'
	@echo '      9296A  - logiFMC-9296A (MIPI2 + MIPI6)'
	@echo ''
	@echo '  make sdt'
	@echo '    Run sdtgen to produce system device tree from xsa'
	@echo ''

.PHONY: all
all: sdt

# File-based target: run_vivado.sh is skipped if both XSA files already exist.
# To rebuild platform XSA files, run 'make clean' followed by 'make all'.
$(PFM_FIXED_XSA) $(PFM_EXT_XSA):
	./run_vivado.sh

xsa: $(PFM_FIXED_XSA) $(PFM_EXT_XSA)
	@echo "INFO: Platform XSA already exists. To rebuild, run 'make clean' followed by 'make all'."

.PHONY: sdt
sdt: xsa
	@if [ ! -d "$(SDT_OUTPUT)" ]; then \
	  sdtgen $(SDTGEN_TCL) -xsa_path $(PFM_FIXED_XSA) -sdt_path $(SDT_OUTPUT) -board_dts $(BOARD_DTSI); \
	fi

.PHONY: clean
clean:
	$(RM) $(VIV_DESIGN)* .repo.yaml vivado* pspmc.hwdef .Xil *dynamic* *.log *.xpe *.ncr .crashReporter/
	$(RM) $(PFM_FIXED_XSA) $(PFM_EXT_XSA) $(SDT_OUTPUT)
