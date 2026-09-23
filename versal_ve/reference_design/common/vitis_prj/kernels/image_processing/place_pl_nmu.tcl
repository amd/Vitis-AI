# Copyright(C) 2023-2026 Advanced Micro Devices Inc.  All Rights Reserved.

set_property CONFIG.PHYSICAL_LOC {NOC_NMU512_X2Y0} [lindex [get_bd_intf_pins -of_objects  [get_bd_intf_nets -of_objects  [get_bd_intf_pins /image_processing_1/m_axi_mm_video]]] 1]
