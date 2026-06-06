# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
if { ! [info exists ::env(ENABLE_FREQ_ADJUST)] ||
     ([string tolower $::env(ENABLE_FREQ_ADJUST)] eq "false" || [string tolower $::env(ENABLE_FREQ_ADJUST)] eq "0") } {
     return
}

puts "#===================================================================="
puts "#===========================              ==========================="
puts "#==================                                =================="
puts "#=========               FPGA INFO Update                   ========="
puts "#==================                                =================="
puts "#===========================              ==========================="
puts "#===================================================================="

#GET FREQUENCY PARAMETERS
set clkout0_divide          [get_property CLKOUT0_DIVIDE  [get_cells bd_i/versal_clk_4x/inst/clock_primitive_inst/MMCME5_inst]]
set divclk_divide           [get_property DIVCLK_DIVIDE   [get_cells bd_i/versal_clk_4x/inst/clock_primitive_inst/MMCME5_inst]]
set set_multiplier          [get_property CLKFBOUT_MULT   [get_cells bd_i/versal_clk_4x/inst/clock_primitive_inst/MMCME5_inst]]
set set_divider             [expr ( $divclk_divide * $clkout0_divide)]

# -- Compute all variables linked to MMCM Configuration
set clk4xInput                        250
set clk4xMultiplier                   $set_multiplier
set clk4xDivider                      $set_divider
set clk4x                             [expr int([expr $clk4xInput * $clk4xMultiplier / $clk4xDivider])]
set clk4xClkfboutMul                  [expr int($clk4xMultiplier)]
set clk4xClkfboutMulFrac              0.0
set clk4xClkfboutMulFracEna           0
set clk4xDivclkDivide                 $divclk_divide
set clk4xClkout0Div                   $clkout0_divide
set clk4xClkout0DivFrac               0.0
set clk4xClkout0DivFracEna            0
set frequencyMeterCoef                64000

#CREATE FINAL FPGA_INFO
set info_path [file normalize [file dirname [info script]]/../../]
set fpga_info [glob $info_path/fpga_info_*.txt]
if { [llength $fpga_info] != 1 } {
  puts "ERROR: should be one and only one fpga_info file in vitis_prj/, please fix it"
  exit 1
}
set fpga_info [regsub -all -line {^.*fpga_info_} $fpga_info {\1}]
set timestamp [regsub -all -line {.txt} $fpga_info {\1}]

set in [open $info_path/fpga_info_$timestamp.txt r]
set out [open $info_path/fpga_info_${timestamp}_post_route.txt w]

while {[gets $in line] != -1} {
  if {[string match "    clk4xMultiplier=*" $line]} {
    puts $out "    clk4xMultiplier=$clk4xMultiplier"
  } elseif {[string match "    clk4xDivider=*" $line]} {
    puts $out "    clk4xDivider=$clk4xDivider"
  } elseif {[string match "    clk4x=*" $line]} {
    puts $out "    clk4x=$clk4x"
  } elseif {[string match "    clk4xClkfboutMul=*" $line]} {
    puts $out "    clk4xClkfboutMul=$clk4xClkfboutMul"
  } elseif {[string match "    clk4xDivclkDivide=*" $line]} {
    puts $out "    clk4xDivclkDivide=$clk4xDivclkDivide"
  } elseif {[string match "    clk4xClkout0Div=*" $line]} {
    puts $out "    clk4xClkout0Div=$clk4xClkout0Div"
  } else {
    puts $out $line
  }
}

close $in
close $out

file delete -force $info_path/fpga_info_${timestamp}.txt
file rename -force $info_path/fpga_info_${timestamp}_post_route.txt  $info_path/fpga_info_${timestamp}.txt

# reports for customer
report_utilization    -file zebra_example_utilization.rpt
report_utilization    -file zebra_example_utilization_hierarc.rpt -hierarchical -hierarchical_depth 3

# Compilation time log
set compilDate [exec date]
set timelog [open [file normalize [file dirname [info script]]/../../compil_time.txt] a]
puts $timelog "$compilDate Start LINK : post-route"
close $timelog
