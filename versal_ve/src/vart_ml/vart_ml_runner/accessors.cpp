/**
 * @file accessors.cpp
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

#include "vart_ml_runner.hpp"

namespace vart
{
const std::vector<NpuTensorInfo>& VartMLRunner::get_tensors_info(TensorDirection direction,
                                                                 TensorType      type) const
{
	if (direction == TensorDirection::INPUT)
	{
		if (type == TensorType::CPU)
		{
			return cpu_input_tensors_;
		}
		else
		{ // TensorType::HW
			return hw_input_tensors_;
		}
	}
	else
	{ // TensorDirection::OUTPUT
		if (type == TensorType::CPU)
		{
			return cpu_output_tensors_;
		}
		else
		{ // TensorType::HW
			return hw_output_tensors_;
		}
	}
}

const NpuTensorInfo& VartMLRunner::get_tensor_info_by_name(const std::string& tensor_name,
                                                           TensorType         type) const
{
	return const_cast<VartMLRunner*>(this)->get_tensor_info_by_name(tensor_name, type);
}

const QuantParameters& VartMLRunner::get_quant_parameters(const std::string& tensor_name) const
{
	/* Retrieve quantization parameters for the specified tensor */
	if (!quant_params_.contains(tensor_name))
		throw std::runtime_error("Quantization parameters not found for tensor: " + tensor_name);

	return quant_params_.at(tensor_name);
}

size_t VartMLRunner::get_num_input_tensors(void) const { return names_in_.size(); }

size_t VartMLRunner::get_num_output_tensors(void) const { return names_out_.size(); }

size_t VartMLRunner::get_batch_size(void) const { return batchSize_; }

size_t VartMLRunner::get_batch_size(TensorDirection direction) const
{
	(void)direction;
	return get_batch_size();
}

size_t VartMLRunner::get_thread_pool_size(void) const { return thread_pool_->threads.size(); }

} // namespace vart
