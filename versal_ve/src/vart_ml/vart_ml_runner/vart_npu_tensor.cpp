/**
 * @file vart_npu_tensor.cpp
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

#include "io/io.h"
#include "utils/log.h"
#include "vart_ml_runner.hpp"
#include "vart_npu_tensor.hpp"
#include "vart_npu_tensor_impl.hpp"

namespace vart
{

std::string_view to_string(DataType value) noexcept
{
	switch (value)
	{
	case DataType::UNKNOWN:
		return "UNKNOWN";
	case DataType::BOOLEAN:
		return "BOOLEAN";
	case DataType::INT8:
		return "INT8";
	case DataType::UINT8:
		return "UINT8";
	case DataType::INT16:
		return "INT16";
	case DataType::UINT16:
		return "UINT16";
	case DataType::BF16:
		return "BF16";
	case DataType::FP16:
		return "FP16";
	case DataType::INT32:
		return "INT32";
	case DataType::UINT32:
		return "UINT32";
	case DataType::FLOAT32:
		return "FLOAT32";
	case DataType::INT64:
		return "INT64";
	case DataType::UINT64:
		return "UINT64";
	}
	return "UNKNOWN";
}

std::string_view to_string(MemoryLayout value) noexcept
{
	switch (value)
	{
	case MemoryLayout::UNKNOWN:
		return "UNKNOWN";
	case MemoryLayout::NC:
		return "NC";
	case MemoryLayout::NCH:
		return "NCH";
	case MemoryLayout::NHC:
		return "NHC";
	case MemoryLayout::NHW:
		return "NHW";
	case MemoryLayout::NWC:
		return "NWC";
	case MemoryLayout::NHWC:
		return "NHWC";
	case MemoryLayout::NCHW:
		return "NCHW";
	case MemoryLayout::NHWC4:
		return "NHWC4";
	case MemoryLayout::NHWC8:
		return "NHWC8";
	case MemoryLayout::NC4HW4:
		return "NC4HW4";
	case MemoryLayout::NC8HW8:
		return "NC8HW8";
	case MemoryLayout::HCWNC4:
		return "HCWNC4";
	case MemoryLayout::HCWNC8:
		return "HCWNC8";
	case MemoryLayout::HCWNC16:
		return "HCWNC16";
	case MemoryLayout::NHW16C4WC:
		return "NHW16C4WC";
	case MemoryLayout::NHW16WC4C:
		return "NHW16WC4C";
	case MemoryLayout::NH2HWC4C:
		return "NH2HWC4C";
	case MemoryLayout::NH2C4HWC:
		return "NH2C4HWC";
	case MemoryLayout::GENERIC:
		return "GENERIC";
	}
	return "UNKNOWN";
}

std::string_view to_string(MemoryType value) noexcept
{
	switch (value)
	{
	case MemoryType::UNKNOWN:
		return "UNKNOWN";
	case MemoryType::XRT_BO:
		return "XRT_BO";
	case MemoryType::DMA_FD:
		return "DMA_FD";
	case MemoryType::USER_POINTER_CMA:
		return "USER_POINTER_CMA";
	case MemoryType::USER_POINTER_NON_CMA:
		return "USER_POINTER_NON_CMA";
	}
	return "UNKNOWN";
}

std::string_view to_string(TensorDirection value) noexcept
{
	switch (value)
	{
	case TensorDirection::INPUT:
		return "INPUT";
	case TensorDirection::OUTPUT:
		return "OUTPUT";
	}
	return "UNKNOWN";
}

std::string_view to_string(TensorType value) noexcept
{
	switch (value)
	{
	case TensorType::CPU:
		return "CPU";
	case TensorType::HW:
		return "HW";
	}
	return "UNKNOWN";
}

static bool validate_npu_tensor_info(const NpuTensorInfo& info)
{
	bool        ok      = true;
	std::string err_msg = "";

	if (info.name.empty())
	{
		err_msg += "Tensor name must not be empty to ensure each tensor can be uniquely identified.\n";
		ok = false;
	}

	if (info.data_type == DataType::UNKNOWN)
	{
		err_msg += "Tensor data type must be known; tensors with UNKNOWN data type cannot be "
		           "processed or validated.\n";
		ok = false;
	}

	if ((info.direction != TensorDirection::INPUT) && (info.direction != TensorDirection::OUTPUT))
	{
		err_msg +=
		    "Tensor direction must be either INPUT, OUTPUT or INTERNAL to ensure proper tensor usage.\n";
		ok = false;
	}

	if ((info.tensor_type != TensorType::CPU) && (info.tensor_type != TensorType::HW))
	{
		err_msg += "Tensor type must be either CPU or HW to ensure proper tensor usage.\n";
		ok = false;
	}

	if (info.memory_layout == MemoryLayout::UNKNOWN)
	{
		err_msg += "Tensor memory layout must not be UNKNOWN to ensure a valid tensor memory arrangement.\n";
		ok = false;
	}

	if (info.memory_layout == MemoryLayout::GENERIC && info.memory_layout_order.empty())
	{
		err_msg += "For GENERIC memory layout, a layout order must be specified to define the dimension "
		           "order explicitly.\n";
		ok = false;
	}

	if (info.size == 0 || info.size_in_bytes == 0)
	{
		err_msg += "Both tensor size (number of elements) and tensor size_in_bytes (total memory in bytes) "
		           "must be non-zero to ensure the tensor is properly allocated and valid for computation "
		           "and memory operations.\n";
		ok = false;
	}

	if (info.shape.empty())
	{
		err_msg += "Tensor shape must not be empty; a tensor must have at least one dimension.\n";
		ok = false;
	}

	if (info.strides.empty())
	{
		err_msg +=
		    "Tensor strides must not be empty to ensure correct memory layout and access for the tensor.\n";
		ok = false;
	}

	if (!ok)
		vart_ml_log_err_msg(CONFIG_UNSUPPORTED_NPU_TENSOR_INFO, "%s\n", err_msg.c_str());

	return ok;
}

// Implementation of NpuTensorPriv validation method
void NpuTensorPriv::validate_and_initialize()
{
	if (!buffer_.get())
		throw std::runtime_error("Buffer pointer cannot be null");

	if (memory_type_ == MemoryType::UNKNOWN)
		throw std::runtime_error("MemoryType cannot be UNKNOWN");
	else if (memory_type_ == MemoryType::DMA_FD)
		throw std::runtime_error("MemoryType DMA_FD is not supported");

	if (!validate_npu_tensor_info(info_))
		throw std::runtime_error("Invalid NpuTensorInfo provided");

	if ((info_.tensor_type == TensorType::HW) && (memory_type_ == MemoryType::USER_POINTER_CMA))
		/* Hardware TensorType is not supported with User Pointer CMA MemoryType */
		throw std::runtime_error("Hardware TensorType is not supported with User Pointer CMA memory type");
}

void NpuTensorInfo::print() const
{
	vart_ml_log(LOG_INFO, "  Name:                 %s\n", name.c_str());
	vart_ml_log(LOG_INFO, "  Data Type:            %s\n", to_string(data_type).data());
	vart_ml_log(LOG_INFO, "  Direction:            %s\n", to_string(direction).data());
	vart_ml_log(LOG_INFO, "  Tensor Type:          %s\n", to_string(tensor_type).data());
	vart_ml_log(LOG_INFO, "  Memory Layout:        %s\n", to_string(memory_layout).data());
	if (MemoryLayout::GENERIC == memory_layout)
	{
		vart_ml_log(LOG_INFO, "  Memory Layout order:  ");
		for (const auto& dim : memory_layout_order)
			vart_ml_log(LOG_INFO, "%u ", dim);
		vart_ml_log(LOG_INFO, "\n");
	}
	vart_ml_log(LOG_INFO, "  Shape:                ");
	for (const auto& dim : shape)
		vart_ml_log(LOG_INFO, "%d ", dim);
	vart_ml_log(LOG_INFO, "\n");
	vart_ml_log(LOG_INFO, "  Strides:              ");
	for (const auto& stride : strides)
		vart_ml_log(LOG_INFO, "%u ", stride);
	vart_ml_log(LOG_INFO, "\n");
	vart_ml_log(LOG_INFO, "  Size:                 %zu\n", size);
	vart_ml_log(LOG_INFO, "  Size in Bytes:        %zu\n", size_in_bytes);
	vart_ml_log(LOG_INFO, "\n");
}

NpuTensor::NpuTensor() : priv_(std::make_shared<NpuTensorPriv>()) {}

NpuTensor::NpuTensor(const NpuTensorInfo& info, void* buffer, MemoryType mem_type)
    : priv_(std::make_shared<NpuTensorPriv>(info, buffer, mem_type))
{
}

NpuTensor::NpuTensor(const NpuTensorInfo& info, const void* buffer, MemoryType mem_type)
    : priv_(std::make_shared<NpuTensorPriv>(info, buffer, mem_type))
{
}

void* NpuTensor::get_buffer()
{
	if (!priv_)
	{
		vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                    "get_buffer() failed: invalid tensor state (null impl).\n");
		return nullptr;
	}

	if (priv_->is_const_buffer_)
	{
		vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                    "get_buffer() failed for tensor '%s': buffer is const.\n",
		                    priv_->info_.name.c_str());
		return nullptr;
	}

	return priv_->buffer_.get();
}

const void* NpuTensor::get_buffer() const
{
	if (!priv_)
	{
		vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                    "get_buffer() const failed: invalid tensor state (null impl).\n");
		return nullptr;
	}

	return static_cast<const void*>(priv_->buffer_.get());
}

void* NpuTensor::get_virtual_address()
{
	if (!priv_)
	{
		vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                    "get_virtual_address() failed: invalid tensor state (null impl).\n");
		return nullptr;
	}

	if (priv_->is_const_buffer_)
	{
		vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                    "get_virtual_address() failed for tensor '%s': buffer is const.\n",
		                    priv_->info_.name.c_str());
		return nullptr;
	}

	switch (priv_->memory_type_)
	{
	case MemoryType::DMA_FD:
		/* Virtual address retrieval is not supported for DMA_FD */
		vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                    "get_virtual_address() is unsupported for DMA_FD tensor '%s'.\n",
		                    priv_->info_.name.c_str());
		return nullptr;

	case MemoryType::XRT_BO:
		return npu_get_ddr_vaddr_from_vaddr(priv_->buffer_.get());

	default:
		return priv_->buffer_.get();
	}
}

const void* NpuTensor::get_virtual_address() const
{
	if (!priv_)
	{
		vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                    "get_virtual_address() failed: invalid tensor state (null impl).\n");
		return nullptr;
	}

	switch (priv_->memory_type_)
	{
	case MemoryType::DMA_FD:
		/* Virtual address retrieval is not supported for DMA_FD */
		vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                    "get_virtual_address() const is unsupported for DMA_FD tensor '%s'.\n",
		                    priv_->info_.name.c_str());
		return nullptr;

	case MemoryType::XRT_BO:
		return npu_get_ddr_vaddr_from_vaddr(priv_->buffer_.get());

	default:
		return priv_->buffer_.get();
	}
}

uint64_t NpuTensor::get_physical_address() const
{
	if (!priv_)
	{
		vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                    "get_physical_address() failed: invalid tensor state (null impl).\n");
		return 0;
	}

	/* Physical address retrieval is supported only for MemoryType::XRT_BO */
	if (MemoryType::XRT_BO == priv_->memory_type_)
		return npu_get_phy_addr_from_ddr_vaddr(priv_->buffer_.get());

	vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
	                    "get_physical_address() is unsupported for non-XRT_BO tensor '%s'.\n",
	                    priv_->info_.name.c_str());
	return 0;
}

const NpuTensorInfo& NpuTensor::get_info() const
{
	if (!priv_)
	{
		static NpuTensorInfo empty_info;
		return empty_info;
	}
	return priv_->info_;
}

MemoryType NpuTensor::get_memory_type() const
{
	if (!priv_)
		return MemoryType::UNKNOWN;
	return priv_->memory_type_;
}

void NpuTensor::sync_buffer(void) const
{
	if (!priv_)
	{
		vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                    "sync_buffer() failed: invalid tensor state (null impl).\n");
		return;
	}

	if (!priv_->is_wrapped_)
	{
		if (priv_->buffer_.get())
		{
			if (TensorDirection::INPUT == priv_->info_.direction)
				npu_sync_buffer_to_device(priv_->buffer_.get());
			else
				npu_sync_buffer_from_device(priv_->buffer_.get());
		}
		else
		{
			vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
			                    "sync_buffer() failed for tensor '%s': buffer is null.\n",
			                    priv_->info_.name.c_str());
		}
	}
	else
	{
		vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                    "sync_buffer() failed for tensor '%s': wrapped buffers are not supported.\n",
		                    priv_->info_.name.c_str());
		throw std::runtime_error("Buffer synchronization is not supported for wrapped tensors.");
	}
}

void NpuTensor::print_info() const
{
	if (!priv_)
	{
		vart_ml_log(LOG_INFO, "NpuTensor Info: [Invalid - moved-from object]\n");
		return;
	}

	vart_ml_log(LOG_INFO, "NpuTensor Info:\n");
	priv_->info_.print();
	vart_ml_log(LOG_INFO, " Memory Type: %s\n", to_string(priv_->memory_type_).data());
}

int NpuTensor::export_buffer() const
{
	if (!priv_ || !priv_->buffer_)
	{
		vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                    "export_buffer() failed: invalid tensor state (null impl or buffer).\n");
		return -1;
	}

	switch (priv_->memory_type_)
	{
	case MemoryType::XRT_BO:
		return npu_export_buffer(priv_->buffer_.get());

	case MemoryType::DMA_FD:
		vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                    "export_buffer() failed: MemoryType DMA_FD is not supported.\n");
		return -1;

	case MemoryType::USER_POINTER_CMA:
		vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                    "export_buffer() failed: MemoryType USER_POINTER_CMA is not supported.\n");
		return -1;

	case MemoryType::USER_POINTER_NON_CMA:
		vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                    "export_buffer() failed: MemoryType USER_POINTER_NON_CMA is not supported.\n");
		return -1;

	case MemoryType::UNKNOWN:
	default:
		vart_ml_log_err_msg(vart_ml_error::DEVICE_UNEXPECTED_ARG_VALUE,
		                    "export_buffer() failed: unknown memory type.\n");
		return -1;
	}
}
} // namespace vart
