set project_name $::env(PROJECT_NAME)
set pre_synth false

# Create platform project
create_project $project_name . -part $::env(CHIP_PART) -force
#set_param board.repoPaths ./boardRepo
set my_board [get_board_parts xilinx.com:vek385:part0:* -latest_file_version]
set_property board_part $my_board [current_project]

set vivado_path $::env(XILINX_VIVADO)

set_property platform.extensible true [current_project]
set_property ip_repo_paths ./custom_ips [current_project]
update_ip_catalog

# Import CED design and update it to add required custom changes
create_bd_design "bd" -mode batch
instantiate_example_design -template xilinx.com:design:versal_comn_platform:2.0 -design bd -options {Design_type.VALUE Extensible Include_AIE.VALUE true }
update_compile_order -fileset sources_1

# Apply Vitis AI specific block design customizations (NoC, LPDDR5X, DDRMC5) on top of CED design
source pfm_bd.tcl

# Source platform ports
set_property platform.extensible true [current_project]
set_property platform.board_id  "vek385-reva" [current_project]
set_property PFM_NAME {amd:VEK385:telluride:0.0} [get_files [current_bd_design].bd]

validate_bd_design
save_bd_design

make_wrapper -files [get_files $project_name.srcs/sources_1/bd/bd/bd.bd] -top
add_files -norecurse $project_name.gen/sources_1/bd/bd/hdl/bd_wrapper.v
add_files -norecurse $project_name.srcs/sources_1/bd/bd/bd.bd

set_property NOC_SOLUTION_FILE "" [get_runs impl_1]

#Overwrite the default rtl simulations models with tlm
set_property preferred_sim_model "tlm" [current_project]
update_compile_order -fileset sources_1

assign_bd_address

## Generate output products
generate_target all [get_files $project_name.srcs/sources_1/bd/bd/bd.bd]

# Platform exportation
set_property pfm_name {amd:vek385:example_design_pfm:0.0} [get_files -all $project_name.srcs/sources_1/bd/bd/bd.bd]
set_property platform.vendor {amd} [current_project]
set_property platform.name ${project_name}_pfm [current_project]

# Pre_synth Platform Flow applicable for non-segmented designs
if {$pre_synth} {
  puts "Generating the pre_synth xsa"
  set_property platform.platform_state "pre_synth" [current_project]
  write_hw_platform -force -file ./${project_name}_pfm.xsa

} else {

  puts "Generating the post_implementation xsa started"
  # Post_implememtation Platform
  # Synthesis Run
  launch_runs synth_1 -jobs 20
  wait_on_run synth_1
 
  # adding it as workaround for pl overlay pdi programming failure
  set_param noc.enableNOCClockGating false

  # Implementation Run
  launch_runs impl_1 -to_step write_device_image
  wait_on_run impl_1

  open_run impl_1

  # Protects the static/platform portion of the NoC solution when PL is reconfigured later for segmented boot
  set_property lock true [get_noc_net_routes -of [get_noc_logical_path -filter initial_boot]]
  
  # Generating dynamic reload extensible XSA as default hardware platform
  write_hw_platform -fixed -include_bit -force -file ./example_design_pfm_fixed.xsa
  write_hw_platform -hw -force -file ./example_design_pfm_extensible.xsa
  puts "Generation of both post_implementation fixed and extensible xsa completed"

}

exit
