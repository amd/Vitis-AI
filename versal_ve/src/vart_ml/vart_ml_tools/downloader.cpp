/**
 * @file downloader.cpp
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

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

#include "io/io.h"
#include "utils/log.h"
#include "utils/shell.h"

int ddr_download(int argc, const char** argv)
{
	if (argc != 4)
		return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
		                           "Usage: vart_ml_tools %s <addr> <size> <binary file>.\n",
		                           argv[0]);

	uint32_t             size = parse_uint(argv[2]);
	std::vector<uint8_t> output(size);

	int err = npu_connect_peripherals();
	if (err)
		return err;

	err = npu_connect_ip(0);
	if (err)
		return err;

	if (npu_is_xrt_en())
		for (size_t i = 0; i < npu_get_nbddrs(); i++)
			npu_malloc(npu_get_extmemlen(i), i);

	err = npu_read_ddr(npu_get_addr_from_phy_addr(parse_uint(argv[1])), output.data(), size);
	if (err)
		return err;

	std::ofstream outfile(argv[3], std::ios::out | std::ios::binary);
	if (!outfile)
		return vart_ml_log_err_msg(vart_ml_error::FILE_ACCESS_OPEN_FAILURE, "Can't open output file.\n");

	outfile.write((const char*)output.data(), output.size());
	outfile.close();

	return vart_ml_error::SUCCESS;
}
