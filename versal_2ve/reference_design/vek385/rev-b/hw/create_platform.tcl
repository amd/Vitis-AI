set project_name $::env(PROJECT_NAME)
set fmc_card $::env(FMC_CARD)
set mipi $::env(MIPI)
set pre_synth false

# Create platform project
create_project $project_name . -part $::env(CHIP_PART) -force
#set_param board.repoPaths ./boardRepo
set my_board [get_board_parts xilinx.com:vek385_1:part0:* -latest_file_version]
set_property board_part $my_board [current_project]

set vivado_path $::env(XILINX_VIVADO)

set_property platform.extensible true [current_project]
set_property ip_repo_paths ./custom_ips [current_project]
update_ip_catalog

# Force a consistent simulation model for every cell in the design.
# The added video subsystems (mipi_rx_ss_hier / hdmi_tx_ss_hier) instantiate
# several NoC IPs (axi_noc2_*) that would otherwise default to rtl and make
# validate_bd_design fail with "different Simulation Modes" (BD 41-2662).
set_property preferred_sim_model tlm [current_project]

# Import CED design and update it to add required custom changes
create_bd_design "bd" -mode batch
instantiate_example_design -template xilinx.com:design:edf_base:1.0 -design bd
update_compile_order -fileset sources_1

source custom_pfm_ports_bd.tcl
# Apply Vitis AI specific block design customizations (NoC, LPDDR5X, DDRMC5) on top of CED design
source custom_ddr_cfg_bd.tcl

# Add RPU to AIE remap settings for 16 columns (sentry mode).
# Only applied on the NPU firmware build (NPU_FW=1 in rev-b/build.cfg).
if {[info exists ::env(NPU_FW)] && $::env(NPU_FW) eq "1"} {
  source custom_sentrymode_pfm_bd.tcl
}

# ===========================================================================
# Video pipeline (MIPI-RX capture + HDMI-TX display) integration
#
# The edf_base CED ships with example ISP_hier and VCU_hier subsystems that are
# unused by this platform. They (and their Master_NoC INI connections) are
# removed and replaced with the mipi_rx_ss_hier / hdmi_tx_ss_hier video
# subsystems. The subsystem definitions are kept modular in dedicated Tcl files
# (mipi_rx_ss_hier.tcl / hdmi_tx_ss_hier.tcl), mirroring the reference
# vaiml_platform flow.
#
# NOTE: In the current edf_base design the AIE config NoC is named
#       "AIE_ConfigNoc" (it was "ConfigNoc" in the older design).
# ===========================================================================

# --- Remove the unused ISP_hier / VCU_hier example subsystems --------------
# ISP_hier is fed by Master_NoC M08/M09/M10, VCU_hier by Master_NoC M07.
# Delete their interface nets first, then the hierarchies themselves. This only
# frees the Master_NoC master INI ports previously driven by ISP/VCU; the base
# Master_NoC configuration (NUM_NMI, PS-slave routing) is otherwise unchanged.
# The freed M07/M08 ports are reused for the mipi loopback (M06 stays as base).
foreach unused_net {Master_NoC_M07_INI Master_NoC_M08_INI Master_NoC_M09_INI Master_NoC_M10_INI} {
  catch { delete_bd_objs [get_bd_intf_nets $unused_net] }
}
catch { delete_bd_objs [get_bd_cells ISP_hier] }
catch { delete_bd_objs [get_bd_cells VCU_hier] }

# --- Shared video infrastructure (NoC ports, reset, clocks, PS config) -----
if {$mipi} {
  source mipi_hdmi_common_infra_bd.tcl
}

# --- MIPI-RX capture subsystem (instantiation + connections) ---------------
if {$mipi} {
  source mipi_rx_ss_hier.tcl
  create_hier_cell_mipi_rx_ss_hier / mipi_rx_ss_hier
  source mipi_rx_ss_hier_connections.tcl
}

# Assign addresses for the newly added video subsystem and re-layout
assign_bd_address
regenerate_bd_layout

# Source platform ports
set_property platform.extensible true [current_project]
set_property platform.board_id  "vek385-revb" [current_project]
set_property PFM_NAME {amd:VEK385:telluride:0.0} [get_files [current_bd_design].bd]

# Constraining AIE NSU near to 0 to 3 columns
source aie_constraints.tcl

# Allow relaxed NoC solution during BD validation (before validate_bd_design)
set_msg_config -suppress -id {Ipconfig 75-4216} -string {CRITICAL WARNING: [Ipconfig 75-4216] A NoC solution that meets the requested bandwidths could not be found}

validate_bd_design
save_bd_design

make_wrapper -files [get_files $project_name.srcs/sources_1/bd/bd/bd.bd] -top
add_files -norecurse $project_name.gen/sources_1/bd/bd/hdl/bd_wrapper.v
add_files -norecurse $project_name.srcs/sources_1/bd/bd/bd.bd

# Ignore the CED’s imported golden NCR
# Let Vivado generate a fresh NoC solution during impl_1 for the modified platform
set_property NOC_SOLUTION_FILE "" [get_runs impl_1]
# IMX728 4x4K raises NoC bandwidth; allow relaxed solution if exact BW unavailable
set_msg_config -suppress -id {Ipconfig 75-4216} -string {CRITICAL WARNING: [Ipconfig 75-4216] A NoC solution that meets the requested bandwidths could not be found}

#Overwrite the default rtl simulations models with tlm
set_property preferred_sim_model "tlm" [current_project]
update_compile_order -fileset sources_1

#Assign all the addresses
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
