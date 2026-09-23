set project_name $::env(PROJECT_NAME)
#set project_name example_design
# Create platform project
create_project $project_name . -part $::env(CHIP_PART) -force
set_param board.repoPaths ./boardRepo

#Rev B1 board
#set my_board [get_board_parts xilinx.com:vek280_es:part0:* -latest_file_version]

#Production Board
set my_board [get_board_parts xilinx.com:vek280:part0:* -latest_file_version]

set_property board_part $my_board [current_project]

set_property platform.extensible true [current_project]
set_property ip_repo_paths "./custom_ips ../vitis_prj/kernels/npu_tail" [current_project]
update_ip_catalog

# Import BD
# 1- Compatible with PP enabled build using : PERF IP, DUAL IP, STD IP
# 2- Compatible with IP for Interleaved DDRs (M1)
# 3- Compatible with disabled PP builds
if {[info exists ::env(NPU_IP)] && ![string match "*M1" $::env(NPU_IP)] } {
  puts "Building with 3 DDRs platform"
  if {[catch {source pfm_bd.tcl} err]} {
    puts "ERROR while sourcing pfm_bd.tcl: $err"
    puts $::errorInfo
    exit 1
  }
} elseif {[info exists ::env(NPU_IP)] && [string match "*M1" $::env(NPU_IP)]} {
  puts "Building with interleaved DDRs platform"
  if {[catch {source pfm_bd_il.tcl} err]} {
    puts "ERROR while sourcing pfm_bd_il.tcl: $err"
    puts $::errorInfo
    exit 1
  }
} else {
  puts "ERROR : NPU_IP env variable isn't set.\nExiting... "
  exit 1
}

#write_bd_tcl -force bd.tcl
make_wrapper -files [get_files $project_name.srcs/sources_1/bd/bd/bd.bd] -top
add_files -norecurse $project_name.gen/sources_1/bd/bd/hdl/bd_wrapper.v
add_files -norecurse $project_name.srcs/sources_1/bd/bd/bd.bd
set_property preferred_sim_model "tlm" [current_project]
update_compile_order -fileset sources_1

# Generate output products
generate_target all [get_files $project_name.srcs/sources_1/bd/bd/bd.bd]

# Platform exportation
set_property pfm_name "mipsology:mipso:${project_name}_pfm:0.0" [get_files -all $project_name.srcs/sources_1/bd/bd/bd.bd]
set_property platform.vendor {mipsology} [current_project]
set_property platform.name ${project_name}_pfm [current_project]
write_hw_platform -force -file ./${project_name}_pfm.xsa
exit
