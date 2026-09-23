#******************************************************************************
# Copyright (C) 2020-2022 Xilinx, Inc. All rights reserved.
# Copyright (C) 2022-2023 Advanced Micro Devices, Inc. All rights reserved.
# SPDX-License-Identifier: MIT
#******************************************************************************

.PHONY: all linux clean

all linux:
	$(info patch sw/Makefile for petalinux building)
	bash ./create_petalinux.sh $(BSP_PATH)

clean:
	$(RM) -r petalinux tmp platform cmd.log
