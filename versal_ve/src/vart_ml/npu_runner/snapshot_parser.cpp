/**
 * @file snapshot_parser.cpp
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

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>

#include "io/io.h"
#include "npu_runner.h"
#include "utils/fpga_info.h"
#include "utils/json_parser.h"
#include "utils/log.h"

#define NPU_MALLOC_ALIGN         4096ULL
#define SIZEUP_TO_ALIGNMENT(x)   (x + NPU_MALLOC_ALIGN)
#define GET_NEXT_ALIGNED_ADDR(x) ((x + NPU_MALLOC_ALIGN - 1) & ~(NPU_MALLOC_ALIGN - 1))

#define BATCHSIZE_MASK 0xffffffc0

using json = nlohmann::json;

static FpgaArchitecture arch;

size_t nb_input;
size_t nb_constant;
size_t nb_output;

static int parse_single_input(npu_tensor_t*                                 in,
                              std::string                                   name,
                              const json&                                   uploadValue,
                              size_t                                        batchsizePerCore,
                              const std::vector<std::pair<size_t, size_t>>& active_cores)
{
	in->name                         = name;
	const json&         ddrInterface = uploadValue["ddrInterface"];
	const json::array_t coreBlocks   = uploadValue["coreBlocks"].get<json::array_t>();

	in->coeff  = (float)ddrInterface["coef"];
	in->format = stringToFormat(ddrInterface["shape"].get<std::string>().c_str());

	if (arch == FpgaArchitecture::AIEML_V1C)
	{
		if (ddrInterface.contains("wrapper"))
			in->ddr_format = stringToFormat(ddrInterface["wrapper"]["ddr_format"].get<std::string>().c_str());
		else
			in->ddr_format = NC8HW8;

		if (ddrInterface["nbMapsTotal"] == 4)
		{
			if (in->ddr_format == NC8HW8)
				in->ddr_format = NC4HW4;
			else if (in->ddr_format == NHWC8)
				in->ddr_format = NHWC4;
		}
		else if (ddrInterface["nbMapsTotal"] < 4)
		{
			if (in->ddr_format == NC8HW8)
				in->ddr_format = NCHW;
			else if (in->ddr_format == NHWC8)
				in->ddr_format = NHWC;
		}
	}
	else
		in->ddr_format = NHW16C4WC;

	if (ddrInterface.contains("/quantizationElement/view_precision"_json_pointer)
	    && ddrInterface["quantizationElement"]["view_precision"].get<std::string>() != "UNKNOWN")
		in->data_type = stringToDataType(
		    ddrInterface["quantizationElement"]["input_precision"].get<std::string>().c_str());
	else
		in->data_type = stringToDataType(ddrInterface["vaiswMemoryType"].get<std::string>().c_str());

	in->ddrimgsize          = parse_uint(ddrInterface["ddrImgSizeInBytes"].get<std::string>(), 16);
	in->batchSize           = ddrInterface["nbImages"];
	in->batch_size_per_core = batchsizePerCore;

	if (arch == FpgaArchitecture::AIEML_V1C)
	{
		// There is only one system in V1C arch.
		in->ddr_addrs.emplace_back(std::vector<std::vector<addr>>(1, std::vector<addr>(in->batchSize)));
		in->nbuf_idx.emplace_back(std::vector<size_t>(in->batchSize));

		if (in->batch_size_per_core != 1)
			return vart_ml_log_err_msg(
			    vart_ml_error::CONFIG_SNAPSHOT_MALFORMED,
			    "Unexpected batch size per core %lu. Should be 1 for AIEML_V1C architecture.\n",
			    in->batch_size_per_core);

		for (size_t i = 0; i < in->batchSize; i++)
		{
			const json& coreBlock = coreBlocks[i];
			size_t      addr      = parse_uint(coreBlock["addr"].get<std::string>(), 16);
			size_t      phy_addr  = npu_get_phy_addr_from_snapshot_addr(addr);

			in->ddr_addrs[0][0][i] = npu_get_addr_from_phy_addr(phy_addr);

			if (coreBlock.contains("nbuf_idx"))
				in->nbuf_idx[0][i] = coreBlock["nbuf_idx"];
			else
				return vart_ml_log_err_msg(vart_ml_error::CONFIG_SNAPSHOT_MALFORMED,
				                           "Missing \"nbuff\" attribute in infos of input %lu\n",
				                           nb_input);

			vart_ml_log(LOG_DBG,
			            "[VART] Read input phy_addr 0x%011lx into nbuff index %lu\n",
			            in->ddr_addrs[0][0][i].phy_addr,
			            in->nbuf_idx[0][i]);
		}
	}
	else
	{
		/*
		 * In arch V1 and V2, there is one individual nbuf module per system.
		 */
		size_t nb_sys           = ddrInterface["deviceParameters"]["nbSystemsPerBoard"];
		size_t nb_cores_per_sys = ddrInterface["deviceParameters"]["nbCoresPerSystem"];

		in->ddr_addrs = std::vector<std::vector<std::vector<struct addr>>>(
		    nb_sys,
		    std::vector<std::vector<struct addr>>(nb_cores_per_sys,
		                                          std::vector<struct addr>(in->batch_size_per_core)));
		in->nbuf_idx = std::vector<std::vector<size_t>>(nb_sys, std::vector<size_t>(nb_cores_per_sys));

		for (size_t cb = 0; cb < coreBlocks.size(); cb++)
		{
			const auto& coreBlock = coreBlocks[cb];
			size_t      addr      = parse_uint(coreBlock["addr"].get<std::string>(), 16);
			size_t      phy_addr  = npu_get_phy_addr_from_snapshot_addr(addr);
			size_t      sys       = active_cores[cb].first;
			size_t      core      = active_cores[cb].second;

			for (size_t i = 0; i < in->batch_size_per_core; i++)
				in->ddr_addrs[sys][core][i] = npu_get_addr_from_phy_addr(phy_addr + i * in->ddrimgsize);

			if (coreBlock.contains("nbuf_idx"))
				in->nbuf_idx[sys][core] = coreBlock["nbuf_idx"];
			else
				return vart_ml_log_err_msg(vart_ml_error::CONFIG_SNAPSHOT_MALFORMED,
				                           "Missing \"nbuff\" attribute in infos of input %lu\n",
				                           nb_input);

			vart_ml_log(LOG_DBG,
			            "[VART] Read input phy_addr 0x%011lx into nbuff index %lu\n",
			            in->ddr_addrs[sys][core][0].phy_addr,
			            in->nbuf_idx[sys][core]);
		}
	}
	nb_input++;

	return vart_ml_error::SUCCESS;
}

static int
parse_single_constant(struct npu_constant_tensor* constant, std::string name, const json& constantValue)
{
	constant->name                    = name;
	const json&         ddr_interface = constantValue["ddr_interface"];
	const json::array_t blocks        = constantValue["blocks"].get<json::array_t>();

	constant->coeff  = (float)ddr_interface["coef"];
	constant->format = stringToFormat(ddr_interface["shape"].get<std::string>().c_str());
	if (ddr_interface.contains("wrapper"))
		constant->ddr_format =
		    stringToFormat(ddr_interface["wrapper"]["ddr_format"].get<std::string>().c_str());
	else
		constant->ddr_format = NC8HW8;

	if (ddr_interface.contains("/quantizationElement/view_precision"_json_pointer)
	    && ddr_interface["quantizationElement"]["view_precision"].get<std::string>() != "UNKNOWN")
		constant->data_type = stringToDataType(
		    ddr_interface["quantizationElement"]["input_precision"].get<std::string>().c_str());
	else
		constant->data_type = stringToDataType(ddr_interface["vaiswMemoryType"].get<std::string>().c_str());

	constant->ddrimgsize = parse_uint(ddr_interface["ddrImgSizeInBytes"].get<std::string>(), 16);
	constant->batchSize  = ddr_interface["nbImages"];

	constant->ddr_addrs.emplace_back(
	    std::vector<std::vector<struct addr>>(1, std::vector<struct addr>(constant->batchSize)));
	constant->nbuf_idx.emplace_back(std::vector<size_t>(constant->batchSize));

	for (size_t i = 0; i < constant->batchSize; i++)
	{
		const json& block    = blocks[i];
		size_t      addr     = parse_uint(block["addr"].get<std::string>(), 16);
		size_t      phy_addr = npu_get_phy_addr_from_snapshot_addr(addr);

		constant->ddr_addrs[0][0][i] = npu_get_addr_from_phy_addr(phy_addr);
		constant->nbuf_idx[0][i]     = block["nbuf_idx"];
	}
	nb_constant++;

	return vart_ml_error::SUCCESS;
}

static int check_snapshot_matching(const npu_snapshot_t* snap)
{
	uint32_t device_timestamp;
	npu_get_timestamp_from_ip(snap->ip_idx, &device_timestamp);
	// note: IPs order of env is not the same as the one for xrt idx

	char* ip_name0 = getenv("NPU_IP");
	char* ip_name1 = getenv("NPU_IP2");

	if (device_timestamp == snap->timestamp)
	{
		vart_ml_log(LOG_INFO,
		            "[VART] Found snapshot for IP %s IP on kernel %d.\n",
		            snap->boardname.empty() ? "\b" : snap->boardname.c_str(),
		            snap->ip_idx);
		return vart_ml_error::SUCCESS;
	}

	return vart_ml_log_err_msg(vart_ml_error::CONFIG_SNAPSHOT_MISMATCH,
	                           "Snapshot is for %s (0x%x) while available %s %s%s%s having timestamp 0x%x.\n",
	                           snap->boardname.c_str(),
	                           snap->timestamp,
	                           ip_name1 == NULL ? "IP is" : "IPs are",
	                           ip_name0 == NULL ? "unknown" : ip_name0,
	                           ip_name1 == NULL ? "" : " and ",
	                           ip_name1 == NULL ? "" : ip_name1,
	                           device_timestamp);
}

static int parse_inputs(npu_snapshot_t* snap, std::string snap_path)
{
	std::ifstream f(snap_path + "/snapshot.dump.uploadInfos");
	if (!f.is_open())
		return vart_ml_log_err_msg(vart_ml_error::FILE_ACCESS_OPEN_FAILURE,
		                           "Failed opening snapshot file %s/snapshot.dump.uploadInfos\n",
		                           snap_path.c_str());

	json table = json::parse(f);
	int  err;

	size_t batchsizePerCore = 1;
	if (arch == FpgaArchitecture::AIEML_V1C)
	{
		snap->active_cores.push_back({ 0, 0 });
		snap->nbSupra                    = (uint32_t)table["nbsupra"];
		snap->config.bf.supra_addr_ddr   = (uint32_t)table["supraAddr"] >> 4;
		snap->config.bf.supra_block_size = (uint32_t)table["supraSize"] >> 4;
	}
	else
	{
		// parse fields non related with inputs
		const auto& coreDescriptors = table["coreDescriptors"];
		batchsizePerCore            = (size_t)coreDescriptors["batchsize_per_core"];
		snap->index_mctrl_network   = (uint32_t)table["nbMasterCtrls"];
		snap->sublayer_count        = (uint32_t)table["nbSublayersInLastGroup"];

		for (size_t i = 0; i < coreDescriptors["cores"].size(); i++)
		{
			const auto& coreDesc = coreDescriptors["cores"][i];
			if ((int)coreDesc["board"] != 0)
			{
				f.close();
				return vart_ml_log_err(vart_ml_error::CONFIG_UNSUPPORTED_MULTI_BOARD);
			}

			size_t s = (size_t)coreDesc["sys"];
			size_t c = (size_t)coreDesc["core"];

			snap->active_cores.push_back({ s, c });
		}
	}

	// now, parse all inputs
	const auto& uploads = table["uploads"];

	snap->inputs.resize(uploads.size());
	if (snap->inputs.size() == 0)
	{
		f.close();
		return vart_ml_log_err_msg(vart_ml_error::CONFIG_SNAPSHOT_MALFORMED,
		                           "Malformed snapshot.dump.uploadInfos file.\n");
	}

	size_t i = 0;
	for (auto& x : uploads.items())
	{
		err = parse_single_input(&snap->inputs[i], x.key(), x.value(), batchsizePerCore, snap->active_cores);
		if (err)
		{
			f.close();
			return err;
		}

		if (snap->inputs[i].ddr_addrs.size() != npu_get_nbsystems(snap->ip_idx))
			return vart_ml_log_err_msg(
			    vart_ml_error::CONFIG_SNAPSHOT_MALFORMED,
			    "coreBlock count (%lu) for input %lu does not match system count (%lu).\n",
			    snap->inputs[i].ddr_addrs.size(),
			    i,
			    npu_get_nbsystems(snap->ip_idx));

		i++;
	}

	// finally, parse all constants
	const auto& constants = table["constants"];

	snap->constants.resize(constants.size());

	i = 0;
	for (auto& constant : constants.items())
	{
		err = parse_single_constant(&snap->constants[i++], constant.key(), constant.value());
		if (err)
		{
			f.close();
			return err;
		}
	}

	f.close();
	return vart_ml_error::SUCCESS;
}

static int parse_single_output(npu_tensor_t*                                 out,
                               std::string                                   name,
                               const json&                                   description,
                               const std::vector<std::pair<size_t, size_t>>& active_cores)
{
	out->name                        = name;
	const json::array_t coreBlocks   = description["coreBlocks"].get<json::array_t>();
	const json          ddrInterface = description["ddrInterface"];

	out->coeff  = ddrInterface["coef"];
	out->format = stringToFormat(ddrInterface["shape"].get<std::string>().c_str());

	if (arch == FpgaArchitecture::AIEML_V1C)
	{
		if (ddrInterface.contains("wrapper"))
			out->ddr_format =
			    stringToFormat(ddrInterface["wrapper"]["ddr_format"].get<std::string>().c_str());
		else
			out->ddr_format = NC8HW8;

		if (ddrInterface["nbMapsTotal"] == 4)
		{
			if (out->ddr_format == NC8HW8)
				out->ddr_format = NC4HW4;
			else if (out->ddr_format == NHWC8)
				out->ddr_format = NHWC4;
		}
		else if (ddrInterface["nbMapsTotal"] < 4)
		{
			if (out->ddr_format == NC8HW8)
				out->ddr_format = NCHW;
			else if (out->ddr_format == NHWC8)
				out->ddr_format = NHWC;
		}
	}
	else
		out->ddr_format = NHW16C4WC;

	if (ddrInterface.contains("/quantizationElement/view_precision"_json_pointer)
	    && ddrInterface["quantizationElement"]["view_precision"].get<std::string>() != "UNKNOWN")
		out->data_type = stringToDataType(
		    ddrInterface["quantizationElement"]["output_precision"].get<std::string>().c_str());
	else
		out->data_type = stringToDataType(ddrInterface["vaiswMemoryType"].get<std::string>().c_str());

	out->ddrimgsize = parse_uint(ddrInterface["ddrImgSizeInBytes"].get<std::string>(), 16);
	out->batchSize  = ddrInterface["nbImages"];

	if (arch == FpgaArchitecture::AIEML_V1C)
	{
		size_t nbImages = ddrInterface["nbImages"];

		// There is only one system in V1C arch.
		out->ddr_addrs.emplace_back(
		    std::vector<std::vector<struct addr>>(1, std::vector<struct addr>(out->batchSize)));
		out->nbuf_idx.emplace_back(std::vector<size_t>(out->batchSize));
		out->batch_size_per_core = 1;

		for (size_t i = 0; i < nbImages; ++i)
		{
			const json& coreBlock = coreBlocks[i];
			size_t      addr      = parse_uint(coreBlock["addr"].get<std::string>(), 16);
			size_t      phy_addr  = npu_get_phy_addr_from_snapshot_addr(addr);

			out->ddr_addrs[0][0][i] = npu_get_addr_from_phy_addr(phy_addr);

			if (coreBlock.contains("nbuf_idx"))
				out->nbuf_idx[0][i] = coreBlock["nbuf_idx"];
			else
				return vart_ml_log_err_msg(vart_ml_error::CONFIG_SNAPSHOT_MALFORMED,
				                           "Missing \"nbuff\" attribute in infos of output %lu\n",
				                           nb_output);

			vart_ml_log(LOG_DBG,
			            "[VART] Read output phy_addr 0x%011lx into nbuff index %lu\n",
			            out->ddr_addrs[0][0][i].phy_addr,
			            out->nbuf_idx[0][i]);
		}
	}
	else
	{
		/*
		 * In arch V1 and V2, there is one individual nbuf module per system.
		 */
		size_t nb_sys           = ddrInterface["deviceParameters"]["nbSystemsPerBoard"];
		size_t nb_cores_per_sys = ddrInterface["deviceParameters"]["nbCoresPerSystem"];

		/*
		 * Images are distributed equally over all systems, and within systems they are distributed equally
		 * over all cores. All cores have the same batch size, i.e. the batch size is bumped up to the max of
		 * batch size of each core.
		 */
		out->batch_size_per_core =
		    static_cast<size_t>(std::ceil((float)out->batchSize / nb_sys / nb_cores_per_sys));

		out->ddr_addrs = std::vector<std::vector<std::vector<struct addr>>>(
		    nb_sys,
		    std::vector<std::vector<struct addr>>(nb_cores_per_sys,
		                                          std::vector<struct addr>(out->batch_size_per_core)));
		out->nbuf_idx = std::vector<std::vector<size_t>>(nb_sys, std::vector<size_t>(nb_cores_per_sys));

		for (size_t cb = 0; cb < coreBlocks.size(); cb++)
		{
			const auto& coreBlock = coreBlocks[cb];
			size_t      addr      = parse_uint(coreBlock["addr"].get<std::string>(), 16);
			size_t      phy_addr  = npu_get_phy_addr_from_snapshot_addr(addr);
			size_t      sys       = active_cores[cb].first;
			size_t      core      = active_cores[cb].second;

			for (size_t i = 0; i < out->batch_size_per_core; i++)
				out->ddr_addrs[sys][core][i] = npu_get_addr_from_phy_addr(phy_addr + i * out->ddrimgsize);

			if (coreBlock.contains("nbuf_idx"))
				out->nbuf_idx[sys][core] = coreBlock["nbuf_idx"];
			else
				return vart_ml_log_err_msg(vart_ml_error::CONFIG_SNAPSHOT_MALFORMED,
				                           "Missing \"nbuff\" attribute in infos of input %lu\n",
				                           nb_input);

			vart_ml_log(LOG_DBG,
			            "[VART] Read output phy_addr 0x%011lx into nbuff index %lu\n",
			            out->ddr_addrs[sys][core][0].phy_addr,
			            out->nbuf_idx[sys][core]);
		}
	}
	nb_output++;

	return vart_ml_error::SUCCESS;
}

static int parse_outputs(npu_snapshot_t* snap, std::string snap_path)
{
	std::ifstream f(snap_path + "/snapshot.dump.downloadInfos");
	if (!f.is_open())
		return vart_ml_log_err_msg(vart_ml_error::FILE_ACCESS_OPEN_FAILURE,
		                           "Failed opening snapshot file %s/snapshot.dump.downloadInfos.\n",
		                           snap_path.c_str());

	json table = json::parse(f);

	snap->outputs.resize(table.size());
	if (snap->outputs.size() == 0)
	{
		f.close();
		return vart_ml_log_err_msg(vart_ml_error::CONFIG_SNAPSHOT_MALFORMED,
		                           "Malformed snapshot.dump.downloadInfos file.\n");
	}

	size_t outputno = 0;
	int    err;
	for (auto& x : table.items())
	{
		if ((err = parse_single_output(&snap->outputs[outputno], x.key(), x.value(), snap->active_cores)))
			break;

		if (snap->outputs[outputno].ddr_addrs.size() != npu_get_nbsystems(snap->ip_idx))
			return vart_ml_log_err_msg(
			    vart_ml_error::CONFIG_SNAPSHOT_MALFORMED,
			    "coreBlock count (%lu) for output %lu does not match system count (%lu).\n",
			    snap->outputs[outputno].ddr_addrs.size(),
			    outputno,
			    npu_get_nbsystems(snap->ip_idx));

		outputno++;
	}

	f.close();
	return err;
}

static int parse_infos(npu_snapshot_t* snap, std::string snap_path)
{
	std::ifstream f(snap_path + "/snapshot.dump.infos");

	// Expect dump.infos file in snapshot
	if (!f.is_open())
		return vart_ml_log_err_msg(vart_ml_error::FILE_ACCESS_OPEN_FAILURE,
		                           "Failed opening snapshot file %s/snapshot.dump.infos\n",
		                           snap_path.c_str());

	json table      = json::parse(f);
	snap->boardname = table["boardname"];

	// Read the timestamp as string
	std::string timestamp_str = table["timestamp"];
	// Drop leading "0x" or "0X"
	if (timestamp_str.rfind("0x", 0) == 0 || timestamp_str.rfind("0X", 0) == 0)
		timestamp_str = timestamp_str.substr(2);

	snap->timestamp = static_cast<uint32_t>(parse_uint(timestamp_str, 16));

	// Extract config and temp area layout
	if (table.contains("ddr"))
	{
		const json::array_t ddr = table["ddr"].get<json::array_t>();

		for (size_t i = 0; i < ddr.size(); i++)
		{
			if (ddr[i].contains("data_addr"))
				snap->tmp_area_addr_in_snap.push_back(parse_uint(ddr[i]["data_addr"].get<std::string>()));

			if (ddr[i].contains("data_size"))
				snap->tmp_area_size.push_back(parse_uint(ddr[i]["data_size"].get<std::string>().c_str()));

			if (ddr[i].contains("config_addr"))
				snap->config_addr_in_snap.push_back(
				    parse_uint(ddr[i]["config_addr"].get<std::string>().c_str()));

			if (ddr[i].contains("config_size"))
				snap->config_size.push_back(parse_uint(ddr[i]["config_size"].get<std::string>().c_str()));

			if (ddr[i].contains("constant_addr"))
				snap->constant_addr_in_snap.push_back(
				    parse_uint(ddr[i]["constant_addr"].get<std::string>().c_str()));

			if (ddr[i].contains("constant_size"))
				snap->constant_size.push_back(parse_uint(ddr[i]["constant_size"].get<std::string>().c_str()));
		}
	}

	f.close();

	return vart_ml_error::SUCCESS;
}

static int allocate_config_and_temp_areas(npu_snapshot_t* snap)
{
	int err;

	// Check that the snapshot config area info count matches ddr count
	if (snap->config_addr_in_snap.size() != npu_get_nbddrs() || snap->config_size.size() != npu_get_nbddrs())
		return vart_ml_log_err_msg(vart_ml_error::CONFIG_SNAPSHOT_MALFORMED,
		                           "The number of configuration areas extracted from the snapshot (addrs: "
		                           "%lu, sizes: %lu) does not match ddr count (%lu).\n",
		                           snap->config_addr_in_snap.size(),
		                           snap->config_size.size(),
		                           npu_get_nbddrs());

	// Constants are placed before the config, need to allocate space for them
	if (!snap->constant_size.empty())
		for (size_t ddr_index = 0; ddr_index < snap->constant_size.size(); ddr_index++)
		{
			snap->config_size[ddr_index] += snap->constant_size[ddr_index];

			/*
			 * If there are constants in the snapshot, set the config address to the constant address as the
			 * constants are part of the config area.
			 * Later on, the constants will be fully integrated in the config and these lines wont be needed
			 * anymore.
			 */
			if (snap->constant_size[ddr_index])
				snap->config_addr_in_snap[ddr_index] = snap->constant_addr_in_snap[ddr_index];
		}

	// Append zeros to tmp info vectors if necessary
	snap->tmp_area_addr_in_snap.resize(npu_get_nbddrs(), 0);
	snap->tmp_area_size.resize(npu_get_nbddrs(), 0);

	// These vectors will contain the allocated addresses that will actually be used
	snap->config_addr.resize(npu_get_nbddrs());
	snap->tmp_area_addr.resize(npu_get_nbddrs());

	/*
	 * Static config area allocation - allocate the entire DDR to execute snapshot using dumped
	 * in/out/tmp/config addresses.
	 */
	if (!snap->is_nbuff_en)
	{
		snap->context_addr.resize(npu_get_nbddrs());
		snap->context_size.clear();

		for (size_t i = 0; i < npu_get_nbddrs(); i++)
		{
			// Reserve the entire DDR
			snap->context_size.push_back(npu_get_extmemlen(i));
			err = npu_allocate_memory(snap->context_size[i], i, &snap->context_addr[i]);
			if (err)
				return err;

			if (arch == AIEML_V1C)
			{
				snap->config_addr[i]   = npu_get_addr_from_phy_addr(snap->config_addr_in_snap[i]);
				snap->tmp_area_addr[i] = npu_get_addr_from_phy_addr(snap->tmp_area_addr_in_snap[i]);
			}
			else
			{
				snap->config_addr[i]   = npu_get_addr_from_idx_and_offset(i, snap->config_addr_in_snap[i]);
				snap->tmp_area_addr[i] = npu_get_addr_from_idx_and_offset(i, snap->tmp_area_addr_in_snap[i]);
			}
		}
	}
	else // Dynamic allocation, i.e. nbuff supported
	{
		for (size_t i = 0; i < npu_get_nbddrs(); i++)
		{
			// Dynamic config area allocation
			err = npu_allocate_memory(SIZEUP_TO_ALIGNMENT(snap->config_size[i]), i, &snap->config_addr[i]);
			if (err)
				return err;

			uint64_t phy_addr = snap->config_addr[i].phy_addr;

			snap->config_addr[i].offset = GET_NEXT_ALIGNED_ADDR(phy_addr) - phy_addr;
			snap->config_addr[i].phy_addr += snap->config_addr[i].offset;
			snap->config_addr[i].nbuff_conf_val =
			    npu_get_nbuff_conf_val_from_phy(snap->config_addr[i].phy_addr);

			// Prepare master control register values
			snap->config.reg[3 + i] = snap->config_addr[i].phy_addr - npu_get_extmemBaseAddr(i);

			if (snap->tmp_area_size[i] != 0)
			{
				err = npu_allocate_memory(
				    SIZEUP_TO_ALIGNMENT(snap->tmp_area_size[i]), i, &snap->tmp_area_addr[i]);
				if (err)
					return err;

				uint64_t tmp_phy_addr = snap->tmp_area_addr[i].phy_addr;

				snap->tmp_area_addr[i].offset = GET_NEXT_ALIGNED_ADDR(tmp_phy_addr) - tmp_phy_addr;
				snap->tmp_area_addr[i].phy_addr += snap->tmp_area_addr[i].offset;
				snap->tmp_area_addr[i].nbuff_conf_val =
				    npu_get_nbuff_conf_val_from_phy(snap->tmp_area_addr[i].phy_addr);
			}
			else
			{
				// Make temp area point at the end of the DDR
				size_t phy_addr        = npu_get_extmemBaseAddr(i) + npu_get_extmemlen(i);
				snap->tmp_area_addr[i] = npu_get_addr_from_phy_addr(phy_addr);
			}
		}

		// Trim trailing entries with no tmp area
		for (size_t i = snap->tmp_area_addr.size(); i > 0; i--)
		{
			if (snap->tmp_area_size[i - 1] == 0)
			{
				snap->tmp_area_size.resize(i - 1);
				snap->tmp_area_addr.resize(i - 1);
			}
		}
	}

	vart_ml_log(LOG_INFO, "[VART] Allocated config area in DDR: \tAddr = [");
	for (size_t i = 0; i < npu_get_nbddrs(); i++)
		vart_ml_log(LOG_INFO, " %#14lx,", snap->config_addr[i].phy_addr);
	vart_ml_log(LOG_INFO, "\b ] \tSize = [");
	for (size_t i = 0; i < npu_get_nbddrs(); i++)
		vart_ml_log(LOG_INFO, " %#10lx,", snap->config_size[i]);
	vart_ml_log(LOG_INFO, "\b]\n");

	vart_ml_log(LOG_INFO, "[VART] Allocated tmp area in DDR: \tAddr = [");
	for (size_t i = 0; i < snap->tmp_area_addr.size(); i++)
		vart_ml_log(LOG_INFO, " %#14lx,", snap->tmp_area_addr[i].phy_addr);
	vart_ml_log(LOG_INFO, "\b ] \tSize = [");
	for (size_t i = 0; i < snap->tmp_area_addr.size(); i++)
		vart_ml_log(LOG_INFO, " %#10lx,", snap->tmp_area_size[i]);
	vart_ml_log(LOG_INFO, "\b]\n");

	return vart_ml_error::SUCCESS;
}

static int parse_pl(npu_snapshot_t* snap, std::string snap_path)
{
	if (arch != AIEML_V1C || check_user_config("plstream.disable"))
		return vart_ml_error::SUCCESS;

	std::string pl_path     = snap_path + "/pl.json";
	char*       pl_json_env = getenv("VAISW_PL_JSON_PATH");
	if (pl_json_env != NULL)
		pl_path = pl_json_env;

	std::ifstream f(pl_path);
	if (!f.is_open())
		return vart_ml_error::SUCCESS;

	json table = json::parse(f);

	snap->pl_timestamp = (uint32_t)table["timestamp"];

	const auto& inputs = table["inputs"];

	for (auto& x : inputs.items())
		for (size_t i = 0; i < snap->outputs.size(); i++)
			if (snap->outputs[i].name == x.key())
				for (size_t j = 0; j < x.value().size(); j++)
					// TODO: fix nbuf_idx dim1 indexing to enable PP in V2
					snap->pl[x.key()].pl_nbuf_addr[snap->outputs[i].nbuf_idx[0][j]] =
					    parse_uint(x.value()[j].get<std::string>(), 16);

	const auto& outputs = table["outputs"];

	for (auto& x : outputs.items())
	{
		snap->pl[x.key()].pl_data_type =
		    stringToDataType(outputs[x.key()]["type"].get<std::string>().c_str());

		for (auto& s : outputs[x.key()]["shape"])
			snap->pl[x.key()].pl_shape.push_back(s);

		for (auto& s : outputs[x.key()]["strides"])
			snap->pl[x.key()].pl_strides.push_back(s);
		snap->pl[x.key()].pl_buff_nbytes = snap->pl[x.key()].pl_strides[0];

		snap->pl[x.key()].pl_cfg_base_addr = parse_uint(outputs[x.key()]["baseaddr"].get<std::string>(), 16);

		for (auto& offset : outputs[x.key()]["offset"])
		{
			struct addr ddr_addr;

			int err = npu_allocate_memory(snap->pl[x.key()].pl_buff_nbytes, 0, &ddr_addr);
			if (err)
				return err;

			snap->pl[x.key()].pl_cfg_offset.push_back(parse_uint(offset.get<std::string>(), 16));
			snap->pl[x.key()].pl_ddr_addrs.push_back(ddr_addr);
		}

		snap->pl_info[snap->pl[x.key()].pl_cfg_base_addr] &= BATCHSIZE_MASK;
		snap->pl_info[snap->pl[x.key()].pl_cfg_base_addr] += snap->pl[x.key()].pl_cfg_offset.size();
		snap->pl_info[snap->pl[x.key()].pl_cfg_base_addr] += 1 << 6;
	}

	return vart_ml_error::SUCCESS;
}

static void dump_srvbus_config(const npu_snapshot_t* snap)
{
	vart_ml_log(LOG_INFO, "SRVBUS CONFIG DUMP: %lu entries\n", snap->srv_write_sequence.size());
	vart_ml_log(LOG_INFO, "        OFFSET <-      VALUE\n");
	vart_ml_log(LOG_INFO, "----------------------------\n");

	for (const auto& kv : snap->srv_write_sequence)
	{
		uint32_t offset = static_cast<uint32_t>(kv.first);
		uint32_t data   = static_cast<uint32_t>(kv.second);

		vart_ml_log(LOG_INFO, "W @ 0x%08x <- 0x%08x\n", offset * sizeof(uint32_t), data);
	}

	vart_ml_log(LOG_INFO, "----------------------------\n");
}

int npu_parse_snapshot(const std::string& snap_path, npu_snapshot_t** snap)
{
	npu_snapshot_t* snap_tmp = new npu_snapshot_t();
	int             err;
	const char*     info;

	if (snap_path.empty() || snap_path[0] == '\0')
	{
		err = get_user_config("snapshot.directory", &info);
		if (err)
		{
			delete snap_tmp;
			return err;
		}
		snap_tmp->path = info;

		if (snap_path.empty())
		{
			delete snap_tmp;
			return vart_ml_log_err(vart_ml_error::CONFIG_MISSING_SNAPSHOT_DIR);
		}
	}
	else
		snap_tmp->path.assign(snap_path);

	snap_tmp->debug_show_IO_address = check_user_config("debug.show_io_addr");
	snap_tmp->checkConfig           = check_user_config("snapshot.checkConfig");
	snap_tmp->skipTimestampCheck    = check_user_config("debug.skip_timestamp_check");
	snap_tmp->is_nbuff_en           = !check_user_config("debug.forceSnapshotBuffer");

	err = npu_create_ip_context();
	if (err)
	{
		delete snap_tmp;
		return err;
	}

	err = npu_connect_peripherals();
	if (err)
		return err;

	err = parse_infos(snap_tmp, snap_tmp->path);
	if (err)
	{
		delete snap_tmp;
		return err;
	}

	/* Get IP index based on snapshot timestamp */
	if (snap_tmp->skipTimestampCheck)
	{
		vart_ml_log(LOG_INFO, "[VART] Timestamp check skipped\n");
		err = npu_get_ip(false, &snap_tmp->ip_idx);
		if (err)
			return err;
	}
	else
	{
		err = npu_get_ip_from_timestamp(snap_tmp->timestamp, &snap_tmp->ip_idx);
		if (err)
			snap_tmp->ip_idx = 0; // let check_snapshot returning the error message

		err = check_snapshot_matching(snap_tmp);
		if (err)
			return err;
	}

	err = npu_connect_ip(snap_tmp->ip_idx);
	if (err)
		return err;

	err = npu_get_architecture(&arch);
	if (err)
		return err;

	snap_tmp->arch = arch;

	FpgaFamily family;
	err = npu_get_fpgafamily(&family);
	if (err)
		return err;

	snap_tmp->fpgaFamily = family;

	err = allocate_config_and_temp_areas(snap_tmp);
	if (err)
		return err;

	nb_input  = 0;
	nb_output = 0;

	err = parse_inputs(snap_tmp, snap_tmp->path);
	if (err)
	{
		delete snap_tmp;
		return err;
	}

	err = parse_outputs(snap_tmp, snap_tmp->path);
	if (err)
	{
		delete snap_tmp;
		return err;
	}

	err = parse_pl(snap_tmp, snap_tmp->path);
	if (err)
	{
		delete snap_tmp;
		return err;
	}

	if (!snap_tmp->checkConfig)
	{
		err = npu_check_cores(snap_tmp);
		if (err)
		{
			delete snap_tmp;
			return err;
		}
	}

	/* Remove the extra path to print only the path to the root folder */
	std::string path_to_print = snap_tmp->path.substr(0, snap_tmp->path.find_last_of("/"));
	if (path_to_print.substr(path_to_print.find_last_of("/") + 1) == "embedded_export")
		path_to_print = path_to_print.substr(0, path_to_print.find_last_of("/"));
	vart_ml_log(LOG_INFO, "[VART] Parsing snapshot %s\n", path_to_print.c_str());

	err = npu_run_snapshot(snap_tmp);
	if (err)
	{
		delete snap_tmp;
		return err;
	}

	if (check_user_config("dump.srvbus_config"))
		dump_srvbus_config(snap_tmp);

	if (!npu_check_asserts(snap_tmp->ip_idx))
	{
		delete snap_tmp;
		return vart_ml_log_err(vart_ml_error::DEVICE_SYSTEM_CHECK_FAILURE);
	}

	if (snap_tmp->checkConfig)
	{
		err = npu_check_cores(snap_tmp);
		if (err)
		{
			delete snap_tmp;
			return err;
		}
	}

	err = npu_is_interrupt_en(&snap_tmp->interrupt_en);
	if (err)
	{
		delete snap_tmp;
		return err;
	}

	err = npu_set_clock_state(snap_tmp->ip_idx);
	if (err)
	{
		delete snap_tmp;
		return err;
	}

	*snap = snap_tmp;
	return vart_ml_error::SUCCESS;
}

void npu_free_snapshot(npu_snapshot_t* snap)
{
	for (size_t i = 0; i < npu_get_nbddrs(); i++)
	{
		if (snap->is_nbuff_en)
		{
			npu_free(snap->config_addr[i].ddr_vaddr);

			if (i < snap->tmp_area_addr.size())
				npu_free(snap->tmp_area_addr[i].ddr_vaddr);
		}
		else if (npu_is_xrt_en())
			npu_free(snap->context_addr[i].ddr_vaddr);
	}

	for (auto& pl : snap->pl)
		for (size_t b = 0; b < pl.second.pl_ddr_addrs.size(); b++)
			npu_free(pl.second.pl_ddr_addrs[b].ddr_vaddr);

	npu_disconnect_ip(snap->ip_idx);

	delete snap;
}
