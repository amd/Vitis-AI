# Copyright(C) 2023-2026 Advanced Micro Devices Inc.  All Rights Reserved.

puts "#========= file : post_route_opt-99-reports.tcl ============================="

# Resolve output dir: link/reports/ (sibling of link/constraints/ where this script is copied)
set _rpt_dir [file normalize [file join [file dirname [info script]] ../reports]]
if { ![file isdirectory $_rpt_dir] } {
    file mkdir $_rpt_dir
}

set _hier_log [file join $_rpt_dir utilization_post_route_opt_hierarc.log]

report_utilization \
    -file $_hier_log \
    -hierarchical -hierarchical_depth 10

# Append the chip's total AVAILABLE resources
set _part   [get_parts [get_property PART [current_design]]]
set _slices [get_property SLICES     $_part]
set _bram   [get_property BLOCK_RAMS $_part]
set _uram   [get_property ULTRA_RAMS $_part]
set _dsp    [get_property DSP         $_part]
set _aie    [llength [get_sites -quiet -filter {SITE_TYPE =~ *AIE*CORE*}]]
set _aienmu [llength [get_sites -quiet -filter {SITE_TYPE =~ AIE*NOC}]]
set _plnmu  [llength [get_sites -quiet -filter {SITE_TYPE =~ *NMU*512*}]]

set _availf [open $_hier_log a]
puts $_availf "#NPU_AVAIL LUTS [expr {$_slices * 8}]"
puts $_availf "#NPU_AVAIL REGISTERS [expr {$_slices * 16}]"
puts $_availf "#NPU_AVAIL BLOCK_RAMS $_bram"
puts $_availf "#NPU_AVAIL URAM $_uram"
puts $_availf "#NPU_AVAIL DSP $_dsp"
puts $_availf "#NPU_AVAIL PL_NMU $_plnmu"
puts $_availf "#NPU_AVAIL AIE_NMU $_aienmu"
puts $_availf "#NPU_AVAIL AIE $_aie"
close $_availf

unset _availf _part _slices _bram _uram _dsp _aie _aienmu _plnmu

report_timing_summary \
    -file [file join $_rpt_dir timing_summary_post_route_opt.log]

unset _rpt_dir _hier_log

puts "#========= file end : post_route_opt-99-reports.tcl ============================="
