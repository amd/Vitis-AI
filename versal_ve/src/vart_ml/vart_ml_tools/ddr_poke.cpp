/**
 * @file ddr_poke.cpp
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
#include <iomanip>
#include <iostream>

#include "ddr_poke.h"
#include "io/io.h"
#include "npu_runner/npu_runner.h"
#include "utils/fpga_info.h"
#include "utils/log.h"
#include "utils/parsing_combinators.h"
#include "utils/shell.h"

#define BUFFER_BYTE_SIZE 64

int ddr_poke(int argc, const char** argv)
{
	uint32_t buf;

	int err = npu_connect_peripherals();
	if (err)
		return err;

	err = npu_connect_ip(0);
	if (err)
		return err;

	if (npu_is_xrt_en())
		for (size_t i = 0; i < npu_get_nbddrs(); i++)
			npu_malloc(npu_get_extmemlen(i), i);

	switch (argc)
	{
	case 2:
		err = npu_read_ddr(npu_get_addr_from_phy_addr(parse_uint(argv[1])), (uint8_t*)&buf, sizeof(buf));
		if (err)
			return err;

		vart_ml_log(LOG_INFO, "0x%08x\n", buf);
		break;
	case 3:
		buf = parse_uint(argv[2]);
		err = npu_write_ddr(npu_get_addr_from_phy_addr(parse_uint(argv[1])), (uint8_t*)&buf, sizeof(buf));
		if (err)
			return err;

		break;
	default:
		return vart_ml_log_err_msg(
		    vart_ml_error::TOOLS_BAD_USAGE, "Usage: vart_ml_tools %s <addr> [<value to write>].\n", argv[0]);
	}

	return vart_ml_error::SUCCESS;
}

int ddr_index_poke(int argc, const char** argv)
{
	uint32_t buf;
	uint8_t  index = parse_uint(argv[1]);

	int err = npu_connect_peripherals();
	if (err)
		return err;

	err = npu_connect_ip(0);
	if (err)
		return err;

	if (index > npu_get_nb_ddrs() - 1)
		return vart_ml_log_err_msg(
		    vart_ml_error::DEVICE_DEV_ACCESS_FAILURE, "Index exceeds DDR count (%d).\n", npu_get_nb_ddrs());

	if (npu_is_xrt_en())
		npu_malloc(npu_get_extmemlen(index), index);

	switch (argc)
	{
	case 3:
		err = npu_read_ddr(
		    npu_get_addr_from_idx_and_offset(index, parse_uint(argv[2])), (uint8_t*)&buf, sizeof(buf));
		if (err)
			return err;

		vart_ml_log(LOG_INFO, "0x%08x\n", buf);
		break;
	case 4:
		buf = parse_uint(argv[3]);
		err = npu_write_ddr(
		    npu_get_addr_from_idx_and_offset(index, parse_uint(argv[2])), (uint8_t*)&buf, sizeof(buf));
		if (err)
			return err;

		break;
	default:
		return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
		                           "Usage: vart_ml_tools %s <index> <offset> [<value to write>].\n",
		                           argv[0]);
	}

	return vart_ml_error::SUCCESS;
}

static int check_memtile_support(void)
{
	if (npu_is_xrt_en())
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_UNSUPPORTED_FEATURE,
		                           "Memtiles are not accessible using XRT.\n");

	if (npu_is_devmem_en())
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_UNSUPPORTED_FEATURE,
		                           "Memtiles are not accessible using DEVMEM.\n");

	enum FpgaArchitecture arch;
	int                   err = npu_get_architecture(&arch);
	if (err)
		return err;

	if (arch != AIEML_V1C)
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_UNSUPPORTED_FEATURE,
		                           "Memtiles are only supported on AIEML_V1C architecture.\n");

	return vart_ml_error::SUCCESS;
}

int memtile_poke(int argc, const char** argv)
{
	uint32_t buf;

	int err = npu_connect_peripherals();
	if (err)
		return err;

	err = npu_connect_ip(0);
	if (err)
		return err;

	err = check_memtile_support();
	if (err)
		return err;

	switch (argc)
	{
	case 2:
		err = npu_read_ddr(npu_get_addr_from_phy_addr(parse_uint(argv[1])), (uint8_t*)&buf, sizeof(buf));
		if (err)
			return err;

		vart_ml_log(LOG_INFO, "0x%08x\n", buf);
		break;
	case 3:
		buf = parse_uint(argv[2]);
		err = npu_write_ddr(npu_get_addr_from_phy_addr(parse_uint(argv[1])), (uint8_t*)&buf, sizeof(buf));
		if (err)
			return err;

		break;
	default:
		return vart_ml_log_err_msg(
		    vart_ml_error::TOOLS_BAD_USAGE, "Usage: vart_ml_tools %s <addr> [<value to write>].\n", argv[0]);
	}

	return vart_ml_error::SUCCESS;
}

int memtile_index_poke(int argc, const char** argv)
{
	uint32_t buf;
	uint8_t  index = parse_uint(argv[1]);

	int err = npu_connect_peripherals();
	if (err)
		return err;

	err = npu_connect_ip(0);
	if (err)
		return err;

	err = check_memtile_support();
	if (err)
		return err;

	if (index + npu_get_nbddrs() > npu_get_nbextmems() - 1)
		return vart_ml_log_err_msg(vart_ml_error::DEVICE_DEV_ACCESS_FAILURE,
		                           "Index exceeds memtile count (%d).\n",
		                           npu_get_nbextmems() - npu_get_nb_ddrs());

	struct addr addr = npu_get_addr_from_idx_and_offset(index + npu_get_nbddrs(), parse_uint(argv[2]));
	switch (argc)
	{
	case 3:
		err = npu_read_ddr(addr, (uint8_t*)&buf, sizeof(buf));
		if (err)
			return err;

		vart_ml_log(LOG_INFO, "0x%08x\n", buf);
		break;
	case 4:
		buf = parse_uint(argv[3]);
		err = npu_write_ddr(addr, (uint8_t*)&buf, sizeof(buf));
		if (err)
			return err;

		break;
	default:
		return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
		                           "Usage: vart_ml_tools %s <index> <offset> [<value to write>].\n",
		                           argv[0]);
	}

	return vart_ml_error::SUCCESS;
}

int ddr_index_poke_range(int argc, const char** argv)
{
	const size_t buffer_word_cnt = BUFFER_BYTE_SIZE / sizeof(uint32_t);

	int err = npu_connect_peripherals();
	if (err)
		return err;

	err = npu_connect_ip(0);
	if (err)
		return err;

	if (argc < 4)
		return vart_ml_log_err_msg(
		    vart_ml_error::TOOLS_BAD_USAGE,
		    "Usage: vart_ml_tools %s <index> <offset> <size> [<32-bit pattern to write>].\n",
		    argv[0]);

	uint8_t index = std::stoi(argv[1], NULL, 0);

	if (index > npu_get_nb_ddrs() - 1)
		return vart_ml_log_err_msg(
		    vart_ml_error::DEVICE_DEV_ACCESS_FAILURE, "Index exceeds DDR count (%d).\n", npu_get_nb_ddrs());

	if (npu_is_xrt_en())
		npu_malloc(npu_get_extmemlen(index), index);

	struct addr addr = npu_get_addr_from_idx_and_offset(index, parse_uint(argv[2]));
	uint64_t    size = parse_uint(argv[3]);
	uint8_t*    buf  = new uint8_t[BUFFER_BYTE_SIZE];
	uint32_t*   wbuf = (uint32_t*)buf;

	switch (argc)
	{
	case 4:
	{
		while (size > BUFFER_BYTE_SIZE)
		{
			err = npu_read_ddr(addr, buf, BUFFER_BYTE_SIZE);
			if (err)
				return err;

			for (size_t i = 0; i < buffer_word_cnt; i++)
				std::cout << std::hex << "0x" << std::setfill('0') << std::setw(8) << wbuf[i] << std::endl;

			addr.offset += BUFFER_BYTE_SIZE;
			addr.phy_addr += BUFFER_BYTE_SIZE;
			size -= BUFFER_BYTE_SIZE;
		}

		err = npu_read_ddr(addr, buf, size);
		if (err)
			return err;

		for (size_t i = 0; i < size; i += sizeof(uint32_t))
		{
			std::cout << std::hex << "0x" << std::setfill('0') << std::setw(8) << *wbuf << std::endl;
			wbuf++;
		}

		break;
	}
	case 5:
	{
		uint32_t pattern = parse_uint(argv[4]);

		for (size_t i = 0; i < buffer_word_cnt; i++)
			wbuf[i] = pattern;

		while (size > BUFFER_BYTE_SIZE)
		{
			err = npu_write_ddr(addr, buf, BUFFER_BYTE_SIZE);
			if (err)
				return err;
			addr.offset += BUFFER_BYTE_SIZE;
			addr.phy_addr += BUFFER_BYTE_SIZE;
			size -= BUFFER_BYTE_SIZE;
		}

		for (size_t i = 0; i < size; i += sizeof(uint32_t))
		{
			err = npu_write_ddr(addr, (uint8_t*)&pattern, sizeof(uint32_t));
			if (err)
				return err;
			addr.offset += sizeof(uint32_t);
			addr.phy_addr += sizeof(uint32_t);
		}

		break;
	}
	default:
		return vart_ml_log_err_msg(vart_ml_error::TOOLS_BAD_USAGE,
		                           "Usage: vart_ml_tools %s <index> <offset> <size> [<pattern to write>].\n",
		                           argv[0]);
	}

	delete[] buf;
	return vart_ml_error::SUCCESS;
}

int ddr_index_compare_range(int argc, const char** argv)
{
	const size_t buffer_word_cnt = BUFFER_BYTE_SIZE / sizeof(uint32_t);

	int err = npu_connect_peripherals();
	if (err)
		return err;

	err = npu_connect_ip(0);
	if (err)
		return err;

	if (argc != 5)
		return vart_ml_log_err_msg(
		    vart_ml_error::TOOLS_BAD_USAGE,
		    "Usage: vart_ml_tools %s <index> <offset> <size> <32-bit pattern to compare to>.\n",
		    argv[0]);

	uint8_t index = std::stoi(argv[1], NULL, 0);

	if (index > npu_get_nb_ddrs() - 1)
		return vart_ml_log_err_msg(
		    vart_ml_error::DEVICE_DEV_ACCESS_FAILURE, "Index exceeds DDR count (%d).\n", npu_get_nb_ddrs());

	if (npu_is_xrt_en())
		npu_malloc(npu_get_extmemlen(index), index);

	struct addr addr    = npu_get_addr_from_idx_and_offset(index, parse_uint(argv[2]));
	uint64_t    size    = parse_uint(argv[3]);
	uint32_t    pattern = parse_uint(argv[4]);

	uint8_t*  buf  = new uint8_t[BUFFER_BYTE_SIZE];
	uint32_t* wbuf = (uint32_t*)buf;

	while (size > BUFFER_BYTE_SIZE)
	{
		err = npu_read_ddr(addr, buf, BUFFER_BYTE_SIZE);
		if (err)
			return err;

		for (size_t i = 0; i < buffer_word_cnt; i++)
			if (wbuf[i] != pattern)
				std::cout << std::hex << "0x" << std::setfill('0') << std::setw(8)
				          << addr.phy_addr + addr.offset + i * sizeof(uint32_t) << "\t0x" << std::setfill('0')
				          << std::setw(8) << wbuf[i] << std::endl;

		addr.offset += BUFFER_BYTE_SIZE;
		addr.phy_addr += BUFFER_BYTE_SIZE;
		size -= BUFFER_BYTE_SIZE;
	}

	err = npu_read_ddr(addr, buf, size);
	if (err)
		return err;

	for (size_t i = 0; i < size; i += sizeof(uint32_t))
	{
		if (*wbuf != pattern)
			std::cout << std::hex << "0x" << std::setfill('0') << std::setw(8)
			          << addr.phy_addr + addr.offset + i << "\t0x" << std::setfill('0') << std::setw(8)
			          << *wbuf << std::endl;
		wbuf++;
	}
	delete[] buf;

	return vart_ml_error::SUCCESS;
}
