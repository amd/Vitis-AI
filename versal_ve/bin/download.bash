#! /bin/bash

# Copyright (C) 2024 Advanced Micro Devices, Inc.

# this script will download from xilinx.com all the necessary materials
# first argument is the name of the materials to download


src=$1

vitis_top=$(dirname $( dirname $( realpath "${BASH_SOURCE[0]}" ) ) )

declare -A deliverables=(
[vitis_ai_V6.3_VAI_NPU_SW.tgz]="                                47d8352109442c2e555e50e23aa7037f https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VAI_NPU_SW.tgz"

[vitis_ai_V6.3_NPU_TAIL_YOLO_V5.tgz]="                          a68da33f51e54eeb9a299f6967ed5a79 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_NPU_TAIL_YOLO_V5.tgz"
[vitis_ai_V6.3_NPU_TAIL_YOLO_V7.tgz]="                          9229caa95ab9c8f9dac0ebd5ace10ad4 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_NPU_TAIL_YOLO_V7.tgz"
[vitis_ai_V6.3_NPU_TAIL_YOLO_V8.tgz]="                          775156de2c2edb17ad97aac64c80dbb6 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_NPU_TAIL_YOLO_V8.tgz"
[vitis_ai_V6.3_NPU_TAIL_YOLO_X.tgz]="                           bf38517cef783a94ae0d32a1dcb7655d https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_NPU_TAIL_YOLO_X.tgz"

[vitis_ai_V6.3_VC1902_NPU_IP_1103.tgz]="                        df4ddc7fb896ba6df79926cdf655d488 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VC1902_NPU_IP_1103.tgz"
[vitis_ai_V6.3_VC1902_NPU_IP_1106.tgz]="                        eb370a84231eddbe61e36be79db029d5 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VC1902_NPU_IP_1106.tgz"
[vitis_ai_V6.3_VC1902_NPU_IP_1109.tgz]="                        3a151f03158a7f2aa9e1f8012ae44ab3 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VC1902_NPU_IP_1109.tgz"
[vitis_ai_V6.3_VC1902_NPU_IP_3109.tgz]="                        ebb78125882ee98a261459096d6fffa3 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VC1902_NPU_IP_3109.tgz"
[vitis_ai_V6.3_VC1902_NPU_IP_3206.tgz]="                        e20404c1cf480707a11818b7a8ffa8a1 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VC1902_NPU_IP_3206.tgz"
[vitis_ai_V6.3_VC1902_NPU_IP_3208.tgz]="                        5a1bbf7c8edf4a4a7137fcbb100e5380 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VC1902_NPU_IP_3208.tgz"
[vitis_ai_V6.3_VC1902_NPU_IP_3305.tgz]="                        424cfe5b12008f6adbd7ec9a67d4d14a https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VC1902_NPU_IP_3305.tgz"


[vitis_ai_V6.3_VR1602_NPU_IP_1103.tgz]="                        bac3c895074d704e9739ede4802a8f70 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VR1602_NPU_IP_1103.tgz"
[vitis_ai_V6.3_VR1602_NPU_IP_1106.tgz]="                        79d715780854d99c2693a5c22a9e7f00 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VR1602_NPU_IP_1106.tgz"
[vitis_ai_V6.3_VR1602_NPU_IP_1109.tgz]="                        60d3457244bd324415519bf914069314 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VR1602_NPU_IP_1109.tgz"
[vitis_ai_V6.3_VR1602_NPU_IP_1303.tgz]="                        9dcc1d31e4ba5b7eac1fcda5ab3ba033 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VR1602_NPU_IP_1303.tgz"
[vitis_ai_V6.3_VR1602_NPU_IP_3105.tgz]="                        9b5137319ed785d1ed8ecdf5ab903eb0 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VR1602_NPU_IP_3105.tgz"

[vitis_ai_V6.3_VE2202_NPU_IP_O05_A024_M1.tgz]="                 920416b5bf743b8959be241240d4f348 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VE2202_NPU_IP_O05_A024_M1.tgz"
[vitis_ai_V6.3_VE2302_NPU_IP_O00_A016_M1.tgz]="                 34801443dbc7899be1f96dda6047334d https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VE2302_NPU_IP_O00_A016_M1.tgz"
[vitis_ai_V6.3_VE2302_NPU_IP_O00_A032_M1.tgz]="                 ee6258e3e2e5c77c62573c388451a046 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VE2302_NPU_IP_O00_A032_M1.tgz"
[vitis_ai_V6.3_VE2602_NPU_IP_O00_A064_M1.tgz]="                 7cbe9046798c8d16d8d8dcf74150fa10 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VE2602_NPU_IP_O00_A064_M1.tgz"

[vitis_ai_V6.3_VE2802_NPU_IP_O00_A128_M1.tgz]="                 4b2d9e26952aa98ec73656aca55cffc9 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VE2802_NPU_IP_O00_A128_M1.tgz"
[vitis_ai_V6.3_VE2802_NPU_IP_O00_A128_M3.tgz]="                 b2d8fe69d847db7d1a2b52d2b5b804f4 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VE2802_NPU_IP_O00_A128_M3.tgz"
[vitis_ai_V6.3_VE2802_NPU_IP_O00_A160_M3.tgz]="                 83a0ab2178f2de3f41d380245def9d1c https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VE2802_NPU_IP_O00_A160_M3.tgz"
[vitis_ai_V6.3_VE2802_NPU_IP_O00_A192_M3.tgz]="                 b8c19a048932679519b449ad07dad612 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VE2802_NPU_IP_O00_A192_M3.tgz"
[vitis_ai_V6.3_VE2802_NPU_IP_O00_A304_M1.tgz]="                 945e9ad905e2d0f8e2a1a9d50e7e9898 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VE2802_NPU_IP_O00_A304_M1.tgz"
[vitis_ai_V6.3_VE2802_NPU_IP_O00_A304_M3.tgz]="                 80ad65a4505cd79bc7ddd1c132431ddb https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VE2802_NPU_IP_O00_A304_M3.tgz"
[vitis_ai_V6.3_VE2802_NPU_IP_O00_A304_M3_generic.tgz]="         545f4050cc97663ebc297b04126d4936 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VE2802_NPU_IP_O00_A304_M3_generic.tgz"
[vitis_ai_V6.3_VE2802_NPU_IP_O16_A080_M3.tgz]="                 bc68f253907dc3da0a987b44724169e1 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VE2802_NPU_IP_O16_A080_M3.tgz"
[vitis_ai_V6.3_VE2802_NPU_IP_O16_A128_M3.tgz]="                 b12d0cb731dcbe468dbec45c2b4d6c0d https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VE2802_NPU_IP_O16_A128_M3.tgz"
[vitis_ai_V6.3_VE2802_NPU_IP_O20_A048_M3.tgz]="                 5c8a8e2efb4fa2b34a78e1bd0ab6a50b https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VE2802_NPU_IP_O20_A048_M3.tgz"
[vitis_ai_V6.3_VE2802_NPU_IP_O24_A064_M1.tgz]="                 0748495ceadf635a8ab3efad974142e6 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VE2802_NPU_IP_O24_A064_M1.tgz"
[vitis_ai_V6.3_VE2802_NPU_IP_O24_A064_M3.tgz]="                 f0c0ce3215cecc0854ac316c5ce84d79 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VE2802_NPU_IP_O24_A064_M3.tgz"
[vitis_ai_V6.3_VE2802_NPU_IP_O24_A112_M3.tgz]="                 5dc2fbda686a95ebaeae2a7498a77728 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_VE2802_NPU_IP_O24_A112_M3.tgz"

[vitis_ai_V6.3_ZU_NPU_IP_1101_BRAM.tgz]="                       64b1e767a2c03e26c036827f96453a5e https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_ZU_NPU_IP_1101_BRAM.tgz"
[vitis_ai_V6.3_ZU_NPU_IP_1101_URAM.tgz]="                       1bb21437bc354bdbd60e021dc379c1e4 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_ZU_NPU_IP_1101_URAM.tgz"
[vitis_ai_V6.3_ZU_NPU_IP_1102_SMALL.tgz]="                      7b7ffa1e682716698e7f055db6b72d26 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_ZU_NPU_IP_1102_SMALL.tgz"
[vitis_ai_V6.3_ZU_NPU_IP_1102.tgz]="                            af6ec75bd1bdfe6c20c10d15990c729d https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_ZU_NPU_IP_1102.tgz"
[vitis_ai_V6.3_ZU_NPU_IP_1103.tgz]="                            7a643488827d59ea6b921ba66cbee5a8 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_ZU_NPU_IP_1103.tgz"
[vitis_ai_V6.3_ZU_NPU_IP_1104.tgz]="                            023a133684cf2970eb014377fb11eb63 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_ZU_NPU_IP_1104.tgz"
[vitis_ai_V6.3_ZU_NPU_IP_1106.tgz]="                            e85148d95bd3a12337f8035575ed0402 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_ZU_NPU_IP_1106.tgz"
[vitis_ai_V6.3_ZU_NPU_IP_1108.tgz]="                            fa6d6b6094938807a5a4c22f2039aec5 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_ZU_NPU_IP_1108.tgz"
[vitis_ai_V6.3_ZU_NPU_IP_1109.tgz]="                            536464764c39d451bb4e279a248b123f https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_ZU_NPU_IP_1109.tgz"
[vitis_ai_V6.3_ZU_NPU_IP_1112.tgz]="                            a4b4a7300c56cefadd7bd017a2ad84b2 https://download.amd.com/opendownload/VitisAI/6.3/vitis_ai_V6.3_ZU_NPU_IP_1112.tgz"

[docker_vai_npu_image_V5.1_u2404.7bd5c332e6ea.tgz]="            edc1d16d12f752f86ef63891735e76df xcorsync02:/proj/zebra/common/releases/docker_images/docker_vai_npu_image_V5.1_u2404.7bd5c332e6ea.tgz"


[aianalyzer-1.7.0.-py3-none-any.whl]="                          f148b89e6321a1416d006a9674f79524 https://download.amd.com/opendownload/VitisAI/6.2/aianalyzer-1.7.0.dev20260130181427+g301504b8-py3-none-any.whl"

[vitis_ai_library_r1.4.0_images.tar.gz]="                       b49daf9498431ce2016920b97f6e9b82 https://www.xilinx.com/bin/public/openDownload?filename=vitis_ai_library_r1.4.0_images.tar.gz"
[tf_mlperf_ssdresnet34_3.5.zip]="                               06c1777b3092062f4ff2e56b2418c2db https://www.xilinx.com/bin/public/openDownload?filename=tf_mlperf_ssdresnet34_3.5.zip"
)
get_hash() { echo $1 ; }
get_link() { echo $2 ; }

[ -f $vitis_top/bin/extra_download.bash ] && . $vitis_top/bin/extra_download.bash

links=$( get_link ${deliverables[$src]} )
md5sum=$( get_hash ${deliverables[$src]} )

if [ "$links" = "" ]
then
  echo "ERROR $src package is not found in the list of deliverables." >&2
  exit 1
fi

if [ "${links:0:4}" = "http" ]
then
  [ "$2" = "-n" ] && echo "$links" && exit 0

  if ! wget -O $src "$links"
  then
    echo "ERROR downloading $src file from $links fails" >&2 && exit 1
  fi
else
  if [ "$USE_DOWNLOAD_DIR" != "" ]
  then
    links=$USE_DOWNLOAD_DIR/$( basename $links )
  fi
  [ "$2" = "-n" ] && echo "$links" && exit 0

  if [ "$2" = "--symlink" ]
  then
    local_link=${links/*:}
    [ -e "$local_link" ] && ln -fs $local_link $src && exit 0
  fi

  if ! rsync -LP "$links" ./$src
  then
    echo "ERROR getting $src file from $links fails" >&2 && exit 1
  fi
fi

[ "$md5sum" = "AUTO" ] && echo "OK: download file from $links" && exit 0

cur_md5sum=$( md5sum "$src" | awk ' { print $1 } ' )
if [ "$md5sum" != "$cur_md5sum" ]
then
  echo "ERROR: download file has wrong md5sum, removing it." >&2
  rm -f "$src"
  exit 1
fi

echo "OK: downloaded file has the correct md5sum: $md5sum"
exit 0
