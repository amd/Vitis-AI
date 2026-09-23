/**
 * @file pl.cpp
 *
 * @copyright Copyright 2026 Advanced Micro Devices Inc.
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
 *
 */

#include "npu_runner.h"
#include "utils/log.h"

bool npu_has_pl(const npu_snapshot_t* snap) { return !snap->pl.empty(); }

bool is_tensor_pl(const npu_snapshot_t* snap, const std::string& tensor_name)
{
	return snap->pl.count(tensor_name);
}

size_t npu_get_pl_nbdims(const npu_snapshot_t* snap, const std::string& tensor_name)
{
	return snap->pl.at(tensor_name).pl_shape.size();
}

const uint32_t* npu_get_pl_shape(const npu_snapshot_t* snap, const std::string& tensor_name)
{
	return snap->pl.at(tensor_name).pl_shape.data();
}

const uint32_t* npu_get_pl_strides(const npu_snapshot_t* snap, const std::string& tensor_name)
{
	return snap->pl.at(tensor_name).pl_strides.data();
}

const std::string& npu_get_pl_data_type(const npu_snapshot_t* snap, const std::string& tensor_name)
{
	return dataTypeToString(snap->pl.at(tensor_name).pl_data_type);
}

int npu_set_pl_info(const npu_snapshot_t* snap, const std::string& tensor_name)
{
	uint32_t ip_idx;

	int err = npu_get_ip_from_timestamp(snap->pl_timestamp, &ip_idx);
	if (err)
		return err;

	npu_write_cfg(snap->pl.at(tensor_name).pl_cfg_base_addr,
	              (const uint32_t*)&snap->pl_info.at(snap->pl.at(tensor_name).pl_cfg_base_addr),
	              1,
	              ip_idx);

	return vart_ml_error::SUCCESS;
}

int npu_set_pl_addr(const npu_snapshot_t* snap, const std::string& tensor_name, size_t batchno, void* v_addr)
{
	uint64_t pl_addr;
	if ((pl_addr = npu_get_phy_addr_from_ddr_vaddr(v_addr)) == 0)
		pl_addr = snap->pl.at(tensor_name).pl_ddr_addrs[batchno].phy_addr;

	if (snap->debug_show_IO_address)
		vart_ml_log(LOG_INFO, "[VART] PL Addr @ 0x%012lx # %s[%zu]\n", pl_addr, tensor_name.c_str(), batchno);

	uint32_t ip_idx;

	int err = npu_get_ip_from_timestamp(snap->pl_timestamp, &ip_idx);
	if (err)
		return err;

	npu_write_cfg(snap->pl.at(tensor_name).pl_cfg_base_addr + snap->pl.at(tensor_name).pl_cfg_offset[batchno],
	              (const uint32_t*)&pl_addr,
	              2,
	              ip_idx);

	return vart_ml_error::SUCCESS;
}
