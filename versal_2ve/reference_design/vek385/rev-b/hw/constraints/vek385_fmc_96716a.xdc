# Copyright (c) 2026 Advanced Micro Devices, Inc.
# SPDX-License-Identifier: MIT
# -----------------------------------------------
# Pin constraints for Xylon FMC-12 logiFMC-96716A card (MIPI2 + MIPI3)

#MIPI2
#LA29_N	MIPI2_D1N
#LA29_P	MIPI2_D1P
#LA33_N	MIPI2_D2N
#LA33_P	MIPI2_D2P
#LA32_N	MIPI2_D3N
#LA32_P	MIPI2_D3P
#LA31_N	MIPI2_CLKN
#LA31_P	MIPI2_CLKP
#LA30_N	MIPI2_D0N
#LA30_P	MIPI2_D0P

#CLK
set_property PACKAGE_PIN AL42     [get_ports "MIPI2_clk_p"]
set_property PACKAGE_PIN AM43     [get_ports "MIPI2_clk_n"]

#D0
set_property PACKAGE_PIN AM41     [get_ports "MIPI2_data_p[0]"]
set_property PACKAGE_PIN AL41     [get_ports "MIPI2_data_n[0]"]

#D1
set_property PACKAGE_PIN AM44     [get_ports "MIPI2_data_p[1]"]
set_property PACKAGE_PIN AL44     [get_ports "MIPI2_data_n[1]"]

#D2
set_property PACKAGE_PIN AN42     [get_ports "MIPI2_data_p[2]"]
set_property PACKAGE_PIN AN43     [get_ports "MIPI2_data_n[2]"]

#D3
set_property PACKAGE_PIN AP44     [get_ports "MIPI2_data_p[3]"]
set_property PACKAGE_PIN AP45     [get_ports "MIPI2_data_n[3]"]

set_property IOSTANDARD MIPI_DPHY [get_ports "MIPI2_*"]
set_property DIFF_TERM_ADV TERM_100 [get_ports "MIPI2_*"]

#MIPI3 (Xylon FMC-12 96716A card - CH3, Deserializer 3, FMC_IIC_3)

#CLK
set_property PACKAGE_PIN AR41     [get_ports "MIPI3_clk_p"]
set_property PACKAGE_PIN AT42     [get_ports "MIPI3_clk_n"]

#D0
set_property PACKAGE_PIN AU41     [get_ports "MIPI3_data_p[0]"]
set_property PACKAGE_PIN AU42     [get_ports "MIPI3_data_n[0]"]

#D1
set_property PACKAGE_PIN AV37     [get_ports "MIPI3_data_p[1]"]
set_property PACKAGE_PIN AV38     [get_ports "MIPI3_data_n[1]"]

#D2
set_property PACKAGE_PIN AU38     [get_ports "MIPI3_data_p[2]"]
set_property PACKAGE_PIN AU39     [get_ports "MIPI3_data_n[2]"]

#D3
set_property PACKAGE_PIN AR38     [get_ports "MIPI3_data_p[3]"]
set_property PACKAGE_PIN AT39     [get_ports "MIPI3_data_n[3]"]

set_property IOSTANDARD MIPI_DPHY [get_ports "MIPI3_*"]
set_property DIFF_TERM_ADV TERM_100 [get_ports "MIPI3_*"]

# FMC IIC (Xylon FMC-12 new FMC card)
set_property PACKAGE_PIN AN45 [get_ports FMC_IIC_2_scl_io]
set_property PACKAGE_PIN AM46 [get_ports FMC_IIC_2_sda_io]
set_property IOSTANDARD LVCMOS12 [get_ports FMC_IIC_2_s*]

set_property PACKAGE_PIN BC15 [get_ports FMC_IIC_3_scl_io]
set_property PACKAGE_PIN BC14 [get_ports FMC_IIC_3_sda_io]
set_property IOSTANDARD LVCMOS12 [get_ports FMC_IIC_3_s*]

set_property PACKAGE_PIN AP38 [get_ports FMC_IIC_5_scl_io]
set_property PACKAGE_PIN AP39 [get_ports FMC_IIC_5_sda_io]
set_property IOSTANDARD LVCMOS12 [get_ports FMC_IIC_5_s*]
