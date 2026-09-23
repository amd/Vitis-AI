#+----------------------------------------------------------------------------
#| Vitis Makefile- Developed using new makefile code
#| This make file is generated when the test is run
#| Generated: 2024-04-21 14:14:48.332143
#+----------------------------------------------------------------------------
# Target and command definitions
XCXX = v++
SHELL = /bin/bash
RM = rm -rf
ABS_PATH	:= $(shell pwd -P)

#VSS Params
VSS_I0	= vss_npu_aieml_I0
VSS_I1	= vss_npu_aieml_I1

# Kernel definitions
SRC_VSS_I0   = $(VAISW_HOME)/npu_ip/$(NPU_IP)/npu_aieml_vss/
ifdef NPU_IP2
  SRC_VSS_I1  = $(VAISW_HOME)/npu_ip/$(NPU_IP2)/npu_aieml_vss/
endif
NMU_CSTR  = $(VAISW_HOME)/npu_ip

# Primary build targets
.PHONY: all clean cleanall

all:
	mkdir -p $(VSS_I0)
	cp $(SRC_VSS_I0)/* ./$(VSS_I0)/
	cp $(SRC_VSS_I0)/../fpga_info* ./$(VSS_I0)/
	cd $(VSS_I0) && ln -f -s libadf_offset_*.a libadf_I0.a
	cd $(VSS_I0) && ln -f -s npu_aieml_*.xo  npu_aieml_I0.xo
	cd $(VSS_I0) && ln -f -s vss_npu_aieml_*.vss vss_npu_aieml_I0.vss
	[ -z "$${NPU_IP2}" ] || mkdir -p $(VSS_I1)
	[ -z "$${NPU_IP2}" ] || cp $(SRC_VSS_I1)/* ./$(VSS_I1)/
	[ -z "$${NPU_IP2}" ] || cp $(SRC_VSS_I1)/../fpga_info* ./$(VSS_I1)/
	[ -z "$${NPU_IP2}" ] || (cd $(VSS_I1) && ln -f -s libadf_offset_*.a libadf_I1.a)
	[ -z "$${NPU_IP2}" ] || (cd $(VSS_I1) && ln -f -s npu_aieml_*.xo  npu_aieml_I1.xo)
	[ -z "$${NPU_IP2}" ] || (cd $(VSS_I1) && ln -f -s vss_npu_aieml_*.vss vss_npu_aieml_I1.vss)
	python3 config_merger.py
	python3 nmu_merger.py


clean:
	$(RM) $(VSS_I0) $(VSS_I1)

cleanall: clean
	$(RM) _x .Xil *.log *.compile_summary *.link_summary 2 $(VSS_I0) $(VSS_I1)

