/**
 * @file vart_ml_runner.cpp
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
#include <filesystem>
#include <fstream>
#include <glob.h>
#include <iostream>
#include <string>

#include "io/io.h"
#include "npu_runner/npu_runner.h"
#include "onnx_runner/onnx_runner.h"
#include "utils/log.h"
#include "utils/vcd_stats.h"
#include "vart_ml_runner.hpp"
#include "vart_npu_tensor_impl.hpp" // For controlled NpuTensor implementation access via NpuTensorPrivAccess

using json = nlohmann::json;

// Get path to iriz directory.
static int get_model_path(const std::string& model_path, std::string& out)
{
	glob_t glob_p;

	out = model_path + "/";

	glob((model_path + "/*_embedded/").c_str(), GLOB_MARK | GLOB_NOSORT, NULL, &glob_p);
	if (glob_p.gl_pathc == 1)
		out = std::string(glob_p.gl_pathv[0]);
	else
	{
		glob((model_path + "/embedded_export/").c_str(), GLOB_MARK | GLOB_NOSORT, NULL, &glob_p);
		if (glob_p.gl_pathc == 1)
			out = std::string(glob_p.gl_pathv[0]);
	}

	globfree(&glob_p);

	return vart_ml_error::SUCCESS;
}

static int get_json(const std::string& json_path, json* parsed_json)
{
	// Open json file.
	std::ifstream f(json_path, std::ios_base::in);
	if (!f.is_open())
		return vart_ml_log_err_msg(
		    vart_ml_error::FILE_ACCESS_OPEN_FAILURE, "Failed opening json file %s\n", json_path.c_str());

	// Parse json file.
	*parsed_json = json::parse(f);

	return vart_ml_error::SUCCESS;
}

namespace vart
{
VartMLRunner::VartMLRunner(const std::string&                               model_path,
                           const std::unordered_map<std::string, std::any>& options)
    : Runner(model_path, options),
      model_path_(model_path),
      options_(options),
      batchSize_(),
      internal_buffers_(),
      stats_(new EmbeddedStats)
{
	const char* info;

	// Set log level.
	if (get_user_config("log.level", &info) == vart_ml_error::SUCCESS)
		set_log_lvl_from_str(info);

	// If no path was given, use the one from user config.
	if (model_path_.empty())
	{
		check_err(get_user_config("snapshot.directory", &info));
		model_path_ = std::string(info);
	}

	// Check if model path exists.
	if (!std::filesystem::exists(model_path_))
		throw std::runtime_error("ERROR: snapshot directory '" + model_path_ + "' not found.\n");

	check_err(get_model_path(model_path_, model_path_));

	// Get main json files.
	json main_json;
	check_err(get_json(model_path_ + "main.json", &main_json));
	json main_attr_json;
	check_err(get_json(model_path_ + "main_attr.json", &main_attr_json));

	// Get model name
	model_name_ = main_attr_json.at("__properties__").at("id");

	// Initialize embedded stats.
	stats_->update_network(model_name_, 1);

	// Register network for VCD stats.
	vcd_id_ = vcd_register_network(model_name_);

	// Get input external tensors names.
	names_in_ = main_attr_json["inputs"];

	// Get output external tensors names.
	std::vector<std::string> output_names;
	if (options.contains("output_names")
	    && !(output_names = std::any_cast<std::vector<std::string>>(options.at("output_names"))).empty())
	{
		// Ensure given output names matches model's output names. If not, throw runtime error.
		std::vector<json> names_out(names_out_.size());
		std::transform(
		    names_out_.begin(), names_out_.end(), names_out.begin(), [](const auto& s) -> json { return s; });
		if (!std::is_permutation(names_out.begin(), names_out.end(), main_attr_json["outputs"].begin()))
			throw std::runtime_error("ERROR: given output names does not match model's output names.\n");

		names_out_ = output_names;
	}
	else
		names_out_ = main_attr_json["outputs"];

	// Iterate on the whole main json to gather all nodes and tensors.
	std::vector<std::string> node_names;
	std::vector<std::string> arm_op_names;
	for (const auto& [key, value] : (std::map<std::string, json::value_type>)main_json)
		if (value.contains("execution"))
		{
			if (value.at("execution").at("device") == "ON_LOGIC")
			{
				auto type = value.at("layer").at("params").at("type");

				// Parse NPU node first to get the batch size as soon as we can.
				// XXX: Remove when the batch size will be handled on a per-node basis instead of globally.
				if (type == "CALL_SNAPSHOT")
					node_names.insert(node_names.begin(), key);
				else if (type == "CALL_ONNX")
					node_names.push_back(key);
				else
					throw std::runtime_error("ERROR: unknown call type:" + std::string(type) + "\n");
			}
			else if (value.at("execution").at("device") == "ON_ARM")
			{
				// Add entry to the translation maps.
				arm_ops_translations_in_[main_json[key].at("outputs")[0]] = main_json[key].at("inputs")[0];
				arm_ops_translations_out_[main_json[key].at("inputs")[0]] = main_json[key].at("outputs")[0];

				arm_op_names.push_back(key);
			}
		}

	// Iterate on arm_op_infos to update the NPU node.
	std::vector<struct arm_ops> arm_ops;
	for (const auto& arm_op_name : arm_op_names)
		if (parse_graph_arm_ops(arm_op_name, &main_json, arm_ops) != vart_ml_error::SUCCESS)
			throw std::runtime_error("ERROR: failed parsing ARM OP: " + arm_op_name);

	// initialize the vectors of inputs tensor
	cpu_input_tensors_.resize(names_in_.size());
	hw_input_tensors_.resize(names_in_.size());

	// initialize the vectors of inputs/outputs tensor
	cpu_output_tensors_.resize(names_out_.size());
	hw_output_tensors_.resize(names_out_.size());

	// Iterate on nodes to initialize nodes_.
	for (const auto& node_name : node_names)
		if (parse_graph_node(node_name, &main_json, arm_ops) != vart_ml_error::SUCCESS)
			throw std::runtime_error("ERROR: failed parsing node " + node_name);

	// Parse the thread pool size option.
	size_t nb_threads = 0;
	if (options.contains("nb_threads"))
	{
		try
		{
			nb_threads = std::any_cast<size_t>(options.at("nb_threads"));
		}
		catch (const std::bad_any_cast& e)
		{
			throw std::runtime_error("Failed to cast nb_threads option to unsigned integer.\n");
		}

		FpgaArchitecture arch;
		npu_get_architecture(&arch);
	}

	// Initialize thread pool.
	tpool_init(nb_threads);

	// Set stats to use wholeGraph step in execute.
	stats_->use_whole_graph();

	if (check_user_config("debug.dump_graph"))
		dump_graph();

	if (check_user_config("debug.dump_IOs"))
		std::filesystem::create_directory(std::string("/tmp/") + model_name_);
}

VartMLRunner::~VartMLRunner()
{
	tpool_destroy();

	for (auto& [execution_order, node] : nodes_)
	{
		(void)execution_order;

		if (node.execution_mode == NodeExecutionMode::ONNX)
		{
			onnx_destroy_node(*(struct onnx_context_node*)node.context_ptr);
			free((struct onnx_context_node*)node.context_ptr);
		}
		else
			npu_free_snapshot((npu_snapshot_t*)node.context_ptr);
	}

	if (onnx_context_global_ != NULL)
	{
		onnx_destroy(*(struct onnx_context_global*)onnx_context_global_);
		free(onnx_context_global_);
	}
}

NpuTensor VartMLRunner::allocate_npu_tensor(const NpuTensorInfo& info) const
{
	/* Allocate NpuTensor based on the provided NpuTensorInfo */
	if (info.size_in_bytes == 0)
		throw std::invalid_argument("Tensor size is zero for tensor: " + info.name + ".\n");

	if (info.tensor_type != TensorType::HW)
		throw std::invalid_argument("Only HW tensor allocation is supported.\n");

	void* raw = npu_malloc(info.size_in_bytes, 0);

	if (raw == nullptr)
		throw std::runtime_error("Fail to allocate buffer for tensor: " + info.name
		                         + ", Size: " + std::to_string(info.size_in_bytes) + " bytes.\n");

	auto  tensor   = NpuTensor(info, raw, MemoryType::XRT_BO);
	auto* pimpl    = NpuTensorPrivAccess::get_impl(tensor);
	pimpl->buffer_ = std::shared_ptr<void>(raw, [](void* p) { npu_free(npu_get_ddr_vaddr_from_vaddr(p)); });
	pimpl->is_wrapped_ = false;

	return tensor;
}

NpuTensor VartMLRunner::allocate_npu_tensor(const NpuTensorInfo& info, uint32_t memory_bank) const
{
	if (info.size_in_bytes == 0)
		throw std::invalid_argument("Tensor size is zero for tensor: " + info.name + ".\n");

	if (info.tensor_type != TensorType::HW)
		throw std::invalid_argument("Only HW tensor allocation is supported.\n");

	void* raw = npu_malloc(info.size_in_bytes, memory_bank);

	if (raw == nullptr)
		throw std::runtime_error("Fail to allocate buffer for tensor: " + info.name
		                         + ", Size: " + std::to_string(info.size_in_bytes) + " bytes.\n");

	auto  tensor   = NpuTensor(info, raw, MemoryType::XRT_BO);
	auto* pimpl    = NpuTensorPrivAccess::get_impl(tensor);
	pimpl->buffer_ = std::shared_ptr<void>(raw, [](void* p) { npu_free(npu_get_ddr_vaddr_from_vaddr(p)); });
	pimpl->is_wrapped_ = false;

	return tensor;
}

NpuTensor
VartMLRunner::allocate_sub_tensor(const NpuTensor& parent, const NpuTensorInfo& info, size_t offset) const
{
	if (info.size_in_bytes == 0)
		throw std::invalid_argument("Sub-tensor size is zero for tensor: " + info.name + ".\n");

	auto* parent_pimpl = NpuTensorPrivAccess::get_impl(parent);
	if (!parent_pimpl)
		throw std::invalid_argument(
		    "Failed to get NpuTensor implementation for parent tensor: " + parent.get_info().name + ".\n");

	if (parent_pimpl->is_wrapped_)
		throw std::invalid_argument("Parent tensor is not runner-allocated, cannot create sub-tensor: "
		                            + parent.get_info().name + ".\n");

	if (parent_pimpl->is_sub_tensor_)
		throw std::invalid_argument("Cannot create sub-tensor from a sub-tensor: " + parent.get_info().name
		                            + ".\n");

	if (offset + info.size_in_bytes > parent.get_info().size_in_bytes)
		throw std::invalid_argument(
		    "Sub-tensor exceeds parent tensor bounds. Parent: " + parent.get_info().name + ", parent size: "
		    + std::to_string(parent.get_info().size_in_bytes) + " bytes, offset: " + std::to_string(offset)
		    + ", sub-tensor size: " + std::to_string(info.size_in_bytes) + " bytes.\n");

	void* sub_raw = (parent.get_memory_type() == MemoryType::XRT_BO)
	                    ? npu_malloc_sub(npu_get_ddr_vaddr_from_vaddr(parent_pimpl->buffer_.get()),
	                                     offset,
	                                     info.size_in_bytes)
	                    : static_cast<uint8_t*>(parent_pimpl->buffer_.get()) + offset;

	if (sub_raw == nullptr)
		throw std::runtime_error("Failed to register sub-buffer in IO layer for tensor: " + info.name
		                         + ".\n");

	auto sub_buffer =
	    std::shared_ptr<void>(sub_raw, [](void* p) { npu_free(npu_get_ddr_vaddr_from_vaddr(p)); });

	auto  tensor          = NpuTensor(info, sub_buffer.get(), parent.get_memory_type());
	auto* pimpl           = NpuTensorPrivAccess::get_impl(tensor);
	pimpl->buffer_        = std::move(sub_buffer);
	pimpl->parent_buffer_ = std::shared_ptr<void>(parent_pimpl->buffer_);
	pimpl->is_wrapped_    = false;
	pimpl->is_sub_tensor_ = true;

	return tensor;
}

// # Runner

std::shared_ptr<Runner> RunnerFactory::create_runner(RunnerType         runner_type,
                                                     const std::string& model_path,
                                                     const std::unordered_map<std::string, std::any>& options)
{
	if (runner_type == RunnerType::VAIML)
		return std::make_shared<VartMLRunner>(model_path, options);

	// Handle other runner types or throw an error
	throw std::runtime_error("Unsupported runner type for Runner creation");
}

} // namespace vart

extern "C" vart::Runner* create_runner(vart::RunnerType                                 runner_type,
                                       const std::string&                               model_path,
                                       const std::unordered_map<std::string, std::any>& options)
{
	if (runner_type == vart::RunnerType::VAIML)
		return new vart::VartMLRunner(model_path, options);

	// Handle other runner types or throw an error
	throw std::runtime_error("Unsupported runner type for Runner creation");
}
