/**
 * @file npu_reg.h
 *
 * @copyright Copyright 2024 Advanced Micro Devices Inc.
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

#ifndef NPU_REG_H
#define NPU_REG_H

#define TIMESTAMP_OFFSET 0x0

#define MAXDDRS               3
#define AIE_PER_COLUMN_VE2302 2

#define MMCM_OFFSET         0x4
#define CLK_10M_OFFSET      0x8
#define CLK4X_OFFSET        0xc
#define CLK2X_OFFSET        0x2c
#define CLK1X_OFFSET        0x4c
#define CLK_DDR_OFFSET(mem) (0x6c + 4 * (mem))

#define MMCM_CFG_BASE  0x300
#define MMCM_CFG_VALID 0x304
#define MMCM_CLK_ENA   0x324

#define MMCM_CFG_BASE_V1C 0x100

#define MMCM_VERSAL_CLKFBOUT_MULT01(base) (base + 0x30)
#define MMCM_VERSAL_CLKFBOUT_MULT02(base) (base + 0x34)
#define MMCM_VERSAL_CLKDIV01(base)        (base + 0x38)
#define MMCM_VERSAL_CLKDIV02(base)        (base + 0x3c)
#define MMCM_VERSAL_DIV01(base)           (base + 0x80)
#define MMCM_VERSAL_DIV02(base)           (base + 0x84)

/* V1C */
#define TICK_COUNTER_ENABLE_OFFSET_V1C 0x54
#define TICK_COUNTER_OFFSET_V1C        0x50
/* Ultrascale, VCK */
#define TICK_COUNTER_ENABLE_OFFSET 0xb0
#define TICK_COUNTER_OFFSET        0xac

#define TICK_COUNTER_TOLERANCE 0.01f

#define ASSERTS_OFFSET          0x3000
#define ASSERTS_BASE_OFFSET     0x3004
#define ASSERTS_OFFSET_V1C      0x1000
#define ASSERTS_BASE_OFFSET_V1C 0x1004
#define NB_ASSERT_REGS          5

#define SUPERVISOR_REG_OFFSET     0x200
#define SUPERVISOR_REG_SYS_OFFSET (SUPERVISOR_REG_OFFSET + 0x40)

/* Ultrascale */
#define NBUFF_BASE_OFFSET_USCALE        0x30000
#define NBUFF_TEMP_AREA_OFFSET_USCALE   (NBUFF_BASE_OFFSET_USCALE + 0x380)
#define NBUFF_CONFIG_AREA_OFFSET_USCALE (NBUFF_BASE_OFFSET_USCALE + 0x3c0)
#define NBUFF_LEN_USCALE                (4 * 256)

#define MCTRL_MEM_BASE_USCALE  0x8000
#define MCTRL_MEM_SUPRA_USCALE 0xc000
#define MCTRL_MEM_LEN_USCALE   (28 << 10) /* 28 KiB */

/* VCK */
#define NBUFF_BASE_OFFSET_START_AIE1     0x30000
#define NBUFF_LEN_AIE1                   (4 * 256)
#define NBUFF_BASE_OFFSET_AIE1(x)        (NBUFF_BASE_OFFSET_START_AIE1 + x * NBUFF_LEN_AIE1)
#define NBUFF_TEMP_AREA_OFFSET_AIE1(x)   (NBUFF_BASE_OFFSET_AIE1(x) + 0x380)
#define NBUFF_CONFIG_AREA_OFFSET_AIE1(x) (NBUFF_BASE_OFFSET_AIE1(x) + 0x3c0)

/* V1C */
#define NBUFF_BASE_OFFSET 0x7000
#define NBUFF_LEN         (8 * 256)
#define NBUFF_INDEX       NBUFF_BASE_OFFSET + 0x800

#define MCTRL_MEM_BASE 0x8000
#define MCTRL_MEM_LEN  (24 << 10) /* 24 KiB */
#define MCTRL_MEM_MASK 0xffffffff

#define VERSAL_MCTRL_MEM_BASE                              0xf000
#define VERSAL_MCTRL_MEM_TEST_BASE                         (VERSAL_MCTRL_MEM_BASE + 0x0c)
#define VERSAL_MCTRL_MEM_TEST_LEN                          20
#define VERSAL_MCTRL_MEM_TEST_MASK                         0xffffff
#define VERSAL_MCTRL_MEM_LEN                               0x40
#define VERSAL_MCTRL_MEM_NPU_MUTEX_BASEADDR                (VERSAL_MCTRL_MEM_BASE + 0x188) // 98th 32-bit word
#define VERSAL_MCTRL_NPU_INFERENCE_MUTEX_OFFSET            0
#define VERSAL_MCTRL_NPU_INFERENCE_QUEUE_TAIL_MUTEX_OFFSET 0x4
#define VERSAL_MCTRL_NPU_MALLOC_MUTEX_OFFSET               0x8
#define VERSAL_MCTRL_NPU_INFERENCE_QUEUE_HEAD_REG          (VERSAL_MCTRL_MEM_BASE + 0x194) // 101st 32-bit word
#define VERSAL_MCTRL_NPU_INFERENCE_QUEUE_TAIL_REG          (VERSAL_MCTRL_MEM_BASE + 0x198) // 102nd 32-bit word

#define NPU_INTERRUPT_CTRL    0xf000 // for uscale and V1C
#define NPU_INTERRUPT_CTRL_V2 0xf004 // for Versal V2
#define NPU_INTERRUPT_MASK    0x04
#define NPU_INTERRUPT_MASK_V2 0x01

#define NB_MAX_SYSTEMS                              8
#define NB_MAX_CORES_PER_SYSTEM                     8
#define CORESIZE                                    0x10000
#define SYSTEMSIZE                                  (NB_MAX_CORES_PER_SYSTEM * CORESIZE)
#define CORE_OFFSET(systemindex, coreindex)         ((coreindex) * CORESIZE + (systemindex) * SYSTEMSIZE)
#define STATUS_OFFSET(systemindex, coreindex)       (0xf000 + CORE_OFFSET(systemindex, coreindex))
#define ENABLE_OFFSET(systemindex, coreindex)       (0xf004 + CORE_OFFSET(systemindex, coreindex))
#define LOAD_OFFSET(systemindex, coreindex)         (0xf008 + CORE_OFFSET(systemindex, coreindex))
#define CONFIG_OFFSET(systemindex, coreindex)       (0xf00c + CORE_OFFSET(systemindex, coreindex))
#define MCTRL_VERSAL_OFFSET(systemindex, coreindex) (0xf010 + CORE_OFFSET(systemindex, coreindex))
#define TIMER_OFFSET(systemindex, coreindex)        (0xf010 + CORE_OFFSET(systemindex, coreindex))
#define TIMER_OFFSET_VERSAL(systemindex, coreindex) (0xf030 + CORE_OFFSET(systemindex, coreindex))
#define TIMER_OFFSET_VERSAL_V1C                     0xf168
#define CONFIG_OFFSET_VERSAL_V1C                    0xf16c

#define DEBUG_SYSMON_DDR_BASE_ADDR 0x10000
#define TEMP_OFFSET                (DEBUG_SYSMON_DDR_BASE_ADDR)
#define MAX_TEMP_OFFSET            (DEBUG_SYSMON_DDR_BASE_ADDR + 0x80)
#define OVER_TEMP_FLAG_OFFSET      (DEBUG_SYSMON_DDR_BASE_ADDR + 0xfc)
#define OVER_TEMP_VALUE_OFFSET     (DEBUG_SYSMON_DDR_BASE_ADDR + 0x14c)
#define OVER_TEMP_MASK             (1 << 3)

#define NOC_PMC_BASE_ADDR          0xf1000000
#define SYSMON_NOC_BASE_ADDR       0x270000
#define NOC_TEMP_OFFSET            (SYSMON_NOC_BASE_ADDR + 0x1030)
#define NOC_MAX_TEMP_OFFSET        (SYSMON_NOC_BASE_ADDR + 0x1f90)
#define NOC_OVER_TEMP_VALUE_OFFSET (SYSMON_NOC_BASE_ADDR + 0x197c)

#define NOC_AIE_V2_FREQ_BASE_ADDR        0x060a0000
#define NOC_AIE_ML_FREQ_VE2302_BASE_ADDR 0x05420000
#define NOC_AIE_ML_FREQ_VE2802_BASE_ADDR 0x05d10000
#define NOC_AIE_FREQ_VALID_OFFSET        0x000c
#define NOC_AIE_FREQ_OFFSET              0x0100
#define NOC_ME_PLL_STATUS_OFFSET         0x010c
#define NOC_ME_CORE_REF_CTRL_OFFSET      0x0138

#define DEBUG_SRV_DDR_BASE_ADDR 0x20000
#define DDR_WR_DATA             (DEBUG_SRV_DDR_BASE_ADDR)
#define DDR_RD_DATA             (DEBUG_SRV_DDR_BASE_ADDR + 0x40)
#define DDR_ADDR                (DEBUG_SRV_DDR_BASE_ADDR + 0x80)
#define DDR_CTRL                (DEBUG_SRV_DDR_BASE_ADDR + 0x88)
#define DDR_COMMAND_MASK        1
#define DDR_START_MASK          1 << 1
#define DDR_STATUS_MASK         1 << 2
#define DDR_TIMEOUT             1000

#endif
