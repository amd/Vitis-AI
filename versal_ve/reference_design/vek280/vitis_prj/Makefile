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
	@echo "      Package EDF image (BOOT.BIN + xclbin)"
	@echo ""
	@echo "  make all NPU_IP=<IP NAME>"
	@echo "      Generate all design"
	@echo ""
	@echo "  make clean "
	@echo "      Remove IP files from vitis_prj folder and all the generated files"
	@echo ""

############################ Setting Up Project Variables #######################################
ABS_PATH        := $(shell pwd -P)
PROJECT_NAME    ?= example_design
TARGET          ?= hw
#Platform setting
PLATFORM        = $(ABS_PATH)/../hw/${PROJECT_NAME}_pfm.xsa
PKG_PLATFORM    = $(ABS_PATH)/link/${PROJECT_NAME}_link.xsa
XCL_EMULATION_MODE  ?= hw
unexport XCL_EMULATION_MODE

DISABLE_IMG_PROCE ?= False
export DISABLE_IMG_PROCE

# Common compilers settings
XCXX_COMMON_OPTS = --vivado.synth.jobs 4 -s -t $(TARGET) -I . --config $(ABS_PATH)/package_link.ini

VSS_I0        = vss_npu_aieml_I0
NPUVSSFILE_I0 = $(ABS_PATH)/kernels/npu_vss/${VSS_I0}/${VSS_I0}.vss
LIBADF_I0     = $(ABS_PATH)/kernels/npu_vss/$(VSS_I0)/libadf_I0.a

ifdef NPU_IP2
  VSS_I1        = vss_npu_aieml_I1
  NPUVSSFILE_I1 = $(ABS_PATH)/kernels/npu_vss/${VSS_I1}/${VSS_I1}.vss
  LIBADF_I1     = $(ABS_PATH)/kernels/npu_vss/$(VSS_I1)/libadf_I1.a
endif

#Build directory
BUILD_DIR     = $(XCL_EMULATION_MODE)
LINK_OUTPUT  := $(ABS_PATH)/link/${PROJECT_NAME}_link.xsa

#Get npu ip from share path
ifneq ($(filter clean,$(MAKECMDGOALS)),clean)

ifndef NPU_IP
    $(error NPU_IP is not set, please source npu_ip/settings.sh to select the IP to generate)
endif

endif
# Check for the directory and set RESNET50_SNAPSHOT accordingly
SNAPSHOT_DIR_50 = $(ABS_PATH)/../snapshot.$(NPU_IP).resnet50.TF

ifeq ($(wildcard $(SNAPSHOT_DIR_50)),)
    RESNET50_SNAPSHOT =
else
    RESNET50_SNAPSHOT = --package.sd_dir=$(SNAPSHOT_DIR_50)
endif

ifdef NPU_IP2
  # Check for the directory and set RESNET50_SNAPSHOT accordingly
  SNAPSHOT2_DIR_50  = $(ABS_PATH)/../snapshot.$(NPU_IP2).resnet50.TF
  ifeq ($(wildcard $(SNAPSHOT2_DIR_50)),)
      RESNET50_SNAPSHOT2 =
  else
      RESNET50_SNAPSHOT2 = --package.sd_dir=$(SNAPSHOT2_DIR_50)
  endif
endif

ifneq ($(filter $(shell echo $(DISABLE_IMG_PROCE) | tr '[:upper:]' '[:lower:]'), false 0),)
    IMG_PROCE_XO  = $(ABS_PATH)/kernels/image_processing/image_processing.xo
    IMG_PROCE_CFG = --package.sd_file=$(ABS_PATH)/kernels/image_processing/image_processing.cfg
else
    IMG_PROCE_XO  =
    IMG_PROCE_CFG =
endif

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
############################ Building steps ###################################################

.PHONY: all package
all: package

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
#Link Image preprocessing PL kernel, NPUIP PL kernel and AIE graph with Platform
link: $(LINK_OUTPUT)

$(LINK_OUTPUT):
	@echo "INFO:- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -"
	@echo "INFO:Building $(notdir $@) for $(BUILD_DIR)..."

	if [ "$(IMG_PROCE_XO)" != "" ] && [ ! -e $(IMG_PROCE_XO) ] ; then \
		echo "Building image process XO from sources..."; \
		$(MAKE) -C $(ABS_PATH)/kernels/image_processing all PLATFORM_FILE=$(PLATFORM); \
	fi

	#Remove existed bitstream file
	@-[ -f "$(FPGA_INFO_FILE)" ] && $(RM) -f -r $$( cat $(FPGA_INFO_FILE) ) ; true

	#Get IP specific bitstream file from npu_ip
	@cp $(VAISW_HOME)/npu_ip/$(NPU_IP)/fpga_info_*.txt .
	-@[ "$(NPU_IP2)" != "" ] && cp $(VAISW_HOME)/npu_ip/$(NPU_IP2)/fpga_info_*.txt .

	#Get IP specific constraints from npu_ip (referenced relatively by the merged system.cfg; first IP only, matching config_merger)
	#Also creates link/ so config_merger.py / nmu_merger.py can write system.cfg and place_pl_nmu.tcl
	@rm -rf $(ABS_PATH)/link/constraints
	@mkdir -p $(ABS_PATH)/link/constraints
	@cp -rf $(VAISW_HOME)/npu_ip/$(NPU_IP)/link/constraints/. $(ABS_PATH)/link/constraints/

	#Copy common Vivado scripts (repo-tracked, not in NPU IP package)
	@cp -f $(ABS_PATH)/../../common/vitis_prj/vivado_scripts/*.tcl $(ABS_PATH)/link/constraints/

	#build NPU vss subsystem
	@cd $(ABS_PATH)/kernels/npu_vss && make -C . all

	@echo "INFO:- - - - - - - - - npu_vss build successful - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -"
	cd $(ABS_PATH)/link; v++ --from_step vpl.impl.post_route_phys_opt_design $(XCXX_COMMON_OPTS) --platform $(PLATFORM) -l $(NPUVSSFILE_I0) $(NPUVSSFILE_I1) $(IMG_PROCE_XO) -o ${PROJECT_NAME}_link.xsa --config $(ABS_PATH)/link/system.cfg

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# Package the design (EDF/Yocto flow — Vitis 2026.1)
# v++ -p  : with libadf.a → produces system.pdi + aie_image CDO + x_plus_ml_unpatched.xclbin + BIF
# xclbinutil: mem-patches the xclbin (CMA allocation limit 0x400000 → 0x300000; keep until validated)
# bootgen : reads v++ generated BIF (system.pdi + aie_image CDO sections), injects EDF APU subsystem
#           (ZOCL_DTB = EDF Yocto bitbake device-tree with npu_versal.dtsi; EDF_ATF; EDF_UBOOT)
# Output  : hw_outputs/{BOOT.BIN, x_plus_ml.xclbin}  — wic-copied by create_image.sh assemble
package: $(LINK_OUTPUT)
	@[ -n "$(ZOCL_DTB)" ]  || { echo "ERROR: ZOCL_DTB not set  — run create_pfm_sw.sh first"; exit 1; }
	@[ -n "$(EDF_UBOOT)" ] || { echo "ERROR: EDF_UBOOT not set — run create_pfm_sw.sh first"; exit 1; }
	@[ -n "$(EDF_ATF)" ]   || { echo "ERROR: EDF_ATF not set   — run create_pfm_sw.sh first"; exit 1; }
	@[ -f "$(ZOCL_DTB)" ]  || { echo "ERROR: ZOCL_DTB file missing: $(ZOCL_DTB)"; exit 1; }
	@[ -f "$(EDF_UBOOT)" ] || { echo "ERROR: EDF_UBOOT file missing: $(EDF_UBOOT)"; exit 1; }
	@[ -f "$(EDF_ATF)" ]   || { echo "ERROR: EDF_ATF file missing: $(EDF_ATF)"; exit 1; }
	@echo "INFO:Packaging (EDF: v++ -p + libadf + bootgen with EDF assets -> BOOT.BIN + xclbin)"
	-@rm -rf $(ABS_PATH)/package/hw_outputs $(ABS_PATH)/package/hw_outputs_unpatched
	# Step 1: v++ -p with libadf.a — generates aie_image CDO section in BIF + unpatched xclbin
	# LIBADF_I1 is empty when NPU_IP2 is not set; expands safely to nothing.
	cd $(ABS_PATH)/package && v++ -p --debug --save-temp --platform $(PKG_PLATFORM) --target hw \
		--package.out_dir hw_outputs_unpatched \
		$(LIBADF_I0) $(LIBADF_I1) \
		-o x_plus_ml_unpatched.xclbin
	# Step 2: mem-patch xclbin (CMA allocation limit 0x400000 → 0x300000)
	# NOTE: not documented in Confluence for standard VEK280 EDF 2026.1; keep until board NPU test validates.
	mkdir -p $(ABS_PATH)/package/hw_outputs
	cd $(ABS_PATH)/package && \
	xclbinutil --dump-section MEM_TOPOLOGY:JSON:./mem_topology.json \
		-i x_plus_ml_unpatched.xclbin && \
	xclbinutil --dump-section GROUP_TOPOLOGY:JSON:./group_topology.json \
		-i x_plus_ml_unpatched.xclbin && \
	sed -i 's#0x400000#0x300000#g' mem_topology.json group_topology.json && \
	python3 -c "import json; [d.__setitem__(list(d)[0],{**d[list(d)[0]],'m_mem_data':[{**e,'m_used':'1'} if e.get('m_tag','').startswith('GMIO') else e for e in d[list(d)[0]]['m_mem_data']]}) or open(f,'w').write(json.dumps(d,indent=2)) for f in ['mem_topology.json','group_topology.json'] for d in [json.load(open(f))]]" && \
	xclbinutil --replace-section MEM_TOPOLOGY:JSON:./mem_topology.json \
		--replace-section GROUP_TOPOLOGY:JSON:./group_topology.json \
		-i x_plus_ml_unpatched.xclbin \
		-o hw_outputs/x_plus_ml.xclbin && \
	rm -f mem_topology.json group_topology.json
	# Step 3: Assemble BOOT.BIN — read v++ generated BIF (has system.pdi + aie_image CDO sections),
	# strip closing }, inject EDF APU subsystem with ZOCL_DTB (EDF bitbake device-tree with npu_versal.dtsi)
	# Result: all:{ bootimage(system.pdi) + aie_image(CDO) + apu_subsystem(ZOCL_DTB+ATF+UBOOT) }
	head -n -1 $(ABS_PATH)/package/hw_outputs_unpatched/x_plus_ml_unpatched.bif \
		> $(ABS_PATH)/package/hw_outputs/boot.bif
	printf 'image\n{\n  id = 0x1c000000, name=apu_subsystem\n  { type=raw, load=0x1000, file=%s }\n  { core=a72-0, exception_level=el-3, trustzone, file=%s }\n  { core=a72-0, exception_level=el-2, load=0x8000000, file=%s }\n}\n}\n' \
		"$(ZOCL_DTB)" \
		"$(EDF_ATF)" \
		"$(EDF_UBOOT)" \
		>> $(ABS_PATH)/package/hw_outputs/boot.bif
	cd $(ABS_PATH)/package && bootgen -arch versal -image $(ABS_PATH)/package/hw_outputs/boot.bif \
		-o $(ABS_PATH)/package/hw_outputs/BOOT.BIN -w 2>&1
	@echo "INFO:EDF package done -> package/hw_outputs/{BOOT.BIN, x_plus_ml.xclbin}"
	@echo "INFO:  BOOT.BIN size: $$(du -h $(ABS_PATH)/package/hw_outputs/BOOT.BIN | awk '{print $$1}')"
	@[ $$(stat -c%s $(ABS_PATH)/package/hw_outputs/BOOT.BIN) -gt 1000000 ] || \
	    { echo "WARN: BOOT.BIN looks tiny — check system.pdi was packaged correctly"; }
	python3 kernels/npu_vss/npu_utilization.py  kernels/npu_vss/$(VSS_I0) $(NPU_IP) && mv npu_utilization.rpt $(ABS_PATH)/package/hw_outputs/npu_I0_utilization.rpt || echo "WARN: npu_I0 utilization rpt skipped (missing build.rpt)"
	[ "$(NPU_IP2)" = "" ] || (python3 kernels/npu_vss/npu_utilization.py kernels/npu_vss/$(VSS_I1) $(NPU_IP2) && mv npu_utilization.rpt $(ABS_PATH)/package/hw_outputs/npu_I1_utilization.rpt) || echo "WARN: npu_I1 utilization rpt skipped"
	[ "$(NPU_TAIL)" = "" ] || (python3 kernels/npu_tail/npu_tail_utilization.py $(ABS_PATH) && mv npu_tail_utilization.rpt $(ABS_PATH)/package/hw_outputs/npu_tail_utilization.rpt)
	#Copy post-route reports into hw_outputs so they land in output/ via top-level mv
	@[ ! -d $(ABS_PATH)/link/reports ] || cp -rf $(ABS_PATH)/link/reports $(ABS_PATH)/package/hw_outputs/

############################ Clean ############################################################
#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
#Clean build products
.PHONY: clean
clean:
	@cd $(ABS_PATH)/kernels/npu_vss && make -C . cleanall
	#@cd $(ABS_PATH)/kernels/softmax_vss && make -C . cleanall
	$(RM) -r _x hw prep_target_output .Xil vivado* *.txt *.log *.xsa .ipcache sample*.ini packaged_kernel_*_hw_* tmp_kernel_pack_*_hw_*
	$(RM) -r link/_x link/*.log link/.Xil link/.ipcache link/.crashReporter link/example_design_link.xsa link/*.summary $(ABS_PATH)/package/package_out
	$(RM) -r package/_x package/*.log package/.Xil package/.ipcache package/.crashReporter package/hw_outputs package/hw_outputs_unpatched package/*.summary package/*.xclbin
	$(RM) -r link/example_design_link.xsa.link_summary package/x_plus_ml_unpatched.xclbin.package_summary
	$(RM) -r link/constraints link/system.cfg link/place_pl_nmu.tcl link/reports
