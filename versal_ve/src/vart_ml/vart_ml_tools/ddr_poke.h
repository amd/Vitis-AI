/**
 * @file ddr_poke.h
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

#ifndef DDR_POKE_H
#define DDR_POKE_H

/**
 * @brief Read or Write in DDR.
 *
 * @param argc Read or Write.
 * @param argv For write, new value.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int ddr_poke(int argc, const char** argv);

/**
 * @brief Read or Write in DDR.
 *
 * @param argc Argument count.
 * @param argv Argument list: ddr index, offset and optionally new value to write.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int ddr_index_poke(int argc, const char** argv);

/**
 * @brief Read or Write in memtiles by physical address.
 *
 * @param argc Argument count.
 * @param argv Argument list: physical address and optionally new value to write.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int memtile_poke(int argc, const char** argv);

/**
 * @brief Read or Write in memtiles by index and offset.
 *
 * @param argc Argument count.
 * @param argv Argument list: memtile index, offset and optionally new value to write.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int memtile_index_poke(int argc, const char** argv);

/**
 * @brief Read or Write a range in DDR.
 *
 * @param argc Argument count.
 * @param argv Argument list: ddr index, offset, range size and optionally new value to write.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int ddr_index_poke_range(int argc, const char** argv);

/**
 * @brief Compare each 32-bit word of a range in DDR against a pattern.
 *
 * @param argc Argument count.
 * @param argv Argument list: ddr index, offset, range size and a pattern.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int ddr_index_compare_range(int argc, const char** argv);

#endif
