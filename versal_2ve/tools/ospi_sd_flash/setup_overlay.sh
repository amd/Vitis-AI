#!/bin/bash

echo "----load PL xclbins----"
fpgautil -b x_plus_ml.pdi -o x_plus_ml.dtbo

echo "----copy PL cfg and xclbin to rootfs----"
mkdir -p /run/media/mmcblk0p1/
cp ./x_plus_ml.xclbin /run/media/mmcblk0p1/
cp ./image_processing.cfg /run/media/mmcblk0p1/

