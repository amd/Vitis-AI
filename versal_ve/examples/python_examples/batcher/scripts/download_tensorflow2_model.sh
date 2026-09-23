#!/bin/bash

# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

set -o pipefail

dir="$(dirname "$(readlink -e "${BASH_SOURCE[0]}")")"

model=$1
shift

LABEL_DIR=""
MODEL_DIR="./models"
network_dir=$MODEL_DIR/$model/tensorflow2

[ "$VAISW_INSTALL_DIR" != "" ] && . $VAISW_INSTALL_DIR/unset.sh

case $model in
    "resnet50")
        mkdir -p $network_dir/network
        LABEL_DIR="links/caffe_synset_words.txt"
        wget -O - https://tfhub.dev/tensorflow/resnet_50/classification/1?tf-hub-format=compressed | tar xz -C $network_dir/network || exit 1
        ;;
    "resnet50v1")
        mkdir -p $network_dir/network
        LABEL_DIR="links/imagenet_synset_words.txt"
        wget -O - https://tfhub.dev/google/imagenet/resnet_v1_50/classification/5?tf-hub-format=compressed | tar xz -C $network_dir/network || exit 1
        ;;
    "resnet50v2")
        mkdir -p $network_dir/network
        LABEL_DIR="links/imagenet_synset_words.txt"
        wget -O - https://tfhub.dev/google/imagenet/resnet_v2_50/classification/5?tf-hub-format=compressed | tar xz -C $network_dir/network || exit 1
        ;;
    "resnet101")
        mkdir -p $network_dir/network
        LABEL_DIR="links/imagenet_synset_words.txt"
        wget -O - https://tfhub.dev/google/imagenet/resnet_v1_101/classification/5?tf-hub-format=compressed | tar xz -C $network_dir/network || exit 1
        ;;
    "resnet101v2")
        mkdir -p $network_dir/network
        LABEL_DIR="links/imagenet_synset_words.txt"
        wget -O - https://tfhub.dev/google/imagenet/resnet_v2_101/classification/5?tf-hub-format=compressed | tar xz -C $network_dir/network || exit 1
        ;;
    "resnet152")
        mkdir -p $network_dir/network
        LABEL_DIR="links/imagenet_synset_words.txt"
        wget -O - https://tfhub.dev/google/imagenet/resnet_v1_152/classification/5?tf-hub-format=compressed | tar xz -C $network_dir/network || exit 1
        ;;
    "resnet152v2")
        mkdir -p $network_dir/network
        LABEL_DIR="links/imagenet_synset_words.txt"
        wget -O - https://tfhub.dev/google/imagenet/resnet_v2_152/classification/5?tf-hub-format=compressed | tar xz -C $network_dir/network || exit 1
        ;;
    "inception_resnet_v2")
        mkdir -p $network_dir/network
        LABEL_DIR="links/imagenet_synset_words.txt"
        wget -O - https://tfhub.dev/google/imagenet/inception_resnet_v2/classification/5?tf-hub-format=compressed | tar xz -C $network_dir/network || exit 1
        ;;
    "inceptionv1")
        mkdir -p $network_dir/network
        LABEL_DIR="links/imagenet_synset_words.txt"
        wget -O - https://tfhub.dev/google/imagenet/inception_v1/classification/5?tf-hub-format=compressed | tar xz -C $network_dir/network || exit 1
        ;;
    "inceptionv2")
        mkdir -p $network_dir/network
        LABEL_DIR="links/imagenet_synset_words.txt"
        wget -O - https://tfhub.dev/google/imagenet/inception_v2/classification/5?tf-hub-format=compressed | tar xz -C $network_dir/network || exit 1
        ;;
    "inceptionv3")
        mkdir -p $network_dir/network
        LABEL_DIR="links/imagenet_synset_words.txt"
        wget -O - https://tfhub.dev/google/imagenet/inception_v3/classification/5?tf-hub-format=compressed | tar xz -C $network_dir/network || exit 1
        ;;
    "mobilenet_v1")
        mkdir -p $network_dir/network
        LABEL_DIR="links/imagenet_synset_words.txt"
        wget -O - https://tfhub.dev/google/imagenet/mobilenet_v1_100_224/classification/5?tf-hub-format=compressed | tar xz -C $network_dir/network || exit 1
        ;;
    "mobilenet_v2")
        mkdir -p $network_dir/network
        LABEL_DIR="links/imagenet_synset_words.txt"
        wget -O - https://tfhub.dev/google/imagenet/mobilenet_v2_100_224/classification/5?tf-hub-format=compressed | tar xz -C $network_dir/network || exit 1
        ;;
    "efficientdetd0")
        mkdir -p $network_dir/network
        LABEL_DIR="links/labels91"
        wget -O - https://tfhub.dev/tensorflow/efficientdet/d0/1?tf-hub-format=compressed | tar xz -C $network_dir/network || exit 1
        ;;
    "efficientdetd1")
        mkdir -p $network_dir/network
        LABEL_DIR="links/labels91"
        wget -O - https://tfhub.dev/tensorflow/efficientdet/d1/1?tf-hub-format=compressed | tar xz -C $network_dir/network || exit 1
        ;;
    "ssd-mobilenetv1-640")
        mkdir -p $network_dir/network
        LABEL_DIR="links/labels91"
        wget -O - https://tfhub.dev/tensorflow/ssd_mobilenet_v1/fpn_640x640/1?tf-hub-format=compressed | tar xz -C $network_dir/network || exit 1
        ;;
    "ssd-mobilenetv2-320")
        mkdir -p $network_dir/network
        LABEL_DIR="links/labels91"
        wget -O - https://tfhub.dev/tensorflow/ssd_mobilenet_v2/2?tf-hub-format=compressed | tar xz -C $network_dir/network || exit 1
        ;;
    "retinanet-rn50-640")
        mkdir -p $network_dir/network
        LABEL_DIR="links/labels91"
        wget -O - https://tfhub.dev/tensorflow/retinanet/resnet50_v1_fpn_640x640/1?tf-hub-format=compressed | tar xz -C $network_dir/network || exit 1
        ;;
    "vgg16")
        mkdir -p $network_dir/network
        python3 $dir/download_tensorflow2_model.py $model $network_dir/network || exit 1
        LABEL_DIR="links/caffe_synset_words.txt"
        ;;
    "vgg19")
        mkdir -p $network_dir/network
        python3 $dir/download_tensorflow2_model.py $model $network_dir/network || exit 1
        LABEL_DIR="links/caffe_synset_words.txt"
        ;;
    "densenet121" | "densenet169" | "densenet201")
        mkdir -p $network_dir/network
        python3 $dir/download_tensorflow2_model.py $model $network_dir/network || exit 1
        LABEL_DIR="links/caffe_synset_words.txt"
        ;;
    "xception")
        mkdir -p $network_dir/network
        python3 $dir/download_tensorflow2_model.py $model $network_dir/network || exit 1
        LABEL_DIR="links/caffe_synset_words.txt"
        ;;
    *)
        echo "ERROR: model $model not found; Cannot be downloaded"
        exit 1 ;;
esac

if [ ! -f $network_dir/labels ]
then
    cp $LABEL_DIR $network_dir/labels
fi

