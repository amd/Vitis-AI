# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.

foreach cell [ get_cells -hierarchical -filter {ORIG_REF_NAME == multicycle || REF_NAME == multicycle} ] {
    set MULTICYCLE    [ get_property MULTICYCLE [get_cell $cell] ]
    set MULTICYCLE_m1 [ expr $MULTICYCLE - 1 ]
    set_multicycle_path $MULTICYCLE -setup -start -through [get_pins $cell/out[*]]
    set_multicycle_path $MULTICYCLE_m1 -hold  -start -through [get_pins $cell/out[*]]
}
