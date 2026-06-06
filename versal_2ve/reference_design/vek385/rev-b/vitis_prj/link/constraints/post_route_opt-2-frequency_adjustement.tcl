# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.

# ZEBRA_IP : file : ignore

if { ! [info exists ::env(ENABLE_FREQ_ADJUST)] ||
     [string tolower $::env(ENABLE_FREQ_ADJUST)] eq "false" ||
     [string tolower $::env(ENABLE_FREQ_ADJUST)] eq "0" } {
   return
}
set STANDALONE            0

puts "#===================================================================="
puts "#===========================              ==========================="
puts "#==================                                =================="
puts "#=========              Frequency Adjustment                ========="
puts "#==================                                =================="
puts "#===========================              ==========================="
puts "#===================================================================="

if { ${STANDALONE} != 0 } {
  puts "#=========                Stand Alone Mode                  ========="
  puts "#===================================================================="
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
}

set frequency_error         0

#########################
# Core clocks
#########################
set mmcm_instance_name          "bd_i/versal_clk_4x/inst/clock_primitive_inst/MMCME5_inst"
# no adjustement when slack is greater or equal to slack_threshold (marging error / light overclocking)
set slack_threshold             -0.00
# default value 500 Mhz => QA running 449 Mhz, frequency_tolerance = (100 * (1 - 449/500) ~ 10.20 % => 11.00 %
set frequency_tolerance         -50.00

set set_clkin1_period           [get_property CLKIN1_PERIOD   [get_cells ${mmcm_instance_name}]]
set set_clkout0_divide          [get_property CLKOUT0_DIVIDE  [get_cells ${mmcm_instance_name}]]
set set_divclk_divide           [get_property DIVCLK_DIVIDE   [get_cells ${mmcm_instance_name}]]
set set_clkfbout_mult           [get_property CLKFBOUT_MULT   [get_cells ${mmcm_instance_name}]]
set real_clkout0_divide         ${set_clkout0_divide}
set real_divclk_divide          ${set_divclk_divide}
set real_clkfbout_mult          ${set_clkfbout_mult}
set set_frequency               0
set real_frequency              0

# frequency unit : Mhz, period unit : ns
set required_frequency          [expr 1000 * $set_clkfbout_mult / ( $set_divclk_divide * $set_clkout0_divide * $set_clkin1_period)]

set slack_4x                    [expr [get_property SLACK [get_timing_paths -max_paths 1 -nworst 1 -from ${clock_clk_4x}]]       ]
set slack                       ${slack_4x}

for { set iteration 0}  {$iteration < 4} {incr iteration} {
  if { ${slack} < ${slack_threshold} } {
    # frequency unit : Mhz, period unit : ns
    set set_clkout0_divide      [get_property CLKOUT0_DIVIDE  [get_cells ${mmcm_instance_name}]]
    set set_divclk_divide       [get_property DIVCLK_DIVIDE   [get_cells ${mmcm_instance_name}]]
    set set_clkfbout_mult       [get_property CLKFBOUT_MULT   [get_cells ${mmcm_instance_name}]]
    set set_frequency           [expr 1000 * $set_clkfbout_mult / ( $set_divclk_divide * $set_clkout0_divide * $set_clkin1_period)]

    set real_frequency          [expr 1000 / (1000 / $set_frequency -$slack)]
    set real_clkout0_divide     ${set_clkout0_divide}
    set real_divclk_divide      ${set_divclk_divide}
    set real_clkfbout_mult      [expr 0.001 * $real_frequency * $real_divclk_divide * $real_clkout0_divide * $set_clkin1_period ]
    set real_clkfbout_mult      [expr floor($real_clkfbout_mult) ]
    set real_frequency          [expr 1000 * $real_clkfbout_mult / ( $real_divclk_divide * $set_clkout0_divide * $set_clkin1_period)]

    # VCO requirements are met : VCO frequency must be included in [2160 MHz - 4320 MHz]
    set real_vco_frequency      [expr 1000 * $real_clkfbout_mult / ( $real_divclk_divide * $set_clkin1_period)]
    if { ${real_vco_frequency}       < 2160.0 } {
      set real_clkout0_divide   [expr       ${real_clkout0_divide} * ceil(2160.0 / ${real_vco_frequency}) ]
      set real_clkfbout_mult    [expr       ${real_clkfbout_mult}  * ceil(2160.0 / ${real_vco_frequency}) ]
    } elseif { ${real_vco_frequency} > 4320.0 } {
      set real_divclk_divide    [expr       ${real_divclk_divide}  * ceil(${real_vco_frequency} / 4320.0) ]
      set real_clkout0_divide   [expr floor(${real_clkout0_divide} / ceil(${real_vco_frequency} / 4320.0))]
    }

    # change frequency :
    set_property CLKOUT0_DIVIDE ${real_clkout0_divide} [get_cells ${mmcm_instance_name}]
    set_property DIVCLK_DIVIDE  ${real_divclk_divide}  [get_cells ${mmcm_instance_name}]
    set_property CLKFBOUT_MULT  ${real_clkfbout_mult}  [get_cells ${mmcm_instance_name}]
    set slack_4x                [expr [get_property SLACK [get_timing_paths -max_paths 1 -nworst 1 -from ${clock_clk_4x}]]       ]
    set slack                   ${slack_4x}
  }
}

if { ${set_clkfbout_mult} != ${real_clkfbout_mult} } {
  puts [format "WARNING: Current clk_4x's frequency : %.0f MHz, is lower than required frequency : %.0f MHz." ${real_frequency} ${required_frequency}]
  puts [format "WARNING: Current clk_4x's slack is: %.3f ns" ${slack_4x}]
  # tolerance
  set frequency_variation [expr 100 * (${real_frequency} / ${set_frequency} -1.0)]
  if { ${frequency_variation} < ${frequency_tolerance} } {
    puts [format "ERROR: Current clk_4x's frequency is %.2f %% lower than of required frequency. Maximum tolerance is %.2f %%." ${frequency_variation} ${frequency_tolerance} ]
    set frequency_error [expr ${frequency_error} + 1]
  }
} else {
  puts [format "INFO: clk_4x: Required frequency %.0f MHz met (slack = %.3f ns)." ${required_frequency} ${slack}]
  puts [format "INFO: Current clk_4x's slack is: %.3f ns" ${slack_4x}]
}

#########################
# Service Bus clock
#########################
set mmcm_instance_name      "bd_i/versal_clk_srvbus/inst/clock_primitive_inst/MMCME5_inst"
set slack_threshold         -0.00
set frequency_tolerance     -0.00

set set_clkin1_period           [get_property CLKIN1_PERIOD   [get_cells ${mmcm_instance_name}]]
set set_clkout0_divide          [get_property CLKOUT0_DIVIDE  [get_cells ${mmcm_instance_name}]]
set set_divclk_divide           [get_property DIVCLK_DIVIDE   [get_cells ${mmcm_instance_name}]]
set set_clkfbout_mult           [get_property CLKFBOUT_MULT   [get_cells ${mmcm_instance_name}]]
set real_clkout0_divide         ${set_clkout0_divide}
set real_divclk_divide          ${set_divclk_divide}
set real_clkfbout_mult          ${set_clkfbout_mult}
set set_frequency               0
set real_frequency              0

# frequency unit : Mhz, period unit : ns
set required_frequency          [expr 1000 * $set_clkfbout_mult / ( $set_divclk_divide * $set_clkout0_divide * $set_clkin1_period)]

set slack_srv                   [expr [get_property SLACK [get_timing_paths -max_paths 1 -nworst 1 -from ${clock_clk_srv}]]]
set slack                       ${slack_srv}

for { set iteration 0}  {$iteration < 4} {incr iteration} {
  if { ${slack} < ${slack_threshold} } {
    # frequency unit : Mhz, period unit : ns
    set set_clkout0_divide      [get_property CLKOUT0_DIVIDE  [get_cells ${mmcm_instance_name}]]
    set set_divclk_divide       [get_property DIVCLK_DIVIDE   [get_cells ${mmcm_instance_name}]]
    set set_clkfbout_mult       [get_property CLKFBOUT_MULT   [get_cells ${mmcm_instance_name}]]
    set set_frequency           [expr 1000 * $set_clkfbout_mult / ( $set_divclk_divide * $set_clkout0_divide * $set_clkin1_period)]

    set real_frequency          [expr 1000 / (1000 / $set_frequency -$slack)]
    set real_clkout0_divide     ${set_clkout0_divide}
    set real_divclk_divide      ${set_divclk_divide}
    set real_clkfbout_mult      [expr 0.001 * $real_frequency * $real_divclk_divide * $real_clkout0_divide * $set_clkin1_period ]
    set real_clkfbout_mult      [expr floor($real_clkfbout_mult) ]
    set real_frequency          [expr 1000 * $real_clkfbout_mult / ( $real_divclk_divide * $set_clkout0_divide * $set_clkin1_period)]

    # VCO requirements are met : VCO frequency must be included in [2160 MHz - 4320 MHz]
    set real_vco_frequency      [expr 1000 * $real_clkfbout_mult / ( $real_divclk_divide * $set_clkin1_period)]
    if { ${real_vco_frequency}       < 2160.0 } {
      set real_clkout0_divide   [expr       ${real_clkout0_divide} * ceil(2160.0 / ${real_vco_frequency}) ]
      set real_clkfbout_mult    [expr       ${real_clkfbout_mult}  * ceil(2160.0 / ${real_vco_frequency}) ]
    } elseif { ${real_vco_frequency} > 4320.0 } {
      set real_divclk_divide    [expr       ${real_divclk_divide}  * ceil(${real_vco_frequency} / 4320.0) ]
      set real_clkout0_divide   [expr floor(${real_clkout0_divide} / ceil(${real_vco_frequency} / 4320.0))]
    }

    # change frequency :
    set_property CLKOUT0_DIVIDE ${real_clkout0_divide} [get_cells ${mmcm_instance_name}]
    set_property DIVCLK_DIVIDE  ${real_divclk_divide}  [get_cells ${mmcm_instance_name}]
    set_property CLKFBOUT_MULT  ${real_clkfbout_mult}  [get_cells ${mmcm_instance_name}]
    set slack_srv               [expr [get_property SLACK [get_timing_paths -max_paths 1 -nworst 1 -from ${clock_clk_srv}]]]
    set slack                   ${slack_srv}
  }
}

if { ${set_clkfbout_mult} != ${real_clkfbout_mult} } {
  puts [format "WARNING: Current clk_srv's frequency : %.0f MHz, is lower than required frequency : %.0f MHz." ${real_frequency} ${required_frequency}]
  puts [format "WARNING: Current clk_srv's slack is: %.3f ns" ${slack_srv}]
  # tolerance
  set frequency_variation [expr 100 * (${real_frequency} / ${set_frequency} -1.0)]
  if { ${frequency_variation} < ${frequency_tolerance} } {
    puts [format "ERROR: Current clk_srv's frequency is %.2f %% lower than of required frequency. Maximum tolerance is %.2f %%." ${frequency_variation} ${frequency_tolerance} ]
    set frequency_error [expr ${frequency_error} + 1]
  }
} else {
  puts [format "INFO: clk_srv: Required frequency %.0f MHz met (slack = %.3f ns)." ${required_frequency} ${slack}]
  puts [format "INFO: Current clk_srv's slack is: %.3f ns" ${slack_srv}]
}

#########################
# report post adjustement
#########################
puts [format "INFO: Generate bd_wrapper_timing_summary_post_freq_adj.rpt file ..." ]
report_timing_summary -file bd_wrapper_timing_summary_post_freq_adj.rpt  -max_paths 10 -nworst 1 -warn_on_violation  -report_unconstrained
report_utilization    -file bd_wrapper_utilization_post_freq_adj_hierarc.rpt -hierarchical -hierarchical_depth 10

# cleanup
unset -nocomplain slack_threshold
unset -nocomplain frequency_tolerancex
unset -nocomplain frequency_variation
unset -nocomplain required_frequency
unset -nocomplain set_clkin1_period
unset -nocomplain set_clkout0_divide
unset -nocomplain set_divclk_divide
unset -nocomplain set_clkfbout_mult
unset -nocomplain set_frequency
unset -nocomplain real_clkout0_divide
unset -nocomplain real_divclk_divide
unset -nocomplain real_clkfbout_mult
unset -nocomplain real_frequency
unset -nocomplain slack
unset -nocomplain slack_4x
unset -nocomplain slack_ddr
unset -nocomplain slack_srv
unset -nocomplain iteration

#########################
# error checking
#########################
if { [expr ${frequency_error} != 0 ] } {
  unset -nocomplain frequency_error
  puts "WARNING: Desired frequencies not met"
#  exit 1
} else {
  unset -nocomplain frequency_error
}

puts "#=== end of Frequency Adjustment ===================================="

