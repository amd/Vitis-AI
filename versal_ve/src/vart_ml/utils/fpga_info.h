/**
 * @file fpga_info.h
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

#ifndef FPGA_INFO_H
#define FPGA_INFO_H

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
extern "C"
{
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#endif

	enum FpgaFamily
	{
		ZYNQ,
		VERSAL,
		FPGA_FAMILY_COUNT
	};

	static inline const char* stringFromFpgaFamily(enum FpgaFamily family)
	{
		static const char* strings[] = { "ZYNQ", "VERSAL" };

		return strings[family];
	}

	static inline enum FpgaFamily stringToFpgaFamily(const char* str)
	{
		if (str == NULL || strcmp(str, "ZYNQ") == 0)
			return ZYNQ;
		else
			return VERSAL;
	}

	enum FpgaArchitecture
	{
		V1,
		V2,
		AIEML_V1C,
	};

	static inline const char* stringFromArch(enum FpgaArchitecture arch)
	{
		static const char* strings[] = { "V1", "V2", "AIEML_V1C" };

		return strings[arch];
	}

	static inline enum FpgaArchitecture stringToArch(const char* str)
	{
		if (str == NULL || strcmp(str, "V1") == 0)
			return V1;
		else if (strcmp(str, "V2") == 0)
			return V2;
		else
			return AIEML_V1C;
	}

	/**
	 * @brief Parse the fpga_info path.
	 *
	 *
	 * @param timestamp Timestamp of the FPGA.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int print_fpga_info_path(uint32_t timestamp);

	/**
	 * @brief Parse the vaisw.ini file and return the value of a given configuration.
	 *
	 * @param cfg Configuration whose value is requested.
	 * @param info Returned pointer to the value of the configuration.
	 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
	 */
	int get_user_config(const char* cfg, const char** info);

	/**
	 * @brief Parse the vaisw.ini file and the env variable and check the value of a given confdiguration.
	 *
	 * @param cfg Configuration whose value is requested.
	 * @return bool true if the value of the configuration is true or 1, false otherwise.
	 */
	bool check_user_config(const char* cfg);

	/**
	 * @brief Check if the logs are enable.
	 *
	 * @return bool Return true is the logs will be displayed, false otherwise.
	 */
	bool is_verbose(void);

#ifdef __cplusplus
}

/* ****************** */
/* PURE CPP FUNCTIONS */
/* ****************** */

/**
 * @brief Convert a string to a vector of size_t.
 *
 * @param src The string to convert. Either a single number or a bracketed comma-separated list.
 * @return std::vector<size_t> The converted vector.
 */
std::vector<size_t> convertToVector(const std::string& src);

/**
 * @brief Parse the fpga_info file.
 *
 * This function is called from io dlopen shared libraries.
 *
 * @param timestamp Return the timestamp of the FPGA.
 * @param fpga_info The fpga_info container to be filled through parsing.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int parse_fpga_info(uint32_t timestamp, std::map<std::string, std::string>& fpga_info);

/**
 * @brief Print the fpga_info file content.
 *
 * @param fpga_info The fpga_info to be printed.
 */
void print_fpga_info(const std::map<std::string, std::string>& fpga_info);

#endif

#endif
