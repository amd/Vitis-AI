/**
 * @file snapshot_parser.cpp
 *
 * @copyright Copyright 2025 Advanced Micro Devices Inc.
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

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>
#include <unordered_set>

#include "io/io.h"
#include "npu_runner/npu_runner.h"
#include "onnx_runner/onnx_runner.h"
#include "utils/log.h"
#include "vart_ml_runner.hpp"

using json = nlohmann::json;

namespace vart
{
static void node_set_execution_mode(struct node_desc& node, const std::string& execution_mode)
{
	if (execution_mode == "CALL_ONNX")
		node.execution_mode = NodeExecutionMode::ONNX;
	else
	{
		if (execution_mode != "CALL_SNAPSHOT")
		{
			vart_ml_log_err_msg(vart_ml_error::CONFIG_IRIZ_MALFORMED,
			                    "Invalid subgraph type: `%s'.\n",
			                    execution_mode.c_str());
			vart_ml_log(LOG_WARN, "Defaults to NPU node.\n");
		}

		node.execution_mode = NodeExecutionMode::NPU;
	}
}

static std::string name_translate(const std::map<std::string, std::string>& translations,
                                  const std::string&                        name)
{
	std::string res = name;

	for (size_t i = 0; i <= translations.size(); i++)
	{
		auto translation = translations.find(res);
		if (translation == translations.end() || translation->second == res)
			return res;
		res = translation->second;
	}

	vart_ml_log(LOG_ERR, "Cycle detected in name translations for '%s'\n", name.c_str());
	return name;
}

// Try to resolve arm op as a view, return true if it worked, false otherwise.
static bool exec_arm_op_view(struct arm_ops& arm_op, NpuTensorInfo& tensor)
{
	std::vector<uint32_t> old_strides = tensor.strides;

	switch (arm_op.type)
	{
	case ArmOps::SLICE:
		// If slice does not starts at 0 or does not have a step of 1, it cannot be a view.
		if ((std::any_cast<uint32_t>(arm_op.parameter["starts"]) != 0)
		    || (std::any_cast<uint32_t>(arm_op.parameter["strides"]) != 1))
			return false;

		// Set the targeted dimension to the given end.
		tensor.shape[std::any_cast<uint32_t>(arm_op.parameter["axes"])] =
		    std::any_cast<uint32_t>(arm_op.parameter["ends"]);

		return true;

	case ArmOps::RESHAPE:
		// We only support ArmOps view for reshape that merge 2 dimensions.
		if (arm_op.output_shape.size() != old_strides.size() - 1)
			return false;

		// Set tensor's infos to arm op's output's.
		tensor.shape         = arm_op.output_shape;
		tensor.memory_layout = arm_op.output_memory_layout;

		// Recompute the memory strides using the new memory shape. Can not use arm op's strides as they
		// represent contiguous data as we can have non-contiguous data when a SLICE has been resolved as view
		// before the RESHAPE.
		// XXX: Assuming that height and width dimensions are merged.
		if (arm_op.input_memory_layout == MemoryLayout::NHWC)
			tensor.strides = std::vector<uint32_t>({ old_strides[0], old_strides[2], old_strides[3] });
		else if (arm_op.input_memory_layout == MemoryLayout::NCHW)
			tensor.strides = std::vector<uint32_t>({ old_strides[0], old_strides[1], old_strides[3] });
		else
			throw std::runtime_error("Unsupported reshape input layout: "
			                         + std::string(to_string(arm_op.input_memory_layout)) + ".\n");

		return true;

	case ArmOps::TRANSPOSE:
		// We only support ArmOps view for transposes on 3 dimensions shapes.
		if (arm_op.output_shape.size() != 3)
			return false;

		// Set tensor's infos to arm op's output's.
		tensor.shape         = arm_op.output_shape;
		tensor.memory_layout = arm_op.output_memory_layout;

		for (size_t j = 0; j < old_strides.size(); j++)
			old_strides[j] = tensor.strides[std::any_cast<std::vector<int>>(arm_op.parameter["perm"])[j]];
		tensor.strides = old_strides;

		return true;

	default:
		return false;
	}
}

int VartMLRunner::build_arm_op_in(struct node_desc&            node,
                                  const std::string            input_name,
                                  std::vector<struct arm_ops>& arm_ops,
                                  std::vector<struct arm_ops>& arm_ops_in,
                                  const std::string&           tensor_name)
{
	// If arm_ops_in is not empty, arm ops have already been built for this input.
	if (!arm_ops_in.empty())
		return vart_ml_error::SUCCESS;

	std::string arm_op_input_name = input_name;

	size_t i = 0;
	while (i < arm_ops.size())
	{
		if (arm_ops[i].input_name == arm_op_input_name)
		{
			arm_ops_in.push_back(arm_ops[i]);

			// Look for next ARM op
			arm_op_input_name = arm_ops[i].output_name;

			// Start looking from the beginning again
			i = 0;
		}
		else
			i++;
	}

	if (arm_ops_in.empty())
		return vart_ml_error::SUCCESS;

	NpuTensorInfo& cpu_tensor = get_tensor_info_by_name(tensor_name, TensorType::CPU);
	NpuTensorInfo& hw_tensor  = get_tensor_info_by_name(tensor_name, TensorType::HW);

	hw_tensor.size       = arm_ops_in.back().output_size;
	hw_tensor.shape      = arm_ops_in.back().output_shape;
	hw_tensor.strides    = arm_ops_in.back().output_strides;
	hw_tensor.strides[0] = hw_tensor.size_in_bytes / get_data_type_size(hw_tensor.data_type);

	if (hw_tensor.shape.size() == 3)
		hw_tensor.memory_layout = MemoryLayout::NHW;
	else if (hw_tensor.shape.size() == 2)
		hw_tensor.memory_layout = MemoryLayout::NC;
	else if (node.execution_mode == NodeExecutionMode::NPU)
		hw_tensor.memory_layout = arm_ops_in.back().output_memory_layout;

	// Set CPU tensor's shape, strides, and layout using the first arm op's input.
	cpu_tensor.shape         = arm_ops_in.front().input_shape;
	cpu_tensor.strides       = arm_ops_in.front().input_strides;
	cpu_tensor.memory_layout = arm_ops_in.front().input_memory_layout;

	// Optimize ArmOps
	MemoryLayout inputs_format = cpu_tensor.memory_layout;
	if (options_.contains("in_shape_format"))
		inputs_format = to_memory_layout(std::any_cast<std::string>(options_.at("in_shape_format")));

	if (is_external_input(tensor_name) && inputs_format != cpu_tensor.memory_layout)
	{
		// Check if the Transposes are needed
		std::vector<int> perm;
		bool             remove_transpose = false;
		for (i = 0; i < arm_ops_in.size(); i++)
		{
			if (arm_ops_in[i].type == ArmOps::TRANSPOSE)
			{
				// If user format is already correct, we can remove the transpose
				if (inputs_format == arm_ops_in[i].output_memory_layout)
				{
					cpu_tensor.memory_layout = inputs_format;

					perm = std::any_cast<std::vector<int>>(arm_ops_in[i].parameter["perm"]);

					std::vector<uint32_t> shape_t(cpu_tensor.shape.size());
					std::vector<uint32_t> strides_t(cpu_tensor.strides.size());

					for (size_t j = 0; j < cpu_tensor.shape.size(); j++)
						shape_t[j] = cpu_tensor.shape[perm[j]];

					strides_t[cpu_tensor.strides.size() - 1] = 1;
					for (size_t j = cpu_tensor.strides.size() - 1; j > 0; j--)
						strides_t[j - 1] = shape_t[j] * strides_t[j];

					cpu_tensor.shape   = shape_t;
					cpu_tensor.strides = strides_t;

					remove_transpose = true;
					break;
				}
			}
			else if (arm_ops_in[i].type == ArmOps::SLICE)
				break;
		}

		// Remove all unnecessary transpose
		if (remove_transpose)
		{
			for (ssize_t j = i; j >= 0; j--)
				if (arm_ops_in[j].type == ArmOps::TRANSPOSE)
					arm_ops_in.erase(arm_ops_in.begin() + j);
				else if (arm_ops_in[j].type == ArmOps::PAD)
				{
					// Permute PAD's input shape through the removed transpose's perm.
					std::vector<uint32_t> permuted_shape(arm_ops_in[j].input_shape.size());
					for (size_t p = 0; p < perm.size(); p++)
						permuted_shape[p] = arm_ops_in[j].input_shape[perm[p]];
					arm_ops_in[j].input_shape = permuted_shape;

					arm_ops_in[j].input_strides[arm_ops_in[j].input_strides.size() - 1] = 1;
					for (ssize_t p = arm_ops_in[j].input_strides.size() - 2; p >= 0; p--)
						arm_ops_in[j].input_strides[p] =
						    arm_ops_in[j].input_strides[p + 1] * arm_ops_in[j].input_shape[p + 1];

					// Compute new padding with transposition
					std::vector<uint32_t> old_padding =
					    std::any_cast<std::vector<uint32_t>>(arm_ops_in[j].parameter["padding"]);
					std::vector<uint32_t> new_padding(old_padding.size());
					for (size_t p = 1; p < perm.size(); p++)
					{
						new_padding[p * 2 - 2] = old_padding[2 * perm[p] - 2];
						new_padding[p * 2 - 1] = old_padding[2 * perm[p] - 1];
					}
					arm_ops_in[j].parameter["padding"] = new_padding;

					// Compute new output shape and strides
					arm_ops_in[j].output_shape = arm_ops_in[j].input_shape;
					for (size_t p = 1; p < arm_ops_in[j].output_shape.size(); p++)
						arm_ops_in[j].output_shape[p] =
						    arm_ops_in[j].output_shape[p] + new_padding[p * 2 - 2] + new_padding[p * 2 - 1];
					for (ssize_t p = arm_ops_in[j].output_shape.size() - 2; p >= 0; p--)
						arm_ops_in[j].output_strides[p] =
						    arm_ops_in[j].output_strides[p + 1] * arm_ops_in[j].output_shape[p + 1];
				}
		}
		else
			return vart_ml_log_err_msg(vart_ml_error::CONFIG_BAD_ARGUMENT,
			                           "Cannot set shape format %s for tensor %s.\n",
			                           to_string(inputs_format).data(),
			                           tensor_name.c_str());
	}

	if (check_user_config("debug.skipArmOpsOptimization"))
		return vart_ml_error::SUCCESS;

	// Check if Transpose and channel padding ArmOps can be merged
	while (i < arm_ops_in.size())
	{
		if (arm_ops_in[i].type == ArmOps::TRANSPOSE)
		{
			if (i != 0 && arm_ops_in[i - 1].type == ArmOps::PAD)
			{
				arm_ops_in[i].input_shape   = arm_ops_in[i - 1].input_shape;
				arm_ops_in[i].input_strides = arm_ops_in[i - 1].input_strides;
				// Erase unnecessary
				arm_ops_in.erase(arm_ops_in.begin() + i - 1);
				// Start looking from the beginning again.
				i = 0;
				continue;
			}

			if ((i < arm_ops_in.size() - 1) && arm_ops_in[i + 1].type == ArmOps::PAD)
			{
				arm_ops_in[i].output_shape   = arm_ops_in[i + 1].output_shape;
				arm_ops_in[i].output_strides = arm_ops_in[i + 1].output_strides;
				// Erase unnecessary
				arm_ops_in.erase(arm_ops_in.begin() + i + 1);
				// Start looking from the beginning again.
				i = 0;
				continue;
			}
		}
		i++;
	}

	return vart_ml_error::SUCCESS;
}

int VartMLRunner::build_arm_op_out(struct node_desc&            node,
                                   const std::string            output_name,
                                   std::vector<struct arm_ops>& arm_ops,
                                   std::vector<struct arm_ops>& arm_ops_out,
                                   const std::string&           tensor_name)
{
	// If arm_ops_out is not empty, arm ops have already been built for this output.
	if (!arm_ops_out.empty())
		return vart_ml_error::SUCCESS;

	// Check if we must resolve arm op views during parsing.
	bool arm_op_views = false;
	if (options_.contains("arm_op_views"))
		arm_op_views = std::any_cast<bool>(options_.at("arm_op_views"));

	std::string arm_op_input_name = output_name;

	NpuTensorInfo& hw_tensor = get_tensor_info_by_name(tensor_name, TensorType::HW);

	size_t i = 0;
	while (i < arm_ops.size())
	{
		if (arm_ops[i].input_name == arm_op_input_name)
		{
			if (arm_ops_out.empty())
			{
				hw_tensor.size       = arm_ops[i].input_size;
				hw_tensor.shape      = arm_ops[i].input_shape;
				hw_tensor.strides    = arm_ops[i].input_strides;
				hw_tensor.strides[0] = hw_tensor.size_in_bytes / get_data_type_size(hw_tensor.data_type);

				if (hw_tensor.shape.size() == 3)
					hw_tensor.memory_layout = MemoryLayout::NHW;
				else if (hw_tensor.shape.size() == 2)
					hw_tensor.memory_layout = MemoryLayout::NC;
				else if (node.execution_mode == NodeExecutionMode::NPU)
					hw_tensor.memory_layout = arm_ops[i].input_memory_layout;
			}

			// If feature is enabled, try to resolve arm op as views.
			if (!(arm_op_views && exec_arm_op_view(arm_ops[i], hw_tensor)))
				// If one arm op could not be resolved, do not try to resolve the next ones.
				arm_op_views = false;

			// Push it in runtime arm ops.
			arm_ops_out.push_back(arm_ops[i]);

			// Look for next ARM op.
			arm_op_input_name = arm_ops[i].output_name;

			// Start looking from the beginning again.
			i = 0;
		}
		else
			i++;
	}

	if (arm_ops_out.empty())
		return vart_ml_error::SUCCESS;

	NpuTensorInfo& cpu_tensor = get_tensor_info_by_name(tensor_name, TensorType::CPU);

	// Set CPU tensor's shape, strides, and layout using the last arm op's output.
	cpu_tensor.shape         = arm_ops_out.back().output_shape;
	cpu_tensor.strides       = arm_ops_out.back().output_strides;
	cpu_tensor.memory_layout = arm_ops_out.back().output_memory_layout;

	// Optimize ArmOps
	if (is_external_output(tensor_name))
	{
		MemoryLayout outputs_format = cpu_tensor.memory_layout;
		if (options_.contains("out_shape_format"))
			outputs_format = to_memory_layout(std::any_cast<std::string>(options_.at("out_shape_format")));

		if (outputs_format != cpu_tensor.memory_layout)
		{
			// Check if the Transposes are needed
			std::vector<int> perm;
			bool             remove_transpose = false;
			for (i = arm_ops_out.size(); i > 0; i--)
			{
				if (arm_ops_out[i - 1].type == ArmOps::TRANSPOSE)
				{
					// If user format is already correct, we can remove the transpose
					if (outputs_format == arm_ops_out[i - 1].input_memory_layout)
					{
						cpu_tensor.memory_layout = outputs_format;

						perm = std::any_cast<std::vector<int>>(arm_ops_out[i - 1].parameter["perm"]);

						std::vector<uint32_t> shape_t(cpu_tensor.shape.size());
						std::vector<uint32_t> strides_t(cpu_tensor.strides.size());

						for (size_t j = 0; j < cpu_tensor.shape.size(); j++)
							shape_t[j] = cpu_tensor.shape[perm[j]];

						strides_t[cpu_tensor.strides.size() - 1] = 1;
						for (size_t j = cpu_tensor.strides.size() - 1; j > 0; j--)
							strides_t[j - 1] = shape_t[j] * strides_t[j];

						cpu_tensor.shape   = shape_t;
						cpu_tensor.strides = strides_t;

						remove_transpose = true;
						break;
					}
				}
			}

			// Remove all unnecessary transpose
			if (remove_transpose)
			{
				for (size_t j = i - 1; j < arm_ops_out.size(); j++)
					if (arm_ops_out[j].type == ArmOps::TRANSPOSE)
						arm_ops_out.erase(arm_ops_out.begin() + j);
					else if (arm_ops_out[j].type == ArmOps::PAD)
					{
						// Permute PAD's input shape through the removed transpose's perm.
						std::vector<uint32_t> permuted_shape(arm_ops_out[j].input_shape.size());
						for (size_t p = 0; p < perm.size(); p++)
							permuted_shape[p] = arm_ops_out[j].input_shape[perm[p]];
						arm_ops_out[j].input_shape = permuted_shape;

						arm_ops_out[j].input_strides[arm_ops_out[j].input_strides.size() - 1] = 1;
						for (ssize_t p = arm_ops_out[j].input_strides.size() - 2; p >= 0; p--)
							arm_ops_out[j].input_strides[p] =
							    arm_ops_out[j].input_strides[p + 1] * arm_ops_out[j].input_shape[p + 1];

						// Compute new padding with transposition
						std::vector<uint32_t> old_padding =
						    std::any_cast<std::vector<uint32_t>>(arm_ops_out[j].parameter["padding"]);
						std::vector<uint32_t> new_padding(old_padding.size());
						for (size_t p = 1; p < perm.size(); p++)
						{
							new_padding[p * 2 - 2] = old_padding[2 * perm[p] - 2];
							new_padding[p * 2 - 1] = old_padding[2 * perm[p] - 1];
						}
						arm_ops_out[j].parameter["padding"] = new_padding;

						// Compute new output shape and strides
						arm_ops_out[j].output_shape = arm_ops_out[j].input_shape;
						for (size_t p = 1; p < arm_ops_out[j].output_shape.size(); p++)
							arm_ops_out[j].output_shape[p] = arm_ops_out[j].output_shape[p]
							                                 + new_padding[p * 2 - 2]
							                                 + new_padding[p * 2 - 1];
						for (ssize_t p = arm_ops_out[j].output_shape.size() - 2; p >= 0; p--)
							arm_ops_out[j].output_strides[p] =
							    arm_ops_out[j].output_strides[p + 1] * arm_ops_out[j].output_shape[p + 1];
					}
			}
			else
				return vart_ml_log_err_msg(vart_ml_error::CONFIG_BAD_ARGUMENT,
				                           "Cannot set shape format %s for tensor %s.\n",
				                           to_string(outputs_format).data(),
				                           tensor_name.c_str());
		}
	}

	if (check_user_config("debug.skipArmOpsOptimization"))
		return vart_ml_error::SUCCESS;

	// Check if Transpose and channel padding ArmOps can be merged
	i = 0;
	while (i < arm_ops_out.size())
	{
		if (arm_ops_out[i].type == ArmOps::TRANSPOSE)
		{
			if (i != 0 && arm_ops_out[i - 1].type == ArmOps::PAD)
			{
				arm_ops_out[i].input_shape   = arm_ops_out[i - 1].input_shape;
				arm_ops_out[i].input_strides = arm_ops_out[i - 1].input_strides;
				// Erase unnecessary
				arm_ops_out.erase(arm_ops_out.begin() + i - 1);
				// Start looking from the beginning again.
				i = 0;
				continue;
			}

			if ((i < arm_ops_out.size() - 1) && arm_ops_out[i + 1].type == ArmOps::PAD)
			{
				arm_ops_out[i].output_shape   = arm_ops_out[i + 1].output_shape;
				arm_ops_out[i].output_strides = arm_ops_out[i + 1].output_strides;
				// Erase unnecessary
				arm_ops_out.erase(arm_ops_out.begin() + i + 1);
				// Start looking from the beginning again.
				i = 0;
				continue;
			}
		}
		i++;
	}

	// Check if TRANSPOSE and SLICE can be merged.
	i = 0;
	while (i < arm_ops_out.size())
	{
		if (arm_ops_out[i].type == ArmOps::TRANSPOSE)
		{
			if (i != 0 && arm_ops_out[i - 1].type == ArmOps::SLICE)
			{
				arm_ops_out[i].input_shape   = arm_ops_out[i - 1].input_shape;
				arm_ops_out[i].input_strides = arm_ops_out[i - 1].input_strides;

				// Erase the SLICE.
				arm_ops_out.erase(arm_ops_out.begin() + i - 1);

				// Start looking from the beginning again.
				i = 0;
				continue;
			}

			if ((i < arm_ops_out.size() - 1) && arm_ops_out[i + 1].type == ArmOps::SLICE)
			{
				arm_ops_out[i].output_shape   = arm_ops_out[i + 1].output_shape;
				arm_ops_out[i].output_strides = arm_ops_out[i + 1].output_strides;

				// Erase the SLICE.
				arm_ops_out.erase(arm_ops_out.begin() + i + 1);

				// Start looking from the beginning again.
				i = 0;
				continue;
			}
		}
		i++;
	}

	return vart_ml_error::SUCCESS;
}

std::string VartMLRunner::name_translate_in(const std::string& name)
{
	return name_translate(arm_ops_translations_in_, name_translate(npu_translations_in_, name));
}

std::string VartMLRunner::name_translate_out(const std::string& name)
{
	return name_translate(arm_ops_translations_out_, name_translate(npu_translations_out_, name));
}

bool VartMLRunner::is_external_input(const std::string& input_name)
{
	return std::find(names_in_.begin(), names_in_.end(), input_name) != names_in_.end();
}

bool VartMLRunner::is_external_output(const std::string& output_name)
{
	return std::find(names_out_.begin(), names_out_.end(), output_name) != names_out_.end();
}

int VartMLRunner::onnx_init(void)
{
	onnx_context_global_ = (void*)malloc(sizeof(struct onnx_context_global));
	if (onnx_context_global_ == NULL)
		return vart_ml_error::SYSTEM_ERROR_MEM_ALLOC_FAILURE;

	return ::onnx_init((struct onnx_context_global*)onnx_context_global_);
}

int VartMLRunner::onnx_init_node(struct node_desc& node)
{
	// To be removed w/ fix from frontend
	std::string filename   = node.name;
	std::string bad_suffix = "_call";
	// Strip trailing "_call" suffix
	if (std::equal(bad_suffix.rbegin(), bad_suffix.rend(), filename.rbegin()))
		filename.erase(filename.size() - bad_suffix.size());

	std::string onnx_path(model_path_ + node.name + '/' + filename + ".onnx");

	struct onnx_context_node* onnx_context_node =
	    (struct onnx_context_node*)malloc(sizeof(struct onnx_context_node));
	if (onnx_context_node == NULL)
		return vart_ml_error::SYSTEM_ERROR_MEM_ALLOC_FAILURE;

	const char* mem_arena_cfg = nullptr;
	get_user_config("onnx.disableCpuMemArena", &mem_arena_cfg);
	bool disable_cpu_mem_arena;
	if (mem_arena_cfg != nullptr)
		disable_cpu_mem_arena =
		    (strcasecmp(mem_arena_cfg, "true") == 0 || strcasecmp(mem_arena_cfg, "1") == 0);
	else
	{
		enum FpgaFamily family;
		npu_get_fpgafamily(&family);
		disable_cpu_mem_arena = (family == ZYNQ);
	}

	int err = ::onnx_init_node(onnx_path.c_str(),
	                           *((struct onnx_context_global*)onnx_context_global_),
	                           onnx_context_node,
	                           disable_cpu_mem_arena);

	if (err)
	{
		free(onnx_context_node);
		return err;
	}

	node.context_ptr = onnx_context_node;

	return vart_ml_error::SUCCESS;
}

void VartMLRunner::build_pl_nodes(const std::vector<std::string>& output_names)
{
	// NPU node should be the first and only node.
	assert(nodes_.size() == 1);
	assert(nodes_[0].execution_mode == NodeExecutionMode::NPU);

	// Reset NPU node's outputs.
	nodes_[0].output_count = output_names.size();
	nodes_[0].output_tensors_name.clear();
	nodes_[0].link_mode_out = NodeExtLinkMode::DIRECT;

	// Build PL output tensors and push them into the NPU node.
	npu_snapshot_t* npu_snapshot = (npu_snapshot_t*)nodes_[0].context_ptr;
	for (auto& output_name : output_names)
	{
		assert(is_tensor_pl(npu_snapshot, output_name.c_str()));

		NpuTensorInfo& cpu_tensor = get_tensor_info_by_name(output_name, TensorType::CPU);
		NpuTensorInfo& hw_tensor  = get_tensor_info_by_name(output_name, TensorType::HW);

		cpu_tensor.data_type = to_data_type(npu_get_pl_data_type(npu_snapshot, output_name));
		hw_tensor.data_type  = cpu_tensor.data_type;

		cpu_tensor.shape.resize(npu_get_pl_nbdims(npu_snapshot, output_name));
		for (size_t i = 0; i < cpu_tensor.shape.size(); i++)
			cpu_tensor.shape[i] = npu_get_pl_shape(npu_snapshot, output_name)[i];
		hw_tensor.shape = cpu_tensor.shape;

		cpu_tensor.strides.resize(npu_get_pl_nbdims(npu_snapshot, output_name));
		for (size_t i = 0; i < cpu_tensor.strides.size(); i++)
			cpu_tensor.strides[i] =
			    npu_get_pl_strides(npu_snapshot, output_name)[i] / get_data_type_size(cpu_tensor.data_type);
		hw_tensor.strides = cpu_tensor.strides;

		cpu_tensor.size          = cpu_tensor.strides[0];
		hw_tensor.size           = cpu_tensor.size;
		cpu_tensor.size_in_bytes = cpu_tensor.strides[0] * get_data_type_size(cpu_tensor.data_type);
		hw_tensor.size_in_bytes  = cpu_tensor.size_in_bytes;

		nodes_[0].output_tensors_name.push_back(output_name);

		// No ArmOps associated with pl nodes output
		arm_ops_[output_name] = {};
	}
}

int VartMLRunner::set_npu_tensors(struct node_desc& node)
{
	npu_snapshot_t* snap = (npu_snapshot_t*)node.context_ptr;

	for (size_t i = 0; i < npu_get_nbinputs(snap); i++)
	{
		std::string in_name = name_translate_in(npu_get_in_name(snap, i));

		NpuTensorInfo& cpu_tensor = get_tensor_info_by_name(in_name, TensorType::CPU);
		NpuTensorInfo& hw_tensor  = get_tensor_info_by_name(in_name, TensorType::HW);

		hw_tensor.data_type     = to_data_type(npu_get_in_data_type(snap, i));
		hw_tensor.size_in_bytes = npu_get_in_ddrimgsize(snap, i);
		hw_tensor.strides[0]    = hw_tensor.size_in_bytes / get_data_type_size(hw_tensor.data_type);

		if (hw_tensor.shape.size() == 2)
			hw_tensor.memory_layout = MemoryLayout::NC;
		else
			hw_tensor.memory_layout = to_memory_layout(npu_get_in_ddr_shape_format(snap, i));

		// Set CPU tensor's layout to HW's by default. If there are arm ops linked to this tensor, it will be
		// adjusted.
		cpu_tensor.memory_layout = hw_tensor.memory_layout;

		quant_params_.at(in_name).scale         = npu_get_in_quantization_coeff(snap, i);
		quant_params_.at(in_name).zero_point    = 0;
		quant_params_.at(in_name).rounding_mode = RoundingMode::ROUND_TO_NEAREST_EVEN;
	}

	for (size_t i = 0; i < npu_get_nboutputs(snap); i++)
	{
		std::string out_name = name_translate_out(npu_get_out_name(snap, i));

		NpuTensorInfo& cpu_tensor = get_tensor_info_by_name(out_name, TensorType::CPU);
		NpuTensorInfo& hw_tensor  = get_tensor_info_by_name(out_name, TensorType::HW);

		hw_tensor.data_type     = to_data_type(npu_get_out_data_type(snap, i));
		hw_tensor.size_in_bytes = npu_get_out_ddrimgsize(snap, i);
		hw_tensor.strides[0]    = hw_tensor.size_in_bytes / get_data_type_size(hw_tensor.data_type);

		if (hw_tensor.shape.size() == 2)
			hw_tensor.memory_layout = MemoryLayout::NC;
		else
			hw_tensor.memory_layout = to_memory_layout(npu_get_out_ddr_shape_format(snap, i));

		// Set CPU tensor's layout to HW's by default. If there are arm ops linked to this tensor, it will be
		// adjusted.
		cpu_tensor.memory_layout = hw_tensor.memory_layout;

		quant_params_.at(out_name).scale         = npu_get_out_quantization_coeff(snap, i);
		quant_params_.at(out_name).zero_point    = 0;
		quant_params_.at(out_name).rounding_mode = RoundingMode::ROUND_TO_NEAREST_EVEN;

		if (is_tensor_pl(snap, npu_get_out_name(snap, i)))
		{
			pl_                  = true;
			node.pl_node         = true;
			cpu_tensor.data_type = to_data_type(npu_get_out_data_type(snap, i));
		}
	}

	FpgaArchitecture arch;
	int              err = npu_get_architecture(&arch);
	if (err)
		return err;

	std::ostringstream oss;
	if (arch == FpgaArchitecture::AIEML_V1C)
		oss << "board " << npu_get_boardname(snap->ip_idx)
		    << " (AIE: " << npu_get_nbcolumns(snap->ip_idx) * npu_get_nbaiepercolumn(snap->ip_idx) << " = "
		    << npu_get_nbcolumns(snap->ip_idx) << "x" << npu_get_nbaiepercolumn(snap->ip_idx) << ")";
	else
		oss << "board " << npu_get_boardname(snap->ip_idx) << " (config: " << npu_get_nbsystems(snap->ip_idx)
		    << "x" << npu_get_nbcores(snap->ip_idx) << "x" << npu_get_nbnces(snap->ip_idx) << ")";

	stats_->set_config_string(oss.str());

	return vart_ml_error::SUCCESS;
}

void
VartMLRunner::parse_graph_tensor(const std::string& key, const void* model_info, TensorDirection direction)
{
	// If tensor was already parsed, return.
	if (tensors_.contains(key))
		return;

	const json::value_type& tensor_json = (*(json::value_type*)model_info)[key];

	NpuTensorInfo tensor;

	tensor.name      = key;
	tensor.direction = direction;

	// Get data type of the tensor.
	tensor.data_type = to_data_type(tensor_json.at("params").at("type"));
	tensor.shape     = std::vector<uint32_t>(tensor_json.at("params").at("shape").begin(),
                                         tensor_json.at("params").at("shape").end());

	// Set memory layout to GENERIC and fill memory_layout_order with increasing values by default.
	tensor.memory_layout       = MemoryLayout::GENERIC;
	tensor.memory_layout_order = std::vector<uint32_t>(tensor.shape.size());
	std::iota(std::begin(tensor.memory_layout_order), std::end(tensor.memory_layout_order), 0);

	tensor.size = 1;
	tensor.strides.resize(tensor_json.at("params").at("shape").size());
	for (ssize_t i = tensor.shape.size() - 1; i > 0; i--)
	{
		tensor.strides[i] = tensor.size;
		tensor.size *= tensor.shape[i];
	}
	tensor.strides[0]    = tensor.size;
	tensor.size_in_bytes = tensor.size * get_data_type_size(tensor.data_type);

	// Add tensor to quant params with default values.
	quant_params_[tensor.name].scale         = 0;
	quant_params_[tensor.name].zero_point    = 0;
	quant_params_[tensor.name].rounding_mode = RoundingMode::UNKNOWN;

	uint32_t aligned_size = tensor.size_in_bytes;
	if (!npu_is_xrt_en())
		aligned_size = npu_get_aligned_size(tensor.size_in_bytes);

	std::vector<std::string>::iterator it;

	if ((it = std::find(names_in_.begin(), names_in_.end(), tensor.name)) != names_in_.end())
	{
		auto index = std::distance(names_in_.begin(), it);

		tensor.tensor_type        = TensorType::CPU;
		cpu_input_tensors_[index] = tensor;
		tensor.tensor_type        = TensorType::HW;
		tensor.size_in_bytes      = aligned_size;
		tensor.strides[0]         = tensor.size_in_bytes / get_data_type_size(tensor.data_type);
		hw_input_tensors_[index]  = tensor;

		tensors_[tensor.name] = std::make_pair(TensorDirectionPriv::INPUT, index);
	}
	else if ((it = std::find(names_out_.begin(), names_out_.end(), tensor.name)) != names_out_.end())
	{
		auto index = std::distance(names_out_.begin(), it);

		tensor.tensor_type         = TensorType::CPU;
		cpu_output_tensors_[index] = tensor;
		tensor.tensor_type         = TensorType::HW;
		tensor.size_in_bytes       = aligned_size;
		tensor.strides[0]          = tensor.size_in_bytes / get_data_type_size(tensor.data_type);
		hw_output_tensors_[index]  = tensor;

		tensors_[tensor.name] = std::make_pair(TensorDirectionPriv::OUTPUT, index);
	}
	else
	{
		tensor.tensor_type = TensorType::CPU;
		cpu_internal_tensors_.push_back(tensor);
		tensor.tensor_type   = TensorType::HW;
		tensor.size_in_bytes = aligned_size;
		tensor.strides[0]    = tensor.size_in_bytes / get_data_type_size(tensor.data_type);
		hw_internal_tensors_.push_back(tensor);

		tensors_[tensor.name] =
		    std::make_pair(TensorDirectionPriv::INTERNAL, cpu_internal_tensors_.size() - 1);
	}
}

int VartMLRunner::parse_graph_node(const std::string&           name,
                                   const void*                  model_info,
                                   std::vector<struct arm_ops>& arm_ops)
{
	int err;

	const json::value_type& node_json = (*(json::value_type*)model_info)[name];

	// Get node's execution mode.
	std::string execution_mode(node_json.at("layer").at("params").at("type"));

	// Get node's execution order.
	unsigned execution_order = (unsigned)(node_json.at("execution").at("execution_order")) - 1;

	// Check if the run is npu only.
	bool npu_only = false;
	if (options_.contains("npu_only"))
		npu_only = std::any_cast<bool>(options_.at("npu_only"));

	if (npu_only)
	{
		// In npu only mode, set execution_order to 0 (as the NPU node may not be the first in the whole
		// graph).
		if (execution_mode == "CALL_SNAPSHOT")
		{
			// In npu only, there must be only one NPU node in the graph.
			if (nodes_.contains(0))
			{
				vart_ml_log(LOG_WARN,
				            "Found more than one NPU node with npu_only mode set. Skipping node %s.\n",
				            name.c_str());
				return vart_ml_error::SUCCESS;
			}

			execution_order = 0;
		}
		// And skip non-NPU nodes.
		else
		{
			vart_ml_log(LOG_INFO, "NPU only mode set. Skipping node %s.\n", name.c_str());
			return vart_ml_error::SUCCESS;
		}
	}
	else if (pl_)
	{
		// We currently only support snapshots that start with an NPU node followed by an ONNX node.
		assert(nodes_[execution_order - 1].execution_mode == NodeExecutionMode::NPU);

		for (auto& output_name : node_json.at("outputs"))
			parse_graph_tensor(output_name, model_info, TensorDirection::OUTPUT);

		build_pl_nodes(node_json.at("outputs"));
		return vart_ml_error::SUCCESS;
	}

	struct node_desc& node = nodes_[execution_order];

	// Set node's name & exec mode.
	node.name = name;
	node_set_execution_mode(node, execution_mode);

	// If it is a NPU graph, parse the NPU snapshot.
	if (node.execution_mode == NodeExecutionMode::NPU)
	{
		// Get the batch size.
		batchSize_ = node_json.at("layer").at("params").at("batch_size");

		// Set NPU translation maps.
		if (node_json.at("layer").at("params").contains("input_translate"))
			npu_translations_in_ =
			    (std::map<std::string, std::string>)node_json.at("layer").at("params").at("input_translate");
		else
			npu_translations_in_.clear();
		if (node_json.at("layer").at("params").contains("output_translate"))
			npu_translations_out_ =
			    (std::map<std::string, std::string>)node_json.at("layer").at("params").at("output_translate");
		else
			npu_translations_out_.clear();

		// Give default shapes here as we don't know yet if the node's tensors are modifiable or not. If any
		// non-default shape format was given initially, it will be set after parsing all nodes.
		check_err(npu_parse_snapshot((model_path_ + node.name).c_str(), (npu_snapshot_t**)&node.context_ptr));

		std::vector<std::pair<size_t, size_t>> active_cores =
		    ((npu_snapshot_t*)node.context_ptr)->active_cores;

		std::unordered_set<size_t> unique_sys_idx;
		std::unordered_set<size_t> unique_core_idx;

		for (const auto& [s, c] : active_cores)
		{
			unique_sys_idx.insert(s);
			unique_core_idx.insert(c);
		}

		nb_systems_          = unique_sys_idx.size();
		nb_cores_per_system_ = unique_core_idx.size();
		batch_size_per_core_ =
		    static_cast<size_t>(std::ceil((float)batchSize_ / nb_systems_ / nb_cores_per_system_));
	}

	// Else if it is an ONNX graph, initialize global context and onnx node.
	else
	{
		// Initialize global onnx context if not done before.
		if (onnx_context_global_ == NULL)
			check_err(this->onnx_init());

		check_err(this->onnx_init_node(node));
	}

	node.input_count  = node_json.at("inputs").size();
	node.output_count = node_json.at("outputs").size();

	// Gather input & output names before computing link_mode_in & link_mode_out.  This is done because in NPU
	// nodes, the snapshot's inputs & outputs may not be in the same order as specified in main.json.  Also
	// translate input/output names because the names specified in the snapshot are not the names specified in
	// main.json.
	if (node.execution_mode == NodeExecutionMode::NPU)
	{
		node.input_tensors_name.reserve(node.input_count);
		for (size_t i = 0; i < node.input_count; i++)
			node.input_tensors_name.push_back(
			    name_translate_in(npu_get_in_name((npu_snapshot_t*)node.context_ptr, i)));

		node.output_tensors_name.reserve(node.output_count);
		for (size_t i = 0; i < node.output_count; i++)
			node.output_tensors_name.push_back(
			    name_translate_out(npu_get_out_name((npu_snapshot_t*)node.context_ptr, i)));

		// In npu only mode, global IOs must be set to the node's. Do this here instead of during the npu_only
		// handling so we have the translations.
		if (npu_only)
		{
			names_in_  = node.input_tensors_name;
			names_out_ = node.output_tensors_name;

			// resize the vectors of inputs tensor
			cpu_input_tensors_.resize(names_in_.size());
			hw_input_tensors_.resize(names_in_.size());

			// resize the vectors of inputs/outputs tensor
			cpu_output_tensors_.resize(names_out_.size());
			hw_output_tensors_.resize(names_out_.size());
		}
	}
	else
	{
		// For CPU nodes, translate the names only using arm ops.
		node.input_tensors_name.reserve(node.input_count);
		for (size_t i = 0; i < node.input_count; i++)
			node.input_tensors_name.push_back(
			    name_translate(arm_ops_translations_in_, node_json.at("inputs")[i]));
		node.output_tensors_name.reserve(node.output_count);
		for (size_t i = 0; i < node.output_count; i++)
			node.output_tensors_name.push_back(
			    name_translate(arm_ops_translations_out_, node_json.at("outputs")[i]));

		// Can not apply user input format to this node
		if (options_.contains("in_shape_format"))
			for (auto& input_name : node.input_tensors_name)
				if (is_external_input(input_name))
					return vart_ml_log_err_msg(
					    vart_ml_error::CONFIG_BAD_ARGUMENT,
					    "Cannot modify shape format of tensor %s. Tensor is linked to an ONNX node.\n",
					    input_name.c_str());

		// Can not apply user output format to this node
		if (options_.contains("out_shape_format"))
			for (auto& output_name : node.output_tensors_name)
				if (is_external_output(output_name))
					return vart_ml_log_err_msg(
					    vart_ml_error::CONFIG_BAD_ARGUMENT,
					    "Cannot modify shape format of tensor %s. Tensor is linked to an ONNX node.\n",
					    output_name.c_str());
	}

	// Determine the way node's external tensors will be linked to execute's args. Only used if the node
	// got external tensors.
	node.link_mode_in  = (((node.execution_mode == NodeExecutionMode::NPU) || (batchSize_ == 1))
                         && (names_in_ == node.input_tensors_name))
	                         ? NodeExtLinkMode::DIRECT
	                         : NodeExtLinkMode::COPY_PTRS;
	node.link_mode_out = (((node.execution_mode == NodeExecutionMode::NPU) || (batchSize_ == 1))
	                      && (names_out_ == node.output_tensors_name))
	                         ? NodeExtLinkMode::DIRECT
	                         : NodeExtLinkMode::COPY_PTRS;

	// Fill input_tensors.
	for (size_t i = 0; i < node.input_count; i++)
		parse_graph_tensor(node.input_tensors_name[i], model_info, TensorDirection::INPUT);

	// Fill output_tensors.
	for (size_t i = 0; i < node.output_count; i++)
		parse_graph_tensor(node.output_tensors_name[i], model_info, TensorDirection::OUTPUT);

	// Set node's hash.
	node.hash = std::hash<std::string>{}(node.name);

	// Set NPU-specific tensors data.
	if (node.execution_mode == NodeExecutionMode::NPU)
		check_err(set_npu_tensors(node));

	// Build ARM operations list.
	for (auto& name : node.input_tensors_name)
		if ((err = build_arm_op_in(node, name, arm_ops, arm_ops_[name], name)) != vart_ml_error::SUCCESS)
			return err;

	for (auto& name : node_json.at("outputs"))
	{
		const std::string& tensor_name = name_translate(arm_ops_translations_out_, name);
		if ((err = build_arm_op_out(node, name, arm_ops, arm_ops_[tensor_name], tensor_name))
		    != vart_ml_error::SUCCESS)
			return err;
	}

	// Add node to stat graph.
	stats_->addSubGraph(node.hash, node.name, node.execution_mode == NodeExecutionMode::ONNX);

	return vart_ml_error::SUCCESS;
}

int VartMLRunner::parse_graph_arm_ops(const std::string&           name,
                                      const void*                  model_info,
                                      std::vector<struct arm_ops>& arm_ops)
{
	const json::value_type& arm_ops_json = (*(json::value_type*)model_info)[name];

	arm_ops.resize(arm_ops.size() + 1);
	struct arm_ops& arm_op = arm_ops.back();

	arm_op.name        = name;
	arm_op.type        = to_arm_ops(arm_ops_json.at("layer").at("params").at("type"));
	arm_op.input_name  = arm_ops_json.at("inputs")[0];
	arm_op.output_name = arm_ops_json.at("outputs")[0];

	arm_op.input_memory_layout  = to_memory_layout(arm_ops_json.at("layer").at("params").at("input_format"));
	arm_op.output_memory_layout = to_memory_layout(arm_ops_json.at("layer").at("params").at("output_format"));

	std::vector<uint32_t> input_shape =
	    (*(json::value_type*)model_info)[arm_op.input_name].at("params").at("shape");
	std::vector<uint32_t> output_shape =
	    (*(json::value_type*)model_info)[arm_op.output_name].at("params").at("shape");

	std::vector<uint32_t> input_strides(input_shape.size());
	std::vector<uint32_t> output_strides(output_shape.size());

	size_t input_size  = 1;
	size_t output_size = 1;

	// Store normalized input strides and size
	input_strides[input_shape.size() - 1] = 1;
	for (size_t i = input_shape.size() - 1; i > 0; i--)
	{
		input_strides[i - 1] = input_shape[i] * input_strides[i];
		input_size *= input_shape[i];
	}

	// Store normalized output strides
	output_strides[output_shape.size() - 1] = 1;
	for (size_t i = output_shape.size() - 1; i > 0; i--)
	{
		output_strides[i - 1] = output_shape[i] * output_strides[i];
		output_size *= output_shape[i];
	}

	arm_op.input_shape  = input_shape;
	arm_op.output_shape = output_shape;

	arm_op.input_strides  = input_strides;
	arm_op.output_strides = output_strides;

	arm_op.input_size  = input_size;
	arm_op.output_size = output_size;

	arm_op.input_data_type  = to_data_type(arm_ops_json.at("execution").at("input_type"));
	arm_op.output_data_type = to_data_type(arm_ops_json.at("execution").at("output_type"));

	arm_op.output_size_in_bytes = output_size * get_data_type_size(arm_op.output_data_type);

	switch (arm_op.type)
	{
	case ArmOps::CAST:
		arm_op.parameter["use_accurate_bf16"] = check_user_config("use.accurate_bf16_quantization");
		break;

	case ArmOps::PAD:
		if (arm_ops_json.at("layer").at("params").contains("padding"))
			arm_op.parameter["padding"] =
			    (std::vector<uint32_t>)arm_ops_json.at("layer").at("params").at("padding");
		else
		{
			if (arm_op.input_memory_layout == MemoryLayout::NHWC)
				arm_op.parameter["padding"] = (std::vector<uint32_t>){
					0, 0, 0, 0, 0, (uint32_t)arm_ops_json.at("layer").at("params").at("channel_pad")
				};
			else if (arm_op.input_memory_layout == MemoryLayout::NCHW)
				arm_op.parameter["padding"] = (std::vector<uint32_t>){
					0, (uint32_t)arm_ops_json.at("layer").at("params").at("channel_pad"), 0, 0, 0, 0
				};
			else
				return vart_ml_log_err_msg(vart_ml_error::CONFIG_IRIZ_MALFORMED,
				                           "Invalid pad input format: `%s'.\n",
				                           to_string(arm_op.input_memory_layout).data());
		}
		break;

	case ArmOps::DEQUANTIZE:
		if (arm_ops_json.at("layer").at("quantization").at("op").at("type") == "EXPONENT")
		{
			arm_op.parameter["scale"] = (float)std::pow(
			    2, (float)arm_ops_json.at("layer").at("quantization").at("op").at("exponent"));

			if (arm_ops_json.at("layer").at("quantization").at("op").at("rounding") == "NEAREST_EVEN")
				arm_op.parameter["rounding_mode"] = RoundingMode::ROUND_TO_NEAREST_EVEN;
			else if (arm_ops_json.at("layer").at("quantization").at("op").at("rounding") == "TOWARD_ZERO")
				arm_op.parameter["rounding_mode"] = RoundingMode::ROUND_TOWARD_ZERO;
			else
				arm_op.parameter["rounding_mode"] = RoundingMode::UNKNOWN;
		}
		else
		{
			arm_op.parameter["scale"]         = (float)1;
			arm_op.parameter["rounding_mode"] = RoundingMode::UNKNOWN;
		}
		break;

	case ArmOps::QUANTIZE:
		if (arm_ops_json.at("layer").at("quantization").at("op").at("type") == "EXPONENT")
		{
			arm_op.parameter["scale"] = (float)std::pow(
			    2, (float)arm_ops_json.at("layer").at("quantization").at("op").at("exponent"));

			if (arm_ops_json.at("layer").at("quantization").at("op").at("rounding") == "NEAREST_EVEN")
				arm_op.parameter["rounding_mode"] = RoundingMode::ROUND_TO_NEAREST_EVEN;
			else if (arm_ops_json.at("layer").at("quantization").at("op").at("rounding") == "TOWARD_ZERO")
				arm_op.parameter["rounding_mode"] = RoundingMode::ROUND_TOWARD_ZERO;
			else
				arm_op.parameter["rounding_mode"] = RoundingMode::UNKNOWN;
		}
		arm_op.parameter["use_accurate_bf16"] = check_user_config("use.accurate_bf16_quantization");
		break;

	case ArmOps::RESHAPE:
		break;

	case ArmOps::SLICE:
		arm_op.parameter["starts"]  = (uint32_t)arm_ops_json.at("layer").at("params").at("starts")[0];
		arm_op.parameter["ends"]    = (uint32_t)arm_ops_json.at("layer").at("params").at("ends")[0];
		arm_op.parameter["axes"]    = (uint32_t)arm_ops_json.at("layer").at("params").at("axes")[0];
		arm_op.parameter["strides"] = (uint32_t)arm_ops_json.at("layer").at("params").at("strides")[0];
		break;

	case ArmOps::TRANSPOSE:
		arm_op.parameter["perm"] = std::vector<int>(arm_ops_json.at("layer").at("params").at("perm"));
		break;

	default:
		vart_ml_log(
		    LOG_WARN, "Parsing not implemented for ARM Ops: %s.\n", arm_ops_to_string(arm_op.type).c_str());
		break;
	}

	return vart_ml_error::SUCCESS;
}

} // namespace vart
