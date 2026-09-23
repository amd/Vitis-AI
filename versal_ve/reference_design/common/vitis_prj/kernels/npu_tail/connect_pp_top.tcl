set pp_seg [get_bd_addr_segs pp_top_0/axi_mm_aie_pp_0/reg0]

foreach s_axi_pin [get_bd_intf_pins aggr_noc/S*_AXI] {

    # --- Pass 1: add M00_AXI to CONNECTIONS ---
    set existing_cons [get_property CONFIG.CONNECTIONS $s_axi_pin]
    if {![dict exists $existing_cons M00_AXI]} {
        dict set existing_cons M00_AXI {read_bw {500} write_bw {500} read_avg_burst {4} write_avg_burst {4}}
        set_property CONFIG.CONNECTIONS $existing_cons $s_axi_pin
        puts "PP_TOP fix: added M00_AXI to CONNECTIONS of $s_axi_pin"
    } else {
        puts "PP_TOP fix: M00_AXI already present on $s_axi_pin"
    }

    # --- Pass 2: address assignment from AIE GMIO master ---
    if {$pp_seg eq ""} continue
    set net [get_bd_intf_nets -of_objects $s_axi_pin]
    if {$net eq ""} continue
    set master_pin [lindex [get_bd_intf_pins -of_objects $net -filter {MODE==Master}] 0]
    if {$master_pin eq ""} continue
    foreach as [get_bd_addr_spaces -of_objects $master_pin] {
        assign_bd_address \
            -offset 0x020100000000 \
            -range  0x40000000 \
            -target_address_space $as \
            $pp_seg -force
        puts "PP_TOP fix: assigned pp_top_0/axi_mm_aie_pp_0 @ 0x020100000000 from $as"
    }
}

#validate_bd_design
