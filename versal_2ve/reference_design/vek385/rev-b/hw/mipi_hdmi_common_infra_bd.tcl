# ===========================================================================
# Video infrastructure: shared resources for MIPI-RX and HDMI-TX subsystems
#
# This script sets up the common platform infrastructure required by both the
# MIPI-RX capture and HDMI-TX display subsystems:
#   - Control interconnect (ctrl_smc) extra ports and clock
#   - NoC slave port allocation (NUM_NSI) on NoC_C0 and NoC_C1_C4
#   - 150 MHz video clock domain reset (proc_sys_reset_150)
#   - PS wizard interrupt/clock/AXI configuration for the video pipeline
#
# Sourced from create_platform.tcl AFTER:
#   - The base CED design has been loaded and customized
#   - ISP_hier / VCU_hier have been removed
# and BEFORE:
#   - The video subsystems (mipi_rx_ss_hier / hdmi_tx_ss_hier) are instantiated
#
# Requires: $fmc_card variable to be set by the caller (96716A or 9296A)
# ===========================================================================

# --- Control interconnect: add master ports for the video subsystems -------
set_property -dict [list \
  CONFIG.NUM_CLKS {2} \
  CONFIG.NUM_MI {4} \
] [get_bd_cells ctrl_smc]

# ===========================================================================
# NoC additions for the video subsystems (MIPI-RX / HDMI-TX)
#
# IMPORTANT: The base design's Master_NoC / DDR-NoC / Aggr-NoC / AIE_ConfigNoc
# connections (established by the edf_base CED + update_bd.tcl + pfm_bd.tcl) are
# intentionally left untouched here. In particular Master_NoC keeps the base
# NUM_NMI and the base PS-slave routing (pfm_bd.tcl sets only CONFIG.CATEGORY on
# S00_AXI..S10_AXI and leaves the master INI mapping to the base design).
#
# Only the *additional* NoC slave (NSI) ports needed for the video masters to
# reach the LPDDR memory controllers are added below. These are appended as new
# ports (existing base NSI ports are not modified) and are a MIPI/HDMI-specific
# requirement. The video INI pins are wired to these ports in the per-subsystem
# connection scripts; the Master_NoC->mipi loopback reuses the master ports
# freed by the ISP_hier/VCU_hier removal (see mipi_rx_ss_hier_connections.tcl).
# ===========================================================================
#C0-noc: add NSI slave ports for the video masters (-> MC_0)
set_property -dict [list \
  CONFIG.NUM_NSI {21} \
] [get_bd_cells NoC_C0]

#C1-C4-noc
set_property -dict [list \
  CONFIG.NUM_NSI {19} \
] [get_bd_cells NoC_C1_C4]

# --- 150 MHz video clock domain reset -------------------------------------
#proc-sys-rst-for-150 (150 MHz video reset)
create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 proc_sys_reset_0
set_property name proc_sys_reset_150 [get_bd_cells proc_sys_reset_0]
#dcm-locked-constant
create_bd_cell -type inline_hdl -vlnv xilinx.com:inline_hdl:ilconstant:1.0 ilconstant_dcm
set_property name ilxconstant_dcm_locked [get_bd_cells ilconstant_dcm]

#connection-between-the-ips
connect_bd_net [get_bd_pins ps_wizard_0/pl1_ref_clk] [get_bd_pins proc_sys_reset_150/slowest_sync_clk]
connect_bd_net [get_bd_pins ps_wizard_0/pl0_resetn] [get_bd_pins proc_sys_reset_150/ext_reset_in]
connect_bd_net [get_bd_pins ilxconstant_dcm_locked/dout] [get_bd_pins proc_sys_reset_150/dcm_locked]

# Shared control interconnect clock (ctrl_smc second clock domain)
connect_bd_net [get_bd_pins ctrl_smc/aclk1] [get_bd_pins ps_wizard_0/pl1_ref_clk]

# --- PS wizard: enable PL interrupts, clocks, and AXI for video -----------
# The base ps_wizard_0 only exposes pl_lpd_irq0..3; the MIPI-RX / HDMI-TX
# interrupts need pl_lpd_irq0..11 + 20..23 and pl_fpd_irq0..3. The MIPI D-PHY
# also needs the 200 MHz pl3_ref_clk, and the 150 MHz video domain uses
# pl1_ref_clk. Only these interrupt/clock sub-keys are set (matching the working
# fork design); all other PS settings from the base CED are left as-is.
if {$fmc_card eq "96716A"} {
  set_property -dict [list \
    CONFIG.PS11_CONFIG(PL_FPD_IRQ_USAGE) {CH0 1 CH1 1 CH2 1 CH3 1 CH4 0 CH5 0 CH6 0 CH7 0} \
    CONFIG.PS11_CONFIG(PL_LPD_IRQ_USAGE) {CH0 1 CH1 1 CH2 1 CH3 1 CH4 1 CH5 1 CH6 1 CH7 1 CH8 1 CH9 1 CH10 1 CH11 1 CH12 0 CH13 0 CH14 0 CH15 0 CH16 0 CH17 1 CH18 1 CH19 1 CH20 1 CH21 1 CH22 1 CH23 1} \
    CONFIG.PS11_CONFIG(PS_USE_PMCPL_CLK1) {1} \
    CONFIG.PS11_CONFIG(PS_USE_PMCPL_CLK3) {1} \
    CONFIG.PS11_CONFIG(PMC_CRP_PL1_REF_CTRL_FREQMHZ) {150} \
    CONFIG.PS11_CONFIG(PMC_CRP_PL3_REF_CTRL_FREQMHZ) {200} \
    CONFIG.PS11_CONFIG(PS_USE_LPD_AXI_PL) {1} \
    CONFIG.PS11_CONFIG(PS_LPD_AXI_PL_DATA_WIDTH) {64} \
  ] [get_bd_cells ps_wizard_0]
} else {
  set_property -dict [list \
    CONFIG.PS11_CONFIG(PL_FPD_IRQ_USAGE) {CH0 1 CH1 1 CH2 1 CH3 1 CH4 0 CH5 0 CH6 0 CH7 0} \
    CONFIG.PS11_CONFIG(PL_LPD_IRQ_USAGE) {CH0 1 CH1 1 CH2 1 CH3 1 CH4 1 CH5 1 CH6 1 CH7 1 CH8 1 CH9 1 CH10 1 CH11 1 CH12 0 CH13 0 CH14 0 CH15 0 CH16 0 CH17 0 CH18 0 CH19 0 CH20 1 CH21 1 CH22 1 CH23 1} \
    CONFIG.PS11_CONFIG(PS_USE_PMCPL_CLK1) {1} \
    CONFIG.PS11_CONFIG(PS_USE_PMCPL_CLK3) {1} \
    CONFIG.PS11_CONFIG(PMC_CRP_PL1_REF_CTRL_FREQMHZ) {150} \
    CONFIG.PS11_CONFIG(PMC_CRP_PL3_REF_CTRL_FREQMHZ) {200} \
  ] [get_bd_cells ps_wizard_0]
}
