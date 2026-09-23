/**
 * @file uploader.cpp
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

#include <fstream>
#include <iostream>
#include <vector>

#include <cstdlib>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "io/io.h"
#include "utils/log.h"
#include "utils/shell.h"

static int get_file_size(const char* pathname, off_t* filesz)
{
	struct stat fileinfo;

	if (stat(pathname, &fileinfo))
	{
		perror("Error stat-ing input file");
		return vart_ml_log_err_msg(vart_ml_error::FILE_ACCESS_REFERENCE_FAILURE, "Error stat-ing input file");
	}

	*filesz = fileinfo.st_size;

	return vart_ml_error::SUCCESS;
}

int ddr_upload(int argc, const char** argv)
{
	if (argc != 3)
		return vart_ml_log_err_msg(
		    vart_ml_error::TOOLS_BAD_USAGE, "Usage: vart_ml_tools %s <bin img file> <addr>.\n", argv[0]);

	int err = npu_connect_peripherals();
	if (err)
		return err;

	err = npu_connect_ip(0);
	if (err)
		return err;

	if (npu_is_xrt_en())
		for (size_t i = 0; i < npu_get_nbddrs(); i++)
			npu_malloc(npu_get_extmemlen(i), i);

	off_t size = 0;

	err = get_file_size(argv[1], &size);
	if (err)
		return err;

	uint32_t             filesz = (uint32_t)size;
	std::vector<uint8_t> buf(filesz);

	std::ifstream file(argv[1], std::ios::binary);
	if (!file.read((char*)buf.data(), filesz))
		return vart_ml_log_err(vart_ml_error::FILE_ACCESS_READ_FAILURE);

	file.close();
	return npu_write_ddr(npu_get_addr_from_phy_addr(parse_uint(argv[2])), buf.data(), filesz);
}
