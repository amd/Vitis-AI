/**
 * @file utils.cpp
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
 */

#include <algorithm>
#include <fstream>

#include "io/io.h"
#include "npu_runner/npu_runner.h"
#include "onnx_runner/onnx_runner.h"
#include "vart_ml_runner.hpp"

namespace vart
{
static std::unordered_map<std::string, DataType> string_to_data_type_map = {
	{ "UNKNOWN", DataType::UNKNOWN }, { "BOOLEAN", DataType::BOOLEAN }, { "INT8", DataType::INT8 },
	{ "QINT8", DataType::INT8 },      { "UINT8", DataType::UINT8 },     { "QUINT8", DataType::UINT8 },
	{ "INT16", DataType::INT16 },     { "UINT16", DataType::UINT16 },   { "BF16", DataType::BF16 },
	{ "BFLOAT16", DataType::BF16 },   { "FP16", DataType::FP16 },       { "INT32", DataType::INT32 },
	{ "FLOAT32", DataType::FLOAT32 }, { "INT64", DataType::INT64 },     { "UINT64", DataType::UINT64 }
};

static std::unordered_map<std::string, MemoryLayout> string_to_memory_layout_map = {
	{ "UNKNOWN", MemoryLayout::UNKNOWN },
	{ "NC", MemoryLayout::NC },
	{ "NCH", MemoryLayout::NCH },
	{ "NHC", MemoryLayout::NHC },
	{ "NHW", MemoryLayout::NHW },
	{ "NWC", MemoryLayout::NWC },
	{ "NHWC", MemoryLayout::NHWC },
	{ "NCHW", MemoryLayout::NCHW },
	{ "NHWC4", MemoryLayout::NHWC4 },
	{ "NHWC8", MemoryLayout::NHWC8 },
	{ "NC4HW4", MemoryLayout::NC4HW4 },
	{ "NC8HW8", MemoryLayout::NC8HW8 },
	{ "HCWNC4", MemoryLayout::HCWNC4 },
	{ "HCWNC8", MemoryLayout::HCWNC8 },
	{ "HCWNC16", MemoryLayout::HCWNC16 },
	{ "NHW16C4WC", MemoryLayout::NHW16C4WC },
	{ "NHW16WC4C", MemoryLayout::NHW16WC4C },
	{ "NH2HWC4C", MemoryLayout::NH2HWC4C },
	{ "NH2C4HWC", MemoryLayout::NH2C4HWC },
	{ "GENERIC", MemoryLayout::GENERIC },
};

static std::unordered_map<std::string, TensorType> string_to_tensor_type_map = { { "CPU", TensorType::CPU },
	                                                                             { "HW", TensorType::HW } };

static std::unordered_map<std::string, MemoryType> string_to_memory_type_map = {
	{ "UNKNOWN", MemoryType::UNKNOWN },
	{ "XRT_BO", MemoryType::XRT_BO },
	{ "DMA_FD", MemoryType::DMA_FD },
	{ "USER_POINTER_CMA", MemoryType::USER_POINTER_CMA },
	{ "USER_POINTER_NON_CMA", MemoryType::USER_POINTER_NON_CMA }
};

static std::unordered_map<std::string, TensorDirection> string_to_tensor_direction_map = {
	{ "INPUT", TensorDirection::INPUT },
	{ "OUTPUT", TensorDirection::OUTPUT }
};

static std::unordered_map<ArmOps, std::string> arm_ops_to_string_map = { { ArmOps::CAST, "CAST" },
	                                                                     { ArmOps::PAD, "PAD" },
	                                                                     { ArmOps::DEQUANTIZE, "DEQUANTIZE" },
	                                                                     { ArmOps::QUANTIZE,
	                                                                       "EMBD_QUANTIZE" },
	                                                                     { ArmOps::RESHAPE, "RESHAPE" },
	                                                                     { ArmOps::SLICE, "SLICE" },
	                                                                     { ArmOps::TRANSPOSE, "TRANSPOSE" } };

static std::unordered_map<std::string, ArmOps> string_to_arm_ops_map = { { "CAST", ArmOps::CAST },
	                                                                     { "CHANNEL_PAD", ArmOps::PAD },
	                                                                     { "PAD", ArmOps::PAD },
	                                                                     { "DEQUANTIZE", ArmOps::DEQUANTIZE },
	                                                                     { "EMBD_QUANTIZE",
	                                                                       ArmOps::QUANTIZE },
	                                                                     { "RESHAPE", ArmOps::RESHAPE },
	                                                                     { "SLICE", ArmOps::SLICE },
	                                                                     { "TRANSPOSE", ArmOps::TRANSPOSE } };

static void print_arm_op(const struct arm_ops& arm_op)
{
	printf("    Name:           %s\n", arm_op.name.c_str());
	printf("    Input Name:     %s\n", arm_op.input_name.c_str());
	printf("    Output Name:    %s\n", arm_op.output_name.c_str());
	printf("    Type:           %s\n", arm_ops_to_string(arm_op.type).c_str());

	switch (arm_op.type)
	{
	case ArmOps::CAST:
		break;

	case ArmOps::PAD:
		printf("    Padding:        ");
		for (auto p : std::any_cast<std::vector<uint32_t>>(arm_op.parameter.at("padding")))
			printf("%d ", p);
		printf("\n");
		break;

	case ArmOps::DEQUANTIZE:
		printf("    Scale:          %f\n", std::any_cast<float>(arm_op.parameter.at("scale")));

		printf("    Rounding Mode:  ");

		if (std::any_cast<RoundingMode>(arm_op.parameter.at("rounding_mode"))
		    == RoundingMode::ROUND_TO_NEAREST_EVEN)
			printf("ROUND_TO_NEAREST_EVEN\n");

		else if (std::any_cast<RoundingMode>(arm_op.parameter.at("rounding_mode"))
		         == RoundingMode::ROUND_TOWARD_ZERO)
			printf("ROUND_TOWARD_ZERO\n");

		else
			printf("UNKNOWN\n");

		break;

	case ArmOps::QUANTIZE:
		printf("    Scale:          %f\n", std::any_cast<float>(arm_op.parameter.at("scale")));

		printf("    Rounding Mode:  ");

		if (std::any_cast<RoundingMode>(arm_op.parameter.at("rounding_mode"))
		    == RoundingMode::ROUND_TO_NEAREST_EVEN)
			printf("ROUND_TO_NEAREST_EVEN\n");

		else if (std::any_cast<RoundingMode>(arm_op.parameter.at("rounding_mode"))
		         == RoundingMode::ROUND_TOWARD_ZERO)
			printf("ROUND_TOWARD_ZERO\n");

		else
			printf("UNKNOWN\n");

		break;

	case ArmOps::SLICE:
		printf("    Starts:         %u\n", std::any_cast<uint32_t>(arm_op.parameter.at("starts")));
		printf("    Ends:           %u\n", std::any_cast<uint32_t>(arm_op.parameter.at("ends")));
		printf("    Axes:           %u\n", std::any_cast<uint32_t>(arm_op.parameter.at("axes")));
		printf("    Strides:        %u\n", std::any_cast<uint32_t>(arm_op.parameter.at("strides")));
		break;

	case ArmOps::TRANSPOSE:
		printf("    Perm:           ");
		for (auto p : std::any_cast<std::vector<int>>(arm_op.parameter.at("perm")))
			printf("%d ", p);
		printf("\n");
		break;

	default:
		break;
	}

	printf("    Input Type:     %s\n", to_string(arm_op.input_data_type).data());

	printf("    Output Type:    %s\n", to_string(arm_op.output_data_type).data());

	printf("    Input Format    %s\n", to_string(arm_op.input_memory_layout).data());
	printf("    Output Format   %s\n", to_string(arm_op.output_memory_layout).data());

	printf("    Input Shape:    ");
	const std::vector<uint32_t>& input_shape = arm_op.input_shape;
	for (auto s : input_shape)
		printf("%u ", s);
	printf("\n");

	printf("    Output Shape:   ");
	const std::vector<uint32_t>& output_shape = arm_op.output_shape;
	for (auto s : output_shape)
		printf("%u ", s);
	printf("\n");

	printf("    Input Strides:  ");
	const std::vector<uint32_t>& input_strides = arm_op.input_strides;
	for (auto s : input_strides)
		printf("%u ", s);
	printf("\n");

	printf("    Output Strides: ");
	const std::vector<uint32_t>& output_strides = arm_op.output_strides;
	for (auto s : output_strides)
		printf("%u ", s);
	printf("\n");

	printf("\n");
}

DataType to_data_type(std::string data_type_str)
{
	// Character case is variable. Convert by default in uppercase.
	std::transform(data_type_str.begin(), data_type_str.end(), data_type_str.begin(), [](unsigned char c) {
		return std::toupper(c);
	});

	if (!string_to_data_type_map.contains(data_type_str))
	{
		vart_ml_log(LOG_WARN, "Unsupported tensor data type: %s.\n", data_type_str.c_str());
		return DataType::UNKNOWN;
	}

	return string_to_data_type_map.at(data_type_str);
}

MemoryLayout to_memory_layout(std::string memory_layout_str)
{
	// Character case is variable. Convert by default in uppercase.
	std::transform(memory_layout_str.begin(),
	               memory_layout_str.end(),
	               memory_layout_str.begin(),
	               [](unsigned char c) { return std::toupper(c); });

	if (!string_to_memory_layout_map.contains(memory_layout_str))
	{
		vart_ml_log(LOG_WARN, "Unsupported memory layout: %s.\n", memory_layout_str.c_str());
		return MemoryLayout::UNKNOWN;
	}

	return string_to_memory_layout_map.at(memory_layout_str);
}

TensorType to_tensor_type(std::string tensor_type_str)
{
	// Character case is variable. Convert by default in uppercase.
	std::transform(tensor_type_str.begin(),
	               tensor_type_str.end(),
	               tensor_type_str.begin(),
	               [](unsigned char c) { return std::toupper(c); });

	if (!string_to_tensor_type_map.contains(tensor_type_str))
		vart_ml_log(LOG_WARN, "Unsupported tensor type: %s.\n", tensor_type_str.c_str());

	return string_to_tensor_type_map.at(tensor_type_str);
}

MemoryType to_memory_type(std::string memory_type_str)
{
	// Character case is variable. Convert by default in uppercase.
	std::transform(memory_type_str.begin(),
	               memory_type_str.end(),
	               memory_type_str.begin(),
	               [](unsigned char c) { return std::toupper(c); });

	if (!string_to_memory_type_map.contains(memory_type_str))
	{
		vart_ml_log(LOG_WARN, "Unsupported tensor memory type: %s.\n", memory_type_str.c_str());
		return MemoryType::UNKNOWN;
	}

	return string_to_memory_type_map.at(memory_type_str);
}

TensorDirection to_tensor_direction(std::string tensor_direction_str)
{
	// Character case is variable. Convert by default in uppercase.
	std::transform(tensor_direction_str.begin(),
	               tensor_direction_str.end(),
	               tensor_direction_str.begin(),
	               [](unsigned char c) { return std::toupper(c); });

	if (!string_to_tensor_direction_map.contains(tensor_direction_str))
		vart_ml_log(LOG_WARN, "Unsupported tensor direction: %s.\n", tensor_direction_str.c_str());

	return string_to_tensor_direction_map.at(tensor_direction_str);
}

const std::string& arm_ops_to_string(ArmOps arm_op) { return arm_ops_to_string_map[arm_op]; }

ArmOps to_arm_ops(std::string arm_op_str)
{
	// Character case is variable. Convert by default in uppercase.
	std::transform(arm_op_str.begin(), arm_op_str.end(), arm_op_str.begin(), [](unsigned char c) {
		return std::toupper(c);
	});

	if (!string_to_arm_ops_map.contains(arm_op_str))
		vart_ml_log(LOG_WARN, "Unsupported ARM ops: %s.\n", arm_op_str.c_str());

	return string_to_arm_ops_map.at(arm_op_str);
}

std::string_view to_string(RoundingMode value) noexcept
{
	switch (value)
	{
	case RoundingMode::UNKNOWN:
		return "UNKNOWN";
	case RoundingMode::ROUND_TO_NEAREST_EVEN:
		return "ROUND_TO_NEAREST_EVEN";
	case RoundingMode::ROUND_TOWARD_ZERO:
		return "ROUND_TOWARD_ZERO";
	}
	return "UNKNOWN";
}

std::string_view to_string(StatusCode value) noexcept
{
	switch (value)
	{
	case StatusCode::SUCCESS:
		return "SUCCESS";
	case StatusCode::FAILURE:
		return "FAILURE";
	case StatusCode::INVALID_INPUT:
		return "INVALID_INPUT";
	case StatusCode::INVALID_OUTPUT:
		return "INVALID_OUTPUT";
	case StatusCode::OUT_OF_MEMORY:
		return "OUT_OF_MEMORY";
	case StatusCode::RUNTIME_ERROR:
		return "RUNTIME_ERROR";
	case StatusCode::JOB_PENDING:
		return "JOB_PENDING";
	case StatusCode::INVALID_JOB_ID:
		return "INVALID_JOB_ID";
	case StatusCode::RESOURCE_UNAVAILABLE:
		return "RESOURCE_UNAVAILABLE";
	case StatusCode::UNSUPPORTED:
		return "UNSUPPORTED";
	}
	return "UNKNOWN";
}

size_t get_data_type_size(DataType data_type)
{
	switch (data_type)
	{
	case DataType::INT8:
	case DataType::UINT8:
		return sizeof(int8_t);

	case DataType::INT16:
	case DataType::UINT16:
	case DataType::BF16:
	case DataType::FP16:
		return sizeof(uint16_t);

	case DataType::INT32:
		return sizeof(int32_t);

	case DataType::FLOAT32:
		return sizeof(float);

	case DataType::INT64:
		return sizeof(int64_t);

	default:
		vart_ml_log(LOG_WARN, "Trying to get size of UNKNOWN data type.\n");
		return 0;
	}
}

size_t VartMLRunner::compute_onnx_scratch_bytes(void) const
{
	size_t per_thread = 0;
	for (const auto& [order, node] : nodes_)
	{
		if (node.execution_mode != NodeExecutionMode::ONNX)
			continue;
		const auto* ctx = static_cast<const struct onnx_context_node*>(node.context_ptr);
		for (size_t i = 0; i < ctx->input_count; i++)
		{
			/* ctx->input_infos[i].size is 0 for variable-dim tensors; fall back to the
			 * same source used for staging buffer sizing to get a non-zero estimate. */
			per_thread +=
			    (ctx->input_infos[i].size > 0)
			        ? ctx->input_infos[i].size
			        : get_tensor_info_by_name(node.input_tensors_name[i], TensorType::CPU).size_in_bytes
			              * batchSize_;
		}
		for (size_t i = 0; i < ctx->output_count; i++)
		{
			per_thread +=
			    (ctx->output_infos[i].size > 0)
			        ? ctx->output_infos[i].size
			        : get_tensor_info_by_name(node.output_tensors_name[i], TensorType::CPU).size_in_bytes
			              * batchSize_;
		}
	}
	/* Staging buffers are pre-allocated per node_desc at thread pool init and already accounted
	 * for as heap usage. Multiply the per-inference onnxruntime scratch by the thread pool
	 * capacity: the ONNX path holds no NPU mutex, so all threads can run concurrently. */
	return per_thread * internal_buffers_.size();
}

std::any VartMLRunner::get_property(const std::string& key) const
{
	if (key == "model_name")
		return model_name_;
	if (key == "nb_systems")
		return nb_systems_;
	if (key == "nb_cores_per_system")
		return nb_cores_per_system_;
	if (key == "batch_size_per_core")
		return batch_size_per_core_;
	if (key == "nb_ddrs")
		return static_cast<uint8_t>(npu_get_nb_ddrs());
	if (key == "ddr_free_bytes")
	{
		std::vector<size_t> ddr_free(npu_get_nb_ddrs());
		for (size_t i = 0; i < ddr_free.size(); i++)
			ddr_free[i] = npu_get_ddr_free_bytes(i);
		return ddr_free;
	}
	if (key == "onnx_scratch_bytes")
		return compute_onnx_scratch_bytes();

	throw std::invalid_argument("Unknown property: " + key);
}

NpuTensorInfo& VartMLRunner::get_tensor_info_by_name(const std::string& tensor_name, TensorType type)
{
	auto search = tensors_.find(tensor_name);

	if (search == tensors_.end())
		throw std::runtime_error("Tensor " + tensor_name + " not found");

	TensorDirectionPriv tensor_direction = search->second.first;
	size_t              tensor_idx       = search->second.second;

	if (tensor_direction == TensorDirectionPriv::INPUT)
		return (type == TensorType::CPU) ? cpu_input_tensors_[tensor_idx] : hw_input_tensors_[tensor_idx];
	else if (tensor_direction == TensorDirectionPriv::OUTPUT)
		return (type == TensorType::CPU) ? cpu_output_tensors_[tensor_idx] : hw_output_tensors_[tensor_idx];
	else
		return (type == TensorType::CPU) ? cpu_internal_tensors_[tensor_idx]
		                                 : hw_internal_tensors_[tensor_idx];
}

void* VartMLRunner::malloc_buffer(uint64_t size, uint8_t ddr) const
{
	return npu_get_ddr_vaddr_from_vaddr(npu_malloc(size, ddr));
}

void* VartMLRunner::malloc_sub(void* parent_vaddr, size_t offset, size_t size) const
{
	return npu_malloc_sub(parent_vaddr, offset, size);
}

void VartMLRunner::free_buffer(void* buffer_ptr) const
{
	return npu_free(npu_get_ddr_vaddr_from_vaddr(buffer_ptr));
}

void VartMLRunner::dump_graph()
{
	printf("==== GRAPH DUMP ====\n");

	printf("Global information:\n");
	printf("- batch size: %zu\n", batchSize_);
	printf("- input names (total: %lu):\n", names_in_.size());
	for (const auto& name_in : names_in_)
		printf("  - %s\n", name_in.c_str());
	printf("- output names (total: %lu):\n", names_out_.size());
	for (const auto& name_out : names_out_)
		printf("  - %s\n", name_out.c_str());
	printf("\n");

	printf("Nodes (total %lu):\n", nodes_.size());
	for (const auto& [execution_order, node] : nodes_)
	{
		printf("- node %s:\n", node.name.c_str());
		printf("  - execution order: %u\n", execution_order);
		if (node.execution_mode == NodeExecutionMode::NPU)
			printf("  - execution mode:  NPU\n");
		else
			printf("  - execution mode:  ONNX\n");

		if (node.link_mode_in == NodeExtLinkMode::DIRECT)
			printf("  - link mode in:    DIRECT\n");
		else if (node.link_mode_in == NodeExtLinkMode::COPY_PTRS)
			printf("  - link mode in:    COPY_PTRS\n");
		else
			printf("  - link mode in:    COPY_BUFS\n");

		if (node.link_mode_out == NodeExtLinkMode::DIRECT)
			printf("  - link mode out:   DIRECT\n");
		else if (node.link_mode_out == NodeExtLinkMode::COPY_PTRS)
			printf("  - link mode out:   COPY_PTRS\n");
		else
			printf("  - link mode out:   COPY_BUFS\n");

		printf("  - inputs (total %lu):\n", node.input_count);
		for (size_t i = 0; i < node.input_count; i++)
		{
			printf("    - input %lu:\n", i);
			printf("      - name: %s\n", node.input_tensors_name[i].c_str());

			const std::vector<arm_ops>& in_arm_ops = arm_ops_.at(node.input_tensors_name[i]);
			printf("      - arm ops (total %lu):\n", in_arm_ops.size());
			for (size_t j = 0; j < in_arm_ops.size(); j++)
				printf("          - arm op %lu: %s\n", j, in_arm_ops[j].name.c_str());
		}

		printf("  - outputs (total %lu):\n", node.output_count);
		for (size_t i = 0; i < node.output_count; i++)
		{
			printf("    - output %lu:\n", i);
			printf("      - name: %s\n", node.output_tensors_name[i].c_str());

			const std::vector<arm_ops>& out_arm_ops = arm_ops_.at(node.output_tensors_name[i]);
			printf("      - arm ops (total %lu):\n", out_arm_ops.size());
			for (size_t j = 0; j < out_arm_ops.size(); j++)
				printf("          - arm op %lu: %s\n", j, out_arm_ops[j].name.c_str());
		}
	}
	printf("\n");

	printf("Tensors (total %lu):\n", tensors_.size());
	for (auto& [tensor_name, tensor] : tensors_)
	{
		get_tensor_info_by_name(tensor_name, TensorType::CPU).print();
		get_tensor_info_by_name(tensor_name, TensorType::HW).print();
	}

	printf("Arm Ops:\n\n");
	for (const auto& [tensor_name, arm_ops] : arm_ops_)
	{
		if (arm_ops.empty())
			continue;

		printf("  Tensor Name: %s\n\n", tensor_name.c_str());
		for (const auto& arm_op : arm_ops)
			print_arm_op(arm_op);
	}

	printf("==== GRAPH DUMP END ====\n");
}

void
VartMLRunner::dump_data(const std::string& node_name, char* in_out, size_t idx, const void* ptr, size_t size)
{
	std::string log_file_path = std::string(std::string("/tmp/") + model_name_ + std::string("/")) + node_name
	                            + "_" + in_out + "_" + std::to_string(idx) + ".log";
	std::ofstream log_file;
	log_file.open(log_file_path);

	vart_ml_log(LOG_INFO,
	            "[VART] Dump data for `%s' %s %lu to `%s'.\n",
	            node_name.c_str(),
	            in_out,
	            idx,
	            log_file_path.c_str());

	for (size_t k = 0; k < size; k++)
		log_file << ((const uint8_t*)ptr)[k];
	log_file.close();
}

} // namespace vart
