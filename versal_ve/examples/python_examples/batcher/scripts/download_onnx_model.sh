#!/bin/bash

# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

dir="$(dirname "$(readlink -e "${BASH_SOURCE[0]}")")"
MODEL_DIR="./models"

network_dir=$MODEL_DIR/$1/onnxRuntime
mkdir -p $network_dir

if [ ! -f $network_dir/labels ]
then
    cp links/caffe_synset_words.txt $network_dir/labels
fi

[ "$VAISW_INSTALL_DIR" != "" ] && . $VAISW_INSTALL_DIR/unset.sh

python3 $dir/download_onnx_model.py $1 $network_dir $2 $3 || exit 1
