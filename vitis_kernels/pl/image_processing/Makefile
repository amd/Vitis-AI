##########################################################################
 # Copyright 2020 - 2022 Xilinx, Inc.
 # Copyright (C) 2022-2023 Advanced Micro Devices, Inc.
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
###########################################################################

TARGET := hw

XOCCFLAGS := --platform $(PLATFORM_FILE) -t $(TARGET) -s -g 

IMAGE_PROCESSING_FLAGS := --kernel image_processing -I. -I./src/hls

.PHONY: clean

all: image_processing.cfg image_processing.xo

image_processing.cfg: image_processing_config.h
	bash create_imgproc_config.sh image_processing_config.h

image_processing.xo: src/image_processing.cpp src/v_hresampler.cpp src/v_hscaler.cpp src/v_dma.cpp src/v_csc.cpp src/v_vresampler.cpp src/v_vscaler.cpp
	v++ $(XOCCFLAGS) $(IMAGE_PROCESSING_FLAGS) -c -o xo/$@ $^
	#vitis_prj link command using .xo file form this path.
	cp -rf xo/image_processing.xo .

clean:
	$(RM) -r xo/* *_x .Xil sd_card* *.xclbin *.ltx *.log *.info packaged_kernel* tmp_kernel* vivado*
	$(RM) *summary* *.str *.hwh image_processing.cfg
