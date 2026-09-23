/**
 * @file vart_npu_tensor_impl.hpp
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
 */

#pragma once

#include <memory>
#include <mutex>
#include "vart_npu_tensor.hpp"

namespace vart
{

/**
 * @brief Private implementation class for NpuTensor.
 *
 * This class contains the actual implementation details of NpuTensor.
 * It is only intended for internal use and advanced access scenarios.
 *
 * @warning This class is part of the internal implementation and may
 * change without notice. Use NpuTensorPrivAccess for controlled access.
 */
class NpuTensorPriv
{
  public:
	NpuTensorInfo         info_;
	std::shared_ptr<void> parent_buffer_;
	std::shared_ptr<void> buffer_;
	MemoryType            memory_type_;
	bool                  is_const_buffer_; // true if buffer was provided as const void*
	bool                  is_wrapped_;      // true if buffer_ is user-provided, false if Runner allocated
	bool                  is_sub_tensor_;   // true if tensor is a sub-tensor of a parent tensor
	bool                  is_packed_;       // true if all frames of a batch are contiguous in DDR memory

	NpuTensorPriv()
	    : info_({}),
	      parent_buffer_(nullptr),
	      buffer_(nullptr),
	      memory_type_(MemoryType::UNKNOWN),
	      is_wrapped_(true),
	      is_sub_tensor_(false),
	      is_packed_(false)
	{
	}

	NpuTensorPriv(const NpuTensorInfo& info, void* buffer, const MemoryType& mem_type)
	    : info_(info),
	      parent_buffer_(nullptr),
	      buffer_(buffer, [](void*) {}),
	      memory_type_(mem_type),
	      is_const_buffer_(false),
	      is_wrapped_(true),
	      is_sub_tensor_(false),
	      is_packed_(false)
	{
		validate_and_initialize();
	}

	NpuTensorPriv(const NpuTensorInfo& info, const void* buffer, MemoryType mem_type)
	    : info_(info),
	      parent_buffer_(nullptr),
	      buffer_(const_cast<void*>(buffer), [](void*) {}),
	      memory_type_(mem_type),
	      is_const_buffer_(true),
	      is_wrapped_(true),
	      is_sub_tensor_(false),
	      is_packed_(false)
	{
		validate_and_initialize();
	}

	// Delete copy constructor and copy assignment operator
	NpuTensorPriv(const NpuTensorPriv&)            = delete;
	NpuTensorPriv& operator=(const NpuTensorPriv&) = delete;

	~NpuTensorPriv() = default;

  private:
	void validate_and_initialize();
};

/**
 * @brief Provides controlled access to NpuTensor implementation details.
 *
 * This class allows specific components to access the internal implementation
 * of NpuTensor while maintaining encapsulation for general usage.
 *
 * @warning Direct implementation access should be used sparingly and only
 * when absolutely necessary, as it breaks the encapsulation provided by PIMPL.
 *
 * @example
 * @code
 * #include "vart_npu_tensor_impl.hpp"
 *
 * void advanced_tensor_operation(NpuTensor& tensor) {
 *   NpuTensorPriv* impl = NpuTensorPrivAccess::get_impl(tensor);
 *   void* buffer = impl->buffer_.get();
 *   MemoryType type = impl->memory_type_;
 *   bool wrapped = impl->is_wrapped_;
 *   bool sub    = impl->is_sub_tensor_;
 *   bool packed = impl->is_packed_;
 * }
 * @endcode
 */
class NpuTensorPrivAccess
{
  public:
	/**
	 * @brief Get mutable access to tensor implementation.
	 * @param tensor The NpuTensor to access.
	 * @return Pointer to the implementation object.
	 */
	static NpuTensorPriv* get_impl(NpuTensor& tensor) { return tensor.priv_.get(); }

	/**
	 * @brief Get const access to tensor implementation.
	 * @param tensor The NpuTensor to access.
	 * @return Const pointer to the implementation object.
	 */
	static const NpuTensorPriv* get_impl(const NpuTensor& tensor) { return tensor.priv_.get(); }

  private:
	NpuTensorPrivAccess() = delete; // Static-only class
};

} // namespace vart
