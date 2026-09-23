/**
 * @file clocks.h
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

#ifndef CLOCKS_H
#define CLOCKS_H

/**
 * @brief Read or write the current FPGA frequency.
 *
 * @param argc Read or write.
 * @param argv For write, new value.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int frequency(int argc, const char** argv);

/**
 * @brief Read or write the current AIE frequency.
 *
 * @param argc Read or write.
 * @param argv For write, new value.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int aieFrequency(int argc, const char** argv);

/**
 * @brief Start the FPGA clocks.
 *
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int start_clocks(int argc, const char** argv);

/**
 * @brief Stop the FPGA clocks.
 *
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int stop_clocks(int argc, const char** argv);

#endif
