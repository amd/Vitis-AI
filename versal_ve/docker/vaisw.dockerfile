
# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

FROM ubuntu:24.04 AS vaisw_download

RUN apt-get update && apt-get install -y \
  wget

RUN mkdir /DOWNLOAD

WORKDIR /DOWNLOAD

# we first download the weights to any future docker cache miss implying to
# redownload the weights
# also, we can share the weights within demos

RUN umask 0 && \
  wget https://data.pjreddie.com/files/yolov2-tiny.weights && \
  wget https://data.pjreddie.com/files/yolov2-voc.weights && \
  wget https://data.pjreddie.com/files/yolov2.weights

RUN umask 0 && \
  wget https://data.pjreddie.com/files/yolov3-tiny.weights && \
  wget https://data.pjreddie.com/files/yolov3.weights

COPY wget_google_drive.bash /tmp/wget_google_drive.bash

RUN umask 0 && \
  wget --no-check-certificate --content-disposition https://github.com/AlexeyAB/darknet/releases/download/darknet_yolo_v3_optimal/yolov4.weights && \
  wget --no-check-certificate --content-disposition https://github.com/AlexeyAB/darknet/releases/download/darknet_yolo_v4_pre/yolov4-tiny.weights

FROM vaisw_download AS vaisw_base

ENV DEBIAN_FRONTEND=noninteractive

# install package dependencies
RUN apt-get update && apt-get install -y \
  git \
  vim \
  wget \
  xterm \
  x11-xserver-utils \
  libglib2.0-dev \
  libsm-dev \
  libxrender-dev \
  rsync \
  cython3 \
  python3-pip \
  python3-venv \
  swig \
  python3-tk \
  imagemagick \
  unrar \
  unzip \
  numactl \
  ffmpeg \
  bc

ENV BASE_PATH="$PATH"
# Setup base venv
ENV BASE_VENV=/usr/base_venv
RUN python3 -m venv ${BASE_VENV}
ENV PATH="${BASE_VENV}/bin:${BASE_PATH}"

RUN python3 -m pip --no-cache-dir install --upgrade pip

# Avoid installing torch in base env
RUN python3 -m pip install --no-cache-dir --no-deps \
  thop==0.1.1.post2209072238

RUN python3 -m pip install --no-cache-dir \
  cython==0.29.36 \
  opencv-python==4.11.0.86 pillow==11.2.1 \
  numpy==1.26.4 matplotlib==3.10.3 pandas==2.2.3 \
  scipy==1.15.3 scikit_learn==1.6.1 scikit-image==0.25.2 \
  tensorflow==2.19.0 keras==3.9.2 Keras-Applications==1.0.8 \
  tf2onnx==1.16.1 tensorpack==0.11 tf_slim==1.1.0 \
  onnx==1.17.0 onnxruntime==1.22.0 \
  future==1.0.0 h5py==3.13.0 \
  lxml==5.4.0 tqdm==4.67.1 pycocotools==2.0.8 \
  slidingwindow==0.0.14 imageio==2.37.0 \
  IPython==9.2.0 imgaug==0.4.0 \
  youtube_dl==2021.12.17 pafy==0.5.5 python-box==7.3.2 \
  seaborn==0.13.2 easydict==1.13 mxnet==1.9.1 \
  loguru==0.7.3 tabulate==0.9.0 \
  toml==0.10.2 PyYAML==6.0.2 \
  protobuf==3.20.3
# This last line is for packages forced at an "older" version to prevent issues

# Setup venv with torch+rocm
ENV ROCM_VENV="/usr/rocm_venv"
RUN python3 -m venv ${ROCM_VENV}
ENV PATH="${ROCM_VENV}/bin:${BASE_PATH}"
# Add base env packages as .pth
RUN realpath ${BASE_VENV}/lib/python3.12/site-packages > ${ROCM_VENV}/lib/python3.12/site-packages/base_venv.pth
# Install new packages for this venv specifically
RUN python3 -m pip --no-cache-dir install torch==2.7.0 torchvision==0.22.0 --index-url https://download.pytorch.org/whl/rocm6.3

# Setup venv with torch+cuda
ENV CUDA_VENV="/usr/cuda_venv"
RUN python3 -m venv ${CUDA_VENV}
ENV PATH="${CUDA_VENV}/bin:${BASE_PATH}"
# Add base env packages as .pth
RUN realpath ${BASE_VENV}/lib/python3.12/site-packages > ${CUDA_VENV}/lib/python3.12/site-packages/base_venv.pth
# Install new packages for this venv specifically
RUN python3 -m pip --no-cache-dir install torch==2.7.0 torchvision==0.22.0 --index-url https://download.pytorch.org/whl/cu128

# Reset PATH to only contain the base venv (in case of Python commands in demo setups below)
ENV PATH="${BASE_VENV}/bin:${BASE_PATH}"

COPY vaisw_config_add.bash /tmp/vaisw_config_add.bash

RUN echo "root:demo" | chpasswd

######################################################################
#
# all RUN command should start with umask 0, like
# RUN umask 0 &&
#
# NOTE: if the current build is changed, it will invalidate the cache
# for all below builds
#
######################################################################

WORKDIR /home/demo

######################################################################
# if an application requires a specific python package, it has to install it
# locally (using PYTHONUSERBASE=.local), then the package will be used through
# PYTHONPATH

######################################################################
# all application should follow the following:
# FROM vaisw_base AS <app_name>
# - git clone
# - PYTHONUSERBASE=.local python3 -m pip install --user <dependencies>
# - download weights
# - freeze models (if applicable)
# - copy patch and apply
#
# NOTE: for optimal docker cache, the copy patch should come at the end
#
######################################################################

# Fix tf_slim for newer tf
RUN umask 0 && \
  sed -i -e "s/^.*import control_flow_ops/import tensorflow as tf/" \
         -e "s/control_flow_ops\./tf./g" -e "s/ops\.Tensor/tf.Tensor/g" \
         ${BASE_VENV}/lib/python3.*/site-packages/tf_slim/layers/utils.py



######################################################################
# DEMO:darkflow yoloV2 from https://github.com/thtrieu/darkflow
######################################################################
FROM vaisw_base AS darkflow

RUN umask 0 && \
  git clone https://github.com/thtrieu/darkflow && \
  cd darkflow && \
  git checkout -b mipso b2aee0000cd2a956b9f1de6dbfef94d53158b7d8

COPY darkflow.patch /tmp/darkflow.patch

RUN umask 0 && \
  cd darkflow && \
  patch -p1 -i /tmp/darkflow.patch

RUN umask 0 && \
  cd darkflow && \
  python3 setup.py build_ext --inplace && \
  mkdir -p download && \
  cd download && \
  wget https://github.com/pjreddie/darknet/raw/master/cfg/yolov2-voc.cfg && \
  wget https://github.com/pjreddie/darknet/raw/master/cfg/yolov2.cfg && \
  wget https://github.com/pjreddie/darknet/raw/master/cfg/yolov2-tiny.cfg && \
  tail -c +5 /DOWNLOAD/yolov2-voc.weights > yolov2-voc_fixed.weights && \
  tail -c +5 /DOWNLOAD/yolov2-tiny.weights > yolov2-tiny_fixed.weights && \
  cd .. && \
  python3 ./flow --model download/yolov2-voc.cfg --load download/yolov2-voc_fixed.weights --labels /dev/null --savepb ; test -f built_graph/yolov2-voc.pb && \
  python3 ./flow --model download/yolov2.cfg --load /DOWNLOAD/yolov2.weights --labels cfg/coco.names --savepb ; test -f built_graph/yolov2.pb && \
  python3 ./flow --model download/yolov2-tiny.cfg --load download/yolov2-tiny_fixed.weights --labels cfg/coco.names --savepb ; test -f built_graph/yolov2-tiny.pb && \
  rm -rf download

# Disabled because keras3 not supported
# ######################################################################
# # DEMO:tensorflow-yolo3 yoloV3 from https://github.com/aloyschen/tensorflow-yolo3
# ######################################################################
# FROM vaisw_base AS tensorflow_yolov3

# RUN umask 0 && \
#   git clone https://github.com/aloyschen/tensorflow-yolo3 && \
#   cd tensorflow-yolo3 && \
#   git checkout -b mipso 646f4532487ff728695c55fb3b9a29fcd631e68d

# COPY tensorflow-yolo3.patch /tmp/tensorflow-yolo3.patch

# RUN umask 0 && \
#   cd tensorflow-yolo3 && \
#   patch -p1 -i /tmp/tensorflow-yolo3.patch

# RUN umask 0 && \
#   cd tensorflow-yolo3 && \
#   cd model_data && \
#   ln -s /DOWNLOAD/yolov3.weights . && \
#   cd .. && \
#   python3 detect.py --save_pb --out_file /dev/null --image_file dog.jpg && \
#   rm model_data/yolov3.weights


### CMU weights download fails
# ######################################################################
# # DEMO:ildoonet-tf-pose-estimation from https://github.com/jiajunhua/ildoonet-tf-pose-estimation
# ######################################################################
# FROM vaisw_base AS ildoonet-tf-pose-estimation

# RUN umask 0 && \
#   git clone https://github.com/jiajunhua/ildoonet-tf-pose-estimation && \
#   cd ildoonet-tf-pose-estimation && \
#   git checkout -b mipso c58f309c22ee84df4ee8d5a5dde81e851f3ede3f

# RUN umask 0 && \
#   cd ildoonet-tf-pose-estimation && \
#   cd tf_pose/pafprocess && \
#   swig -python -c++ pafprocess.i && python3 setup.py build_ext --inplace

# RUN umask 0 && \
#   cd ildoonet-tf-pose-estimation && \
#   sh ./models/graph/cmu/download.sh


######################################################################
# DEMO:EDSR-Pytorch from https://github.com/thstkdgus35/EDSR-PyTorch/
######################################################################
FROM vaisw_base AS edsr_pytorch

RUN umask 0 && \
  git clone https://github.com/thstkdgus35/EDSR-PyTorch && \
  cd EDSR-PyTorch  && \
  git checkout -b mipso 9d3bb0ec620ea2ac1b5e5e7a32b0133fbba66fd2

RUN umask 0 && \
  cd EDSR-PyTorch && \
  mkdir models && \
  cd models && \
  ( wget --no-hsts --secure-protocol=TLSv1 https://cv.snu.ac.kr/research/EDSR/models/edsr_baseline_x2-1bc95232.pt || \
    wget --no-hsts --secure-protocol=TLSv1 https://cv.snu.ac.kr/research/EDSR/models/edsr_baseline_x2-1bc95232.pt ) && \
  ( wget --no-hsts --secure-protocol=TLSv1 https://cv.snu.ac.kr/research/EDSR/models/edsr_baseline_x3-abf2a44e.pt || \
    wget --no-hsts --secure-protocol=TLSv1 https://cv.snu.ac.kr/research/EDSR/models/edsr_baseline_x3-abf2a44e.pt ) && \
  ( wget --no-hsts --secure-protocol=TLSv1 https://cv.snu.ac.kr/research/EDSR/models/edsr_baseline_x4-6b446fab.pt || \
    wget --no-hsts --secure-protocol=TLSv1 https://cv.snu.ac.kr/research/EDSR/models/edsr_baseline_x4-6b446fab.pt )


### Keras reliant
# ######################################################################
# # DEMO:Mask_RCNN from https://github.com/matterport/Mask_RCNN
# ######################################################################
# FROM vaisw_base AS mask_rcnn

# RUN umask 0 && \
#   git clone https://github.com/matterport/Mask_RCNN && \
#   cd Mask_RCNN  && \
#   git checkout -b mipso 3deaec5d902d16e1daf56b62d5971d428dc920bc

# RUN umask 0 && \
#   cd Mask_RCNN && \
#   mkdir weights && \
#   wget -O weights/mask_rcnn_coco_v1.0.h5 https://github.com/matterport/Mask_RCNN/releases/download/v1.0/mask_rcnn_coco.h5 && \
#   wget -O weights/mask_rcnn_coco_v2.0.h5 https://github.com/matterport/Mask_RCNN/releases/download/v2.0/mask_rcnn_coco.h5


######################################################################
# DEMO:SRGAN-PyTorch from https://github.com/twhui/SRGAN-PyTorch
######################################################################
FROM vaisw_base AS srgan_pytorch

RUN umask 0 && \
  git clone https://github.com/twhui/SRGAN-PyTorch && \
  cd SRGAN-PyTorch  && \
  git checkout -b mipso 039b15bc688a4bba6c197f1bc1de4b7a0eb4d115

COPY wget_google_drive.bash /tmp/wget_google_drive.bash

RUN umask 0 && \
  cd SRGAN-PyTorch && \
  /tmp/wget_google_drive.bash https://drive.google.com/uc?id=1BRRfis9HEWccJJsIEgPg0Ou3zV-3gVq5 SRResnet.rar && \
  /tmp/wget_google_drive.bash https://drive.google.com/uc?id=1vAtPLGbdyt--SZQxUl0YKPRQgu6-kR6v SRGAN.rar && \
  mkdir weights && \
  unrar x SRResnet.rar weights && \
  unrar x SRGAN.rar weights/ && \
  rm *.rar


# Disabled because keras3 not supported
# ############################################################################################
# # DEMO:tensorflow-yolov4-tflite from https://github.com/hunglc007/tensorflow-yolov4-tflite/
# ############################################################################################
# FROM vaisw_base AS tensorflow_yolov4_tflite

# RUN umask 0 && \
#   git clone https://github.com/hunglc007/tensorflow-yolov4-tflite && \
#   cd tensorflow-yolov4-tflite  && \
#   git checkout -b mipso 9f16748aa3f45ff240608da4bd9b1216a29127f5

# COPY tensorflow-yolov4-tflite.patch /tmp/tensorflow-yolov4-tflite.patch

# RUN umask 0 && \
#   cd tensorflow-yolov4-tflite && \
#   patch -p1 -i /tmp/tensorflow-yolov4-tflite.patch

# AHAHAHAHAHAHAHAHAHHAHAHAH KERAS3 :):):):):)
# RUN umask 0 && \
#   cd tensorflow-yolov4-tflite && \
#   mkdir weights && \
#   cd weights && \
#   ln -s /DOWNLOAD/yolov3.weights . && \
#   ln -s /DOWNLOAD/yolov3-tiny.weights . && \
#   ln -s /DOWNLOAD/yolov4.weights . && \
#   ln -s /DOWNLOAD/yolov4-tiny.weights . && \
#   cd .. && \
#   mkdir FROZEN && \
#   python3 save_model.py --weights "./weights/yolov3.weights" --output "FROZEN/yolov3" --model "yolov3" && \
#   python3 save_model.py --weights "./weights/yolov3-tiny.weights" --output "FROZEN/yolov3-tiny" --model "yolov3" --tiny && \
#   python3 save_model.py --weights "./weights/yolov4.weights" --output "FROZEN/yolov4" --model "yolov4" && \
#   python3 save_model.py --weights "./weights/yolov4-tiny.weights" --output "FROZEN/yolov4-tiny" --model "yolov4" --tiny && \
#   rm -rf weights/

######################################################################
# DEMO:YoloV5 from https://github.com/ultralytics/yolov5
#####################################################################
FROM vaisw_base AS yolov5

RUN umask 0 && \
  git clone https://github.com/ultralytics/yolov5 && \
  cd yolov5 && \
  git checkout -b mipso fdc9d9198e0dea90d0536f63b6408b97b1399cc1

RUN umask 0 && \
  cd yolov5 && \
  mkdir weights && cd weights && \
  wget https://github.com/ultralytics/yolov5/releases/download/v6.0/yolov5n.pt && \
  wget https://github.com/ultralytics/yolov5/releases/download/v6.0/yolov5s.pt && \
  wget https://github.com/ultralytics/yolov5/releases/download/v6.0/yolov5m.pt && \
  wget https://github.com/ultralytics/yolov5/releases/download/v6.0/yolov5l.pt && \
  wget https://github.com/ultralytics/yolov5/releases/download/v6.0/yolov5x.pt && \
  wget https://github.com/ultralytics/yolov5/releases/download/v6.0/yolov5n6.pt && \
  wget https://github.com/ultralytics/yolov5/releases/download/v6.0/yolov5s6.pt && \
  wget https://github.com/ultralytics/yolov5/releases/download/v6.0/yolov5m6.pt && \
  wget https://github.com/ultralytics/yolov5/releases/download/v6.0/yolov5l6.pt && \
  wget https://github.com/ultralytics/yolov5/releases/download/v6.0/yolov5x6.pt

RUN umask 0 && \
  cd yolov5 && \
  wget https://ultralytics.com/assets/Arial.ttf


######################################################################
# DEMO:PyTorch SSD from https://github.com/qfgaohao/pytorch-ssd
#####################################################################
FROM vaisw_base AS pytorch-ssd

RUN umask 0 && \
  git clone https://github.com/qfgaohao/pytorch-ssd && \
  cd pytorch-ssd && \
  git checkout -b mipso f61ab424d09bf3d4bb3925693579ac0a92541b0d

RUN umask 0 && \
  mkdir -p pytorch-ssd/models && \
  cd pytorch-ssd/models && \
  /tmp/wget_google_drive.bash https://drive.google.com/uc?id=1pSPLnWGGNs3kV_YSxr4vsmSvDCLpUsEr mobilenet-v1-ssd-mp-0_675.pth && \
  /tmp/wget_google_drive.bash https://drive.google.com/uc?id=1puI6ltKZKJ4RoiCO-ypivzEysHaDVBsa mb2-ssd-lite-mp-0_686.pth && \
  /tmp/wget_google_drive.bash https://drive.google.com/uc?id=1q1sXhKIxniInw3WASnEDKYMqPMuiDDvc voc-model-labels.txt


### Keras reliant
# ######################################################################
# # DEMO:EfficientDet from https://github.com/xuannianz/EfficientDet
# ######################################################################
# FROM vaisw_base AS efficientdet


# RUN umask 0 && \
#   git clone https://github.com/xuannianz/EfficientDet && \
#   cd EfficientDet && \
#   git checkout -b mipso 030fb7e10ab69a297c7723120c2d1be856a852c0

# COPY wget_google_drive.bash /tmp/wget_google_drive.bash

# RUN umask 0 && \
#   cd EfficientDet && \
#   /tmp/wget_google_drive.bash https://drive.google.com/uc?id=1MNB5q6rJ4TK_gen3iriu8-ArG9jB8aR9 efficientdet-d0.h5 && \
#   /tmp/wget_google_drive.bash https://drive.google.com/uc?id=11pQznCTi4MaVXqkJmCMcQhphMXurpx5Z efficientdet-d1.h5 && \
#   /tmp/wget_google_drive.bash https://drive.google.com/uc?id=1_yXrOrY0FDnH-d_FQIPbGy4z2ax4aNh8 efficientdet-d2.h5 && \
#   /tmp/wget_google_drive.bash https://drive.google.com/uc?id=1VnxoBpEQmm0Z2uO3gjhYDeu-rNirba6c efficientdet-d3.h5 && \
#   /tmp/wget_google_drive.bash https://drive.google.com/uc?id=1lQvTpnO_mfkHCRpcP28dxU4CWyK3xUzj efficientdet-d4.h5

# RUN umask 0 && \
#   cd EfficientDet && \
#   python3 setup.py build_ext --inplace


######################################################################
# DEMO:YoloX from https://github.com/Megvii-BaseDetection/YOLOX
#####################################################################
FROM vaisw_base AS yolox

RUN umask 0 && \
  git clone https://github.com/Megvii-BaseDetection/YOLOX && \
  cd YOLOX && \
  git checkout -b mipso 8ba7b6d1acbeb204917eb4ab82bfb0dda413dd10

RUN umask 0 && \
  cd YOLOX && \
  wget -P models https://github.com/Megvii-BaseDetection/YOLOX/releases/download/0.1.1rc0/yolox_nano.pth && \
  wget -P models https://github.com/Megvii-BaseDetection/YOLOX/releases/download/0.1.1rc0/yolox_tiny.pth && \
  wget -P models https://github.com/Megvii-BaseDetection/YOLOX/releases/download/0.1.1rc0/yolox_s.pth && \
  wget -P models https://github.com/Megvii-BaseDetection/YOLOX/releases/download/0.1.1rc0/yolox_m.pth && \
  wget -P models https://github.com/Megvii-BaseDetection/YOLOX/releases/download/0.1.1rc0/yolox_l.pth && \
  wget -P models https://github.com/Megvii-BaseDetection/YOLOX/releases/download/0.1.1rc0/yolox_x.pth


######################################################################
# DEMO:Refinedet Pytorch from https://github.com/dd604/refinedet.pytorch
#####################################################################
FROM vaisw_base AS refinedet.pytorch

RUN umask 0 && \
  git clone https://github.com/dd604/refinedet.pytorch.git && \
  cd refinedet.pytorch && \
  git checkout -b mipso 8ad0dae9b7e45d76f14e59c5fb40b60e59b9ea60

RUN umask 0 && \
  cd refinedet.pytorch && \
  wget -P data/pretrained_model https://s3.amazonaws.com/amdegroot-models/vgg16_reducedfc.pth && \
  wget -P data/pretrained_model https://download.pytorch.org/models/resnet101-5d3b4d8f.pth -O resnet101.pth && \
  wget -P output https://www.dropbox.com/s/gynb405fixwqitv/vgg16_refinedet320_voc_120000.pth && \
  wget -P output https://www.dropbox.com/s/y527gz2dz4ow0wz/vgg16_refinedet512_voc_120000.pth && \
  wget -P output https://www.dropbox.com/s/bu8khr18ped59n5/resnet101_refinedet320_coco_400000.pth && \
  wget -P output https://www.dropbox.com/s/d5wouxm12bp50ke/resnet101_refinedet512_coco_400000.pth


######################################################################
# DEMO:YoloV6 from https://github.com/meituan/YOLOv6.git
#####################################################################
FROM vaisw_base AS yolov6

RUN umask 0 && \
  git clone https://github.com/meituan/YOLOv6 && \
  cd YOLOv6 && \
  git checkout -b mipso 124a67ce82fd7625b2ad90a2845719a9969ca582

RUN umask 0 && \
  cd YOLOv6 && \
  wget https://github.com/meituan/YOLOv6/releases/download/0.2.0/yolov6n.pt && \
  wget https://github.com/meituan/YOLOv6/releases/download/0.2.0/yolov6t.pt && \
  wget https://github.com/meituan/YOLOv6/releases/download/0.2.0/yolov6s.pt && \
  wget https://github.com/meituan/YOLOv6/releases/download/0.2.0/yolov6m.pt && \
  wget https://github.com/meituan/YOLOv6/releases/download/0.2.0/yolov6l.pt


######################################################################
# DEMO:YoloV7 from https://github.com/WongKinYiu/yolov7
#####################################################################
FROM vaisw_base AS yolov7

RUN umask 0 && \
  git clone https://github.com/WongKinYiu/yolov7 && \
  cd yolov7 && \
  git checkout -b mipso b1850c7dcafc3cdb4fdf002a902d55ea10db481e

RUN umask 0 && \
  cd yolov7 && \
  wget https://github.com/WongKinYiu/yolov7/releases/download/v0.1/yolov7.pt && \
  wget https://github.com/WongKinYiu/yolov7/releases/download/v0.1/yolov7x.pt && \
  wget https://github.com/WongKinYiu/yolov7/releases/download/v0.1/yolov7-w6.pt && \
  wget https://github.com/WongKinYiu/yolov7/releases/download/v0.1/yolov7-e6.pt && \
  wget https://github.com/WongKinYiu/yolov7/releases/download/v0.1/yolov7-d6.pt && \
  wget https://github.com/WongKinYiu/yolov7/releases/download/v0.1/yolov7-e6e.pt


######################################################################
# DEMO:YoloV7 segmentation from https://github.com/WongKinYiu/yolov7
#####################################################################
FROM vaisw_base AS yolov7.seg

RUN umask 0 && \
  git clone https://github.com/WongKinYiu/yolov7 yolov7.seg && \
  cd yolov7.seg && \
  git checkout -b mipso 44f30af0daccb1a3baecc5d80eae22948516c579

RUN umask 0 && \
  cd yolov7.seg && \
  cd seg && \
  wget https://github.com/WongKinYiu/yolov7/releases/download/v0.1/yolov7-seg.pt


######################################################################
# DEMO:ALL all demo
######################################################################
FROM vaisw_base AS all_common

# now copy all examples

COPY --from=darkflow            /home/demo/ /home/demo/
#COPY --from=tensorflow_yolov3   /home/demo/ /home/demo/
#COPY --from=ildoonet-tf-pose-estimation  /home/demo/ /home/demo/
COPY --from=edsr_pytorch        /home/demo/ /home/demo/
#COPY --from=mask_rcnn           /home/demo/ /home/demo/
COPY --from=srgan_pytorch       /home/demo/ /home/demo/
#COPY --from=tensorflow_yolov4_tflite /home/demo  /home/demo/
COPY --from=yolov5              /home/demo/ /home/demo/
COPY --from=pytorch-ssd         /home/demo/ /home/demo/
#COPY --from=efficientdet        /home/demo/ /home/demo/
COPY --from=yolox               /home/demo/ /home/demo/
COPY --from=refinedet.pytorch   /home/demo/ /home/demo/
COPY --from=yolov6              /home/demo/ /home/demo/
COPY --from=yolov7              /home/demo/ /home/demo/
COPY --from=yolov7.seg          /home/demo/ /home/demo/

RUN rm -rf /DOWNLOAD

RUN apt-get update \
 && apt-get install -y sudo


# all commands above are inside the saved image
FROM all_common AS all
# all commands below are executed on top of the saved image (or the usual build)

# Hack to correct pafy (https://github.com/mps-youtube/pafy/pull/288#issuecomment-812841914)
RUN umask 0 && \
  sed -i "s/^\(.*self._likes = \).*/\1self._ydl_info.get('like_count',0)/g" ${BASE_VENV}/lib/python3.*/site-packages/pafy/backend_youtube_dl.py && \
  sed -i "s/^\(.*self._dislikes = \).*/\1self._ydl_info.get('dislike_count',0)/g" ${BASE_VENV}/lib/python3.*/site-packages/pafy/backend_youtube_dl.py

# Applying patches. Commented line means patch was applied in the demo
#COPY darkflow.patch                     /tmp/darkflow.patch
#COPY tensorflow-yolo3.patch             /tmp/tensorflow-yolo3.patch
#COPY ildoonet-tf-pose-estimation.patch  /tmp/ildoonet-tf-pose-estimation.patch
COPY EDSR-PyTorch.patch                 /tmp/EDSR-PyTorch.patch
#COPY Mask_RCNN.patch                    /tmp/Mask_RCNN.patch
COPY SRGAN-PyTorch.patch                /tmp/SRGAN-PyTorch.patch
#COPY tensorflow-yolov4-tflite.patch     /tmp/tensorflow-yolov4-tflite.patch
COPY yolov5.patch                       /tmp/yolov5.patch
COPY pytorch-ssd.patch                  /tmp/pytorch-ssd.patch
#COPY EfficientDet.patch                 /tmp/EfficientDet.patch
COPY YOLOX.patch                        /tmp/YOLOX.patch
COPY refinedet.pytorch.patch            /tmp/refinedet.pytorch.patch
COPY YOLOv6.patch                       /tmp/YOLOv6.patch
COPY yolov7.patch                       /tmp/yolov7.patch
COPY yolov7.seg.patch                   /tmp/yolov7.seg.patch

RUN umask 0 && find /tmp -name '*.patch' -exec bash -c 'patch -p1 -d /home/demo/$( basename {} .patch) -i {}' \;

COPY mipso_custom.py /tmp/mipso_custom.py

RUN cd /home/demo ; find -path ./vaisw/examples/docker -prune -o -name mipso_custom.py -exec cp /tmp/mipso_custom.py {} \;

# Hack to fix python packages (to make them compliant with python3.12+)
COPY python_env.patch /tmp/python_env.patch
RUN umask 0 && patch -p0 -d /usr/base_venv/lib/python3.12/site-packages/ -i /tmp/python_env.patch

# set custom user config

# RUN umask 0 && \
#   cd tensorflow-yolo3 && \
#   /tmp/vaisw_config_add.bash "debug.forceModelConversion=true" > vaisw.ini

# RUN umask 0 && \
#   cd ildoonet-tf-pose-estimation && \
#   /tmp/vaisw_config_add.bash "quantization.minimalBatchSize=1" > vaisw.ini

RUN umask 0 && \
  cd EDSR-PyTorch && \
  cd src && \
  /tmp/vaisw_config_add.bash "quantization.minimalBatchSize=1" \
                             "runOptimization.downloadNbThreads=4" \
                             "runOptimization.uploadNbThreads=4" > vaisw.ini

# RUN umask 0 && \
#   cd Mask_RCNN && \
#   /tmp/vaisw_config_add.bash "debug.forceModelConversion=true" \
#                              "quantization.minimalBatchSize=1" > vaisw.ini

RUN umask 0 && \
  cd SRGAN-PyTorch && \
  /tmp/vaisw_config_add.bash "quantization.minimalBatchSize=1" > vaisw.ini

# RUN umask 0 && \
#   cd tensorflow-yolov4-tflite && \
#   /tmp/vaisw_config_add.bash "quantization.minimalBatchSize=1" \
#                              "debug.forceModelConversion=true" \
#                              "quantization.ignoreNegativeValuesOnLastLayer=false" > vaisw.ini

RUN umask 0 && \
  cd yolov5 && \
  /tmp/vaisw_config_add.bash "quantization.minimalBatchSize=1" \
                             "fe.precision=MIXED" > vaisw.ini

RUN umask 0 && \
  cd pytorch-ssd && \
  /tmp/vaisw_config_add.bash "quantization.minimalBatchSize=1" > vaisw.ini

# RUN umask 0 && \
#   cd EfficientDet && \
#   /tmp/vaisw_config_add.bash "quantization.ignoreNegativeValuesOnLastLayer=false" \
#                              "runSession.subGraphs=auto" > vaisw.ini

RUN umask 0 && \
  cd YOLOX && \
  /tmp/vaisw_config_add.bash "quantization.minimalBatchSize=1" \
                             "fe.precision=MIXED" > vaisw.ini

RUN umask 0 && \
  cd refinedet.pytorch && \
  /tmp/vaisw_config_add.bash "quantization.minimalBatchSize=1" \
                             "runOptimization.depthToSpaceMode=MEMTILE" > vaisw.ini

RUN umask 0 && \
  cd YOLOv6 && \
  /tmp/vaisw_config_add.bash "quantization.minimalBatchSize=1" > vaisw.ini

RUN umask 0 && \
  cd yolov7 && \
  /tmp/vaisw_config_add.bash "quantization.minimalBatchSize=1" \
                             "fe.precision=MIXED" > vaisw.ini

RUN umask 0 && \
  cd yolov7.seg && \
  cd seg && \
  /tmp/vaisw_config_add.bash "quantization.minimalBatchSize=1" > vaisw.ini

RUN umask 0 && \
  cd darkflow && \
  /tmp/vaisw_config_add.bash "debug.ccontiguous=true" > vaisw.ini

# set python encoding
ENV PYTHONIOENCODING="utf-8"

ENV MIPSO_DOCKER=1

# create local user for outside access (X and volumes)

RUN deluser ubuntu

ARG UID=1000
ARG USERNAME=demo
ARG USERHOME=/home/demo

RUN useradd $USERNAME -l -u $UID -d $USERHOME && \
  mkdir -p $USERHOME && \
  chown $USERNAME:$USERNAME $USERHOME && \
  echo "$USERNAME:demo" | chpasswd

RUN adduser $USERNAME sudo && \
  echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

USER $USERNAME

# Initialize work venv
ENV WORK_VENV=$USERHOME/work_venv
RUN python3 -m venv ${WORK_VENV}
# Add CUDA by default
# Needed because somehow bashrc is sourced twice, and the 1st time without args like AMD_GPU
RUN { realpath ${CUDA_VENV}/lib/python3.12/site-packages ; \
      echo "#EXT_VENV/lib/python3.12/site-packages"      ; \
      realpath ${BASE_VENV}/lib/python3.12/site-packages ; \
      } > ${WORK_VENV}/lib/python3.12/site-packages/base_venv.pth

RUN { echo 'if [ "$EXT_VENV" != "" ]; then'                                                                                         ; \
      echo '  deps=$( find $EXT_VENV/lib -maxdepth 3 -name base_venv.pth 2> /dev/null )'                                            ; \
      echo '  if [[ "$deps" != "" ]]; then'                                                                                         ; \
      echo '    tac $deps | while read -r vd; do'                                                                                   ; \
      echo '      sed -i "/#EXT_VENV/a $vd" ${WORK_VENV}/lib/python3.12/site-packages/base_venv.pth'                                ; \
      echo '    done'                                                                                                               ; \
      echo '  fi'                                                                                                                   ; \
      echo '  sed -i "s|#EXT_VENV|${EXT_VENV}|" ${WORK_VENV}/lib/python3.12/site-packages/base_venv.pth'                            ; \
      echo 'fi'                                                                                                                     ; \
      echo '[ "$AMD_GPU"  != "" ] && sed -i "s|${CUDA_VENV}|${ROCM_VENV}|" ${WORK_VENV}/lib/python3.12/site-packages/base_venv.pth' ; \
      echo 'source ${WORK_VENV}/bin/activate'                                                                                       ; \
      echo '[ -d /opt/rocm ] && export PATH=/opt/rocm/bin:$PATH'                                                                    ; \
      echo 'cat /tmp/vitis_banner.txt'                                                                                              ; \
      echo 'export PS1="vitis-ai-user@\h:\w\$ "'                                                                                    ; \
      echo 'unset BASH_ENV'                                                                                                         ; \
      } > $USERHOME/.bashrc

COPY vitis_banner.txt /tmp/vitis_banner.txt

ENV BASH_ENV=$USERHOME/.bashrc
