#!/bin/bash

# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

set -o pipefail

# use a WIP dir so directory doesn't exist on error
mkdir -p $1/WIP || exit 1
cd $1/WIP

wget -O - "http://host.robots.ox.ac.uk/pascal/VOC/voc2007/VOCtest_06-Nov-2007.tar"     | tar --strip-components 1 -x || exit 1
wget -O - "http://host.robots.ox.ac.uk/pascal/VOC/voc2007/VOCtrainval_06-Nov-2007.tar" | tar --strip-components 1 -x || exit 1

# put images in 2007 directory
mv VOC2007/JPEGImages 2007

cd ..
mv WIP/* .
rmdir WIP
