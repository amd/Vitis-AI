#!/bin/bash

# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

set -o pipefail

model=$1
shift

MODEL_DIR="./models"
network_dir=$MODEL_DIR/$model/tensorflow

[ "$VAISW_INSTALL_DIR" != "" ] && . $VAISW_INSTALL_DIR/unset.sh

case $model in
    "resnet50-v1.5")
        mkdir -p $network_dir
        LABEL_DIR="links/imagenet_synset_words.txt"
        wget -O $network_dir/resnet50_v1.pb https://zenodo.org/record/2535873/files/resnet50_v1.pb || exit 1
        ln -s resnet50_v1.pb $network_dir/network
        ;;
    "mobilenet_v1")
        mkdir -p $network_dir
        LABEL_DIR="links/imagenet_synset_words.txt"
        wget -O - http://download.tensorflow.org/models/mobilenet_v1_2018_08_02/mobilenet_v1_1.0_224.tgz  \
             | tar xz ./mobilenet_v1_1.0_224_frozen.pb && mv mobilenet_v1_1.0_224_frozen.pb $network_dir/network \
             || exit 1 ;;
    "mobilenet_v2")
        mkdir -p $network_dir
        LABEL_DIR="links/imagenet_synset_words.txt"
        wget -O - https://storage.googleapis.com/mobilenet_v2/checkpoints/mobilenet_v2_1.4_224.tgz \
             | tar xz ./mobilenet_v2_1.4_224_frozen.pb && mv ./mobilenet_v2_1.4_224_frozen.pb $network_dir/network \
             || exit 1 ;;
    *)
        echo "ERROR: model $model not found; Cannot be downloaded"
        exit 1 ;;
esac

if [ ! -f $network_dir/labels ]
then
    cp $LABEL_DIR $network_dir/labels
fi

