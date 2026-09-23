/**
 * @file npu_runner.h
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
 *
 */

#ifndef NPU_RUNNER_H
#define NPU_RUNNER_H

#include <stddef.h>

#include "private.h"

/**
 * @brief Parse snapshot files.
 *
 * Parse the snapshot and configure VART ML accordingly. snap_path is the path to the snapshot directory
 * (in which there are snapshot.dump.* files). If snap_path is empty or NULL, npu_parse_snapshot uses
 * setting snapshot.directory from vaisw.ini.
 *
 * @param snap_path Path of the snapshot files repository.
 * @param snap Pointer of pointer to the snapshot descriptor container created in the call.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int npu_parse_snapshot(const std::string& snap_path, npu_snapshot_t** snap);

/**
 * @brief Free any memory allocated by npu_parse_snapshot, including snap.
 *
 * @param snap Snapshot to be freed
 */
void npu_free_snapshot(npu_snapshot_t* snap);

/**
 * @brief Return the number of DDR used by the NPU.
 *
 */
uint8_t npu_get_nb_ddrs(void);

/**
 * @name Accessors
 *
 * These are utility functions that return the static information from the snapshot.
 * They do not have side-effects.
 *
 * @param snap Snapshot to get the information from.
 * @param inputno Input number.
 * @param outputno Output number.
 */
//@{
size_t             npu_get_tmp_area_size(const npu_snapshot_t* snap);
uint64_t           npu_get_tmp_area_nbuff_conf(const npu_snapshot_t* snap, size_t tmpareano);
size_t             npu_get_nbinputs(const npu_snapshot_t* snap);
const std::string& npu_get_in_name(const npu_snapshot_t* snap, size_t inputno);
size_t             npu_get_in_ddrimgsize(const npu_snapshot_t* snap, size_t inputno);
float              npu_get_in_quantization_coeff(const npu_snapshot_t* snap, size_t inputno);
const std::string& npu_get_in_shape_format(const npu_snapshot_t* snap, size_t inputno);
const std::string& npu_get_in_ddr_shape_format(const npu_snapshot_t* snap, size_t inputno);
const std::string& npu_get_in_data_type(const npu_snapshot_t* snap, size_t inputno);

size_t             npu_get_nboutputs(const npu_snapshot_t* snap);
const std::string& npu_get_out_name(const npu_snapshot_t* snap, size_t outputno);
size_t             npu_get_out_ddrimgsize(const npu_snapshot_t* snap, size_t outputno);
float              npu_get_out_quantization_coeff(const npu_snapshot_t* snap, size_t outputno);
const std::string& npu_get_out_shape_format(const npu_snapshot_t* snap, size_t outputno);
const std::string& npu_get_out_ddr_shape_format(const npu_snapshot_t* snap, size_t outputno);
const std::string& npu_get_out_data_type(const npu_snapshot_t* snap, size_t outputno);
//@}

/**
 * @brief Start the inference.
 *
 * @param snap Snapshot of the network the inference will be run for.
 * @param struct vcd_context VCD context for logging.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int npu_start_inference(const npu_snapshot_t* snap, struct vcd_context& vcd_context);

/**
 * @brief Wait for the inference to end.
 *
 * @param snap Snapshot of the current inference.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int npu_wait_for_inference(const npu_snapshot_t* snap);

/**
 * @brief Check if the inference is over.
 *
 * @param snap Snapshot of the current inference.
 * @param retval Returns 0 if the inference is over, 1 otherwise.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int npu_is_inference_done(const npu_snapshot_t* snap, bool* reval);

/**
 * @brief Download the raw data of the inference.
 *
 * @param snap Current snapshot.
 * @param nbuff List of vectors of physical addresses of the buffer.
 * @param start Index of the first buffer to use for the inference.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int npu_set_nbuff_addr(const npu_snapshot_t*                     snap,
                       const std::vector<std::vector<uint64_t>>& nbuff,
                       size_t                                    start = 0);

/**
 * @brief Check if an assertion was raised.
 *
 * @param ip_idx index of the IP being accessed.
 * @return Returns false if an assertion is raised.
 */
bool npu_check_asserts(uint32_t ip_idx);

/**
 * @brief Check the status of all cores.
 *
 * @param snap Current snapshot.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int npu_check_cores(const npu_snapshot_t* snap);

/**
 * @brief Quantizes a void* buffer into a void* buffer of identical shape.
 *
 * There is a one-to-one mapping between src and dst.
 * The src data type is given by the tensor.
 *
 * @param src Source address of the buffer
 * @param dst Destination address of the buffer
 * @param data_type_in Data type of the source buffer.
 * @param data_type_out Data type of the destination buffer.
 * @param size Size (in number of elements) of the buffers.
 * @param coeff Coefficient of unquantization. If 0, uses the snapshot's coeff instead.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int npu_dequantize(const void*        src,
                   void*              dst,
                   const std::string& data_type_in,
                   const std::string& data_type_out,
                   size_t             size,
                   float              coeff);

/**
 * @brief Quantizes a void* buffer into a void* buffer of identical shape.
 *
 * There is a one-to-one mapping between src and dst.
 * The dst data type is given by the tensor.
 *
 * @param src Source address of the buffer
 * @param dst Destination address of the buffer
 * @param data_type_in Data type of the source buffer.
 * @param data_type_out Data type of the destination buffer.
 * @param size Size (in number of elements) of the buffers.
 * @param coeff Coefficient of unquantization. If 0, uses the snapshot's coeff instead.
 * @param use_accurate_bf16 True if we need to use the accurate version of the bf16 to float quantization
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int npu_quantize(const void*        src,
                 void*              dst,
                 const std::string& data_type_in,
                 const std::string& data_type_out,
                 size_t             size,
                 float              coeff,
                 bool               use_accurate_bf16);

/**
 * @name Clock management
 *
 * On PCIe VART ML, npu_connect() also performs some clock management, unless debug options
 * debug.enableStartClock=false and debug.enableStopClock=false are set. Don't expect any of these
 * functions to behave as they do on embedded VART ML otherwise.
 * npu_start_clocks takes a byte to select which systems' clocks should be enabled. Bit i is the enable
 * bit for system i.
 *
 * @param ip_idx index of the IP being accessed.
 * @param systems System number.
 * @param target_freq New frequency for PL/AIE to set.
 */
//@{
int npu_start_clocks(uint32_t ip_idx, uint8_t systems);
int npu_stop_clocks(uint32_t ip_idx);
int npu_change_mmcm_freq(uint32_t ip_idx, float target_freq);
int npu_read_mmcm_freq(uint32_t ip_idx, float* freq);
int npu_change_aie_freq(float target_freq);
int npu_read_aie_freq(float* freq);
int npu_set_clock_state(uint32_t ip_idx);
int npu_read_preciseTimer(uint32_t ip_idx, struct timespec* precise_time);
//@}

enum clock
{
	CLK_10M,
	CLK_4X,
	CLK_2X,
	CLK_1X,
	CLK_DDR
};

/**
 * @brief Mesure the actual frequency of the FPGA.
 *
 * VART ML's frequency counters work by dividing the frequency to be measured by a factor, then by
 * counting how many periods of a reference clock the divided clock's period spans.
 *
 * @param clk Select the clock whose frequency to measure.
 * @param ip_idx index of the IP being accessed.
 * @param ddr_idx Select the DDR/System number.
 * @param freq The returned frequency.
 * @param force_print Force frequency measurement log messages to print. Set to false by default.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int npu_measure_freq(enum clock clk, uint32_t ip_idx, size_t ddr_idx, float* freq, bool force_print = false);

/**
 * @name Pl specific functions
 *
 * These are utility functions that return the static information from the snapshot.
 * They do not have side-effects.
 *
 * @param snap Snapshot to get the information from.
 * @param tensor_name Name of the pl tensor.
 * @param batchno Batch element number.
 * @param v_addr Pointer to the pl tensor data.
 */
//@{
bool               npu_has_pl(const npu_snapshot_t* snap);
bool               is_tensor_pl(const npu_snapshot_t* snap, const std::string& tensor_name);
size_t             npu_get_pl_nbdims(const npu_snapshot_t* snap, const std::string& tensor_name);
const uint32_t*    npu_get_pl_shape(const npu_snapshot_t* snap, const std::string& tensor_name);
const uint32_t*    npu_get_pl_strides(const npu_snapshot_t* snap, const std::string& tensor_name);
const std::string& npu_get_pl_data_type(const npu_snapshot_t* snap, const std::string& tensor_name);
int                npu_set_pl_info(const npu_snapshot_t* snap, const std::string& tensor_name);
int npu_set_pl_addr(const npu_snapshot_t* snap, const std::string& tensor_name, size_t batchno, void* v_addr);
//@}

#endif
