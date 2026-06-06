# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.

# reduce frequency of clock 4x
# 449 Mhz = (1000 × 97 ÷ (9 × 6 × 1 ÷ (250 × 10^6))) × 10^−9
# 375 Mhz = (1000 × 81 ÷ (9 × 6 × 1 ÷ (250 × 10^6))) × 10^−9
# 400 Mhz = (1000 × 87 ÷ (9 × 6 × 1 ÷ (250 × 10^6))) × 10^−9
if { [info exists ::env(ENABLE_FREQ_ADJUST)] &&
     ([string tolower $::env(ENABLE_FREQ_ADJUST)] eq "true" || [string tolower $::env(ENABLE_FREQ_ADJUST)] eq "1") } {
     if { [get_cells -quiet bd_i/versal_clk_4x/inst/clock_primitive_inst/MMCME5_inst] != "" } {
          set_property CLKFBOUT_MULT 108 [get_cells bd_i/versal_clk_4x/inst/clock_primitive_inst/MMCME5_inst]
          set_property DIVCLK_DIVIDE 9 [get_cells bd_i/versal_clk_4x/inst/clock_primitive_inst/MMCME5_inst]
          set_property CLKOUT1_DIVIDE 6 [get_cells bd_i/versal_clk_4x/inst/clock_primitive_inst/MMCME5_inst]
     }
}
# workaround to reduce jitter (Clock Uncertainty) see AR76369 - should be fixed in Vivado 2022.x
set_property USER_RAM_AVERAGE_ACTIVITY 0 [current_design]

foreach cell [ get_cells -hierarchical -filter {ORIG_REF_NAME == bd_versal_clk_srvbus_0 || REF_NAME == bd_versal_clk_srvbus_0} ] {
    set clock_clk_srv [get_clocks -include_generated_clocks -of_objects [get_pins $cell/clk_srv]]
    if {[llength $clock_clk_srv] != 0} {
        set_clock_groups -asynchronous -group ${clock_clk_srv}
    }
}

foreach cell [ get_cells -hierarchical -filter {ORIG_REF_NAME == bd_versal_clk_4x_0 || REF_NAME == bd_versal_clk_4x_0} ] {
    set clock_clk_4x [get_clocks -include_generated_clocks -of_objects [get_pins $cell/clk_500_o1]]
    if {[llength $clock_clk_4x] != 0} {
        set_clock_groups -asynchronous -group ${clock_clk_4x}
    }
}
