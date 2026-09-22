# ===========================================================================
# MIPI-RX capture subsystem (mipi_rx_ss_hier) connections
#
# Sourced from create_platform.tcl AFTER:
#   - mipi_rx_ss_hier has been instantiated,
#   - the Master_NoC / NoC_C0 / NoC_C1_C4 topology has been reconfigured,
#   - proc_sys_reset_150 has been created.
#
# FMC_CARD env var selects between 96716A (MIPI2+MIPI3) and 9296A (MIPI2+MIPI6)
# ===========================================================================
set fmc_card $::env(FMC_CARD)

# IMX728 4x4K: internal video writes in mipi_rx_ss_hier.tcl —
#   ISP NMU + AI (axi_noc2_0 S00-S03, axi_noc2_1 S00-S03): 450 MB/s
#   PO / v_frmbuf_wr (axi_noc2_1 S04-S07): 350 MB/s
# (500 on all paths oversubscribes MC0 @ v++ link; was 270 @ 1080p)
# (same as 1080p top-level). MC at 600 oversubscribes MC0 when v++ link re-compiles
# NoC for platform + AIE + image_processing (generate_target QoS failure).
# Keep MC0 aggregate under the 16000 MB/s DDR compiler limit (900/800 oversubscribes).
# Configure MC connections for NoC ports used by MIPI-RX subsystem
# NoC_C0 ports: S05-S08 (tile0 capture), S13-S20 (tile1 capture + preprocess)
set_property -dict [list CONFIG.CONNECTIONS {MC_0 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C0/S05_INI]
set_property -dict [list CONFIG.CONNECTIONS {MC_0 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C0/S06_INI]
set_property -dict [list CONFIG.CONNECTIONS {MC_0 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C0/S07_INI]
set_property -dict [list CONFIG.CONNECTIONS {MC_0 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C0/S08_INI]
set_property -dict [list CONFIG.CONNECTIONS {MC_0 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C0/S13_INI]
set_property -dict [list CONFIG.CONNECTIONS {MC_0 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C0/S14_INI]
set_property -dict [list CONFIG.CONNECTIONS {MC_0 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C0/S15_INI]
set_property -dict [list CONFIG.CONNECTIONS {MC_0 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C0/S16_INI]
set_property -dict [list CONFIG.CONNECTIONS {MC_0 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C0/S17_INI]
set_property -dict [list CONFIG.CONNECTIONS {MC_0 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C0/S18_INI]
set_property -dict [list CONFIG.CONNECTIONS {MC_0 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C0/S19_INI]
set_property -dict [list CONFIG.CONNECTIONS {MC_0 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C0/S20_INI]
# NoC_C1_C4 ports: S03-S06 (tile0), S11-S18 (tile1)
set_property -dict [list CONFIG.CONNECTIONS {MC_0 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C1_C4/S03_INI]
set_property -dict [list CONFIG.CONNECTIONS {MC_1 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C1_C4/S04_INI]
set_property -dict [list CONFIG.CONNECTIONS {MC_0 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C1_C4/S05_INI]
set_property -dict [list CONFIG.CONNECTIONS {MC_1 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C1_C4/S06_INI]
set_property -dict [list CONFIG.CONNECTIONS {MC_0 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C1_C4/S11_INI]
set_property -dict [list CONFIG.CONNECTIONS {MC_1 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C1_C4/S12_INI]
set_property -dict [list CONFIG.CONNECTIONS {MC_0 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C1_C4/S13_INI]
set_property -dict [list CONFIG.CONNECTIONS {MC_1 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C1_C4/S14_INI]
set_property -dict [list CONFIG.CONNECTIONS {MC_0 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C1_C4/S15_INI]
set_property -dict [list CONFIG.CONNECTIONS {MC_1 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C1_C4/S16_INI]
set_property -dict [list CONFIG.CONNECTIONS {MC_0 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C1_C4/S17_INI]
set_property -dict [list CONFIG.CONNECTIONS {MC_1 {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }}] [get_bd_intf_pins /NoC_C1_C4/S18_INI]

connect_bd_intf_net -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/M00_INI] [get_bd_intf_pins NoC_C0/S05_INI]
connect_bd_intf_net -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/M01_INI] [get_bd_intf_pins NoC_C0/S06_INI]
connect_bd_intf_net -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/M02_INI] [get_bd_intf_pins NoC_C0/S07_INI]
connect_bd_intf_net -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/M03_INI] [get_bd_intf_pins NoC_C0/S08_INI]
connect_bd_intf_net [get_bd_intf_pins mipi_rx_ss_hier/axi_noc2_0/M04_INI] [get_bd_intf_pins NoC_C1_C4/S03_INI]
connect_bd_intf_net [get_bd_intf_pins mipi_rx_ss_hier/axi_noc2_0/M05_INI] [get_bd_intf_pins NoC_C1_C4/S04_INI]
connect_bd_intf_net [get_bd_intf_pins mipi_rx_ss_hier/axi_noc2_0/M06_INI] [get_bd_intf_pins NoC_C1_C4/S05_INI]
connect_bd_intf_net [get_bd_intf_pins mipi_rx_ss_hier/axi_noc2_0/M07_INI] [get_bd_intf_pins NoC_C1_C4/S06_INI]
connect_bd_intf_net -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/M00_INI1] [get_bd_intf_pins NoC_C0/S13_INI]
connect_bd_intf_net -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/M01_INI1] [get_bd_intf_pins NoC_C0/S14_INI]
connect_bd_intf_net -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/M02_INI1] [get_bd_intf_pins NoC_C0/S15_INI]
connect_bd_intf_net -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/M03_INI1] [get_bd_intf_pins NoC_C0/S16_INI]
connect_bd_intf_net -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/M04_INI1] [get_bd_intf_pins NoC_C0/S17_INI]
connect_bd_intf_net -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/M05_INI1] [get_bd_intf_pins NoC_C0/S18_INI]
connect_bd_intf_net -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/M06_INI1] [get_bd_intf_pins NoC_C0/S19_INI]
connect_bd_intf_net -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/M07_INI1] [get_bd_intf_pins NoC_C0/S20_INI]
connect_bd_intf_net -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/M08_INI] [get_bd_intf_pins NoC_C1_C4/S11_INI]
connect_bd_intf_net -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/M09_INI] [get_bd_intf_pins NoC_C1_C4/S12_INI]
connect_bd_intf_net -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/M10_INI] [get_bd_intf_pins NoC_C1_C4/S13_INI]
connect_bd_intf_net -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/M11_INI] [get_bd_intf_pins NoC_C1_C4/S14_INI]
connect_bd_intf_net -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/M12_INI] [get_bd_intf_pins NoC_C1_C4/S15_INI]
connect_bd_intf_net -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/M13_INI] [get_bd_intf_pins NoC_C1_C4/S16_INI]
connect_bd_intf_net -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/M14_INI] [get_bd_intf_pins NoC_C1_C4/S17_INI]
connect_bd_intf_net -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/M15_INI] [get_bd_intf_pins NoC_C1_C4/S18_INI]

# --- Interrupts (to ps_wizard_0) ------------------------------------------
connect_bd_net [get_bd_pins mipi_rx_ss_hier/tile0_isp_isr_irq] [get_bd_pins ps_wizard_0/pl_lpd_irq0]
connect_bd_net [get_bd_pins ps_wizard_0/pl_lpd_irq1] [get_bd_pins mipi_rx_ss_hier/tile0_isp_xmpu_interrupt]
connect_bd_net [get_bd_pins ps_wizard_0/pl_lpd_irq2] [get_bd_pins mipi_rx_ss_hier/tile0_isp0_fusa_irq]
connect_bd_net [get_bd_pins ps_wizard_0/pl_lpd_irq3] [get_bd_pins mipi_rx_ss_hier/tile0_isp0_isp_irq]
connect_bd_net [get_bd_pins ps_wizard_0/pl_lpd_irq4] [get_bd_pins mipi_rx_ss_hier/tile0_isp1_fusa_irq]
connect_bd_net [get_bd_pins ps_wizard_0/pl_lpd_irq5] [get_bd_pins mipi_rx_ss_hier/tile0_isp1_isp_irq]
connect_bd_net [get_bd_pins mipi_rx_ss_hier/tile1_isp_isr_irq] [get_bd_pins ps_wizard_0/pl_lpd_irq6]
connect_bd_net [get_bd_pins mipi_rx_ss_hier/tile1_isp_xmpu_interrupt] [get_bd_pins ps_wizard_0/pl_lpd_irq7]
connect_bd_net [get_bd_pins ps_wizard_0/pl_lpd_irq8] [get_bd_pins mipi_rx_ss_hier/tile1_isp0_fusa_irq]
connect_bd_net [get_bd_pins ps_wizard_0/pl_lpd_irq9] [get_bd_pins mipi_rx_ss_hier/tile1_isp0_isp_irq]
connect_bd_net [get_bd_pins ps_wizard_0/pl_lpd_irq10] [get_bd_pins mipi_rx_ss_hier/tile1_isp1_fusa_irq]
connect_bd_net [get_bd_pins ps_wizard_0/pl_lpd_irq11] [get_bd_pins mipi_rx_ss_hier/tile1_isp1_isp_irq]
if {$fmc_card eq "96716A"} {
  connect_bd_net [get_bd_pins mipi_rx_ss_hier/iic2intc_irpt] [get_bd_pins ps_wizard_0/pl_lpd_irq20]
  connect_bd_net [get_bd_pins mipi_rx_ss_hier/irq] [get_bd_pins ps_wizard_0/pl_lpd_irq17]
  connect_bd_net [get_bd_pins mipi_rx_ss_hier/iic2intc_irpt1] [get_bd_pins ps_wizard_0/pl_lpd_irq18]
  connect_bd_net [get_bd_pins mipi_rx_ss_hier/iic2intc_irpt2] [get_bd_pins ps_wizard_0/pl_lpd_irq19]
} else {
  connect_bd_net [get_bd_pins mipi_rx_ss_hier/irq] [get_bd_pins ps_wizard_0/pl_lpd_irq20]
}

# --- External ports --------------------------------------------------------
if {$fmc_card eq "96716A"} {
  make_bd_intf_pins_external  [get_bd_intf_pins mipi_rx_ss_hier/MIPI2] [get_bd_intf_pins mipi_rx_ss_hier/MIPI3]
  set_property name MIPI2 [get_bd_intf_ports MIPI2_0]
  set_property name MIPI3 [get_bd_intf_ports MIPI3_0]

  # FMC IIC external ports
  set FMC_IIC_2 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:iic_rtl:1.0 FMC_IIC_2 ]
  set FMC_IIC_3 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:iic_rtl:1.0 FMC_IIC_3 ]
  set FMC_IIC_5 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:iic_rtl:1.0 FMC_IIC_5 ]
  connect_bd_intf_net -intf_net mipi_rx_ss_hier_FMC_IIC_2 [get_bd_intf_ports FMC_IIC_2] [get_bd_intf_pins mipi_rx_ss_hier/FMC_IIC_2]
  connect_bd_intf_net -intf_net mipi_rx_ss_hier_FMC_IIC_3 [get_bd_intf_ports FMC_IIC_3] [get_bd_intf_pins mipi_rx_ss_hier/FMC_IIC_3]
  connect_bd_intf_net -intf_net mipi_rx_ss_hier_FMC_IIC_5 [get_bd_intf_ports FMC_IIC_5] [get_bd_intf_pins mipi_rx_ss_hier/FMC_IIC_5]

  # LPD AXI PL (RPU) connection to mipi_rx_ss_hier
  connect_bd_intf_net -intf_net ps_wizard_0_LPD_AXI_PL [get_bd_intf_pins ps_wizard_0/LPD_AXI_PL] [get_bd_intf_pins mipi_rx_ss_hier/S00_AXI1]
  # Drive the LPD AXI PL clock from pl1_ref_clk (150 MHz) so it matches
  # the smartconnect_rpu clock domain inside mipi_rx_ss_hier.
  set lpd_clk_net [get_bd_nets -of_objects [get_bd_pins ps_wizard_0/lpd_axi_pl_aclk]]
  disconnect_bd_net $lpd_clk_net [get_bd_pins ps_wizard_0/lpd_axi_pl_aclk]
  connect_bd_net [get_bd_pins ps_wizard_0/lpd_axi_pl_aclk] [get_bd_pins ps_wizard_0/pl1_ref_clk]
} else {
  make_bd_intf_pins_external  [get_bd_intf_pins mipi_rx_ss_hier/MIPI2] [get_bd_intf_pins mipi_rx_ss_hier/MIPI6]
  set_property name MIPI2 [get_bd_intf_ports MIPI2_0]
  set_property name MIPI6 [get_bd_intf_ports MIPI6_0]
}

# --- Control / clock / reset ----------------------------------------------
connect_bd_intf_net [get_bd_intf_pins ctrl_smc/M01_AXI] -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/S_AXI_LITE]
connect_bd_net [get_bd_pins mipi_rx_ss_hier/video_aclk] [get_bd_pins ps_wizard_0/pl1_ref_clk]
connect_bd_net [get_bd_pins mipi_rx_ss_hier/video_aresetn] [get_bd_pins proc_sys_reset_150/peripheral_aresetn]
connect_bd_net [get_bd_pins mipi_rx_ss_hier/dphy_clk_200M] [get_bd_pins ps_wizard_0/pl3_ref_clk]

# --- Master_NoC loopback (PS access path into the ISP capture subsystem) ---
# Reuse the Master_NoC master INI ports freed by the ISP_hier/VCU_hier removal
# (VCU_hier was M07, ISP_hier was M08/M09/M10). M06_INI is a base connection and
# is left untouched.
connect_bd_intf_net [get_bd_intf_pins Master_NoC/M07_INI] -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/S00_INI]
connect_bd_intf_net [get_bd_intf_pins Master_NoC/M08_INI] -boundary_type upper [get_bd_intf_pins mipi_rx_ss_hier/S01_INI]

# Enable PS traffic to reach the ISP loopback WITHOUT disturbing the base
# DDR/AIE_ConfigNoc routing. Deleting the ISP_hier/VCU_hier cells removes their
# nets but leaves their QoS entries in each Master_NoC PS-slave CONNECTIONS dict,
# so for every Master_NoC S*_AXI port:
#   * drop the stale ISP_hier leftovers M09_INI/M10_INI (now dangling references
#     with no downstream cell), and
#   * force the reused video-loopback targets M07_INI/M08_INI to 300/300
#     bandwidth (enough for PS/R52 ISP register + mailbox; 500 oversubscribes
#     MC0 at v++ link). The removed VCU/ISP left them at 100/100, which starves the
#     PS/R52 ISP register + mailbox path and causes the intermittent R52
#     INIT_FIRMWARE mailbox timeouts (missing /proc/vsi). A plain append would
#     skip them because they are already present, so set the value explicitly.
# CONFIG.CONNECTIONS is a well-formed Tcl dict keyed by master INI port name.
foreach mnoc_si_pin [get_bd_intf_pins -quiet /Master_NoC/S*_AXI] {
  set mnoc_conn [get_property CONFIG.CONNECTIONS $mnoc_si_pin]
  foreach stray {M09_INI M10_INI} {
    if {[dict exists $mnoc_conn $stray]} {
      set mnoc_conn [dict remove $mnoc_conn $stray]
    }
  }
  foreach vid {M07_INI M08_INI} {
    dict set mnoc_conn $vid {read_bw {300} write_bw {300} initial_boot {true}}
  }
  set_property CONFIG.CONNECTIONS $mnoc_conn $mnoc_si_pin
}

# --- Top-level PL interrupt controller sensitivity -------------------------
# The edf_base axi_intc_0 leaves the interrupt kind/sensitivity unset (defaults),
# whereas the known-good design pins edge/intr/level to 0xFFFFFFFF so the
# generated device-tree interrupt types match what the drivers request and avoid
# the "irq: type mismatch" mapping failures seen at boot. Also align the exported
# platform IRQ range with the known-good design.
set_property -dict [list \
  CONFIG.C_DISABLE_SYNCHRONIZERS {0} \
  CONFIG.C_KIND_OF_EDGE {0xFFFFFFFF} \
  CONFIG.C_KIND_OF_INTR {0xFFFFFFFF} \
  CONFIG.C_KIND_OF_LVL {0xFFFFFFFF} \
  CONFIG.C_MB_CLK_NOT_CONNECTED {0} \
] [get_bd_cells /axi_intc_0]
set_property PFM.IRQ {intr {id 0 range 31}} [get_bd_cells /axi_intc_0]

# --- Physical constraints for MIPI pipeline (pin placement + ISP async) ----
# The FMC-specific XDC constrains MIPI D-PHY/GT pin placement, and isp_async.xdc
# relaxes timing for the ISP async clock-domain crossing (implementation-only).
set constrs_dir "constraints"
if {$fmc_card eq "96716A"} {
  set fmc_xdc "$constrs_dir/vek385_fmc_96716a.xdc"
} else {
  set fmc_xdc "$constrs_dir/vek385_fmc_9296a.xdc"
}
add_files -fileset constrs_1 -norecurse $fmc_xdc $constrs_dir/isp_async.xdc
import_files -fileset constrs_1 $fmc_xdc $constrs_dir/isp_async.xdc
set_property USED_IN_SYNTHESIS 0 [get_files -all $project_name.srcs/constrs_1/imports/constraints/isp_async.xdc]
