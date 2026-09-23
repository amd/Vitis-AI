/**
 * @file run_snapshot.cpp
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

#include <cstring>
#include <fstream>

#include "io/io.h"
#include "npu_runner.h"
#include "utils/log.h"
#include "utils/parsing_combinators.h"

/*
 * Validate master control config transaction sequence and group them to leverage burst writes.
 * For AIEML_V1C, also prepend the service bus config registers parsed from the snapshot.
 */
static int npu_prepare_mctrl_mem_config(struct npu_snapshot* snap)
{
	if (snap->arch == FpgaArchitecture::AIEML_V1C)
	{
		snap->ctrl_reg_config.emplace_back();
		auto& pair  = snap->ctrl_reg_config.back();
		pair.first  = CONFIG_OFFSET_VERSAL_V1C;
		pair.second = std::vector<uint32_t>(snap->config.reg,
		                                    snap->config.reg + sizeof(snap->config.reg) / sizeof(uint32_t));
	}

	uint64_t offset_prev = 0;
	for (const auto& [offset, value] : snap->srv_write_sequence)
	{
		if (!snap->ctrl_reg_config.empty() && offset == offset_prev + sizeof(uint32_t))
		{
			snap->ctrl_reg_config.back().second.push_back(value);
		}
		else
		{
			snap->ctrl_reg_config.emplace_back();
			auto& pair = snap->ctrl_reg_config.back();
			pair.first = offset;
			pair.second.push_back(value);
		}

		offset_prev = offset;
	}

	return vart_ml_error_id::SUCCESS;
}

int npu_run_snapshot(npu_snapshot_t* snap)
{
	if (getenv("VAISW_SNAPSHOT_SKIPCONFIG_LOADING") != NULL)
	{
		printf("SKIP CONFIG LOADING\n");
		return vart_ml_error::SUCCESS;
	}

	std::ifstream snapshot(snap->path + "/snapshot.dump.config", std::ios::in | std::ios::binary);
	if (!snapshot.is_open())
		return vart_ml_log_err_msg(vart_ml_error::FILE_ACCESS_OPEN_FAILURE,
		                           "Failed opening snapshot file %s.\n",
		                           (snap->path + "/snapshot.dump.config").c_str());

	char magic_bytes[5];
	snapshot.read(reinterpret_cast<char*>(&magic_bytes), sizeof(magic_bytes));

	int err;
	// Check the magic bytes at the beginning if the file.
	if (strncmp(magic_bytes, "#BIN\n", 5) == 0)
	{
		err = npu_run_snapshot_bin(snap, snapshot);
		if (err)
			return err;
	}
	else
	{
		// If there is no TXT magic bytes, it is an older snapshot. Set the cursor at the beginning of the
		// file for retro compatibility.
		if (strncmp(magic_bytes, "#TXT\n", 5) != 0)
			snapshot.seekg(0);

		err = npu_run_snapshot_txt(snap, snapshot);
		if (err)
			return err;
	}

	// Check mctrl config transactions
	if (snap->ctrl_reg_config.empty())
	{
		err = npu_prepare_mctrl_mem_config(snap);
		if (err)
			return (err);
	}

	return vart_ml_error::SUCCESS;
}
