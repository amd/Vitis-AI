# Copyright (c) 2026 Advanced Micro Devices, Inc.
# SPDX-License-Identifier: MIT
# -----------------------------------------------

# NOTE: The BD in this platform is named "bd", so its wrapper instance is
# "bd_i" (the fork design used "versal_gen2_platform_i").
set_property CLOCK_DEDICATED_ROUTE ANY_CMT_REGION [get_nets bd_i/mipi_rx_ss_hier/clk_wizard_0/inst/clock_primitive_inst/clk_151]
