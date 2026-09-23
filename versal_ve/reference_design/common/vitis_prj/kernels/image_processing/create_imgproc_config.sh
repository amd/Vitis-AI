#!/bin/bash

#
# Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

IMAGE_PROCESSING_CONFIG=$1
echo -n "Creating image_processing.cfg... "

# Converts HAS_* #define lines (from stdin) to VVAS_VIDEO_FORMAT_* names
convert_format_names() {
    sed -e 's:\s\+1.*::' -e 's:^#define ::' \
        -e 's:HAS_Y_UV8_Y_UV8_420:VVAS_VIDEO_FORMAT_Y_UV8_420:' \
        -e 's:HAS_RGB8_YUV8:VVAS_VIDEO_FORMAT_RGB:' \
        -e 's:HAS_BGR8:VVAS_VIDEO_FORMAT_BGR:' \
        -e 's:HAS_Y_UV10_Y_UV10_420:VVAS_VIDEO_FORMAT_NV12_10LE32:' \
        -e 's:HAS_Y8:VVAS_VIDEO_FORMAT_GRAY8:' \
        -e 's:HAS_Y10:VVAS_VIDEO_FORMAT_GRAY10_LE32:' \
        -e 's:HAS_Y_U_V8_420:VVAS_VIDEO_FORMAT_I420:' \
        -e 's:HAS_RGBX8_YUVX8:VVAS_VIDEO_FORMAT_RGBx:' \
        -e 's:HAS_BGRX8:VVAS_VIDEO_FORMAT_BGRx:' \
        -e 's:HAS_RGB_YUV_FP32:VVAS_VIDEO_FORMAT_RGB_FLOAT:' \
        -e 's:HAS_BGR_FP32:VVAS_VIDEO_FORMAT_BGR_FLOAT:' \
        -e 's:HAS_R_G_B8:VVAS_VIDEO_FORMAT_RGBP:' \
        -e 's:HAS_R_G_B_FP32:VVAS_VIDEO_FORMAT_RGBP_FLOAT:' \
        -e 's:HAS_Y_FP32:VVAS_VIDEO_FORMAT_GRAY32_FLOAT:' \
        -e 's:HAS_Y_BF16:VVAS_VIDEO_FORMAT_GRAY_BF16:' \
        -e 's:HAS_RGBX_YUVX_BF16:VVAS_VIDEO_FORMAT_RGBx_BF16:' \
        -e 's:HAS_BGRX_BF16:VVAS_VIDEO_FORMAT_BGRx_BF16:' \
        -e 's:HAS_Y_FP16:VVAS_VIDEO_FORMAT_GRAY_FP16:' \
        -e 's:HAS_RGBX_FP16:VVAS_VIDEO_FORMAT_RGBx_FP16:' \
        -e 's:HAS_BGRX_FP16:VVAS_VIDEO_FORMAT_BGRx_FP16:' \
        -e 's:HAS_RGB_FP16:VVAS_VIDEO_FORMAT_RGB_FP16:' \
        -e 's:HAS_BGR_FP16:VVAS_VIDEO_FORMAT_BGR_FP16:' \
        -e 's:HAS_RGB_BF16:VVAS_VIDEO_FORMAT_RGB_BF16:' \
        -e 's:HAS_BGR_BF16:VVAS_VIDEO_FORMAT_BGR_BF16:' \
        -e 's:HAS_R_G_B_FP16:VVAS_VIDEO_FORMAT_RGBP_FP16:' \
        -e 's:HAS_R_G_B_BF16:VVAS_VIDEO_FORMAT_RGBP_BF16:' \
        -e 's:HAS_B_G_R8:VVAS_VIDEO_FORMAT_BGRP:' \
        -e 's:HAS_B_G_R_FP16:VVAS_VIDEO_FORMAT_BGRP_FP16:' \
        -e 's:HAS_B_G_R_BF16:VVAS_VIDEO_FORMAT_BGRP_BF16:' \
        -e 's:HAS_B_G_R_FP32:VVAS_VIDEO_FORMAT_BGRP_FLOAT:'
}

# Check if a HAS_* define is set to 1 in the INPUT_FORMATS section
is_int8_enabled() {
    sed -n '/INPUT_FORMATS_BEGIN/,/INPUT_FORMATS_END/p' "$IMAGE_PROCESSING_CONFIG" \
        | grep -q "#define $1\s\+1"
}

# Check if a HAS_* define is set to 1 in the OUTPUT_ONLY_FORMATS section
is_float_enabled() {
    sed -n '/OUTPUT_ONLY_FORMATS_BEGIN/,/OUTPUT_ONLY_FORMATS_END/p' "$IMAGE_PROCESSING_CONFIG" \
        | grep -q "#define $1\s\+1"
}

# Validate that each enabled float/BF16/FP16 output format has its
# required INT8 format enabled. Dependency mapping derived from the
# float-to-INT8 switch in image_processing.cpp.
validate_dependencies() {
    local err=0
    local float_fmt int8_fmt

    while read float_fmt int8_fmt; do
        if is_float_enabled "$float_fmt" && ! is_int8_enabled "$int8_fmt"; then
            echo ""
            echo "Error: $float_fmt=1 requires $int8_fmt=1"
            err=1
        fi
    done <<EOF
HAS_RGB_YUV_FP32 HAS_RGB8_YUV8
HAS_RGB_BF16 HAS_RGB8_YUV8
HAS_RGB_FP16 HAS_RGB8_YUV8
HAS_BGR_FP32 HAS_BGR8
HAS_BGR_BF16 HAS_BGR8
HAS_BGR_FP16 HAS_BGR8
HAS_R_G_B_FP32 HAS_R_G_B8
HAS_R_G_B_BF16 HAS_R_G_B8
HAS_R_G_B_FP16 HAS_R_G_B8
HAS_B_G_R_BF16 HAS_B_G_R8
HAS_B_G_R_FP16 HAS_B_G_R8
HAS_B_G_R_FP32 HAS_B_G_R8
HAS_Y_FP32 HAS_Y8
HAS_Y_BF16 HAS_Y8
HAS_Y_FP16 HAS_Y8
HAS_RGBX_YUVX_BF16 HAS_RGBX8_YUVX8
HAS_RGBX_FP16 HAS_RGBX8_YUVX8
HAS_BGRX_BF16 HAS_BGRX8
HAS_BGRX_FP16 HAS_BGRX8
EOF

    return $err
}

# Run dependency validation before generating the config
if ! validate_dependencies; then
    echo ""
    echo "Fix the above errors in $IMAGE_PROCESSING_CONFIG and retry."
    exit 1
fi

# INT8 formats (between INPUT_FORMATS markers) -- valid for both input and output
INT8_FORMATS=$(sed -n '/INPUT_FORMATS_BEGIN/,/INPUT_FORMATS_END/p' \
    "$IMAGE_PROCESSING_CONFIG" | grep "#define HAS.*\s\+1" | convert_format_names)

# Float formats (between OUTPUT_ONLY_FORMATS markers) -- output-only
OUTPUT_ONLY_FORMATS=$(sed -n '/OUTPUT_ONLY_FORMATS_BEGIN/,/OUTPUT_ONLY_FORMATS_END/p' \
    "$IMAGE_PROCESSING_CONFIG" | grep "#define HAS.*\s\+1" | convert_format_names)

# Input: only INT8 formats
echo "[input-color-formats]" > image_processing.cfg
echo "$INT8_FORMATS" >> image_processing.cfg

# Output: INT8 + output-only formats
echo "" >> image_processing.cfg
echo "[output-color-formats]" >> image_processing.cfg
echo "$INT8_FORMATS" >> image_processing.cfg
if [ -n "$OUTPUT_ONLY_FORMATS" ]; then
    echo "$OUTPUT_ONLY_FORMATS" >> image_processing.cfg
fi

echo "" >> image_processing.cfg
echo "[resolution]" >> image_processing.cfg
WIDTH=$(grep "#define HSC_MAX_WIDTH*\s" $IMAGE_PROCESSING_CONFIG | grep -o -E '[0-9]+')
HEIGHT=$(grep "#define HSC_MAX_HEIGHT*\s" $IMAGE_PROCESSING_CONFIG | grep -o -E '[0-9]+')
echo "min-width: 16"  >> image_processing.cfg
echo "min-height: 16"  >> image_processing.cfg
echo "max-width: $WIDTH"  >> image_processing.cfg
echo "max-height: $HEIGHT" >> image_processing.cfg

echo "" >> image_processing.cfg
echo "[kernel-config]" >> image_processing.cfg

BILINEAR_SCALEMODE=$(grep "#define HSC_BILINEAR*\s" $IMAGE_PROCESSING_CONFIG | awk '{print $3}')
BICUBIC_SCALEMODE=$(grep "#define HSC_BICUBIC*\s" $IMAGE_PROCESSING_CONFIG | awk '{print $3}')
POLYPHASE_SCALEMODE=$(grep "#define HSC_POLYPHASE*\s" $IMAGE_PROCESSING_CONFIG | awk '{print $3}')

SCALER_MODE=$(grep "#define HSC_SCALE_MODE*\s" $IMAGE_PROCESSING_CONFIG | awk '{print $3}')

if [ $SCALER_MODE == $BILINEAR_SCALEMODE ]
then
    SCALE_MODE_STRING="HSC_BILINEAR"
elif [ $SCALER_MODE == $BICUBIC_SCALEMODE ]
then
    SCALE_MODE_STRING="HSC_BICUBIC"
elif [ $SCALER_MODE == $POLYPHASE_SCALEMODE ]
then
    SCALE_MODE_STRING="HSC_POLYPHASE"
else
    echo "Error: Unknown Scaling Mode"
    exit
fi

NUM_FILTER_TAPS=$(grep -A 1 "HSC_SCALE_MODE==$SCALE_MODE_STRING" $IMAGE_PROCESSING_CONFIG | grep "#define HSC_TAPS*\s" | awk '{print $3}')

PPC=$(grep "#define HSC_SAMPLES_PER_CLOCK*\s" $IMAGE_PROCESSING_CONFIG | awk '{print $3}')

echo "pixel-per-clock: $PPC" >> image_processing.cfg
echo "scale-mode: $SCALER_MODE" >> image_processing.cfg
echo "filter-taps: $NUM_FILTER_TAPS" >> image_processing.cfg

echo Done!
