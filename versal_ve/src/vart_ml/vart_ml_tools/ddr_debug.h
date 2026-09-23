/**
 * @file ddr_debug.h
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

#ifndef DDR_DEBUG_H
#define DDR_DEBUG_H

/**
 * @brief Read or Write in DDR through the Service Bus.
 *
 * @param argc Read or Write.
 * @param argv For write, new value.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int ddr_debug(int argc, const char** argv);

/**
 * @brief Read in DDR through the Service Bus.
 *
 * @param ip_idx index of the IP being accessed.
 * @param offset Address in DDR to access.
 * @param buf Value read.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int ddr_debug_read(uint32_t ip_idx, uint64_t offset, uint32_t* buf);

/**
 * @brief Write in DDR through the Service Bus.
 *
 * @param ip_idx index of the IP being accessed.
 * @param offset Address in DDR to access.
 * @param buf Value to write.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int ddr_debug_write(uint32_t ip_idx, uint64_t offset, uint32_t* buf);

#endif
