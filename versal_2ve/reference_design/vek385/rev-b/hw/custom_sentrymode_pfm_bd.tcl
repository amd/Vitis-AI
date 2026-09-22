# custom_sentrymode_bd.tcl
#
# Owns ALL sentry-mode customization and the FULL final address map so that
# create_platform.tcl stays generic. Order is important:
#
#   1) Master_NoC slave-port categories (CCI/RPU/MMI) + RPU->AIE remap on S08
#   2) Per-core R52 address assignments (1 GB DDR LEGACY + AIE window)
#   3) dual-remap workaround on S08 (M06_INI)
#   4) assign_bd_address  -> fill every remaining unassigned segment
#   5) RESIZE MED->2G and LEGACY->2G (except CortexR52)  <-- MUST BE LAST
#   6) validate
#
# Rule: the resize MUST run AFTER the final assign_bd_address, otherwise the
# tool's auto-assign re-creates DDR_CH0_MED windows at the 3G default.
#
# No new BD cells or nets are created here.

################################################################
# 0. PS (ps_wizard_0) IPI settings
#    Ported from the manual PS IPI reconfig captured in the write_bd_tcl diff
#    1.tcl -> 2.tcl. End-state values taken verbatim from 2.tcl.
#      - IPI1 no-buf disabled
#      - IPI2 master R52_1 -> R52_2, no-buf disabled
#      - IPI6 no-buf disabled
#    Note: 2.tcl (exported without validation) dropped the explicit
#    IPI3/4/5 no-buf master lines while leaving their NOBUF_ENABLE=1; those
#    masters are left to the tool default here (not force-set). Verify the IPI
#    assignment after validate_bd_design matches 2.tcl.
################################################################
#set_property -dict [list \
#    CONFIG.PS11_CONFIG(PS_GEN_IPI1_NOBUF_ENABLE) {1} \
#    CONFIG.PS11_CONFIG(PS_GEN_IPI2_ENABLE) {1} \
#    CONFIG.PS11_CONFIG(PS_GEN_IPI2_MASTER) {R52_2} \
#    CONFIG.PS11_CONFIG(PS_GEN_IPI2_NOBUF_ENABLE) {1} \
#    CONFIG.PS11_CONFIG(PS_GEN_IPI2_NOBUF_MASTER) {R52_1} \
#    CONFIG.PS11_CONFIG(PS_GEN_IPI3_ENABLE) {1} \
#    CONFIG.PS11_CONFIG(PS_GEN_IPI3_NOBUF_ENABLE) {1} \
#    CONFIG.PS11_CONFIG(PS_GEN_IPI4_ENABLE) {1} \
#    CONFIG.PS11_CONFIG(PS_GEN_IPI4_NOBUF_ENABLE) {1} \
#    CONFIG.PS11_CONFIG(PS_GEN_IPI5_ENABLE) {1} \
#    CONFIG.PS11_CONFIG(PS_GEN_IPI5_NOBUF_ENABLE) {1} \
#    CONFIG.PS11_CONFIG(PS_GEN_IPI6_ENABLE) {1} \
#    CONFIG.PS11_CONFIG(PS_GEN_IPI6_NOBUF_ENABLE) {1} \
#] [get_bd_cells ps_wizard_0]

################################################################
# 1. Master_NoC slave port categories and RPU->AIE remap
#    (categories are also set in pfm_bd.tcl; repeated here to be self-contained)
################################################################
startgroup
set_property -dict [list CONFIG.CATEGORY {ps_cci}] [get_bd_intf_pins /Master_NoC/S00_AXI]
set_property -dict [list CONFIG.CATEGORY {ps_cci}] [get_bd_intf_pins /Master_NoC/S01_AXI]
set_property -dict [list CONFIG.CATEGORY {ps_cci}] [get_bd_intf_pins /Master_NoC/S02_AXI]
set_property -dict [list CONFIG.CATEGORY {ps_cci}] [get_bd_intf_pins /Master_NoC/S03_AXI]
set_property -dict [list CONFIG.CATEGORY {ps_cci}] [get_bd_intf_pins /Master_NoC/S04_AXI]
set_property -dict [list CONFIG.CATEGORY {ps_cci}] [get_bd_intf_pins /Master_NoC/S05_AXI]
set_property -dict [list CONFIG.CATEGORY {ps_cci}] [get_bd_intf_pins /Master_NoC/S06_AXI]
set_property -dict [list CONFIG.CATEGORY {ps_cci}] [get_bd_intf_pins /Master_NoC/S07_AXI]
set_property -dict [list CONFIG.CATEGORY {ps_rpu} CONFIG.REMAPS {M06_INI {{0x4000_0000 0x200_0000_0000 0x40000000}}}] [get_bd_intf_pins /Master_NoC/S08_AXI]
set_property -dict [list CONFIG.CATEGORY {ps_mmi}] [get_bd_intf_pins /Master_NoC/S10_AXI]
endgroup

#set_property -dict [list CONFIG.CATEGORY {ps_rpu} CONFIG.REMAPS {M06_INI {{0x4000_0000 0x200_0000_0000 0x40000000}}}] [get_bd_intf_pins /Master_NoC/S08_AXI]

################################################################
# 2. Per-core R52 address assignments (1 GB DDR + AIE window each)
################################################################
set ps "ps_wizard_0"
for {set i 0} {$i < 10} {incr i} {

    set addr_space [get_bd_addr_spaces ${ps}/ps11_0_cortexr52_${i}]

    # DDR mapping (R52 keeps 1 GB LEGACY at 0x0)
    assign_bd_address \
        -offset 0x00000000 \
        -range  0x40000000 \
        -target_address_space $addr_space \
        [get_bd_addr_segs NoC_C0/DDR_MC_PORTS/DDR_CH0_LEGACY] \
        -force

    # AIE mapping
    assign_bd_address \
        -offset 0x40000000 \
        -range  0x40000000 \
        -target_address_space $addr_space \
        [get_bd_addr_segs ai_engine_0/S00_AXI/AIE_ARRAY_0] \
        -force
}

include_bd_addr_seg [get_bd_addr_segs -excluded ps_wizard_0/ps11_0_cortexr52_*/SEG_ai_engine_0_AIE_ARRAY_0]

################################################################
# 3. Workaround for dual remap on M06_INI
################################################################
set_property -dict [list \
    CONFIG.REMAPS {M06_INI {{0x4000_0000 0x200_0000_0000 0x40000000} {0x200_0000_0000 0x200_0000_0000 4G}}} \
] [get_bd_intf_pins /Master_NoC/S08_AXI]

################################################################
# 4. Assign every remaining unassigned segment
#    (MUST run BEFORE the resize so the resize is the final word)
################################################################
assign_bd_address

################################################################
# 5. FINAL address op: resize DDR windows
#    - DDR_CH0_MED : 3G -> 2G for every master that maps it
#    - DDR_CH0_LEGACY : -> 2G for every master EXCEPT CortexR52
################################################################
foreach seg [get_bd_addr_segs -of_objects [get_bd_addr_segs NoC_C0/DDR_MC_PORTS/DDR_CH0_MED]] {
    set_property range 2G $seg
}

set legacy [get_bd_addr_segs NoC_C0/DDR_MC_PORTS/DDR_CH0_LEGACY]
foreach seg [get_bd_addr_segs -of_objects $legacy] {
    if {[string match "*cortexr52*" $seg]} { continue }   ;# skip CortexR52 (keeps 1 GB)
    set_property range 2G $seg
}

################################################################
# 6. Validate
################################################################
validate_bd_design
