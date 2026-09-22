#******************************************************************************
# Copyright (C) 2020-2022 Xilinx, Inc. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#******************************************************************************

############################## Help ##############################
.PHONY: help
help:
	@echo "Makefile Usage:"
	@echo ""
	@echo "  make link"
	@echo "      Link kernels with platform"
	@echo ""
	@echo "  make package"
	@echo "      Package SD image"
	@echo ""
	@echo "  make clean "
	@echo "      Remove IP files from vitis_prj folder and all the generated files"
	@echo ""

############################ Setting Up Project Variables #######################################
ABS_PATH		:= $(shell pwd -P)
PROJECT_NAME		?= example_design
TARGET			?= hw
#Platform setting
PLATFORM		?= $(ABS_PATH)/../hw/example_design_pfm_extensible.xsa
XCL_EMULATION_MODE	?= hw
unexport XCL_EMULATION_MODE
JOBS ?= $(shell nproc)

# MIPI/NPU_FW build flags -- read directly from the shared build.cfg. A
# command-line override ('make all MIPI=1') still takes precedence,
# and the '?=' below is only a fallback if build.cfg is missing.
-include ../build.cfg
MIPI ?= 0
NPU_FW ?= 0

# NPU_FW is not supported for this release.
ifeq ($(NPU_FW),1)
$(error NPU_FW=1 is not supported for this release. Exiting.)
endif

PLAIN_PDI = $(ABS_PATH)/link/_x/link/int/vpl_gen_fixed_pld.pdi
COMBINED_BIF = $(ABS_PATH)/link/_x/link/int/x_plus_ml_grouped.bif

ifeq ($(NPU_FW),1)
	NPU_FW_ELF ?=
endif

# Common compilers settings
XCXX_COMMON_OPTS =  --vivado.synth.jobs 4 -s -t $(TARGET) -I . --config $(ABS_PATH)/package_link.ini

#Build directory
BUILD_DIR		= $(XCL_EMULATION_MODE)
LINK_OUTPUT		:= $(ABS_PATH)/link/${PROJECT_NAME}_link.xsa

VITIS_KERNELS_PATH = $(ABS_PATH)/../../../../../vitis_kernels/pl
VITIS_KERNELS_CONFIG_PATH = $(ABS_PATH)/vitis_kernels_config/

IMAGE_PROCESSING_XO = $(ABS_PATH)/vitis_kernels_config/image_processing/image_processing.xo
IMAGE_PROCESSING_CFG = $(ABS_PATH)/vitis_kernels_config/image_processing/image_processing.cfg

XOS = $(IMAGE_PROCESSING_XO)
CFGS = $(IMAGE_PROCESSING_CFG)

IMAGE_PROCESSING_SRCS= ${VITIS_KERNELS_PATH}/image_processing/src/image_processing.cpp\
	     ${VITIS_KERNELS_PATH}/image_processing/src/v_hresampler.cpp\
	     ${VITIS_KERNELS_PATH}/image_processing/src/v_hscaler.cpp\
	     ${VITIS_KERNELS_PATH}/image_processing/src/v_dma.cpp\
	     ${VITIS_KERNELS_PATH}/image_processing/src/v_csc.cpp\
	     ${VITIS_KERNELS_PATH}/image_processing/src/v_vresampler.cpp\
	     ${VITIS_KERNELS_PATH}/image_processing/src/v_vscaler.cpp

IMAGE_PROCESSING_FLAGS := --kernel image_processing -I${VITIS_KERNELS_CONFIG_PATH}/image_processing\
             -I${VITIS_KERNELS_PATH}/image_processing/src/hls

# Overlay install dir
OVLY_INSTALL_DIR = $(ABS_PATH)/../artifact/amd/boot_images

# Which dtbo post-processing step(s) apply to overlay/x_plus_ml.dtbo, based on
# MIPI/NPU_FW. Order matters when both are 1: mipi_dtbo fully recompiles the
# overlay from lopper_output/pl.dtsi first, then npu_fw_dtbo appends the
# amdxdna-shmem fragment onto that same pl.dtsi and recompiles -- so both
# transformations land in the same final overlay regardless of combination.
# This ordering is enforced below via NPU_FW_DTBO_DEPS (npu_fw_dtbo depends
# on mipi_dtbo when MIPI=1), not by DTBO_DEPS list order -- package's own
# prerequisite list order isn't a make-guaranteed ordering, especially under
# `make -j`.
DTBO_DEPS :=
ifeq ($(MIPI),1)
DTBO_DEPS += mipi_dtbo
endif
ifeq ($(NPU_FW),1)
DTBO_DEPS += npu_fw_dtbo
endif

# npu_fw_dtbo must run after mipi_dtbo when both apply, since it decompiles
# and appends onto mipi_dtbo's output rather than the raw v++-packaged dtbo.
NPU_FW_DTBO_DEPS := _package
ifeq ($(MIPI),1)
NPU_FW_DTBO_DEPS += mipi_dtbo
endif

############################ Building steps ###################################
.PHONY: all xos link package _package mipi_dtbo npu_fw_dtbo
all: xos link package

xos: $(XOS) $(CFGS)
	@echo "All xos are built!"

$(IMAGE_PROCESSING_XO): $(IMAGE_PROCESSING_SRCS) | $(IMAGE_PROCESSING_CFG)
	@echo "Building image_processing.xo"
	v++ --platform $(PLATFORM) -t $(TARGET) -s $(IMAGE_PROCESSING_FLAGS) -c -o $@ $^ --freqhz 300MHz

$(IMAGE_PROCESSING_CFG): ${VITIS_KERNELS_CONFIG_PATH}/image_processing/image_processing_config.h
	bash ${VITIS_KERNELS_PATH}/image_processing/create_imgproc_config.sh $^
	mv image_processing.cfg $@


#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
#Link Image preprocessing PL kernel and AIE graph with Platform
link: $(LINK_OUTPUT)

$(LINK_OUTPUT):
	@echo "INFO:- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -"
	cd $(ABS_PATH); gmio_train -s --part xc2ve3858-ssva2112-2MP-e-S -o training-libadf.a
	cd $(ABS_PATH)/link; v++ $(XCXX_COMMON_OPTS) --platform $(PLATFORM) \
	--config $(ABS_PATH)/link/system.cfg \
        -l $(ABS_PATH)/training-libadf.a $(IMAGE_PROCESSING_XO) -o ${PROJECT_NAME}_link.xsa; cd $(ABS_PATH)

# Invoke _package, then whichever of mipi_dtbo/npu_fw_dtbo apply (DTBO_DEPS,
# selected above from MIPI/NPU_FW), then install overlay/ to OVLY_INSTALL_DIR.
# MIPI=0,NPU_FW=0 (default/main): DTBO_DEPS is empty, overlay/x_plus_ml.dtbo
# stays exactly as _package's own v++ packaging step produced it.
package: _package $(DTBO_DEPS)
	mkdir -p $(OVLY_INSTALL_DIR)
	@echo Installing overlay/ to $(OVLY_INSTALL_DIR) ...
	cp -r $(ABS_PATH)/overlay $(OVLY_INSTALL_DIR)/
	@echo Vitis packager step to generte dtb overlay and PL xclbin is Done!

# Package design as SD Card Imagea
_package: $(LINK_OUTPUT)
	@echo "INFO:- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -"
	@echo "INFO:Packaging $(notdir $@) for $(BUILD_DIR)..."
	rm -rf $(ABS_PATH)/sdtgen_output $(ABS_PATH)/lopper_output $(ABS_PATH)/package.hw \
    $(ABS_PATH)/package/package.hw
	mkdir -p ./lopper_output ./overlay
	v++ -p -s -f ./link/example_design_link.xsa --package.pl_metadata_only -o x_plus_ml.xclbin \
	--package.out_dir ./package.hw/
ifeq ($(NPU_FW),1)
	@echo "INFO: NPU_FW=1 -- embedding npu_fw.elf into overlay/x_plus_ml.pdi..."
	@test -f "$(NPU_FW_ELF)" || { echo "ERROR: NPU_FW_ELF not found: $(NPU_FW_ELF)"; exit 1; }
	@test -f "$(PLAIN_PDI)" || { echo "ERROR: $(PLAIN_PDI) not found -- Vivado/v++ packaging didn't run as expected"; exit 1; }
	@echo "all:" > "$(COMBINED_BIF)"
	@echo "{" >> "$(COMBINED_BIF)"
	@echo "  image" >> "$(COMBINED_BIF)"
	@echo "  {" >> "$(COMBINED_BIF)"
	@echo "    { type=bootimage, file=$(PLAIN_PDI) }" >> "$(COMBINED_BIF)"
	@echo "    { core=r52-0, cluster=0, file=$(NPU_FW_ELF) }" >> "$(COMBINED_BIF)"
	@echo "  }" >> "$(COMBINED_BIF)"
	@echo "}" >> "$(COMBINED_BIF)"
	bootgen -arch versal_2ve_2vm -image "$(COMBINED_BIF)" -o "$(ABS_PATH)/overlay/x_plus_ml.pdi" -w on
else
	@cp -f ./link/_x/link/int/vpl_gen_fixed_pld.pdi ./overlay/x_plus_ml.pdi
endif
	@cp -f ./x_plus_ml.xclbin ./overlay
	@cp -f ./package.hw/x_plus_ml.dtbo ./overlay
	@cp -f $(IMAGE_PROCESSING_CFG) ./overlay

# Update MIPI DTSI and regenerate device-tree blob overlay
mipi_dtbo: _package
	@echo "INFO: Compiling updated DTSI to overlay/x_plus_ml.dtbo..."
	cp -f $(ABS_PATH)/_x/package/lopper_out_dtbo/pl.dtsi ./lopper_output/pl.dtsi
	python3 $(ABS_PATH)/update_mipi_dtsi.py ./lopper_output/pl.dtsi --xsa $(LINK_OUTPUT)
	dtc -@ -O dtb -o ./overlay/x_plus_ml.dtbo ./lopper_output/pl.dtsi
	@echo "INFO: MIPI DTBO generation complete. overlay/x_plus_ml.dtbo has been replaced."

# Bake the amdxdna-shmem fragment into overlay/x_plus_ml.dtbo. Works from the
# lopper_output/pl.dtsi source (reusing mipi_dtbo's already-modified copy when
# MIPI=1, or seeding it fresh from the raw v++ packager output otherwise)
# instead of decompiling the packaged x_plus_ml.dtbo, to avoid a lossy
# binary dtb->dts->dtb round-trip.
npu_fw_dtbo: $(NPU_FW_DTBO_DEPS)
	@echo "INFO: Baking amdxdna-shmem fragment into overlay/x_plus_ml.dtbo..."
	@test -f ./lopper_output/pl.dtsi || cp -f $(ABS_PATH)/_x/package/lopper_out_dtbo/pl.dtsi ./lopper_output/pl.dtsi
	cat $(ABS_PATH)/amdxdna_shmem_fragment.dtsi >> ./lopper_output/pl.dtsi
	dtc -@ -O dtb -o ./overlay/x_plus_ml.dtbo ./lopper_output/pl.dtsi
	@echo "INFO: NPU_FW DTBO generation complete. overlay/x_plus_ml.dtbo has been replaced."

############################ Clean ############################################
#Clean build products
.PHONY: clean
clean:
	$(RM) -r _x hw prep_target_output .Xil vivado* *.txt *.log *.xsa .ipcache sample*.ini packaged_kernel_*_hw_* tmp_kernel_pack_*_hw_*
	$(RM) -r link/_x link/*.log link/.Xil link/.ipcache link/.crashReporter link/example_design_link.xsa link/*.summary $(ABS_PATH)/package/package_out
	$(RM) -r 2 package/_x package/*.log package/.Xil package/.ipcache package/.crashReporter package/hw_outputs package/*.summary package/*.xclbin
	$(RM) -r tmp training-libadf.a link/example_design_link.xsa.link_summary package/x_plus_ml.xclbin.package_summary
