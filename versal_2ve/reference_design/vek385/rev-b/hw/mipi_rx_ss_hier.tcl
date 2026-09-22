# Copyright (c) 2026 Advanced Micro Devices, Inc.
# SPDX-License-Identifier: MIT
# -----------------------------------------------
# Modular MIPI-RX / ISP capture subsystem hierarchy definitions.
#
# This file provides the Tcl procedures used to (re)create the
# mipi_rx_ss_hier hierarchy and all of its nested sub-hierarchies
# (preproc_hier, SO_frmbuf_hier).
#
# Usage:
#   source mipi_rx_ss_hier.tcl
#   create_hier_cell_mipi_rx_ss_hier / mipi_rx_ss_hier
#
# IMX728 (4x 3840x2160) profile:
#   - VISP IBA: 3840x2160 on all 4 ISP cores (IO_TYPE=1, 8bpp OBA unchanged)
#   - PO/display: full 3840x2160 via v_frmbuf_wr (unchanged)
#   - SO/AI debug: preprocess_accel + frmbuf_accel @ 1920x1080 passthrough
#     (matches hw_original; configure_pipeline.sh sets ISP SO to 1080p)
#   - Internal NoC write_bw: ISP NMU + AI (S00-S03) 450 MB/s; PO (S04-S07) 350 MB/s
#     (500 on all paths oversubscribes MC0 @ v++ link; was 270 @ 1080p)
#   - Top-level MC 500 MB/s (600 oversubscribes MC0 at v++ link)

namespace eval _tcl {
proc get_script_folder {} {
   set script_path [file normalize [info script]]
   set script_folder [file dirname $script_path]
   return $script_folder
}
}
variable script_folder
set script_folder [_tcl::get_script_folder]

##################################################################
# DESIGN PROCs
##################################################################
# Hierarchical cell: SO_frmbuf_hier
proc create_hier_cell_SO_frmbuf_hier { parentCell nameHier } {

  variable script_folder

  if { $parentCell eq "" || $nameHier eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2092 -severity "ERROR" "create_hier_cell_SO_frmbuf_hier() - Empty argument(s)!"}
     return
  }

  # Get object for parentCell
  set parentObj [get_bd_cells $parentCell]
  if { $parentObj == "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2090 -severity "ERROR" "Unable to find parent cell <$parentCell>!"}
     return
  }

  # Make sure parentObj is hier blk
  set parentType [get_property TYPE $parentObj]
  if { $parentType ne "hier" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2091 -severity "ERROR" "Parent <$parentObj> has TYPE = <$parentType>. Expected to be <hier>."}
     return
  }

  # Save current instance; Restore later
  set oldCurInst [current_bd_instance .]

  # Set parent object as current
  current_bd_instance $parentObj

  # Create cell and set as current instance
  set hier_obj [create_bd_cell -type hier $nameHier]
  current_bd_instance $hier_obj

  # Create interface pins
  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:axis_rtl:1.0 s_axis_video

  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:axis_rtl:1.0 s_axis_video1

  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:axis_rtl:1.0 s_axis_video2

  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:axis_rtl:1.0 s_axis_video3

  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:aximm_rtl:1.0 S00_AXI

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 m_axi_mm_video

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 m_axi_mm_video1

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 m_axi_mm_video2

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 m_axi_mm_video3


  # Create pins
  create_bd_pin -dir I -type clk clk_151
  create_bd_pin -dir I -type rst s_axi_lite_rstn
  create_bd_pin -dir O -type intr interrupt
  create_bd_pin -dir O -type intr interrupt1
  create_bd_pin -dir O -type intr interrupt2
  create_bd_pin -dir O -type intr interrupt3
  create_bd_pin -dir I -from 31 -to 0 Din

  # Create instance: v_frmbuf_wr_0, and set properties
  set v_frmbuf_wr_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:v_frmbuf_wr v_frmbuf_wr_0 ]
  set_property -dict [list \
    CONFIG.AXIMM_ADDR_WIDTH {64} \
    CONFIG.AXIMM_DATA_WIDTH {256} \
    CONFIG.C_M_AXI_MM_VIDEO_DATA_WIDTH {256} \
    CONFIG.HAS_BGR8 {1} \
    CONFIG.HAS_BGRX8 {1} \
    CONFIG.HAS_RGB8 {1} \
    CONFIG.HAS_RGBX8 {1} \
    CONFIG.HAS_UYVY8 {1} \
    CONFIG.HAS_Y8 {1} \
    CONFIG.HAS_YUV8 {1} \
    CONFIG.HAS_YUVX8 {1} \
    CONFIG.HAS_YUYV8 {1} \
    CONFIG.HAS_Y_UV8_420 {1} \
    CONFIG.HAS_Y_U_V8 {1} \
    CONFIG.HAS_Y_U_V8_420 {1} \
    CONFIG.IS_TILE_FORMAT {0} \
    CONFIG.MAX_DATA_WIDTH {8} \
    CONFIG.MAX_NR_PLANES {3} \
    CONFIG.SAMPLES_PER_CLOCK {4} \
  ] $v_frmbuf_wr_0


  # Create instance: v_frmbuf_wr_1, and set properties
  set v_frmbuf_wr_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:v_frmbuf_wr v_frmbuf_wr_1 ]
  set_property -dict [list \
    CONFIG.AXIMM_ADDR_WIDTH {64} \
    CONFIG.AXIMM_DATA_WIDTH {256} \
    CONFIG.C_M_AXI_MM_VIDEO_DATA_WIDTH {256} \
    CONFIG.HAS_BGR8 {1} \
    CONFIG.HAS_BGRX8 {1} \
    CONFIG.HAS_RGB8 {1} \
    CONFIG.HAS_RGBX8 {1} \
    CONFIG.HAS_UYVY8 {1} \
    CONFIG.HAS_Y8 {1} \
    CONFIG.HAS_YUV8 {1} \
    CONFIG.HAS_YUVX8 {1} \
    CONFIG.HAS_YUYV8 {1} \
    CONFIG.HAS_Y_UV8_420 {1} \
    CONFIG.HAS_Y_U_V8 {1} \
    CONFIG.HAS_Y_U_V8_420 {1} \
    CONFIG.IS_TILE_FORMAT {0} \
    CONFIG.MAX_DATA_WIDTH {8} \
    CONFIG.MAX_NR_PLANES {3} \
    CONFIG.SAMPLES_PER_CLOCK {4} \
  ] $v_frmbuf_wr_1


  # Create instance: v_frmbuf_wr_2, and set properties
  set v_frmbuf_wr_2 [ create_bd_cell -type ip -vlnv xilinx.com:ip:v_frmbuf_wr v_frmbuf_wr_2 ]
  set_property -dict [list \
    CONFIG.AXIMM_ADDR_WIDTH {64} \
    CONFIG.AXIMM_DATA_WIDTH {256} \
    CONFIG.C_M_AXI_MM_VIDEO_DATA_WIDTH {256} \
    CONFIG.HAS_BGR8 {1} \
    CONFIG.HAS_BGRX8 {1} \
    CONFIG.HAS_RGB8 {1} \
    CONFIG.HAS_RGBX8 {1} \
    CONFIG.HAS_UYVY8 {1} \
    CONFIG.HAS_Y8 {1} \
    CONFIG.HAS_YUV8 {1} \
    CONFIG.HAS_YUVX8 {1} \
    CONFIG.HAS_YUYV8 {1} \
    CONFIG.HAS_Y_UV8_420 {1} \
    CONFIG.HAS_Y_U_V8 {1} \
    CONFIG.HAS_Y_U_V8_420 {1} \
    CONFIG.IS_TILE_FORMAT {0} \
    CONFIG.MAX_DATA_WIDTH {8} \
    CONFIG.MAX_NR_PLANES {3} \
    CONFIG.SAMPLES_PER_CLOCK {4} \
  ] $v_frmbuf_wr_2


  # Create instance: v_frmbuf_wr_3, and set properties
  set v_frmbuf_wr_3 [ create_bd_cell -type ip -vlnv xilinx.com:ip:v_frmbuf_wr v_frmbuf_wr_3 ]
  set_property -dict [list \
    CONFIG.AXIMM_ADDR_WIDTH {64} \
    CONFIG.AXIMM_DATA_WIDTH {256} \
    CONFIG.C_M_AXI_MM_VIDEO_DATA_WIDTH {256} \
    CONFIG.HAS_BGR8 {1} \
    CONFIG.HAS_BGRX8 {1} \
    CONFIG.HAS_RGB8 {1} \
    CONFIG.HAS_RGBX8 {1} \
    CONFIG.HAS_UYVY8 {1} \
    CONFIG.HAS_Y8 {1} \
    CONFIG.HAS_YUV8 {1} \
    CONFIG.HAS_YUVX8 {1} \
    CONFIG.HAS_YUYV8 {1} \
    CONFIG.HAS_Y_UV8_420 {1} \
    CONFIG.HAS_Y_U_V8 {1} \
    CONFIG.HAS_Y_U_V8_420 {1} \
    CONFIG.IS_TILE_FORMAT {0} \
    CONFIG.MAX_DATA_WIDTH {8} \
    CONFIG.MAX_NR_PLANES {3} \
    CONFIG.SAMPLES_PER_CLOCK {4} \
  ] $v_frmbuf_wr_3


  # Create instance: smartconnect_0, and set properties
  set smartconnect_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect smartconnect_0 ]
  set_property -dict [list \
    CONFIG.ADVANCED_PROPERTIES {__experimental_features__ {legacy_low_area_mode 1}} \
    CONFIG.NUM_MI {4} \
    CONFIG.NUM_SI {1} \
  ] $smartconnect_0


  # Create instance: ilslice_8, and set properties
  set ilslice_8 [ create_bd_cell -type inline_hdl -vlnv xilinx.com:inline_hdl:ilslice ilslice_8 ]
  set_property -dict [list \
    CONFIG.DIN_FROM {8} \
    CONFIG.DIN_TO {8} \
    CONFIG.DIN_WIDTH {32} \
  ] $ilslice_8


  # Create instance: ilslice_9, and set properties
  set ilslice_9 [ create_bd_cell -type inline_hdl -vlnv xilinx.com:inline_hdl:ilslice ilslice_9 ]
  set_property -dict [list \
    CONFIG.DIN_FROM {9} \
    CONFIG.DIN_TO {9} \
    CONFIG.DIN_WIDTH {32} \
  ] $ilslice_9


  # Create instance: ilslice_10, and set properties
  set ilslice_10 [ create_bd_cell -type inline_hdl -vlnv xilinx.com:inline_hdl:ilslice ilslice_10 ]
  set_property -dict [list \
    CONFIG.DIN_FROM {10} \
    CONFIG.DIN_TO {10} \
    CONFIG.DIN_WIDTH {32} \
  ] $ilslice_10


  # Create instance: ilslice_11, and set properties
  set ilslice_11 [ create_bd_cell -type inline_hdl -vlnv xilinx.com:inline_hdl:ilslice ilslice_11 ]
  set_property -dict [list \
    CONFIG.DIN_FROM {11} \
    CONFIG.DIN_TO {11} \
    CONFIG.DIN_WIDTH {32} \
  ] $ilslice_11


  # Create interface connections
  connect_bd_intf_net -intf_net Conn1 [get_bd_intf_pins smartconnect_0/S00_AXI] [get_bd_intf_pins S00_AXI]
  connect_bd_intf_net -intf_net Conn2 [get_bd_intf_pins v_frmbuf_wr_0/m_axi_mm_video] [get_bd_intf_pins m_axi_mm_video]
  connect_bd_intf_net -intf_net Conn3 [get_bd_intf_pins v_frmbuf_wr_1/m_axi_mm_video] [get_bd_intf_pins m_axi_mm_video1]
  connect_bd_intf_net -intf_net Conn4 [get_bd_intf_pins v_frmbuf_wr_3/m_axi_mm_video] [get_bd_intf_pins m_axi_mm_video2]
  connect_bd_intf_net -intf_net Conn5 [get_bd_intf_pins v_frmbuf_wr_2/m_axi_mm_video] [get_bd_intf_pins m_axi_mm_video3]
  connect_bd_intf_net -intf_net smartconnect_0_M00_AXI [get_bd_intf_pins smartconnect_0/M00_AXI] [get_bd_intf_pins v_frmbuf_wr_0/s_axi_CTRL]
  connect_bd_intf_net -intf_net smartconnect_0_M01_AXI [get_bd_intf_pins smartconnect_0/M01_AXI] [get_bd_intf_pins v_frmbuf_wr_1/s_axi_CTRL]
  connect_bd_intf_net -intf_net smartconnect_0_M02_AXI [get_bd_intf_pins smartconnect_0/M02_AXI] [get_bd_intf_pins v_frmbuf_wr_2/s_axi_CTRL]
  connect_bd_intf_net -intf_net smartconnect_0_M03_AXI [get_bd_intf_pins smartconnect_0/M03_AXI] [get_bd_intf_pins v_frmbuf_wr_3/s_axi_CTRL]
  connect_bd_intf_net -intf_net visp_ss_0_TILE0_ISP0_VIDOUT_SO [get_bd_intf_pins s_axis_video] [get_bd_intf_pins v_frmbuf_wr_0/s_axis_video]
  connect_bd_intf_net -intf_net visp_ss_0_TILE0_ISP1_VIDOUT_SO [get_bd_intf_pins s_axis_video1] [get_bd_intf_pins v_frmbuf_wr_1/s_axis_video]
  connect_bd_intf_net -intf_net visp_ss_0_TILE1_ISP0_VIDOUT_SO [get_bd_intf_pins s_axis_video2] [get_bd_intf_pins v_frmbuf_wr_2/s_axis_video]
  connect_bd_intf_net -intf_net visp_ss_0_TILE1_ISP1_VIDOUT_SO [get_bd_intf_pins s_axis_video3] [get_bd_intf_pins v_frmbuf_wr_3/s_axis_video]

  # Create port connections
  connect_bd_net -net Net  [get_bd_pins clk_151] \
  [get_bd_pins smartconnect_0/aclk] \
  [get_bd_pins v_frmbuf_wr_0/ap_clk] \
  [get_bd_pins v_frmbuf_wr_1/ap_clk] \
  [get_bd_pins v_frmbuf_wr_2/ap_clk] \
  [get_bd_pins v_frmbuf_wr_3/ap_clk]
  connect_bd_net -net Net1  [get_bd_pins s_axi_lite_rstn] \
  [get_bd_pins smartconnect_0/aresetn]
  connect_bd_net -net axi_gpio_0_gpio_io_o  [get_bd_pins Din] \
  [get_bd_pins ilslice_8/Din] \
  [get_bd_pins ilslice_9/Din] \
  [get_bd_pins ilslice_10/Din] \
  [get_bd_pins ilslice_11/Din]
  connect_bd_net -net ilslice_0_Dout  [get_bd_pins ilslice_8/Dout] \
  [get_bd_pins v_frmbuf_wr_0/ap_rst_n]
  connect_bd_net -net ilslice_1_Dout  [get_bd_pins ilslice_9/Dout] \
  [get_bd_pins v_frmbuf_wr_1/ap_rst_n]
  connect_bd_net -net ilslice_2_Dout  [get_bd_pins ilslice_10/Dout] \
  [get_bd_pins v_frmbuf_wr_2/ap_rst_n]
  connect_bd_net -net ilslice_3_Dout  [get_bd_pins ilslice_11/Dout] \
  [get_bd_pins v_frmbuf_wr_3/ap_rst_n]
  connect_bd_net -net v_frmbuf_wr_0_interrupt  [get_bd_pins v_frmbuf_wr_0/interrupt] \
  [get_bd_pins interrupt]
  connect_bd_net -net v_frmbuf_wr_1_interrupt  [get_bd_pins v_frmbuf_wr_1/interrupt] \
  [get_bd_pins interrupt1]
  connect_bd_net -net v_frmbuf_wr_2_interrupt  [get_bd_pins v_frmbuf_wr_2/interrupt] \
  [get_bd_pins interrupt3]
  connect_bd_net -net v_frmbuf_wr_3_interrupt  [get_bd_pins v_frmbuf_wr_3/interrupt] \
  [get_bd_pins interrupt2]

  # Restore current instance
  current_bd_instance $oldCurInst
}

# Hierarchical cell: preproc_hier
proc create_hier_cell_preproc_hier { parentCell nameHier } {

  variable script_folder

  if { $parentCell eq "" || $nameHier eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2092 -severity "ERROR" "create_hier_cell_preproc_hier() - Empty argument(s)!"}
     return
  }

  # Get object for parentCell
  set parentObj [get_bd_cells $parentCell]
  if { $parentObj == "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2090 -severity "ERROR" "Unable to find parent cell <$parentCell>!"}
     return
  }

  # Make sure parentObj is hier blk
  set parentType [get_property TYPE $parentObj]
  if { $parentType ne "hier" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2091 -severity "ERROR" "Parent <$parentObj> has TYPE = <$parentType>. Expected to be <hier>."}
     return
  }

  # Save current instance; Restore later
  set oldCurInst [current_bd_instance .]

  # Set parent object as current
  current_bd_instance $parentObj

  # Create cell and set as current instance
  set hier_obj [create_bd_cell -type hier $nameHier]
  current_bd_instance $hier_obj

  # Create interface pins
  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:axis_rtl:1.0 s_axis_video

  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:axis_rtl:1.0 s_axis_video1

  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:axis_rtl:1.0 s_axis_video2

  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:axis_rtl:1.0 s_axis_video3

  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:aximm_rtl:1.0 S00_AXI

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 m_axi_gmem1

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 m_axi_gmem2

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 m_axi_gmem3

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 m_axi_gmem0


  # Create pins
  create_bd_pin -dir I -type rst s_axi_lite_rstn
  create_bd_pin -dir I -type clk clk_151
  create_bd_pin -dir O -type intr interrupt
  create_bd_pin -dir O -type intr interrupt1
  create_bd_pin -dir O -type intr interrupt2
  create_bd_pin -dir O -type intr interrupt3
  create_bd_pin -dir O -type intr interrupt4
  create_bd_pin -dir O -type intr interrupt5
  create_bd_pin -dir O -type intr interrupt6
  create_bd_pin -dir O -type intr interrupt7
  create_bd_pin -dir O -from 31 -to 0 gpio_io_o

  # Create instance: axi_gpio_0, and set properties
  set axi_gpio_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_gpio axi_gpio_0 ]
  set_property -dict [list \
    CONFIG.C_ALL_OUTPUTS {1} \
    CONFIG.C_DOUT_DEFAULT {0xFFFFFFFF} \
    CONFIG.C_GPIO_WIDTH {32} \
  ] $axi_gpio_0


  # Create instance: ilslice_0, and set properties
  set ilslice_0 [ create_bd_cell -type inline_hdl -vlnv xilinx.com:inline_hdl:ilslice ilslice_0 ]
  set_property CONFIG.DIN_WIDTH {32} $ilslice_0


  # Create instance: ilslice_1, and set properties
  set ilslice_1 [ create_bd_cell -type inline_hdl -vlnv xilinx.com:inline_hdl:ilslice ilslice_1 ]
  set_property -dict [list \
    CONFIG.DIN_FROM {1} \
    CONFIG.DIN_TO {1} \
    CONFIG.DIN_WIDTH {32} \
  ] $ilslice_1


  # Create instance: ilslice_2, and set properties
  set ilslice_2 [ create_bd_cell -type inline_hdl -vlnv xilinx.com:inline_hdl:ilslice ilslice_2 ]
  set_property -dict [list \
    CONFIG.DIN_FROM {2} \
    CONFIG.DIN_TO {2} \
    CONFIG.DIN_WIDTH {32} \
  ] $ilslice_2


  # Create instance: ilslice_3, and set properties
  set ilslice_3 [ create_bd_cell -type inline_hdl -vlnv xilinx.com:inline_hdl:ilslice ilslice_3 ]
  set_property -dict [list \
    CONFIG.DIN_FROM {3} \
    CONFIG.DIN_TO {3} \
    CONFIG.DIN_WIDTH {32} \
  ] $ilslice_3


  # Create instance: ilslice_4, and set properties
  set ilslice_4 [ create_bd_cell -type inline_hdl -vlnv xilinx.com:inline_hdl:ilslice ilslice_4 ]
  set_property -dict [list \
    CONFIG.DIN_FROM {4} \
    CONFIG.DIN_TO {4} \
    CONFIG.DIN_WIDTH {32} \
  ] $ilslice_4


  # Create instance: ilslice_5, and set properties
  set ilslice_5 [ create_bd_cell -type inline_hdl -vlnv xilinx.com:inline_hdl:ilslice ilslice_5 ]
  set_property -dict [list \
    CONFIG.DIN_FROM {5} \
    CONFIG.DIN_TO {5} \
    CONFIG.DIN_WIDTH {32} \
  ] $ilslice_5


  # Create instance: ilslice_6, and set properties
  set ilslice_6 [ create_bd_cell -type inline_hdl -vlnv xilinx.com:inline_hdl:ilslice ilslice_6 ]
  set_property -dict [list \
    CONFIG.DIN_FROM {6} \
    CONFIG.DIN_TO {6} \
    CONFIG.DIN_WIDTH {32} \
  ] $ilslice_6


  # Create instance: ilslice_7, and set properties
  set ilslice_7 [ create_bd_cell -type inline_hdl -vlnv xilinx.com:inline_hdl:ilslice ilslice_7 ]
  set_property -dict [list \
    CONFIG.DIN_FROM {7} \
    CONFIG.DIN_TO {7} \
    CONFIG.DIN_WIDTH {32} \
  ] $ilslice_7


  # Create instance: smartconnect_0, and set properties
  set smartconnect_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect smartconnect_0 ]
  set_property -dict [list \
    CONFIG.ADVANCED_PROPERTIES {__experimental_features__ {legacy_low_area_mode 1}} \
    CONFIG.NUM_MI {9} \
    CONFIG.NUM_SI {1} \
  ] $smartconnect_0


  # Create instance: preprocess_accel_0, and set properties
  set preprocess_accel_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:preprocess_accel preprocess_accel_0 ]
  set_property -dict [list \
    CONFIG.IBITS_OUT_X {8} \
    CONFIG.INTERPOLATION_X {0} \
    CONFIG.IN_HEIGHT_X {1080} \
    CONFIG.IN_WIDTH_X {1920} \
    CONFIG.NEWHEIGHT_X {1080} \
    CONFIG.NEWWIDTH_X {1920} \
    CONFIG.RGB2RGBA_X {1} \
    CONFIG.SAMPLES_PER_CLOCK_X {4} \
    CONFIG.XF_AXI_GBR_X {1} \
    CONFIG.XF_BF16_X {0} \
    CONFIG.XF_FP16_X {0} \
    CONFIG.XF_FP32_X {0} \
    CONFIG.XF_INT8_X {1} \
    CONFIG.XF_USE_URAM_X {1} \
  ] $preprocess_accel_0


  # Create instance: frmbuf_accel_0, and set properties
  set frmbuf_accel_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:frmbuf_accel frmbuf_accel_0 ]
  set_property -dict [list \
    CONFIG.AXIMM_ADDR_WIDTH_X {64} \
    CONFIG.HEIGHT_X {1080} \
    CONFIG.NPPCX_X {4} \
    CONFIG.WIDTH_X {1920} \
    CONFIG.XF_BF16_X {0} \
    CONFIG.XF_FP16_X {0} \
    CONFIG.XF_FP32_X {0} \
    CONFIG.XF_INT8_X {1} \
    CONFIG._XF_HCWNC4_X {1} \
    CONFIG._XF_HCWNC8_X {0} \
    CONFIG._XF_NCHW_X {1} \
    CONFIG._XF_NHWC_X {1} \
    CONFIG._XF_RGBA_X {1} \
  ] $frmbuf_accel_0


  # Create instance: frmbuf_accel_1, and set properties
  set frmbuf_accel_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:frmbuf_accel frmbuf_accel_1 ]
  set_property -dict [list \
    CONFIG.AXIMM_ADDR_WIDTH_X {64} \
    CONFIG.HEIGHT_X {1080} \
    CONFIG.NPPCX_X {4} \
    CONFIG.WIDTH_X {1920} \
    CONFIG.XF_BF16_X {0} \
    CONFIG.XF_FP16_X {0} \
    CONFIG.XF_FP32_X {0} \
    CONFIG.XF_INT8_X {1} \
    CONFIG._XF_HCWNC4_X {1} \
    CONFIG._XF_HCWNC8_X {0} \
    CONFIG._XF_NCHW_X {1} \
    CONFIG._XF_NHWC_X {1} \
    CONFIG._XF_RGBA_X {1} \
  ] $frmbuf_accel_1


  # Create instance: preprocess_accel_1, and set properties
  set preprocess_accel_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:preprocess_accel preprocess_accel_1 ]
  set_property -dict [list \
    CONFIG.IBITS_OUT_X {8} \
    CONFIG.IN_HEIGHT_X {1080} \
    CONFIG.IN_WIDTH_X {1920} \
    CONFIG.NEWHEIGHT_X {1080} \
    CONFIG.NEWWIDTH_X {1920} \
    CONFIG.RGB2RGBA_X {1} \
    CONFIG.SAMPLES_PER_CLOCK_X {4} \
    CONFIG.XF_AXI_GBR_X {1} \
    CONFIG.XF_BF16_X {0} \
    CONFIG.XF_FP16_X {0} \
    CONFIG.XF_FP32_X {0} \
    CONFIG.XF_INT8_X {1} \
    CONFIG.XF_USE_URAM_X {1} \
  ] $preprocess_accel_1


  # Create instance: frmbuf_accel_2, and set properties
  set frmbuf_accel_2 [ create_bd_cell -type ip -vlnv xilinx.com:ip:frmbuf_accel frmbuf_accel_2 ]
  set_property -dict [list \
    CONFIG.AXIMM_ADDR_WIDTH_X {64} \
    CONFIG.HEIGHT_X {1080} \
    CONFIG.NPPCX_X {4} \
    CONFIG.WIDTH_X {1920} \
    CONFIG.XF_BF16_X {0} \
    CONFIG.XF_FP16_X {0} \
    CONFIG.XF_FP32_X {0} \
    CONFIG.XF_INT8_X {1} \
    CONFIG._XF_HCWNC4_X {1} \
    CONFIG._XF_HCWNC8_X {0} \
    CONFIG._XF_NCHW_X {1} \
    CONFIG._XF_NHWC_X {1} \
    CONFIG._XF_RGBA_X {1} \
  ] $frmbuf_accel_2


  # Create instance: preprocess_accel_2, and set properties
  set preprocess_accel_2 [ create_bd_cell -type ip -vlnv xilinx.com:ip:preprocess_accel preprocess_accel_2 ]
  set_property -dict [list \
    CONFIG.IBITS_OUT_X {8} \
    CONFIG.IN_HEIGHT_X {1080} \
    CONFIG.IN_WIDTH_X {1920} \
    CONFIG.NEWHEIGHT_X {1080} \
    CONFIG.NEWWIDTH_X {1920} \
    CONFIG.RGB2RGBA_X {1} \
    CONFIG.SAMPLES_PER_CLOCK_X {4} \
    CONFIG.XF_AXI_GBR_X {1} \
    CONFIG.XF_BF16_X {0} \
    CONFIG.XF_FP16_X {0} \
    CONFIG.XF_FP32_X {0} \
    CONFIG.XF_INT8_X {1} \
  ] $preprocess_accel_2


  # Create instance: frmbuf_accel_3, and set properties
  set frmbuf_accel_3 [ create_bd_cell -type ip -vlnv xilinx.com:ip:frmbuf_accel frmbuf_accel_3 ]
  set_property -dict [list \
    CONFIG.AXIMM_ADDR_WIDTH_X {64} \
    CONFIG.HEIGHT_X {1080} \
    CONFIG.NPPCX_X {4} \
    CONFIG.WIDTH_X {1920} \
    CONFIG.XF_BF16_X {0} \
    CONFIG.XF_FP16_X {0} \
    CONFIG.XF_FP32_X {0} \
    CONFIG.XF_INT8_X {1} \
    CONFIG._XF_HCWNC4_X {1} \
    CONFIG._XF_HCWNC8_X {0} \
    CONFIG._XF_NCHW_X {1} \
    CONFIG._XF_NHWC_X {1} \
    CONFIG._XF_RGBA_X {1} \
  ] $frmbuf_accel_3


  # Create instance: preprocess_accel_3, and set properties
  set preprocess_accel_3 [ create_bd_cell -type ip -vlnv xilinx.com:ip:preprocess_accel preprocess_accel_3 ]
  set_property -dict [list \
    CONFIG.IBITS_OUT_X {8} \
    CONFIG.IN_HEIGHT_X {1080} \
    CONFIG.IN_WIDTH_X {1920} \
    CONFIG.NEWHEIGHT_X {1080} \
    CONFIG.NEWWIDTH_X {1920} \
    CONFIG.RGB2RGBA_X {1} \
    CONFIG.SAMPLES_PER_CLOCK_X {4} \
    CONFIG.XF_AXI_GBR_X {1} \
    CONFIG.XF_BF16_X {0} \
    CONFIG.XF_FP16_X {0} \
    CONFIG.XF_FP32_X {0} \
    CONFIG.XF_INT8_X {1} \
  ] $preprocess_accel_3


  # Create interface connections
  connect_bd_intf_net -intf_net Conn1 [get_bd_intf_pins smartconnect_0/S00_AXI] [get_bd_intf_pins S00_AXI]
  connect_bd_intf_net -intf_net Conn2 [get_bd_intf_pins frmbuf_accel_0/m_axi_gmem0] [get_bd_intf_pins m_axi_gmem0]
  connect_bd_intf_net -intf_net Conn3 [get_bd_intf_pins frmbuf_accel_1/m_axi_gmem0] [get_bd_intf_pins m_axi_gmem1]
  connect_bd_intf_net -intf_net Conn4 [get_bd_intf_pins frmbuf_accel_2/m_axi_gmem0] [get_bd_intf_pins m_axi_gmem2]
  connect_bd_intf_net -intf_net Conn5 [get_bd_intf_pins frmbuf_accel_3/m_axi_gmem0] [get_bd_intf_pins m_axi_gmem3]
  connect_bd_intf_net -intf_net preprocess_accel_0_m_axis_video [get_bd_intf_pins frmbuf_accel_0/s_axis_video] [get_bd_intf_pins preprocess_accel_0/m_axis_video]
  connect_bd_intf_net -intf_net preprocess_accel_0_m_axis_video1 [get_bd_intf_pins frmbuf_accel_1/s_axis_video] [get_bd_intf_pins preprocess_accel_1/m_axis_video]
  connect_bd_intf_net -intf_net preprocess_accel_0_m_axis_video2 [get_bd_intf_pins frmbuf_accel_2/s_axis_video] [get_bd_intf_pins preprocess_accel_2/m_axis_video]
  connect_bd_intf_net -intf_net preprocess_accel_0_m_axis_video3 [get_bd_intf_pins frmbuf_accel_3/s_axis_video] [get_bd_intf_pins preprocess_accel_3/m_axis_video]
  connect_bd_intf_net -intf_net s_axis_video1_1 [get_bd_intf_pins s_axis_video1] [get_bd_intf_pins preprocess_accel_1/s_axis_video]
  connect_bd_intf_net -intf_net s_axis_video2_1 [get_bd_intf_pins s_axis_video2] [get_bd_intf_pins preprocess_accel_2/s_axis_video]
  connect_bd_intf_net -intf_net s_axis_video3_1 [get_bd_intf_pins s_axis_video3] [get_bd_intf_pins preprocess_accel_3/s_axis_video]
  connect_bd_intf_net -intf_net s_axis_video_1 [get_bd_intf_pins s_axis_video] [get_bd_intf_pins preprocess_accel_0/s_axis_video]
  connect_bd_intf_net -intf_net smartconnect_0_M00_AXI [get_bd_intf_pins smartconnect_0/M00_AXI] [get_bd_intf_pins preprocess_accel_0/s_axi_CTRL]
  connect_bd_intf_net -intf_net smartconnect_0_M01_AXI [get_bd_intf_pins smartconnect_0/M01_AXI] [get_bd_intf_pins preprocess_accel_1/s_axi_CTRL]
  connect_bd_intf_net -intf_net smartconnect_0_M02_AXI [get_bd_intf_pins preprocess_accel_2/s_axi_CTRL] [get_bd_intf_pins smartconnect_0/M02_AXI]
  connect_bd_intf_net -intf_net smartconnect_0_M03_AXI [get_bd_intf_pins smartconnect_0/M03_AXI] [get_bd_intf_pins preprocess_accel_3/s_axi_CTRL]
  connect_bd_intf_net -intf_net smartconnect_0_M04_AXI [get_bd_intf_pins smartconnect_0/M04_AXI] [get_bd_intf_pins frmbuf_accel_0/s_axi_CTRL]
  connect_bd_intf_net -intf_net smartconnect_0_M05_AXI [get_bd_intf_pins smartconnect_0/M05_AXI] [get_bd_intf_pins frmbuf_accel_1/s_axi_CTRL]
  connect_bd_intf_net -intf_net smartconnect_0_M06_AXI [get_bd_intf_pins frmbuf_accel_2/s_axi_CTRL] [get_bd_intf_pins smartconnect_0/M06_AXI]
  connect_bd_intf_net -intf_net smartconnect_0_M07_AXI [get_bd_intf_pins smartconnect_0/M07_AXI] [get_bd_intf_pins frmbuf_accel_3/s_axi_CTRL]
  connect_bd_intf_net -intf_net smartconnect_0_M08_AXI [get_bd_intf_pins smartconnect_0/M08_AXI] [get_bd_intf_pins axi_gpio_0/S_AXI]

  # Create port connections
  connect_bd_net -net Net  [get_bd_pins clk_151] \
  [get_bd_pins preprocess_accel_0/ap_clk] \
  [get_bd_pins frmbuf_accel_0/ap_clk] \
  [get_bd_pins preprocess_accel_1/ap_clk] \
  [get_bd_pins frmbuf_accel_1/ap_clk] \
  [get_bd_pins preprocess_accel_2/ap_clk] \
  [get_bd_pins frmbuf_accel_2/ap_clk] \
  [get_bd_pins preprocess_accel_3/ap_clk] \
  [get_bd_pins frmbuf_accel_3/ap_clk] \
  [get_bd_pins axi_gpio_0/s_axi_aclk] \
  [get_bd_pins smartconnect_0/aclk]
  connect_bd_net -net axi_gpio_0_gpio_io_o  [get_bd_pins axi_gpio_0/gpio_io_o] \
  [get_bd_pins ilslice_0/Din] \
  [get_bd_pins ilslice_1/Din] \
  [get_bd_pins ilslice_2/Din] \
  [get_bd_pins ilslice_3/Din] \
  [get_bd_pins ilslice_7/Din] \
  [get_bd_pins ilslice_6/Din] \
  [get_bd_pins ilslice_5/Din] \
  [get_bd_pins ilslice_4/Din] \
  [get_bd_pins gpio_io_o]
  connect_bd_net -net frmbuf_accel_0_interrupt  [get_bd_pins frmbuf_accel_0/interrupt] \
  [get_bd_pins interrupt4]
  connect_bd_net -net frmbuf_accel_1_interrupt  [get_bd_pins frmbuf_accel_1/interrupt] \
  [get_bd_pins interrupt5]
  connect_bd_net -net frmbuf_accel_2_interrupt  [get_bd_pins frmbuf_accel_2/interrupt] \
  [get_bd_pins interrupt6]
  connect_bd_net -net frmbuf_accel_3_interrupt  [get_bd_pins frmbuf_accel_3/interrupt] \
  [get_bd_pins interrupt7]
  connect_bd_net -net ilslice_0_Dout  [get_bd_pins ilslice_0/Dout] \
  [get_bd_pins preprocess_accel_0/ap_rst_n]
  connect_bd_net -net ilslice_1_Dout  [get_bd_pins ilslice_1/Dout] \
  [get_bd_pins preprocess_accel_1/ap_rst_n]
  connect_bd_net -net ilslice_2_Dout  [get_bd_pins ilslice_2/Dout] \
  [get_bd_pins preprocess_accel_2/ap_rst_n]
  connect_bd_net -net ilslice_3_Dout  [get_bd_pins ilslice_3/Dout] \
  [get_bd_pins preprocess_accel_3/ap_rst_n]
  connect_bd_net -net ilslice_4_Dout  [get_bd_pins ilslice_4/Dout] \
  [get_bd_pins frmbuf_accel_0/ap_rst_n]
  connect_bd_net -net ilslice_5_Dout  [get_bd_pins ilslice_5/Dout] \
  [get_bd_pins frmbuf_accel_1/ap_rst_n]
  connect_bd_net -net ilslice_6_Dout  [get_bd_pins ilslice_6/Dout] \
  [get_bd_pins frmbuf_accel_2/ap_rst_n]
  connect_bd_net -net ilslice_7_Dout  [get_bd_pins ilslice_7/Dout] \
  [get_bd_pins frmbuf_accel_3/ap_rst_n]
  connect_bd_net -net preprocess_accel_0_interrupt  [get_bd_pins preprocess_accel_0/interrupt] \
  [get_bd_pins interrupt]
  connect_bd_net -net preprocess_accel_1_interrupt  [get_bd_pins preprocess_accel_1/interrupt] \
  [get_bd_pins interrupt1]
  connect_bd_net -net preprocess_accel_2_interrupt  [get_bd_pins preprocess_accel_2/interrupt] \
  [get_bd_pins interrupt2]
  connect_bd_net -net preprocess_accel_3_interrupt  [get_bd_pins preprocess_accel_3/interrupt] \
  [get_bd_pins interrupt3]
  connect_bd_net -net s_axi_lite_rstn_1  [get_bd_pins s_axi_lite_rstn] \
  [get_bd_pins axi_gpio_0/s_axi_aresetn] \
  [get_bd_pins smartconnect_0/aresetn]

  # Restore current instance
  current_bd_instance $oldCurInst
}

# Hierarchical cell: mipi_rx_ss_hier
proc create_hier_cell_mipi_rx_ss_hier { parentCell nameHier } {

  variable script_folder

  if { $parentCell eq "" || $nameHier eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2092 -severity "ERROR" "create_hier_cell_mipi_rx_ss_hier() - Empty argument(s)!"}
     return
  }

  # Get object for parentCell
  set parentObj [get_bd_cells $parentCell]
  if { $parentObj == "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2090 -severity "ERROR" "Unable to find parent cell <$parentCell>!"}
     return
  }

  # Make sure parentObj is hier blk
  set parentType [get_property TYPE $parentObj]
  if { $parentType ne "hier" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2091 -severity "ERROR" "Parent <$parentObj> has TYPE = <$parentType>. Expected to be <hier>."}
     return
  }

  # Save current instance; Restore later
  set oldCurInst [current_bd_instance .]

  # Set parent object as current
  current_bd_instance $parentObj

  # Create cell and set as current instance
  set hier_obj [create_bd_cell -type hier $nameHier]
  current_bd_instance $hier_obj

  set fmc_card $::env(FMC_CARD)

  # Create interface pins
  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:mipi_phy_rtl:1.0 MIPI2

  if {$fmc_card eq "96716A"} {
    create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:mipi_phy_rtl:1.0 MIPI3
  } else {
    create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:mipi_phy_rtl:1.0 MIPI6
  }

  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:aximm_rtl:1.0 S_AXI_LITE

  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:inimm_rtl:1.0 S00_INI

  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:inimm_rtl:1.0 S01_INI

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M00_INI

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M01_INI

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M02_INI

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M03_INI

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M04_INI

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M05_INI

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M06_INI

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M07_INI

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M00_INI1

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M01_INI1

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M02_INI1

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M03_INI1

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M04_INI1

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M05_INI1

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M06_INI1

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M07_INI1

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M08_INI

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M09_INI

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M10_INI

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M11_INI

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M12_INI

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M13_INI

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M14_INI

  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M15_INI

  if {$fmc_card eq "96716A"} {
    create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:iic_rtl:1.0 FMC_IIC_2
    create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:iic_rtl:1.0 FMC_IIC_3
    create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:iic_rtl:1.0 FMC_IIC_5
    create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:aximm_rtl:1.0 S00_AXI1
  }

  # Create pins
  create_bd_pin -dir I -type clk dphy_clk_200M
  if {$fmc_card eq "96716A"} {
    create_bd_pin -dir O -type intr iic2intc_irpt
    create_bd_pin -dir O -type intr iic2intc_irpt1
    create_bd_pin -dir O -type intr iic2intc_irpt2
  }
  create_bd_pin -dir O -type intr tile0_isp0_fusa_irq
  create_bd_pin -dir O -type intr tile0_isp0_isp_irq
  create_bd_pin -dir O -type intr tile0_isp1_fusa_irq
  create_bd_pin -dir O -type intr tile0_isp1_isp_irq
  create_bd_pin -dir O -type intr tile0_isp_isr_irq
  create_bd_pin -dir O -type intr tile0_isp_xmpu_interrupt
  create_bd_pin -dir O -type intr tile1_isp0_fusa_irq
  create_bd_pin -dir O -type intr tile1_isp0_isp_irq
  create_bd_pin -dir O -type intr tile1_isp1_fusa_irq
  create_bd_pin -dir O -type intr tile1_isp1_isp_irq
  create_bd_pin -dir O -type intr tile1_isp_isr_irq
  create_bd_pin -dir O -type intr tile1_isp_xmpu_interrupt
  create_bd_pin -dir O -type clk clk_151
  create_bd_pin -dir O -from 0 -to 0 -type rst peripheral_aresetn
  create_bd_pin -dir O -from 31 -to 0 gpio_io_o
  create_bd_pin -dir O -type intr irq
  create_bd_pin -dir I -type clk video_aclk
  create_bd_pin -dir I -type rst video_aresetn

  # Create instance: mipi_csi2_rx_subsyst_0, and set properties
  set mipi_csi2_rx_subsyst_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:mipi_csi2_rx_subsystem mipi_csi2_rx_subsyst_0 ]
  set_property -dict [list \
    CONFIG.CMN_NUM_LANES {4} \
    CONFIG.CMN_NUM_PIXELS {4} \
    CONFIG.CMN_PXL_FORMAT {RAW12} \
    CONFIG.CMN_VC {All} \
    CONFIG.CSI_BUF_DEPTH {4096} \
    CONFIG.C_CLOCK_MASK {39} \
    CONFIG.C_CSI_EN_ACTIVELANES {true} \
    CONFIG.C_CSI_FILTER_USERDATATYPE {true} \
    CONFIG.C_DPHY_LANES {4} \
    CONFIG.C_EXDES_BOARD {VEK280} \
    CONFIG.C_SPRT_ISP_BRIDGE {true} \
    CONFIG.DPY_EN_REG_IF {true} \
    CONFIG.DPY_LINE_RATE {1500} \
    CONFIG.SupportLevel {1} \
  ] $mipi_csi2_rx_subsyst_0


  # Create instance: axis_broadcaster_0, and set properties
  set axis_broadcaster_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_broadcaster axis_broadcaster_0 ]
  set_property -dict [list \
    CONFIG.HAS_TKEEP {1} \
    CONFIG.HAS_TLAST {1} \
    CONFIG.HAS_TREADY {1} \
    CONFIG.HAS_TSTRB {0} \
    CONFIG.M00_TDATA_REMAP {tdata[47:0]} \
    CONFIG.M00_TUSER_REMAP {tuser[111:0]} \
    CONFIG.M01_TDATA_REMAP {tdata[47:0]} \
    CONFIG.M01_TUSER_REMAP {tuser[111:0]} \
    CONFIG.M_TDATA_NUM_BYTES {6} \
    CONFIG.M_TUSER_WIDTH {112} \
    CONFIG.S_TDATA_NUM_BYTES {6} \
    CONFIG.S_TUSER_WIDTH {112} \
    CONFIG.TDEST_WIDTH {11} \
    CONFIG.TID_WIDTH {0} \
  ] $axis_broadcaster_0

  set_property -dict [list \
    CONFIG.HAS_TKEEP.VALUE_MODE {auto} \
    CONFIG.HAS_TLAST.VALUE_MODE {auto} \
    CONFIG.HAS_TREADY.VALUE_MODE {auto} \
    CONFIG.HAS_TSTRB.VALUE_MODE {auto} \
    CONFIG.M_TDATA_NUM_BYTES.VALUE_MODE {auto} \
    CONFIG.M_TUSER_WIDTH.VALUE_MODE {auto} \
    CONFIG.S_TDATA_NUM_BYTES.VALUE_MODE {auto} \
    CONFIG.S_TUSER_WIDTH.VALUE_MODE {auto} \
    CONFIG.TDEST_WIDTH.VALUE_MODE {auto} \
    CONFIG.TID_WIDTH.VALUE_MODE {auto} \
  ] $axis_broadcaster_0


  # Create instance: axis_broadcaster_1, and set properties
  set axis_broadcaster_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_broadcaster axis_broadcaster_1 ]
  set_property -dict [list \
    CONFIG.HAS_TKEEP {1} \
    CONFIG.HAS_TLAST {1} \
    CONFIG.HAS_TREADY {1} \
    CONFIG.HAS_TSTRB {0} \
    CONFIG.M00_TDATA_REMAP {tdata[47:0]} \
    CONFIG.M00_TUSER_REMAP {tuser[111:0]} \
    CONFIG.M01_TDATA_REMAP {tdata[47:0]} \
    CONFIG.M01_TUSER_REMAP {tuser[111:0]} \
    CONFIG.M_TDATA_NUM_BYTES {6} \
    CONFIG.M_TUSER_WIDTH {112} \
    CONFIG.S_TDATA_NUM_BYTES {6} \
    CONFIG.S_TUSER_WIDTH {112} \
    CONFIG.TDEST_WIDTH {11} \
    CONFIG.TID_WIDTH {0} \
  ] $axis_broadcaster_1

  set_property -dict [list \
    CONFIG.HAS_TKEEP.VALUE_MODE {auto} \
    CONFIG.HAS_TLAST.VALUE_MODE {auto} \
    CONFIG.HAS_TREADY.VALUE_MODE {auto} \
    CONFIG.HAS_TSTRB.VALUE_MODE {auto} \
    CONFIG.M_TDATA_NUM_BYTES.VALUE_MODE {auto} \
    CONFIG.M_TUSER_WIDTH.VALUE_MODE {auto} \
    CONFIG.S_TDATA_NUM_BYTES.VALUE_MODE {auto} \
    CONFIG.S_TUSER_WIDTH.VALUE_MODE {auto} \
    CONFIG.TDEST_WIDTH.VALUE_MODE {auto} \
    CONFIG.TID_WIDTH.VALUE_MODE {auto} \
  ] $axis_broadcaster_1


  # Create instance: mipi_csi2_rx_subsyst_1, and set properties
  set mipi_csi2_rx_subsyst_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:mipi_csi2_rx_subsystem mipi_csi2_rx_subsyst_1 ]
  set_property -dict [list \
    CONFIG.CMN_NUM_LANES {4} \
    CONFIG.CMN_NUM_PIXELS {4} \
    CONFIG.CMN_PXL_FORMAT {RAW12} \
    CONFIG.CMN_VC {All} \
    CONFIG.CSI_BUF_DEPTH {4096} \
    CONFIG.C_CLOCK_MASK {39} \
    CONFIG.C_CSI_EN_ACTIVELANES {true} \
    CONFIG.C_CSI_FILTER_USERDATATYPE {true} \
    CONFIG.C_DPHY_LANES {4} \
    CONFIG.C_EXDES_BOARD {VEK280} \
    CONFIG.C_SPRT_ISP_BRIDGE {true} \
    CONFIG.DPY_EN_REG_IF {true} \
    CONFIG.DPY_LINE_RATE {1500} \
    CONFIG.SupportLevel {1} \
  ] $mipi_csi2_rx_subsyst_1


  # Create instance: visp_ss_0, and set properties
  set visp_ss_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:visp_ss visp_ss_0 ]
  set_property -dict [list \
    CONFIG.C_ENABLE_OVERDRIVE {1} \
    CONFIG.C_LLPATH0_TILE {3} \
    CONFIG.C_LLPATH1_TILE {3} \
    CONFIG.C_TARGET_BOARD {1} \
    CONFIG.C_TILE0_CONFIG {1} \
    CONFIG.C_TILE0_DPLL_CLKFBOUT_FRACT {1} \
    CONFIG.C_TILE0_DPLL_CLKFBOUT_MULT {54} \
    CONFIG.C_TILE0_DPLL_DIVCLK_DIVIDE {2} \
    CONFIG.C_TILE0_ISP0_CORE_CLK {600.1} \
    CONFIG.C_TILE0_ISP0_ENABLE_MP {true} \
    CONFIG.C_TILE0_ISP0_ENABLE_SP {true} \
    CONFIG.C_TILE0_ISP0_GPIO_PS_CHECK {true} \
    CONFIG.C_TILE0_ISP0_GPIO_SELECT {0} \
    CONFIG.C_TILE0_ISP0_IBA0_DATA_FORMAT {12} \
    CONFIG.C_TILE0_ISP0_IBA0_FPS {30} \
    CONFIG.C_TILE0_ISP0_IBA0_PPC {4} \
    CONFIG.C_TILE0_ISP0_IBA0_RES_HOR {3840} \
    CONFIG.C_TILE0_ISP0_IBA0_RES_VER {2160} \
    CONFIG.C_TILE0_ISP0_IBA0_VCID {0} \
    CONFIG.C_TILE0_ISP0_IIC_PS_CHECK {true} \
    CONFIG.C_TILE0_ISP0_IIC_SELECT {0} \
    CONFIG.C_TILE0_ISP0_IO_TYPE {1} \
    CONFIG.C_TILE0_ISP0_NETFPS {60} \
    CONFIG.C_TILE0_ISP0_OBA0_MP_BPP {8} \
    CONFIG.C_TILE0_ISP0_OBA0_MP_RGB888 {true} \
    CONFIG.C_TILE0_ISP0_OBA0_MP_Y {true} \
    CONFIG.C_TILE0_ISP0_OBA0_MP_YUV420 {false} \
    CONFIG.C_TILE0_ISP0_OBA0_MP_YUV422 {true} \
    CONFIG.C_TILE0_ISP0_OBA0_PPC {4} \
    CONFIG.C_TILE0_ISP0_OBA0_SP_BPP {8} \
    CONFIG.C_TILE0_ISP0_OBA0_SP_YUV420 {false} \
    CONFIG.C_TILE0_ISP0_RPU {6} \
    CONFIG.C_TILE0_ISP1_CORE_CLK {600.1} \
    CONFIG.C_TILE0_ISP1_ENABLE_MP {true} \
    CONFIG.C_TILE0_ISP1_ENABLE_SP {true} \
    CONFIG.C_TILE0_ISP1_GPIO_PS_CHECK {true} \
    CONFIG.C_TILE0_ISP1_GPIO_SELECT {0} \
    CONFIG.C_TILE0_ISP1_IBA4_DATA_FORMAT {12} \
    CONFIG.C_TILE0_ISP1_IBA4_FPS {30} \
    CONFIG.C_TILE0_ISP1_IBA4_PPC {4} \
    CONFIG.C_TILE0_ISP1_IBA4_RES_HOR {3840} \
    CONFIG.C_TILE0_ISP1_IBA4_RES_VER {2160} \
    CONFIG.C_TILE0_ISP1_IBA4_VCID {1} \
    CONFIG.C_TILE0_ISP1_IIC_PS_CHECK {true} \
    CONFIG.C_TILE0_ISP1_IIC_SELECT {0} \
    CONFIG.C_TILE0_ISP1_IO_TYPE {1} \
    CONFIG.C_TILE0_ISP1_NETFPS {60} \
    CONFIG.C_TILE0_ISP1_OBA1_MP_BPP {8} \
    CONFIG.C_TILE0_ISP1_OBA1_MP_RGB888 {true} \
    CONFIG.C_TILE0_ISP1_OBA1_MP_Y {true} \
    CONFIG.C_TILE0_ISP1_OBA1_MP_YUV420 {false} \
    CONFIG.C_TILE0_ISP1_OBA1_MP_YUV422 {true} \
    CONFIG.C_TILE0_ISP1_OBA1_PPC {4} \
    CONFIG.C_TILE0_ISP1_OBA1_SP_BPP {8} \
    CONFIG.C_TILE0_ISP1_OBA1_SP_YUV420 {false} \
    CONFIG.C_TILE0_ISP1_RPU {6} \
    CONFIG.C_TILE0_VIDIN0_TDATA_WIDTH {48} \
    CONFIG.C_TILE0_VIDIN4_TDATA_WIDTH {48} \
    CONFIG.C_TILE0_VIDOUT01_TDATA_WIDTH {96} \
    CONFIG.C_TILE0_VIDOUT02_TDATA_WIDTH {96} \
    CONFIG.C_TILE0_VIDOUT11_TDATA_WIDTH {96} \
    CONFIG.C_TILE0_VIDOUT12_TDATA_WIDTH {96} \
    CONFIG.C_TILE1_CONFIG {1} \
    CONFIG.C_TILE1_DPLL_CLKFBOUT_FRACT {1} \
    CONFIG.C_TILE1_DPLL_CLKFBOUT_MULT {54} \
    CONFIG.C_TILE1_DPLL_CLKOUT2_DIVIDE {6} \
    CONFIG.C_TILE1_DPLL_CLKOUT3_DIVIDE {6} \
    CONFIG.C_TILE1_DPLL_DIVCLK_DIVIDE {2} \
    CONFIG.C_TILE1_ENABLE {true} \
    CONFIG.C_TILE1_ISP0_CORE_CLK {600.1} \
    CONFIG.C_TILE1_ISP0_ENABLE_MP {true} \
    CONFIG.C_TILE1_ISP0_ENABLE_SP {true} \
    CONFIG.C_TILE1_ISP0_GPIO_PS_CHECK {true} \
    CONFIG.C_TILE1_ISP0_GPIO_SELECT {1} \
    CONFIG.C_TILE1_ISP0_IBA0_DATA_FORMAT {12} \
    CONFIG.C_TILE1_ISP0_IBA0_FPS {30} \
    CONFIG.C_TILE1_ISP0_IBA0_RES_HOR {3840} \
    CONFIG.C_TILE1_ISP0_IBA0_RES_VER {2160} \
    CONFIG.C_TILE1_ISP0_IIC_PS_CHECK {true} \
    CONFIG.C_TILE1_ISP0_IIC_SELECT {1} \
    CONFIG.C_TILE1_ISP0_IO_TYPE {1} \
    CONFIG.C_TILE1_ISP0_NETFPS {60} \
    CONFIG.C_TILE1_ISP0_OBA0_MP_BPP {8} \
    CONFIG.C_TILE1_ISP0_OBA0_MP_YUV420 {false} \
    CONFIG.C_TILE1_ISP0_OBA0_SP_BPP {8} \
    CONFIG.C_TILE1_ISP0_OBA0_SP_YUV420 {false} \
    CONFIG.C_TILE1_ISP0_RPU {7} \
    CONFIG.C_TILE1_ISP1_CORE_CLK {600.1} \
    CONFIG.C_TILE1_ISP1_ENABLE_MP {true} \
    CONFIG.C_TILE1_ISP1_ENABLE_SP {true} \
    CONFIG.C_TILE1_ISP1_GPIO_PS_CHECK {true} \
    CONFIG.C_TILE1_ISP1_GPIO_SELECT {1} \
    CONFIG.C_TILE1_ISP1_IBA4_DATA_FORMAT {12} \
    CONFIG.C_TILE1_ISP1_IBA4_FPS {30} \
    CONFIG.C_TILE1_ISP1_IBA4_RES_HOR {3840} \
    CONFIG.C_TILE1_ISP1_IBA4_RES_VER {2160} \
    CONFIG.C_TILE1_ISP1_IBA4_VCID {1} \
    CONFIG.C_TILE1_ISP1_IIC_PS_CHECK {true} \
    CONFIG.C_TILE1_ISP1_IIC_SELECT {1} \
    CONFIG.C_TILE1_ISP1_IO_TYPE {1} \
    CONFIG.C_TILE1_ISP1_NETFPS {60} \
    CONFIG.C_TILE1_ISP1_OBA1_MP_BPP {8} \
    CONFIG.C_TILE1_ISP1_OBA1_MP_YUV420 {false} \
    CONFIG.C_TILE1_ISP1_OBA1_SP_BPP {8} \
    CONFIG.C_TILE1_ISP1_OBA1_SP_YUV420 {false} \
    CONFIG.C_TILE1_ISP1_RPU {7} \
    CONFIG.C_TILE1_VIDIN0_TDATA_WIDTH {48} \
    CONFIG.C_TILE1_VIDIN4_TDATA_WIDTH {48} \
    CONFIG.C_TILE1_VIDOUT01_TDATA_WIDTH {96} \
    CONFIG.C_TILE1_VIDOUT02_TDATA_WIDTH {96} \
    CONFIG.C_TILE1_VIDOUT11_TDATA_WIDTH {96} \
    CONFIG.C_TILE1_VIDOUT12_TDATA_WIDTH {96} \
    CONFIG.C_TILE2_CONFIG {0} \
    CONFIG.C_TILE2_ENABLE {false} \
    CONFIG.C_TILE2_ISP0_IO_TYPE {0} \
    CONFIG.C_TILE2_ISP0_RPU {8} \
    CONFIG.C_TILE2_ISP1_IO_TYPE {0} \
    CONFIG.C_TILE2_ISP1_RPU {8} \
  ] $visp_ss_0


  set_property -dict [ list \
   CONFIG.DATA_WIDTH {32} \
   CONFIG.PROTOCOL {AXI4LITE} \
   CONFIG.ADDR_WIDTH {12} \
 ] [get_bd_intf_pins $visp_ss_0/S_AXI_LITE]

  set_property -dict [ list \
   CONFIG.CATEGORY {noc} \
   CONFIG.MY_CATEGORY {isp} \
   CONFIG.PHYSICAL_CHANNEL {ISP_TO_NOC_NMU} \
   CONFIG.TILE_INDEX {0} \
   CONFIG.INDEX {0} \
 ] [get_bd_intf_pins $visp_ss_0/TILE0_ISP0_NMU]

  set_property -dict [ list \
   CONFIG.CATEGORY {noc} \
   CONFIG.MY_CATEGORY {isp} \
   CONFIG.PHYSICAL_CHANNEL {ISP_TO_NOC_NMU} \
   CONFIG.TILE_INDEX {0} \
   CONFIG.INDEX {1} \
 ] [get_bd_intf_pins $visp_ss_0/TILE0_ISP1_NMU]

  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {32} \
   CONFIG.CATEGORY {noc} \
   CONFIG.MY_CATEGORY {isp} \
   CONFIG.PHYSICAL_CHANNEL {NOC_NSU_TO_ISP} \
   CONFIG.TILE_INDEX {0} \
   CONFIG.INDEX {0} \
 ] [get_bd_intf_pins $visp_ss_0/TILE0_ISP_NSU]

  set_property -dict [ list \
   CONFIG.CATEGORY {noc} \
   CONFIG.MY_CATEGORY {isp} \
   CONFIG.PHYSICAL_CHANNEL {ISP_TO_NOC_NMU} \
   CONFIG.TILE_INDEX {1} \
   CONFIG.INDEX {0} \
 ] [get_bd_intf_pins $visp_ss_0/TILE1_ISP0_NMU]

  set_property -dict [ list \
   CONFIG.CATEGORY {noc} \
   CONFIG.MY_CATEGORY {isp} \
   CONFIG.PHYSICAL_CHANNEL {ISP_TO_NOC_NMU} \
   CONFIG.TILE_INDEX {1} \
   CONFIG.INDEX {1} \
 ] [get_bd_intf_pins $visp_ss_0/TILE1_ISP1_NMU]

  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {32} \
   CONFIG.CATEGORY {noc} \
   CONFIG.MY_CATEGORY {isp} \
   CONFIG.PHYSICAL_CHANNEL {NOC_NSU_TO_ISP} \
   CONFIG.TILE_INDEX {1} \
   CONFIG.INDEX {0} \
 ] [get_bd_intf_pins $visp_ss_0/TILE1_ISP_NSU]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S_AXI_LITE} \
   CONFIG.ASSOCIATED_RESET {s_axi_lite_rstn} \
 ] [get_bd_pins $visp_ss_0/s_axi_lite_aclk]

  set_property -dict [ list \
   CONFIG.POLARITY {ACTIVE_LOW} \
 ] [get_bd_pins $visp_ss_0/s_axi_lite_rstn]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {TILE0_ISP0_NMU} \
 ] [get_bd_pins $visp_ss_0/tile0_nmu0_axi_clk]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {TILE0_ISP1_NMU} \
 ] [get_bd_pins $visp_ss_0/tile0_nmu1_axi_clk]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {TILE0_ISP_NSU} \
 ] [get_bd_pins $visp_ss_0/tile0_nsu_axi_clk]

  set_property -dict [ list \
   CONFIG.POLARITY {ACTIVE_LOW} \
 ] [get_bd_pins $visp_ss_0/tile0_pl_isp_rstn]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {TILE0_ISP_MIPI_VIDIN0} \
 ] [get_bd_pins $visp_ss_0/tile0_pl_isp_vidin0_clk]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {TILE0_ISP_MIPI_VIDIN4} \
 ] [get_bd_pins $visp_ss_0/tile0_pl_isp_vidin4_clk]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {TILE0_ISP0_VIDOUT_PO:TILE0_ISP0_VIDOUT_SO} \
 ] [get_bd_pins $visp_ss_0/tile0_pl_isp_vidout0_clk]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {TILE0_ISP1_VIDOUT_PO:TILE0_ISP1_VIDOUT_SO} \
 ] [get_bd_pins $visp_ss_0/tile0_pl_isp_vidout1_clk]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {TILE1_ISP0_NMU} \
 ] [get_bd_pins $visp_ss_0/tile1_nmu0_axi_clk]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {TILE1_ISP1_NMU} \
 ] [get_bd_pins $visp_ss_0/tile1_nmu1_axi_clk]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {TILE1_ISP_NSU} \
 ] [get_bd_pins $visp_ss_0/tile1_nsu_axi_clk]

  set_property -dict [ list \
   CONFIG.POLARITY {ACTIVE_LOW} \
 ] [get_bd_pins $visp_ss_0/tile1_pl_isp_rstn]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {TILE1_ISP_MIPI_VIDIN0} \
 ] [get_bd_pins $visp_ss_0/tile1_pl_isp_vidin0_clk]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {TILE1_ISP_MIPI_VIDIN4} \
 ] [get_bd_pins $visp_ss_0/tile1_pl_isp_vidin4_clk]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {TILE1_ISP0_VIDOUT_PO:TILE1_ISP0_VIDOUT_SO} \
 ] [get_bd_pins $visp_ss_0/tile1_pl_isp_vidout0_clk]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {TILE1_ISP1_VIDOUT_PO:TILE1_ISP1_VIDOUT_SO} \
 ] [get_bd_pins $visp_ss_0/tile1_pl_isp_vidout1_clk]

  # Create instance: smartconnect_0, and set properties
  set smartconnect_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect smartconnect_0 ]
  set_property -dict [list \
    CONFIG.ADVANCED_PROPERTIES {__experimental_features__ {legacy_low_area_mode 1}} \
    CONFIG.NUM_CLKS {2} \
    CONFIG.NUM_MI {6} \
    CONFIG.NUM_SI {1} \
  ] $smartconnect_0


  # Create instance: ilvector_logic_2, and set properties
  set ilvector_logic_2 [ create_bd_cell -type inline_hdl -vlnv xilinx.com:inline_hdl:ilvector_logic ilvector_logic_2 ]
  set_property -dict [list \
    CONFIG.C_OPERATION {or} \
    CONFIG.C_SIZE {1} \
  ] $ilvector_logic_2


  # Create instance: ilconcat_1, and set properties
  set ilconcat_1 [ create_bd_cell -type inline_hdl -vlnv xilinx.com:inline_hdl:ilconcat ilconcat_1 ]

  # Create instance: ilconcat_2, and set properties
  set ilconcat_2 [ create_bd_cell -type inline_hdl -vlnv xilinx.com:inline_hdl:ilconcat ilconcat_2 ]

  # Create instance: ilvector_logic_4, and set properties
  set ilvector_logic_4 [ create_bd_cell -type inline_hdl -vlnv xilinx.com:inline_hdl:ilvector_logic ilvector_logic_4 ]
  set_property -dict [list \
    CONFIG.C_OPERATION {or} \
    CONFIG.C_SIZE {1} \
  ] $ilvector_logic_4


  # Create instance: preproc_hier
  create_hier_cell_preproc_hier $hier_obj preproc_hier

  # Create instance: SO_frmbuf_hier
  create_hier_cell_SO_frmbuf_hier $hier_obj SO_frmbuf_hier

  # Create instance: ilconcat_0, and set properties
  set ilconcat_0 [ create_bd_cell -type inline_hdl -vlnv xilinx.com:inline_hdl:ilconcat ilconcat_0 ]
  set_property CONFIG.NUM_PORTS {14} $ilconcat_0


  # Create instance: clk_wizard_0, and set properties
  set clk_wizard_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:clkx5_wiz clk_wizard_0 ]
  set_property -dict [list \
    CONFIG.CLKOUT_DRIVES {BUFG,BUFG,BUFG,BUFG,BUFG,BUFG,BUFG} \
    CONFIG.CLKOUT_DYN_PS {None,None,None,None,None,None,None} \
    CONFIG.CLKOUT_GROUPING {Auto,Auto,Auto,Auto,Auto,Auto,Auto} \
    CONFIG.CLKOUT_MATCHED_ROUTING {false,false,false,false,false,false,false} \
    CONFIG.CLKOUT_PORT {clk_151,clk_out2,clk_out3,clk_out4,clk_out5,clk_out6,clk_out7} \
    CONFIG.CLKOUT_REQUESTED_DUTY_CYCLE {50.000,50.000,50.000,50.000,50.000,50.000,50.000} \
    CONFIG.CLKOUT_REQUESTED_OUT_FREQUENCY {151,145,100.000,100.000,100.000,100.000,100.000} \
    CONFIG.CLKOUT_REQUESTED_PHASE {0.000,0.000,0.000,0.000,0.000,0.000,0.000} \
    CONFIG.CLKOUT_USED {true,false,false,false,false,false,false} \
    CONFIG.PRIM_SOURCE {No_buffer} \
    CONFIG.RESET_TYPE {ACTIVE_LOW} \
    CONFIG.USE_LOCKED {true} \
    CONFIG.USE_PHASE_ALIGNMENT {true} \
    CONFIG.USE_RESET {true} \
  ] $clk_wizard_0


  # Create instance: proc_sys_reset_151, and set properties
  set proc_sys_reset_151 [ create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset proc_sys_reset_151 ]

  # Create instance: axi_intc_0, and set properties
  set axi_intc_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_intc axi_intc_0 ]
  set_property -dict [list \
    CONFIG.C_ASYNC_INTR {0xFFFFFFFF} \
    CONFIG.C_DISABLE_SYNCHRONIZERS {0} \
    CONFIG.C_IRQ_CONNECTION {1} \
    CONFIG.C_MB_CLK_NOT_CONNECTED {0} \
  ] $axi_intc_0


  # Create instance: axi_noc2_0, and set properties
  set axi_noc2_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc2 axi_noc2_0 ]
  set_property -dict [list \
    CONFIG.INLINE_HDL {false} \
    CONFIG.NUM_CLKS {7} \
    CONFIG.NUM_MI {2} \
    CONFIG.NUM_NMI {8} \
    CONFIG.NUM_NSI {2} \
    CONFIG.NUM_SI {4} \
  ] $axi_noc2_0


  set_property -dict [ list \
   CONFIG.DATA_WIDTH {128} \
   CONFIG.CATEGORY {isp} \
 ] [get_bd_intf_pins $axi_noc2_0/M00_AXI]

  set_property -dict [ list \
   CONFIG.DATA_WIDTH {128} \
   CONFIG.CATEGORY {isp} \
 ] [get_bd_intf_pins $axi_noc2_0/M01_AXI]

  set_property -dict [ list \
   CONFIG.DATA_WIDTH {128} \
   CONFIG.R_TRAFFIC_CLASS {BEST_EFFORT} \
   CONFIG.W_TRAFFIC_CLASS {ISOCHRONOUS} \
   CONFIG.CONNECTIONS {M04_INI {read_bw {30} write_bw {450} initial_boot {true} } M00_INI {read_bw {30} write_bw {450} initial_boot {true} }} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {isp} \
 ] [get_bd_intf_pins $axi_noc2_0/S00_AXI]

  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_AXI {read_bw {500} write_bw {450} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }} \
 ] [get_bd_intf_pins $axi_noc2_0/S00_INI]

  set_property -dict [ list \
   CONFIG.DATA_WIDTH {128} \
   CONFIG.R_TRAFFIC_CLASS {BEST_EFFORT} \
   CONFIG.W_TRAFFIC_CLASS {ISOCHRONOUS} \
   CONFIG.CONNECTIONS {M01_INI {read_bw {30} write_bw {450} initial_boot {true} } M05_INI {read_bw {30} write_bw {450} initial_boot {true} }} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {isp} \
 ] [get_bd_intf_pins $axi_noc2_0/S01_AXI]

  set_property -dict [ list \
   CONFIG.CONNECTIONS {M01_AXI {read_bw {500} write_bw {450} read_avg_burst {4} write_avg_burst {4} initial_boot {true} }} \
 ] [get_bd_intf_pins $axi_noc2_0/S01_INI]

  set_property -dict [ list \
   CONFIG.DATA_WIDTH {128} \
   CONFIG.R_TRAFFIC_CLASS {BEST_EFFORT} \
   CONFIG.W_TRAFFIC_CLASS {ISOCHRONOUS} \
   CONFIG.CONNECTIONS {M02_INI {read_bw {30} write_bw {450} initial_boot {true} } M06_INI {read_bw {30} write_bw {450} initial_boot {true} }} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {isp} \
 ] [get_bd_intf_pins $axi_noc2_0/S02_AXI]

  set_property -dict [ list \
   CONFIG.DATA_WIDTH {128} \
   CONFIG.R_TRAFFIC_CLASS {BEST_EFFORT} \
   CONFIG.W_TRAFFIC_CLASS {ISOCHRONOUS} \
   CONFIG.CONNECTIONS {M07_INI {read_bw {30} write_bw {450} initial_boot {true} } M03_INI {read_bw {30} write_bw {450} initial_boot {true} }} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {isp} \
 ] [get_bd_intf_pins $axi_noc2_0/S03_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins $axi_noc2_0/aclk0]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S01_AXI} \
 ] [get_bd_pins $axi_noc2_0/aclk1]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {M00_AXI} \
 ] [get_bd_pins $axi_noc2_0/aclk2]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S02_AXI} \
 ] [get_bd_pins $axi_noc2_0/aclk3]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S03_AXI} \
 ] [get_bd_pins $axi_noc2_0/aclk4]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {M01_AXI} \
 ] [get_bd_pins $axi_noc2_0/aclk5]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {} \
 ] [get_bd_pins $axi_noc2_0/aclk6]

  # Create instance: axi_noc2_1, and set properties
  set axi_noc2_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc2 axi_noc2_1 ]
  set_property -dict [list \
    CONFIG.INLINE_HDL {false} \
    CONFIG.NUM_CLKS {1} \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {16} \
    CONFIG.NUM_NSI {0} \
    CONFIG.NUM_SI {8} \
  ] $axi_noc2_1


  set_property -dict [ list \
   CONFIG.DATA_WIDTH {128} \
   CONFIG.R_TRAFFIC_CLASS {BEST_EFFORT} \
   CONFIG.W_TRAFFIC_CLASS {ISOCHRONOUS} \
   CONFIG.CONNECTIONS {M08_INI {read_bw {30} write_bw {450} } M00_INI {read_bw {30} write_bw {450} }} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins $axi_noc2_1/S00_AXI]

  set_property -dict [ list \
   CONFIG.DATA_WIDTH {128} \
   CONFIG.R_TRAFFIC_CLASS {BEST_EFFORT} \
   CONFIG.W_TRAFFIC_CLASS {ISOCHRONOUS} \
   CONFIG.CONNECTIONS {M01_INI {read_bw {30} write_bw {450} } M09_INI {read_bw {30} write_bw {450} }} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins $axi_noc2_1/S01_AXI]

  set_property -dict [ list \
   CONFIG.DATA_WIDTH {128} \
   CONFIG.R_TRAFFIC_CLASS {BEST_EFFORT} \
   CONFIG.W_TRAFFIC_CLASS {ISOCHRONOUS} \
   CONFIG.CONNECTIONS {M02_INI {read_bw {30} write_bw {450} } M10_INI {read_bw {30} write_bw {450} }} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins $axi_noc2_1/S02_AXI]

  set_property -dict [ list \
   CONFIG.DATA_WIDTH {128} \
   CONFIG.R_TRAFFIC_CLASS {BEST_EFFORT} \
   CONFIG.W_TRAFFIC_CLASS {ISOCHRONOUS} \
   CONFIG.CONNECTIONS {M03_INI {read_bw {30} write_bw {450} } M11_INI {read_bw {30} write_bw {450} }} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins $axi_noc2_1/S03_AXI]

  set_property -dict [ list \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.R_TRAFFIC_CLASS {BEST_EFFORT} \
   CONFIG.W_TRAFFIC_CLASS {ISOCHRONOUS} \
   CONFIG.CONNECTIONS {M04_INI {read_bw {30} write_bw {350} } M12_INI {read_bw {30} write_bw {350} }} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins $axi_noc2_1/S04_AXI]

  set_property -dict [ list \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.R_TRAFFIC_CLASS {BEST_EFFORT} \
   CONFIG.W_TRAFFIC_CLASS {ISOCHRONOUS} \
   CONFIG.CONNECTIONS {M05_INI {read_bw {30} write_bw {350} } M13_INI {read_bw {30} write_bw {350} }} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins $axi_noc2_1/S05_AXI]

  set_property -dict [ list \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.R_TRAFFIC_CLASS {BEST_EFFORT} \
   CONFIG.W_TRAFFIC_CLASS {ISOCHRONOUS} \
   CONFIG.CONNECTIONS {M14_INI {read_bw {30} write_bw {350} } M06_INI {read_bw {30} write_bw {350} }} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins $axi_noc2_1/S06_AXI]

  set_property -dict [ list \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.R_TRAFFIC_CLASS {BEST_EFFORT} \
   CONFIG.W_TRAFFIC_CLASS {ISOCHRONOUS} \
   CONFIG.CONNECTIONS {M07_INI {read_bw {30} write_bw {350} } M15_INI {read_bw {30} write_bw {350} }} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins $axi_noc2_1/S07_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI:S01_AXI:S02_AXI:S03_AXI:S04_AXI:S05_AXI:S06_AXI:S07_AXI} \
 ] [get_bd_pins $axi_noc2_1/aclk0]

  if {$fmc_card eq "96716A"} {
    # Create instance: fmc_iic_2, and set properties
    set fmc_iic_2 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_iic:2.1 fmc_iic_2 ]
    set_property CONFIG.IIC_FREQ_KHZ {1000} $fmc_iic_2

    # Create instance: fmc_iic_3, and set properties
    set fmc_iic_3 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_iic:2.1 fmc_iic_3 ]
    set_property CONFIG.IIC_FREQ_KHZ {1000} $fmc_iic_3

    # Create instance: fmc_iic_5, and set properties
    set fmc_iic_5 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_iic:2.1 fmc_iic_5 ]
    set_property CONFIG.IIC_FREQ_KHZ {1000} $fmc_iic_5

    # Create instance: smartconnect_rpu, and set properties
    set smartconnect_rpu [ create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 smartconnect_rpu ]
    set_property -dict [list \
      CONFIG.NUM_MI {3} \
      CONFIG.NUM_SI {1} \
    ] $smartconnect_rpu
  }

  # Create interface connections
  connect_bd_intf_net -intf_net Conn1 [get_bd_intf_pins axi_noc2_0/M04_INI] [get_bd_intf_pins M04_INI]
  connect_bd_intf_net -intf_net Conn2 [get_bd_intf_pins axi_noc2_0/M05_INI] [get_bd_intf_pins M05_INI]
  connect_bd_intf_net -intf_net Conn3 [get_bd_intf_pins axi_noc2_0/M06_INI] [get_bd_intf_pins M06_INI]
  connect_bd_intf_net -intf_net Conn4 [get_bd_intf_pins axi_noc2_0/M07_INI] [get_bd_intf_pins M07_INI]
  connect_bd_intf_net -intf_net Conn5 [get_bd_intf_pins axi_noc2_1/M00_INI] [get_bd_intf_pins M00_INI1]
  connect_bd_intf_net -intf_net Conn6 [get_bd_intf_pins axi_noc2_1/M01_INI] [get_bd_intf_pins M01_INI1]
  connect_bd_intf_net -intf_net Conn7 [get_bd_intf_pins axi_noc2_1/M02_INI] [get_bd_intf_pins M02_INI1]
  connect_bd_intf_net -intf_net Conn8 [get_bd_intf_pins axi_noc2_1/M03_INI] [get_bd_intf_pins M03_INI1]
  connect_bd_intf_net -intf_net Conn9 [get_bd_intf_pins axi_noc2_1/M04_INI] [get_bd_intf_pins M04_INI1]
  connect_bd_intf_net -intf_net Conn10 [get_bd_intf_pins axi_noc2_1/M05_INI] [get_bd_intf_pins M05_INI1]
  connect_bd_intf_net -intf_net Conn11 [get_bd_intf_pins axi_noc2_1/M06_INI] [get_bd_intf_pins M06_INI1]
  connect_bd_intf_net -intf_net Conn12 [get_bd_intf_pins axi_noc2_1/M07_INI] [get_bd_intf_pins M07_INI1]
  connect_bd_intf_net -intf_net Conn13 [get_bd_intf_pins axi_noc2_1/M08_INI] [get_bd_intf_pins M08_INI]
  connect_bd_intf_net -intf_net Conn14 [get_bd_intf_pins axi_noc2_1/M09_INI] [get_bd_intf_pins M09_INI]
  connect_bd_intf_net -intf_net Conn15 [get_bd_intf_pins axi_noc2_1/M10_INI] [get_bd_intf_pins M10_INI]
  connect_bd_intf_net -intf_net Conn16 [get_bd_intf_pins axi_noc2_1/M11_INI] [get_bd_intf_pins M11_INI]
  connect_bd_intf_net -intf_net Conn17 [get_bd_intf_pins axi_noc2_1/M12_INI] [get_bd_intf_pins M12_INI]
  connect_bd_intf_net -intf_net Conn18 [get_bd_intf_pins axi_noc2_1/M13_INI] [get_bd_intf_pins M13_INI]
  connect_bd_intf_net -intf_net Conn19 [get_bd_intf_pins axi_noc2_1/M14_INI] [get_bd_intf_pins M14_INI]
  connect_bd_intf_net -intf_net Conn20 [get_bd_intf_pins axi_noc2_1/M15_INI] [get_bd_intf_pins M15_INI]
  if {$fmc_card eq "96716A"} {
    connect_bd_intf_net -intf_net fmc_iic_2_IIC [get_bd_intf_pins FMC_IIC_2] [get_bd_intf_pins fmc_iic_2/IIC]
    connect_bd_intf_net -intf_net fmc_iic_3_IIC [get_bd_intf_pins FMC_IIC_3] [get_bd_intf_pins fmc_iic_3/IIC]
    connect_bd_intf_net -intf_net fmc_iic_5_IIC [get_bd_intf_pins FMC_IIC_5] [get_bd_intf_pins fmc_iic_5/IIC]
    connect_bd_intf_net -intf_net ps_wizard_0_LPD_AXI_PL [get_bd_intf_pins S00_AXI1] [get_bd_intf_pins smartconnect_rpu/S00_AXI]
    connect_bd_intf_net -intf_net smartconnect_rpu_M00_AXI [get_bd_intf_pins smartconnect_rpu/M00_AXI] [get_bd_intf_pins fmc_iic_2/S_AXI]
    connect_bd_intf_net -intf_net smartconnect_rpu_M01_AXI [get_bd_intf_pins smartconnect_rpu/M01_AXI] [get_bd_intf_pins fmc_iic_3/S_AXI]
    connect_bd_intf_net -intf_net smartconnect_rpu_M02_AXI [get_bd_intf_pins smartconnect_rpu/M02_AXI] [get_bd_intf_pins fmc_iic_5/S_AXI]
  }
  connect_bd_intf_net -intf_net MIPI2_1 [get_bd_intf_pins MIPI2] [get_bd_intf_pins mipi_csi2_rx_subsyst_0/mipi_phy_if]
  if {$fmc_card eq "96716A"} {
    connect_bd_intf_net -intf_net MIPI3_1 [get_bd_intf_pins MIPI3] [get_bd_intf_pins mipi_csi2_rx_subsyst_1/mipi_phy_if]
  } else {
    connect_bd_intf_net -intf_net MIPI6_1 [get_bd_intf_pins MIPI6] [get_bd_intf_pins mipi_csi2_rx_subsyst_1/mipi_phy_if]
  }
  connect_bd_intf_net -intf_net S00_INI_1 [get_bd_intf_pins S00_INI] [get_bd_intf_pins axi_noc2_0/S00_INI]
  connect_bd_intf_net -intf_net S01_INI_1 [get_bd_intf_pins S01_INI] [get_bd_intf_pins axi_noc2_0/S01_INI]
  connect_bd_intf_net -intf_net SO_frmbuf_hier_m_axi_mm_video [get_bd_intf_pins SO_frmbuf_hier/m_axi_mm_video] [get_bd_intf_pins axi_noc2_1/S04_AXI]
  connect_bd_intf_net -intf_net SO_frmbuf_hier_m_axi_mm_video1 [get_bd_intf_pins SO_frmbuf_hier/m_axi_mm_video1] [get_bd_intf_pins axi_noc2_1/S05_AXI]
  connect_bd_intf_net -intf_net SO_frmbuf_hier_m_axi_mm_video2 [get_bd_intf_pins SO_frmbuf_hier/m_axi_mm_video2] [get_bd_intf_pins axi_noc2_1/S06_AXI]
  connect_bd_intf_net -intf_net SO_frmbuf_hier_m_axi_mm_video3 [get_bd_intf_pins SO_frmbuf_hier/m_axi_mm_video3] [get_bd_intf_pins axi_noc2_1/S07_AXI]
  connect_bd_intf_net -intf_net S_AXI_LITE_1 [get_bd_intf_pins S_AXI_LITE] [get_bd_intf_pins smartconnect_0/S00_AXI]
  connect_bd_intf_net -intf_net axi_noc2_0_M00_AXI [get_bd_intf_pins axi_noc2_0/M00_AXI] [get_bd_intf_pins visp_ss_0/TILE0_ISP_NSU]
  connect_bd_intf_net -intf_net axi_noc2_0_M00_INI [get_bd_intf_pins M00_INI] [get_bd_intf_pins axi_noc2_0/M00_INI]
  connect_bd_intf_net -intf_net axi_noc2_0_M01_AXI [get_bd_intf_pins axi_noc2_0/M01_AXI] [get_bd_intf_pins visp_ss_0/TILE1_ISP_NSU]
  connect_bd_intf_net -intf_net axi_noc2_0_M01_INI [get_bd_intf_pins M01_INI] [get_bd_intf_pins axi_noc2_0/M01_INI]
  connect_bd_intf_net -intf_net axi_noc2_0_M02_INI [get_bd_intf_pins M02_INI] [get_bd_intf_pins axi_noc2_0/M02_INI]
  connect_bd_intf_net -intf_net axi_noc2_0_M03_INI [get_bd_intf_pins M03_INI] [get_bd_intf_pins axi_noc2_0/M03_INI]
  connect_bd_intf_net -intf_net axis_broadcaster_0_M00_AXIS [get_bd_intf_pins axis_broadcaster_0/M00_AXIS] [get_bd_intf_pins visp_ss_0/TILE0_ISP_MIPI_VIDIN0]
  connect_bd_intf_net -intf_net axis_broadcaster_0_M01_AXIS [get_bd_intf_pins axis_broadcaster_0/M01_AXIS] [get_bd_intf_pins visp_ss_0/TILE0_ISP_MIPI_VIDIN4]
  connect_bd_intf_net -intf_net axis_broadcaster_1_M00_AXIS [get_bd_intf_pins axis_broadcaster_1/M00_AXIS] [get_bd_intf_pins visp_ss_0/TILE1_ISP_MIPI_VIDIN0]
  connect_bd_intf_net -intf_net axis_broadcaster_1_M01_AXIS [get_bd_intf_pins axis_broadcaster_1/M01_AXIS] [get_bd_intf_pins visp_ss_0/TILE1_ISP_MIPI_VIDIN4]
  connect_bd_intf_net -intf_net mipi_csi2_rx_subsyst_0_video_out [get_bd_intf_pins mipi_csi2_rx_subsyst_0/video_out] [get_bd_intf_pins axis_broadcaster_0/S_AXIS]
  connect_bd_intf_net -intf_net mipi_csi2_rx_subsyst_1_video_out [get_bd_intf_pins axis_broadcaster_1/S_AXIS] [get_bd_intf_pins mipi_csi2_rx_subsyst_1/video_out]
  connect_bd_intf_net -intf_net preproc_hier_m_axi_gmem0 [get_bd_intf_pins preproc_hier/m_axi_gmem0] [get_bd_intf_pins axi_noc2_1/S03_AXI]
  connect_bd_intf_net -intf_net preproc_hier_m_axi_gmem1 [get_bd_intf_pins preproc_hier/m_axi_gmem1] [get_bd_intf_pins axi_noc2_1/S00_AXI]
  connect_bd_intf_net -intf_net preproc_hier_m_axi_gmem2 [get_bd_intf_pins preproc_hier/m_axi_gmem2] [get_bd_intf_pins axi_noc2_1/S01_AXI]
  connect_bd_intf_net -intf_net preproc_hier_m_axi_gmem3 [get_bd_intf_pins preproc_hier/m_axi_gmem3] [get_bd_intf_pins axi_noc2_1/S02_AXI]
  connect_bd_intf_net -intf_net smartconnect_0_M00_AXI [get_bd_intf_pins smartconnect_0/M00_AXI] [get_bd_intf_pins mipi_csi2_rx_subsyst_0/csirxss_s_axi]
  connect_bd_intf_net -intf_net smartconnect_0_M01_AXI [get_bd_intf_pins smartconnect_0/M01_AXI] [get_bd_intf_pins mipi_csi2_rx_subsyst_1/csirxss_s_axi]
  connect_bd_intf_net -intf_net smartconnect_0_M02_AXI [get_bd_intf_pins smartconnect_0/M02_AXI] [get_bd_intf_pins visp_ss_0/S_AXI_LITE]
  connect_bd_intf_net -intf_net smartconnect_0_M03_AXI [get_bd_intf_pins smartconnect_0/M03_AXI] [get_bd_intf_pins preproc_hier/S00_AXI]
  connect_bd_intf_net -intf_net smartconnect_0_M04_AXI [get_bd_intf_pins smartconnect_0/M04_AXI] [get_bd_intf_pins SO_frmbuf_hier/S00_AXI]
  connect_bd_intf_net -intf_net smartconnect_0_M05_AXI [get_bd_intf_pins axi_intc_0/s_axi] [get_bd_intf_pins smartconnect_0/M05_AXI]
  connect_bd_intf_net -intf_net visp_ss_0_TILE0_ISP0_NMU [get_bd_intf_pins visp_ss_0/TILE0_ISP0_NMU] [get_bd_intf_pins axi_noc2_0/S00_AXI]
  connect_bd_intf_net -intf_net visp_ss_0_TILE0_ISP0_VIDOUT_PO [get_bd_intf_pins visp_ss_0/TILE0_ISP0_VIDOUT_PO] [get_bd_intf_pins SO_frmbuf_hier/s_axis_video]
  connect_bd_intf_net -intf_net visp_ss_0_TILE0_ISP0_VIDOUT_SO [get_bd_intf_pins visp_ss_0/TILE0_ISP0_VIDOUT_SO] [get_bd_intf_pins preproc_hier/s_axis_video]
  connect_bd_intf_net -intf_net visp_ss_0_TILE0_ISP1_NMU [get_bd_intf_pins visp_ss_0/TILE0_ISP1_NMU] [get_bd_intf_pins axi_noc2_0/S01_AXI]
  connect_bd_intf_net -intf_net visp_ss_0_TILE0_ISP1_VIDOUT_PO [get_bd_intf_pins visp_ss_0/TILE0_ISP1_VIDOUT_PO] [get_bd_intf_pins SO_frmbuf_hier/s_axis_video1]
  connect_bd_intf_net -intf_net visp_ss_0_TILE0_ISP1_VIDOUT_SO [get_bd_intf_pins visp_ss_0/TILE0_ISP1_VIDOUT_SO] [get_bd_intf_pins preproc_hier/s_axis_video1]
  connect_bd_intf_net -intf_net visp_ss_0_TILE1_ISP0_NMU [get_bd_intf_pins visp_ss_0/TILE1_ISP0_NMU] [get_bd_intf_pins axi_noc2_0/S02_AXI]
  connect_bd_intf_net -intf_net visp_ss_0_TILE1_ISP0_VIDOUT_PO [get_bd_intf_pins visp_ss_0/TILE1_ISP0_VIDOUT_PO] [get_bd_intf_pins SO_frmbuf_hier/s_axis_video2]
  connect_bd_intf_net -intf_net visp_ss_0_TILE1_ISP0_VIDOUT_SO [get_bd_intf_pins visp_ss_0/TILE1_ISP0_VIDOUT_SO] [get_bd_intf_pins preproc_hier/s_axis_video2]
  connect_bd_intf_net -intf_net visp_ss_0_TILE1_ISP1_NMU [get_bd_intf_pins visp_ss_0/TILE1_ISP1_NMU] [get_bd_intf_pins axi_noc2_0/S03_AXI]
  connect_bd_intf_net -intf_net visp_ss_0_TILE1_ISP1_VIDOUT_PO [get_bd_intf_pins visp_ss_0/TILE1_ISP1_VIDOUT_PO] [get_bd_intf_pins SO_frmbuf_hier/s_axis_video3]
  connect_bd_intf_net -intf_net visp_ss_0_TILE1_ISP1_VIDOUT_SO [get_bd_intf_pins visp_ss_0/TILE1_ISP1_VIDOUT_SO] [get_bd_intf_pins preproc_hier/s_axis_video3]

  # Create port connections
  connect_bd_net -net SO_frmbuf_hier_interrupt  [get_bd_pins SO_frmbuf_hier/interrupt] \
  [get_bd_pins ilconcat_0/In2]
  connect_bd_net -net SO_frmbuf_hier_interrupt1  [get_bd_pins SO_frmbuf_hier/interrupt1] \
  [get_bd_pins ilconcat_0/In3]
  connect_bd_net -net SO_frmbuf_hier_interrupt2  [get_bd_pins SO_frmbuf_hier/interrupt2] \
  [get_bd_pins ilconcat_0/In4]
  connect_bd_net -net SO_frmbuf_hier_interrupt3  [get_bd_pins SO_frmbuf_hier/interrupt3] \
  [get_bd_pins ilconcat_0/In5]
  if {$fmc_card eq "96716A"} {
    connect_bd_net -net fmc_iic_2_iic2intc_irpt  [get_bd_pins fmc_iic_2/iic2intc_irpt] \
    [get_bd_pins iic2intc_irpt]
    connect_bd_net -net fmc_iic_3_iic2intc_irpt  [get_bd_pins fmc_iic_3/iic2intc_irpt] \
    [get_bd_pins iic2intc_irpt1]
    connect_bd_net -net fmc_iic_5_iic2intc_irpt  [get_bd_pins fmc_iic_5/iic2intc_irpt] \
    [get_bd_pins iic2intc_irpt2]
  }
  connect_bd_net -net axi_intc_0_irq  [get_bd_pins axi_intc_0/irq] \
  [get_bd_pins irq]
  connect_bd_net -net clk_wizard_0_clk_out1  [get_bd_pins clk_wizard_0/clk_151] \
  [get_bd_pins proc_sys_reset_151/slowest_sync_clk] \
  [get_bd_pins preproc_hier/clk_151] \
  [get_bd_pins SO_frmbuf_hier/clk_151] \
  [get_bd_pins clk_151] \
  [get_bd_pins axi_noc2_0/aclk6] \
  [get_bd_pins axi_noc2_1/aclk0] \
  [get_bd_pins smartconnect_0/aclk1] \
  [get_bd_pins visp_ss_0/tile0_pl_isp_vidout0_clk] \
  [get_bd_pins visp_ss_0/tile0_pl_isp_vidout1_clk] \
  [get_bd_pins visp_ss_0/tile0_ref_dpll_clk] \
  [get_bd_pins visp_ss_0/tile1_pl_isp_vidout0_clk] \
  [get_bd_pins visp_ss_0/tile1_pl_isp_vidout1_clk] \
  [get_bd_pins visp_ss_0/tile1_ref_dpll_clk]
  connect_bd_net -net clk_wizard_0_locked  [get_bd_pins clk_wizard_0/locked] \
  [get_bd_pins proc_sys_reset_151/dcm_locked]
  connect_bd_net -net clkx5_wiz_0_dphy_clk_200M  [get_bd_pins dphy_clk_200M] \
  [get_bd_pins mipi_csi2_rx_subsyst_0/dphy_clk_200M] \
  [get_bd_pins mipi_csi2_rx_subsyst_1/dphy_clk_200M]
  connect_bd_net -net ilconcat_0_dout  [get_bd_pins ilconcat_0/dout] \
  [get_bd_pins axi_intc_0/intr]
  connect_bd_net -net ilconcat_1_dout  [get_bd_pins ilconcat_1/dout] \
  [get_bd_pins axis_broadcaster_0/m_axis_tready]
  connect_bd_net -net ilconcat_2_dout  [get_bd_pins ilconcat_2/dout] \
  [get_bd_pins axis_broadcaster_1/m_axis_tready]
  connect_bd_net -net ilvector_logic_2_Res  [get_bd_pins ilvector_logic_2/Res] \
  [get_bd_pins ilconcat_1/In0] \
  [get_bd_pins ilconcat_1/In1]
  connect_bd_net -net ilvector_logic_2_Res1  [get_bd_pins ilvector_logic_4/Res] \
  [get_bd_pins ilconcat_2/In0] \
  [get_bd_pins ilconcat_2/In1]
  connect_bd_net -net mipi_csi2_rx_subsyst_0_csirxss_csi_irq  [get_bd_pins mipi_csi2_rx_subsyst_0/csirxss_csi_irq] \
  [get_bd_pins ilconcat_0/In1]
  connect_bd_net -net mipi_csi2_rx_subsyst_0_header_data  [get_bd_pins mipi_csi2_rx_subsyst_0/header_data] \
  [get_bd_pins visp_ss_0/tile0_isp_mipi_vidin0_header_data] \
  [get_bd_pins visp_ss_0/tile0_isp_mipi_vidin4_header_data]
  connect_bd_net -net mipi_csi2_rx_subsyst_0_header_valid  [get_bd_pins mipi_csi2_rx_subsyst_0/header_valid] \
  [get_bd_pins visp_ss_0/tile0_isp_mipi_vidin0_header_valid] \
  [get_bd_pins visp_ss_0/tile0_isp_mipi_vidin4_header_valid]
  connect_bd_net -net mipi_csi2_rx_subsyst_1_csirxss_csi_irq  [get_bd_pins mipi_csi2_rx_subsyst_1/csirxss_csi_irq] \
  [get_bd_pins ilconcat_0/In0]
  connect_bd_net -net mipi_csi2_rx_subsyst_1_header_data  [get_bd_pins mipi_csi2_rx_subsyst_1/header_data] \
  [get_bd_pins visp_ss_0/tile1_isp_mipi_vidin0_header_data] \
  [get_bd_pins visp_ss_0/tile1_isp_mipi_vidin4_header_data]
  connect_bd_net -net mipi_csi2_rx_subsyst_1_header_valid  [get_bd_pins mipi_csi2_rx_subsyst_1/header_valid] \
  [get_bd_pins visp_ss_0/tile1_isp_mipi_vidin0_header_valid] \
  [get_bd_pins visp_ss_0/tile1_isp_mipi_vidin4_header_valid]
  connect_bd_net -net preproc_hier_gpio_io_o  [get_bd_pins preproc_hier/gpio_io_o] \
  [get_bd_pins SO_frmbuf_hier/Din] \
  [get_bd_pins gpio_io_o]
  connect_bd_net -net preproc_hier_interrupt  [get_bd_pins preproc_hier/interrupt] \
  [get_bd_pins ilconcat_0/In6]
  connect_bd_net -net preproc_hier_interrupt1  [get_bd_pins preproc_hier/interrupt1] \
  [get_bd_pins ilconcat_0/In7]
  connect_bd_net -net preproc_hier_interrupt2  [get_bd_pins preproc_hier/interrupt2] \
  [get_bd_pins ilconcat_0/In8]
  connect_bd_net -net preproc_hier_interrupt3  [get_bd_pins preproc_hier/interrupt3] \
  [get_bd_pins ilconcat_0/In9]
  connect_bd_net -net preproc_hier_interrupt4  [get_bd_pins preproc_hier/interrupt4] \
  [get_bd_pins ilconcat_0/In10]
  connect_bd_net -net preproc_hier_interrupt5  [get_bd_pins preproc_hier/interrupt5] \
  [get_bd_pins ilconcat_0/In11]
  connect_bd_net -net preproc_hier_interrupt6  [get_bd_pins preproc_hier/interrupt6] \
  [get_bd_pins ilconcat_0/In12]
  connect_bd_net -net preproc_hier_interrupt7  [get_bd_pins preproc_hier/interrupt7] \
  [get_bd_pins ilconcat_0/In13]
  connect_bd_net -net proc_sys_reset_300_peripheral_aresetn  [get_bd_pins proc_sys_reset_151/peripheral_aresetn] \
  [get_bd_pins preproc_hier/s_axi_lite_rstn] \
  [get_bd_pins SO_frmbuf_hier/s_axi_lite_rstn] \
  [get_bd_pins peripheral_aresetn]
  if {$fmc_card eq "96716A"} {
    connect_bd_net -net s_axi_aresetn  [get_bd_pins video_aresetn] \
    [get_bd_pins proc_sys_reset_151/ext_reset_in] \
    [get_bd_pins fmc_iic_2/s_axi_aresetn] \
    [get_bd_pins fmc_iic_3/s_axi_aresetn] \
    [get_bd_pins fmc_iic_5/s_axi_aresetn] \
    [get_bd_pins smartconnect_rpu/aresetn] \
    [get_bd_pins axi_intc_0/s_axi_aresetn] \
    [get_bd_pins axis_broadcaster_0/aresetn] \
    [get_bd_pins axis_broadcaster_1/aresetn] \
    [get_bd_pins clk_wizard_0/resetn] \
    [get_bd_pins mipi_csi2_rx_subsyst_0/lite_aresetn] \
    [get_bd_pins mipi_csi2_rx_subsyst_0/video_aresetn] \
    [get_bd_pins mipi_csi2_rx_subsyst_1/lite_aresetn] \
    [get_bd_pins mipi_csi2_rx_subsyst_1/video_aresetn] \
    [get_bd_pins smartconnect_0/aresetn] \
    [get_bd_pins visp_ss_0/s_axi_lite_rstn] \
    [get_bd_pins visp_ss_0/tile0_pl_isp_rstn] \
    [get_bd_pins visp_ss_0/tile1_pl_isp_rstn]
  } else {
    connect_bd_net -net s_axi_aresetn  [get_bd_pins video_aresetn] \
    [get_bd_pins proc_sys_reset_151/ext_reset_in] \
    [get_bd_pins axi_intc_0/s_axi_aresetn] \
    [get_bd_pins axis_broadcaster_0/aresetn] \
    [get_bd_pins axis_broadcaster_1/aresetn] \
    [get_bd_pins clk_wizard_0/resetn] \
    [get_bd_pins mipi_csi2_rx_subsyst_0/lite_aresetn] \
    [get_bd_pins mipi_csi2_rx_subsyst_0/video_aresetn] \
    [get_bd_pins mipi_csi2_rx_subsyst_1/lite_aresetn] \
    [get_bd_pins mipi_csi2_rx_subsyst_1/video_aresetn] \
    [get_bd_pins smartconnect_0/aresetn] \
    [get_bd_pins visp_ss_0/s_axi_lite_rstn] \
    [get_bd_pins visp_ss_0/tile0_pl_isp_rstn] \
    [get_bd_pins visp_ss_0/tile1_pl_isp_rstn]
  }
  if {$fmc_card eq "96716A"} {
    connect_bd_net -net video_aclk_1  [get_bd_pins video_aclk] \
    [get_bd_pins fmc_iic_2/s_axi_aclk] \
    [get_bd_pins fmc_iic_3/s_axi_aclk] \
    [get_bd_pins fmc_iic_5/s_axi_aclk] \
    [get_bd_pins smartconnect_rpu/aclk] \
    [get_bd_pins axi_intc_0/s_axi_aclk] \
    [get_bd_pins axis_broadcaster_0/aclk] \
    [get_bd_pins axis_broadcaster_1/aclk] \
    [get_bd_pins clk_wizard_0/clk_in1] \
    [get_bd_pins mipi_csi2_rx_subsyst_0/lite_aclk] \
    [get_bd_pins mipi_csi2_rx_subsyst_0/video_aclk] \
    [get_bd_pins mipi_csi2_rx_subsyst_1/lite_aclk] \
    [get_bd_pins mipi_csi2_rx_subsyst_1/video_aclk] \
    [get_bd_pins smartconnect_0/aclk] \
    [get_bd_pins visp_ss_0/s_axi_lite_aclk] \
    [get_bd_pins visp_ss_0/tile0_pl_isp_vidin0_clk] \
    [get_bd_pins visp_ss_0/tile0_pl_isp_vidin4_clk] \
    [get_bd_pins visp_ss_0/tile1_pl_isp_vidin0_clk] \
    [get_bd_pins visp_ss_0/tile1_pl_isp_vidin4_clk]
  } else {
    connect_bd_net -net video_aclk_1  [get_bd_pins video_aclk] \
    [get_bd_pins axi_intc_0/s_axi_aclk] \
    [get_bd_pins axis_broadcaster_0/aclk] \
    [get_bd_pins axis_broadcaster_1/aclk] \
    [get_bd_pins clk_wizard_0/clk_in1] \
    [get_bd_pins mipi_csi2_rx_subsyst_0/lite_aclk] \
    [get_bd_pins mipi_csi2_rx_subsyst_0/video_aclk] \
    [get_bd_pins mipi_csi2_rx_subsyst_1/lite_aclk] \
    [get_bd_pins mipi_csi2_rx_subsyst_1/video_aclk] \
    [get_bd_pins smartconnect_0/aclk] \
    [get_bd_pins visp_ss_0/s_axi_lite_aclk] \
    [get_bd_pins visp_ss_0/tile0_pl_isp_vidin0_clk] \
    [get_bd_pins visp_ss_0/tile0_pl_isp_vidin4_clk] \
    [get_bd_pins visp_ss_0/tile1_pl_isp_vidin0_clk] \
    [get_bd_pins visp_ss_0/tile1_pl_isp_vidin4_clk]
  }
  connect_bd_net -net visp_ss_0_TILE0_ISP_MIPI_VIDIN0_tready  [get_bd_pins visp_ss_0/TILE0_ISP_MIPI_VIDIN0_tready] \
  [get_bd_pins ilvector_logic_2/Op1]
  connect_bd_net -net visp_ss_0_TILE0_ISP_MIPI_VIDIN4_tready  [get_bd_pins visp_ss_0/TILE0_ISP_MIPI_VIDIN4_tready] \
  [get_bd_pins ilvector_logic_2/Op2]
  connect_bd_net -net visp_ss_0_TILE1_ISP_MIPI_VIDIN0_tready  [get_bd_pins visp_ss_0/TILE1_ISP_MIPI_VIDIN0_tready] \
  [get_bd_pins ilvector_logic_4/Op1]
  connect_bd_net -net visp_ss_0_TILE1_ISP_MIPI_VIDIN4_tready  [get_bd_pins visp_ss_0/TILE1_ISP_MIPI_VIDIN4_tready] \
  [get_bd_pins ilvector_logic_4/Op2]
  connect_bd_net -net visp_ss_0_tile0_isp0_fusa_irq  [get_bd_pins visp_ss_0/tile0_isp0_fusa_irq] \
  [get_bd_pins tile0_isp0_fusa_irq]
  connect_bd_net -net visp_ss_0_tile0_isp0_isp_irq  [get_bd_pins visp_ss_0/tile0_isp0_isp_irq] \
  [get_bd_pins tile0_isp0_isp_irq]
  connect_bd_net -net visp_ss_0_tile0_isp1_fusa_irq  [get_bd_pins visp_ss_0/tile0_isp1_fusa_irq] \
  [get_bd_pins tile0_isp1_fusa_irq]
  connect_bd_net -net visp_ss_0_tile0_isp1_isp_irq  [get_bd_pins visp_ss_0/tile0_isp1_isp_irq] \
  [get_bd_pins tile0_isp1_isp_irq]
  connect_bd_net -net visp_ss_0_tile0_isp_isr_irq  [get_bd_pins visp_ss_0/tile0_isp_isr_irq] \
  [get_bd_pins tile0_isp_isr_irq]
  connect_bd_net -net visp_ss_0_tile0_isp_xmpu_interrupt  [get_bd_pins visp_ss_0/tile0_isp_xmpu_interrupt] \
  [get_bd_pins tile0_isp_xmpu_interrupt]
  connect_bd_net -net visp_ss_0_tile0_nmu0_axi_clk  [get_bd_pins visp_ss_0/tile0_nmu0_axi_clk] \
  [get_bd_pins axi_noc2_0/aclk0]
  connect_bd_net -net visp_ss_0_tile0_nmu1_axi_clk  [get_bd_pins visp_ss_0/tile0_nmu1_axi_clk] \
  [get_bd_pins axi_noc2_0/aclk1]
  connect_bd_net -net visp_ss_0_tile0_nsu_axi_clk  [get_bd_pins visp_ss_0/tile0_nsu_axi_clk] \
  [get_bd_pins axi_noc2_0/aclk2]
  connect_bd_net -net visp_ss_0_tile1_isp0_fusa_irq  [get_bd_pins visp_ss_0/tile1_isp0_fusa_irq] \
  [get_bd_pins tile1_isp0_fusa_irq]
  connect_bd_net -net visp_ss_0_tile1_isp0_isp_irq  [get_bd_pins visp_ss_0/tile1_isp0_isp_irq] \
  [get_bd_pins tile1_isp0_isp_irq]
  connect_bd_net -net visp_ss_0_tile1_isp1_fusa_irq  [get_bd_pins visp_ss_0/tile1_isp1_fusa_irq] \
  [get_bd_pins tile1_isp1_fusa_irq]
  connect_bd_net -net visp_ss_0_tile1_isp1_isp_irq  [get_bd_pins visp_ss_0/tile1_isp1_isp_irq] \
  [get_bd_pins tile1_isp1_isp_irq]
  connect_bd_net -net visp_ss_0_tile1_isp_isr_irq  [get_bd_pins visp_ss_0/tile1_isp_isr_irq] \
  [get_bd_pins tile1_isp_isr_irq]
  connect_bd_net -net visp_ss_0_tile1_isp_xmpu_interrupt  [get_bd_pins visp_ss_0/tile1_isp_xmpu_interrupt] \
  [get_bd_pins tile1_isp_xmpu_interrupt]
  connect_bd_net -net visp_ss_0_tile1_nmu0_axi_clk  [get_bd_pins visp_ss_0/tile1_nmu0_axi_clk] \
  [get_bd_pins axi_noc2_0/aclk3]
  connect_bd_net -net visp_ss_0_tile1_nmu1_axi_clk  [get_bd_pins visp_ss_0/tile1_nmu1_axi_clk] \
  [get_bd_pins axi_noc2_0/aclk4]
  connect_bd_net -net visp_ss_0_tile1_nsu_axi_clk  [get_bd_pins visp_ss_0/tile1_nsu_axi_clk] \
  [get_bd_pins axi_noc2_0/aclk5]

  # Restore current instance
  current_bd_instance $oldCurInst
}

