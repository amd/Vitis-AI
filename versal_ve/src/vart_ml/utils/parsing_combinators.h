/**
 * @file parsing_combinators.h
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

#ifndef PARSING_COMBINATORS_H
#define PARSING_COMBINATORS_H

#include <sstream>
#include <vector>

/**
 * @brief Drop a char from the string.
 *
 * These functions don't perform error checking, set ss.exceptions() before calling and handle
 * std::ios_base::failure exceptions that may occur.
 *
 * @param ss std::stringstream currently read.
 * @param c char to be dropped.
 */
void drop(std::stringstream& ss, const char* c);

/**
 * @brief Convert a char from a stringstream into hex number.
 *
 * This function will throw std::range_error if ss doesn't contain a proper hex number.
 *
 * @param ss std::stringstream currently read.
 * @return uint32_t Return a hex number.
 */
uint32_t read_uint32_hex(std::stringstream& ss);

/**
 * @brief Convert a substring from a stringstream into an array of hex number.
 *
 * This function will throw std::range_error if ss doesn't contain a proper hex number.
 *
 * @param ss std::stringstream currently read.
 * @param buf Return an array of hex number.
 */
void read_uint512_t_hex(std::stringstream& ss, uint32_t* buf);

/**
 * @brief Convert a substring from a stringstream into an array of hex number.
 *
 * This function will throw std::range_error if ss doesn't contain a proper hex number.
 *
 * @param ss std::stringstream currently read.
 * @param buf Return an array of hex number.
 */
void read_uint512_t_hex(std::stringstream& ss, uint8_t* buf);

/**
 * @brief Read binary file.
 *
 * The vector data will be resize to the correct size to contain the data from the file.
 *
 * @param filename_ path of the file.
 * @param data_ vector which will contain the data from the file.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int readBinFile(const std::string& filename_, std::vector<float>& data_);
int readBinFile(const std::string& filename_, std::vector<uint8_t>& data_);

#endif
