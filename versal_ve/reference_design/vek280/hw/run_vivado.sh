#!/bin/bash

set -o pipefail
ABS_PATH=$(pwd)
export PROJECT_NAME=example_design
export NPUIP_TOP=npuip_top_embedded
export SYSTEMS=1
export CORES=1
export NCES=6
export AIES_BY_NCE=8
export FIRST_COLUMN=6
export DDRS=3
export AIE_IN_STREAMS=1
export AIE_OUT_STREAMS=2

#Production Board
export BOARD=XIL_VEK280
export CHIP_PART=xcve2802-vsvh1760-2MP-e-S
export BOARD_PART=xilinx.com:vek280:part0:1.0
export CONFIG_PKT_SPLIT=2


echo '# Hardware platform generation is started...'
vivado -mode tcl -source create_platform.tcl >> $ABS_PATH/cmd.log
if [ $? != 0 ]; then tail $ABS_PATH/cmd.log && exit 1; fi
echo '# Hardware platform generation is completed...'
