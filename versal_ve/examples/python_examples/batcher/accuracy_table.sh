#! /bin/bash

# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

n_list="caffenet_no_lrn googlenet_no_lrn inceptionv2 inceptionv3 inceptionv4 resnet50 resnet152 vgg16 vgg19 mobilenet_v1 mobilenet_v2 yolov1 yolov2 yolov3"
# n_list for pytorch framework
n_list="alexnet googlenet_no_lrn resnet18 resnet34 resnet50 resnet101 resnet152 inceptionv3 mobilenet_v2 vgg11 vgg11_bn vgg13 vgg13_bn vgg16 vgg16_bn vgg19 vgg19_bn densenet121 densenet161 densenet169 densenet201 resnext50_32x4d resnext101_32x8d wide_resnet50_2 wide_resnet101_2 squeezenet squeezenet1_1"

if [ "$1" = "-n" ]
then
  shift
  n_list=${1//:/ }
  shift
fi
echo "running network $n_list"

nbImages=$1
shift
[ "$nbImages" = "" ] && nbImages=3000

npu_dir=output/npu.$nbImages
cpu_dir=output/cpu.$nbImages

run()
{
  if [ "$VAISW_INSTALL_DIR" != "" ] && which vaisw_tools &> /dev/null && vaisw_tools --config &> /dev/null
  then
    dir=$npu_dir
  else
    dir=$cpu_dir
  fi

  mkdir -p $dir

  for n in $n_list
  do
    [ -f "$dir/$n.log" ] && continue
    echo "@ $( date ): running $n on $nbImages"
    ./run_classification.sh --nbImages $nbImages --predictFile $dir/$n.pred -n $n "$@" &> $dir/$n.log
    # to run on full imagenet dataset: please, adapt the following command line
    #python ./label_image.py --imgPath datasets/imagenet/ILSVRC2012_img_val --nbImages $nbImages --goldFile links/ILSVRC_2012_val_GroundTruth_10p.gold --batchSize 50 $n &> $dir/$n.log
  done
  echo
}


[ "$VAISW_INSTALL_DIR" = "" ] && echo "ERROR: please source settings.sh" && exit 1

# run with acceleration
run "$@"
. $VAISW_INSTALL_DIR/unset.sh
# run on CPU
run "$@"


echo "Accuracy table"

printf "  %20s |      NPUIP      |    CPU FT32     |   NPUIP - CPU   |\n" ""
printf "| %20s | %6s | %6s | %6s | %6s | %6s | %6s |\n" "network" "top 1" "top 5" "top 1" "top 5" "top 1" "top 5"
printf "|-%20s-|-%6s-|-%6s-|-%6s-|-%6s-|-%6s-|-%6s-|\n" "--------------------" "------" "------" "------" "------" "------" "------"

for n in $n_list
do
  if [ -f "$npu_dir/$n.log" ]
  then
    npu_top5=$( grep "TEST top5]\|TEST top1_box]" $npu_dir/$n.log | sed -e 's/%.*//' -e 's/.* //' )
    npu_top1=$( grep "TEST top1]\|TEST mAP-50]" $npu_dir/$n.log | sed -e 's/%.*//' -e 's/.* //' )
  else
    npu_top5=""
    npu_top1=""
  fi
  if [ -f "$cpu_dir/$n.log" ]
  then
    cpu_top5=$( grep "TEST top5]\|TEST top1_box]" $cpu_dir/$n.log | sed -e 's/%.*//' -e 's/.* //' )
    cpu_top1=$( grep "TEST top1]\|TEST mAP-50]" $cpu_dir/$n.log | sed -e 's/%.*//' -e 's/.* //' )
  else
    cpu_top5=""
    cpu_top1=""
  fi

  [ "$npu_top5" = "" ] && [ "$cpu_top5" = "" ] && continue

  diff_top1=""
  diff_top5=""
  [ "$npu_top5" != "" ] && [ "$cpu_top5" != "" ] && diff_top5=$( echo "$npu_top5 - $cpu_top5" | bc )
  [ "$npu_top1" != "" ] && [ "$cpu_top1" != "" ] && diff_top1=$( echo "$npu_top1 - $cpu_top1" | bc )
  printf "| %20s | %6s | %6s | %6s | %6s | %6s | %6s |\n" "$n" "$npu_top1" "$npu_top5" "$cpu_top1" "$cpu_top5" "$diff_top1" "$diff_top5"
done

