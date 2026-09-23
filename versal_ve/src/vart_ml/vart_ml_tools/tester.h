/**
 * @file tester.h
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

#ifndef TESTER_H
#define TESTER_H

/**
 * @brief Check the state of the DDR.
 *
 * This function check if the DDR is accessible directly and through the Service Bus.
 * It also check the integrity of the memory and if there is no wrapping.
 *
 * @param argc
 * @param argv
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int tester(int argc, const char** argv);

/**
 * @brief Display the current configuration of the FPGA.
 *
 * board name
 * boards
 * systems
 * cores
 * nces
 * neuron type
 * nominal frequency
 * run frequency
 * enabled cores
 * split core
 * fpga family
 * backend
 *
 * @param argc
 * @param argv
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int config(int argc, const char** argv);

/**
 * @brief Display the FPGA information from the fpga_info file.
 *
 * @param argc
 * @param argv
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int fpgaStatus(int argc, const char** argv);

/**
 * @brief Print all detected resources in a table format.
 *
 * @param argc
 * @param argv
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int print_resource_info(int argc, const char** argv);

#endif
