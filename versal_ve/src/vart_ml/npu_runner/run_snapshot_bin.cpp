/**
 * @file run_snapshot_bin.cpp
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
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

#include "io/io.h"
#include "npu_runner.h"
#include "utils/log.h"
#include "utils/npu_reg.h"

#define LOADING_BAR_INC 2

enum class MemAccessType
{
	READ,
	WRITE,
};

struct pcie_access
{
	uint8_t  type;
	uint8_t  mode;
	uint8_t  board_index;
	uint8_t  bar_index;
	uint8_t  DDR_index;
	uint64_t DDR_offset;
} __attribute__((packed));

static int get_one_percent_line_count(std::string snap_path, unsigned* res)
{
	// Get file's size.
	struct stat stat_buf;
	if (stat(snap_path.c_str(), &stat_buf) != 0)
		return vart_ml_log_err_msg(
		    vart_ml_error::FILE_ACCESS_NOT_FOUND, "Failed to stat snapshot file %s.\n", snap_path.c_str());

	off_t file_sz = stat_buf.st_size;

	// Get an estimation of command count. 77 bytes is the length of ~99.9% of the file's commands.
	unsigned line_nb = file_sz / 77;

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

static int run_mem_access(struct addr addr, struct pcie_access access, uint8_t* buf, FpgaArchitecture arch)
{
	// Addresses should fit in 36 bits (44 in v1c and v2).
	if (addr.phy_addr >= pow(2, 36) && arch == V1)
		throw std::range_error("Attempting to access a DDR address wider than 36 bits.");
	else if (addr.phy_addr >= pow(2, 44))
		throw std::range_error("Attempting to access a DDR address wider than 44 bits.");

	int     err;
	uint8_t data[64];

	MemAccessType accessType = (MemAccessType)access.mode;
	switch (accessType)
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
	default:
		throw std::runtime_error("Invalid MEM access type " + std::to_string((int)accessType));
	}

	return vart_ml_error::SUCCESS;
}

static bool check_card_bar(struct pcie_access& access)
{
	if (access.board_index != 0)
	{
		return false;
	}

	if (access.bar_index != 0)
	{
		return false;
	}

	return true;
}

int npu_run_snapshot_bin(npu_snapshot_t* snap, std::ifstream& snapshot)
{
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

	struct pcie_access access;
	snapshot >> std::noskipws;
	for (;;)
	{
		display_percentage(line_count++, one_percent_lines);

		snapshot.read(reinterpret_cast<char*>(&access), sizeof(access));
		if (snapshot.eof())
			break;

		try
		{
			if (access.DDR_index > npu_get_nb_ddrs())
				throw std::runtime_error("Access DDR index " + std::to_string((int)access.DDR_index)
				                         + "exceeds DDR count (" + std::to_string(npu_get_nb_ddrs()) + ")");

			switch (access.type)
			{
			case 0x00:
				uint32_t data;
				snapshot.read(reinterpret_cast<char*>(&data), sizeof(data));
				if (!snap->checkConfig && check_card_bar(access)
				    && (MemAccessType)access.mode == MemAccessType::WRITE
				    && (is_core_config_addr(access.DDR_offset, arch)
				        || is_supervisor_config_addr(access.DDR_offset, arch)))
					snap->srv_write_sequence[access.DDR_offset] = data;
				else
					vart_ml_log(LOG_DBG,
					            "[VART] SRV W 0x%08x 0x%08x -> Out of range, Skipped!\n",
					            (uint32_t)access.DDR_offset,
					            data);
				break;

			case 0x01:
				uint8_t buf[64];
				snapshot.read(reinterpret_cast<char*>(buf), sizeof(buf));
				if (check_card_bar(access))
				{
					struct addr addr;
					if (arch == AIEML_V1C || snap->is_nbuff_en)
					{
						addr = snap->config_addr[access.DDR_index];

						/**
						 * In V1C, access' DDR_offset is expressed as offset within the DDR. Thus, we need
						 * to add the DDR's physical address before substracting the physical config addr
						 * from the snapshot (which is updated to take into account constants).
						 * In other archs, access' DDR_offset is expressed as physical address.
						 */
						uint64_t phy_access =
						    (arch == AIEML_V1C)
						        ? (npu_get_extmemBaseAddr(access.DDR_index) + access.DDR_offset)
						        : access.DDR_offset;
						uint64_t ddr_offset = phy_access - snap->config_addr_in_snap[access.DDR_index];

						addr.phy_addr += ddr_offset;
						addr.offset += ddr_offset;

						/* Check for out-of-range DDR accesses */
						if ((addr.offset - snap->config_addr[access.DDR_index].offset)
						    > snap->config_size[access.DDR_index])
							return vart_ml_log_err_msg(
							    vart_ml_error::CONFIG_SNAPSHOT_MALFORMED,
							    "Out-of-range DDR access detected at DDR %d: offset 0x%llx "
							    "exceeds configuration allocation size 0x%llx. Abort!\n",
							    access.DDR_index,
							    access.DDR_offset,
							    snap->config_size[access.DDR_index]);
					}
					else
						addr = npu_get_addr_from_idx_and_offset(access.DDR_index, access.DDR_offset);

					/* Check the existing snapshot instead of writing a new one */
					if (snap->checkConfig && (MemAccessType)access.mode == MemAccessType::WRITE)
						access.mode = (uint8_t)MemAccessType::READ;

					err = run_mem_access(addr, access, buf, arch);
					if (err)
						return err;
				}
				break;

			default:
				throw std::runtime_error("Unknown access type " + std::to_string((int)access.type));
			}
		}
		catch (const std::exception& e)
		{
			return vart_ml_log_err_msg(vart_ml_error::CONFIG_SNAPSHOT_MALFORMED,
			                           "Malformed snapshot.dump.config file: %s\n",
			                           e.what());
		}
	}

	// Go to next line to avoid writing over the progression bar.
	if (snap->checkConfig)
		vart_ml_log(LOG_INFO, "\n[VART] config is correct in DDR\n");
	else
		vart_ml_log(LOG_INFO, "\n");
	return vart_ml_error::SUCCESS;
}
