/*
 * Copyright (C) 2023-2025 Advanced Micro Devices, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* This is Sample file for Csim and cosim */
#ifndef _IMAGE_PROCESSING_CONFIG_H_
#define _IMAGE_PROCESSING_CONFIG_H_

/* Constant parameters defined for this IP */
#define HSC_BILINEAR                0
#define HSC_BICUBIC                 1
#define HSC_POLYPHASE               2
#define	AXIMM_INTERFACE             1
#define	AXI_STREAM_INTERFACE        0

#define CPW                         3
#define OPMODE                      1
#define NORMALIZATION               1

/* Below parameters are coming from user selection */
#define HSC_SAMPLES_PER_CLOCK       4 // 1, 2, 4
#define HSC_MAX_WIDTH               3840           // Determines BRAM usage
#define HSC_MAX_HEIGHT              2160           // No impact on resources
#define HSC_BITS_PER_COMPONENT      8     // 8, 10
#define HSC_SCALE_MODE              0         // 0 - Bilinear 1 - Bicubic  2 - Polyphase
#if (HSC_SCALE_MODE==HSC_BILINEAR)
#define HSC_TAPS                    2               // Fixed to 2
#elif (HSC_SCALE_MODE==HSC_BICUBIC)
#define HSC_TAPS                    4               // Fixed to 4
#elif(HSC_SCALE_MODE==HSC_POLYPHASE)
#define HSC_TAPS                    6   // 6, 8, 10, 12
#endif

#define MAX_OUTS                    1

/* INT8 Formats -- used for both input and output */
/* INPUT_FORMATS_BEGIN */
#define HAS_RGBX8_YUVX8         1
#define HAS_YUYV8               0
#define HAS_RGBA8_YUVA8         0
#define HAS_RGBX10_YUVX10       0
#define HAS_Y_UV8_Y_UV8_420     1
#define HAS_RGB8_YUV8           1
#define HAS_Y_UV10_Y_UV10_420   0
#define HAS_Y8                  1
#define HAS_Y10                 0
#define HAS_BGRA8               0
#define HAS_BGRX8               1
#define HAS_UYVY8               0
#define HAS_BGR8                1
#define HAS_R_G_B8              1
#define HAS_B_G_R8              1
#define HAS_Y_U_V8_420          0
/* INPUT_FORMATS_END */
#if (NORMALIZATION == 1)
/* Float formats -- supported only at Output Interface
   and only in Normalization mode. Even to enable FP32 and
   BF16/FP16 outputs corresponding INT8 format should be enabled
   as shown below.

---------------------------------------------------
| FP32/BF16/FP16           | INT8                 |
---------------------------------------------------
| HAS_RGB_YUV_FP32         | HAS_RGB8_YUV8        |
| HAS_RGB_BF16             | HAS_RGB8_YUV8        |
| HAS_RGB_FP16             | HAS_RGB8_YUV8        |
| HAS_R_G_B_FP32           | HAS_R_G_B8           |
| HAS_R_G_B_BF16           | HAS_R_G_B8           |
| HAS_R_G_B_FP16           | HAS_R_G_B8           |
| HAS_B_G_R_FP32           | HAS_B_G_R8           |
| HAS_B_G_R_BF16           | HAS_B_G_R8           |
| HAS_B_G_R_FP16           | HAS_B_G_R8           |
| HAS_BGR_FP32             | HAS_BGR8             |
| HAS_BGR_BF16             | HAS_BGR8             |
| HAS_BGR_FP16             | HAS_BGR8             |
| HAS_Y_FP32               | HAS_Y8               |
| HAS_Y_BF16               | HAS_Y8               |
| HAS_Y_FP16               | HAS_Y8               |
| HAS_RGBX_YUVX_BF16       | HAS_RGBX8_YUVX8      |
| HAS_RGBX_FP16            | HAS_RGBX8_YUVX8      |
| HAS_BGRX_BF16            | HAS_BGRX8            |
| HAS_BGRX_FP16            | HAS_BGRX8            |
---------------------------------------------------
*/

/* OUTPUT_ONLY_FORMATS_BEGIN */
#define HAS_RGB_YUV_FP32	1
#define HAS_R_G_B_FP32		1
#define HAS_B_G_R_FP32		1
#define HAS_BGR_FP32		1
#define HAS_Y_FP32		1
#define HAS_RGBX_YUVX_BF16	1
#define HAS_BGRX_BF16		1
#define HAS_Y_BF16		1
#define HAS_RGB_BF16		1
#define HAS_BGR_BF16		1
#define HAS_Y_FP16		1
#define HAS_RGB_FP16		1
#define HAS_BGR_FP16		1
#define HAS_RGBX_FP16		1
#define HAS_BGRX_FP16		1
#define HAS_R_G_B_FP16		1
#define HAS_R_G_B_BF16		1
#define HAS_B_G_R_FP16		1
#define HAS_B_G_R_BF16		1
/* OUTPUT_ONLY_FORMATS_END */
#endif //END OF NORMALIZATION
/* Memory Ports */
#define AXIMM_NUM_OUTSTANDING       4
#define AXIMM_BURST_LENGTH          16

/* Constant parameter coming from coreinfo.yml */
#define HSC_PHASE_SHIFT         6        // Number of bits used for phase
#define USE_URAM                0
#define INPUT_INTERFACE		AXIMM_INTERFACE
#define	OUTPUT_INTERFACE	AXIMM_INTERFACE

#endif // _IMAGE_PROCESSING_CONFIG_H_
