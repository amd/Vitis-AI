# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.

#TODO : clean zebra_reconfig variable to set only 1 variable
set zebra_top zebra_top_embedded
if { !$FPGA_INFO_ONLY } {
  set design_configurable [get_cells -hierarchical  -filter "REF_NAME =~ *pl_reconfig"]
  set design_conf_path [get_cells -hierarchical  -filter "REF_NAME =~ *pl_reconfig"]
  set dsg_cfg [get_cells -hierarchical  -filter "REF_NAME =~ *pl_reconfig"]
}
