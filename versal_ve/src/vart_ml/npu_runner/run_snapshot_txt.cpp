/**
 * @file run_snapshot_txt.cpp
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

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

#include "io/io.h"
#include "npu_runner.h"
#include "utils/fpga_info.h"
#include "utils/log.h"
#include "utils/npu_reg.h"
#include "utils/parsing_combinators.h"

#define LOADING_BAR_INC 2

enum class MemAccessType
{
	READ,
	WRITE
};

static int get_one_percent_line_count(std::string snap_path, unsigned* res)
{
	// Get file's size.
	struct stat stat_buf;
	if (stat(snap_path.c_str(), &stat_buf) != 0)
		return vart_ml_log_err_msg(
		    vart_ml_error::FILE_ACCESS_NOT_FOUND, "Failed to stat snapshot file %s.\n", snap_path.c_str());

	off_t file_sz = stat_buf.st_size;

	// Get an estimation of line count. 158 is the length of ~99.9% of the file's lines.
	unsigned line_nb = file_sz / 158;

	// Return the number of lines representing 1% of the lines.
	*res = line_nb / 100;
	return vart_ml_error::SUCCESS;
}

static void display_percentage(unsigned line_count, unsigned one_percent_lines)
{
	if ((line_count % one_percent_lines) == 0)
	{
		size_t percent = line_count / one_percent_lines;
		if (percent > 100)
			percent = 100;

		std::ostringstream loading_bar;

		loading_bar << "[";

		for (size_t i = 1; i <= percent / LOADING_BAR_INC; i++)
			loading_bar << "=";

		for (size_t i = percent / LOADING_BAR_INC + 1; i <= 100 / LOADING_BAR_INC; i++)
			loading_bar << " ";

		loading_bar << "]\r";

		std::ostringstream display_percent;
		display_percent << std::setw(4) << percent << "% ";

		std::string str_bar = loading_bar.str();
		str_bar.insert(50 / LOADING_BAR_INC + 1, display_percent.str());

		vart_ml_log(LOG_INFO, "\r%s", str_bar.c_str());
		fflush(stdout);
	}
}

static int run_mem_access(struct addr addr, uint8_t* buf, MemAccessType type, FpgaArchitecture arch)
{
	// Addresses should fit in 36 bits (44 in v1c).
	if (addr.phy_addr >= pow(2, 36) && arch == V1)
		throw std::range_error("Attempting to access a DDR address wider than 36 bits.");
	else if (addr.phy_addr >= pow(2, 44))
		throw std::range_error("Attempting to access a DDR address wider than 44 bits.");

	int     err;
	uint8_t data[64];
	switch (type)
	{
	case MemAccessType::READ:
		err = npu_read_ddr(addr, data, sizeof(data));
		if (err)
			return err;

		if (memcmp(data, buf, sizeof(data)) != 0)
		{
			std::ostringstream err;
			err << "Unexpected data at DDR address 0x" << std::hex << addr.phy_addr << ":";
			err << std::endl << "Unexpected: \'h";
			for (int i = 63; i >= 0; i--)
				err << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];

			err << std::endl << "Expected:   \'h";
			for (int i = 63; i >= 0; i--)
				err << std::hex << std::setw(2) << std::setfill('0') << (int)buf[i];
			return vart_ml_log_err_msg(vart_ml_error::DEVICE_DDR_RW_FAILURE, "%s\n", err.str().c_str());
		}
		break;
	case MemAccessType::WRITE:
		return npu_write_ddr(addr, buf, sizeof(data));
		break;
	}

	return vart_ml_error::SUCCESS;
}

static bool parse_card_bar(std::stringstream& ss)
{
	int x;

	drop(ss, " ");
	ss >> x;
	if (x != 0)
	{
		return false;
	}

	drop(ss, " ");
	ss >> x;
	if (x != 0)
	{
		return false;
	}

	return true;
}

static int parse_ctrlbus_access(npu_snapshot_t* snap, std::stringstream& ss, FpgaArchitecture arch)
{
	std::string tmp;
	ss >> tmp;
	if (tmp.size() != 1)
		throw std::runtime_error("Invalid CTRLBUS access type " + tmp);

	if (tmp[0] != 'W')
	{
		if (tmp[0] != 'R' && tmp[0] != 'A' && tmp[0] != 'P' && tmp[0] != 'T')
			throw std::runtime_error("Invalid CTRLBUS access type " + tmp);
		return vart_ml_error::SUCCESS;
	}

	if (!parse_card_bar(ss))
		return vart_ml_error::SUCCESS;

	uint32_t dest, data;
	drop(ss, " 'h");
	ss >> std::hex >> dest;
	drop(ss, " 'h");
	ss >> std::hex >> data;

	if (is_core_config_addr(dest, arch) || is_supervisor_config_addr(dest, arch))
		snap->srv_write_sequence[dest] = data;
	else
		vart_ml_log(LOG_DBG, "[VART] SRV W 0x%08x 0x%08x -> Out of range, Skipped!\n", dest, data);

	return vart_ml_error::SUCCESS;
}

static int parse_mem_access(npu_snapshot_t* snap, std::stringstream& ss, FpgaArchitecture arch)
{
	struct addr* ref_addr     = snap->config_addr.data();
	size_t*      ref_size     = snap->config_size.data();
	bool         check_config = snap->checkConfig;

	std::string   tmp;
	MemAccessType type;
	int64_t       index;
	uint64_t      offset;
	uint8_t       buf[64];

	ss >> tmp;

	if (tmp.size() != 1)
		throw std::runtime_error("Invalid access type " + tmp);

	switch (tmp[0])
	{
	case 'R':
		type = MemAccessType::READ;
		break;
	case 'W':
		type = MemAccessType::WRITE;
		break;
	default:
		throw std::runtime_error("Invalid access type " + tmp);
	}

	if (!parse_card_bar(ss))
		return vart_ml_error::SUCCESS;

	drop(ss, " ");
	ss >> index;
	drop(ss, " 'h");
	ss >> std::hex >> offset;
	drop(ss, " 'h");
	read_uint512_t_hex(ss, buf);

	if (index >= npu_get_nb_ddrs())
		throw std::runtime_error("Access DDR index " + std::to_string((int)index) + " exceeds DDR max index ("
		                         + std::to_string(npu_get_nb_ddrs() - 1) + ")");

	struct addr addr;
	if (arch == AIEML_V1C || snap->is_nbuff_en)
	{
		addr = ref_addr[index];

		/**
		 * In V1C, access' DDR_offset is expressed as offset within the DDR. Thus, we need
		 * to add the DDR's physical address before substracting the physical config addr
		 * written in snapshot.
		 * In other archs, access' DDR_offset is expressed as physical address.
		 */
		uint64_t phy_access = (arch == AIEML_V1C) ? (npu_get_extmemBaseAddr(index) + offset) : offset;
		uint64_t ddr_offset = phy_access - snap->config_addr_in_snap[index];

		addr.phy_addr += ddr_offset;
		addr.offset += ddr_offset;

		/* Check for out-of-range DDR accesses */
		if ((addr.offset - ref_addr[index].offset) > ref_size[index])
			return vart_ml_log_err_msg(vart_ml_error::CONFIG_SNAPSHOT_MALFORMED,
			                           "Out-of-range DDR access detected at DDR %d: offset 0x%llx "
			                           "exceeds configuration allocation size 0x%llx. Abort!\n",
			                           index,
			                           offset,
			                           ref_size[index]);
	}
	else
		addr = npu_get_addr_from_idx_and_offset(index, offset);

	/* Check the existing snapshot instead of writing a new one */
	if (check_config && type == MemAccessType::WRITE)
		type = MemAccessType::READ;

	return run_mem_access(addr, buf, type, arch);
}

int npu_run_snapshot_txt(npu_snapshot_t* snap, std::ifstream& snapshot)
{
	std::string buf;
	size_t      i       = 0;
	bool        verbose = is_verbose();

	unsigned one_percent_lines = 0;
	unsigned line_count        = 0;
	int      err;

	FpgaArchitecture arch;

	err = npu_get_architecture(&arch);
	if (err)
		return err;

	err = get_one_percent_line_count(snap->path + "/snapshot.dump.config", &one_percent_lines);
	if (err)
		return err;

	while (std::getline(snapshot, buf))
	{
		display_percentage(line_count++, one_percent_lines);

		std::string       tok1;
		std::stringstream ss(buf);
		ss.exceptions(std::stringstream::failbit);
		if (verbose)
			std::cout << "Trying to run snapshot line " << buf << std::endl;

		i++;
		try
		{
			ss >> tok1;
			if (tok1[0] == '#')
				continue;

			if (tok1.size() != 3 || ((tok1.rfind("SRV", 0) != 0) && (tok1.rfind("MEM", 0) != 0)))
				throw std::runtime_error("Unknown token " + tok1);

			if (tok1 == "SRV")
			{
				if (snap->checkConfig)
					continue;
				err = parse_ctrlbus_access(snap, ss, arch);
				if (err)
					return err;
			}

			if (tok1 == "MEM")
			{
				err = parse_mem_access(snap, ss, arch);
				if (err)
					return err;
			}
		}
		catch (const std::exception& e)
		{
			return vart_ml_log_err_msg(vart_ml_error::CONFIG_SNAPSHOT_MALFORMED,
			                           "Malformed snapshot.dump.config file at line %d: %s.\n",
			                           i,
			                           e.what());
		}
	}

	// Go to next line to avoid writing over the progression bar.
	std::cout << std::endl;

	// Go to next line to avoid writing over the progression bar.
	if (snap->checkConfig)
		vart_ml_log(LOG_INFO, "\n[VART] config is correct in DDR\n");
	else
		vart_ml_log(LOG_INFO, "\n");
	return vart_ml_error::SUCCESS;
}
