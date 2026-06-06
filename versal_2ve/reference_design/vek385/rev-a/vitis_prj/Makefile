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
PLATFORM		= $(ABS_PATH)/../hw/example_design_pfm_extensible.xsa
XCL_EMULATION_MODE	?= hw
unexport XCL_EMULATION_MODE
JOBS ?= $(shell nproc)


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
############################ Building steps ###################################
.PHONY: all xos link package _package
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

# Invole _package target and install overloay/ to OVLY_INSTALL_DIR
package: _package
	mkdir -p $(OVLY_INSTALL_DIR)
	@echo Installing overlay/ to $(OVLY_INSTALL_DIR) ...
	cp -r $(ABS_PATH)/overlay $(OVLY_INSTALL_DIR)/
	@echo Vitis packager step to generte dtb overlay and PL xclbin is Done!

# Package design as SD Card Imagea
_package: $(LINK_OUTPUT)
	@echo "INFO:- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -"
	@echo "INFO:Packaging $(notdir $@) for $(BUILD_DIR)..."
	rm -rf $(ABS_PATH)/sdt_lopper_output $(ABS_PATH)/sdtout_temp $(ABS_PATH)/package.hw \
    $(ABS_PATH)/package/package.hw
	mkdir -p ./sdtout_temp ./overlay
	v++ -p -s -f ./link/example_design_link.xsa --package.pl_metadata_only -o x_plus_ml.xclbin \
	--package.out_dir ./package.hw/
	@cp -f ./link/_x/link/int/vpl_gen_fixed_pld.pdi ./overlay/x_plus_ml.pdi
	@cp -f ./x_plus_ml.xclbin ./overlay
	@cp -f ./package.hw/x_plus_ml.dtbo ./overlay
	@cp -f $(IMAGE_PROCESSING_CFG) ./overlay


############################ Clean ############################################
#Clean build products
.PHONY: clean
clean:
	$(RM) -r _x hw prep_target_output .Xil vivado* *.txt *.log *.xsa .ipcache sample*.ini packaged_kernel_*_hw_* tmp_kernel_pack_*_hw_*
	$(RM) -r link/_x link/*.log link/.Xil link/.ipcache link/.crashReporter link/example_design_link.xsa link/*.summary $(ABS_PATH)/package/package_out
	$(RM) -r 2 package/_x package/*.log package/.Xil package/.ipcache package/.crashReporter package/hw_outputs package/*.summary package/*.xclbin
	$(RM) -r tmp training-libadf.a link/example_design_link.xsa.link_summary sdtout_temp package/x_plus_ml.xclbin.package_summary
