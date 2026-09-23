/**
 * @file vart_ml_py_api.h
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

#ifndef VART_ML_PY_API_H
#define VART_ML_PY_API_H

#include <optional>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <stdint.h>
#include <vector>

#include "vart_ml_runner/vart_ml_runner.hpp"

namespace py = pybind11;

/**
 * @brief VART ML Runner C++ pybind API
 *
 */
class __attribute__((visibility("hidden"))) VartMLRunner
{
  private:
	std::unique_ptr<vart::VartMLRunner> vartMLRunner_;
	std::vector<void*>                  inputs_ptr_;
	std::vector<py::array>              outputs_py_;
	std::vector<void*>                  outputs_ptr_;

  public:
	/**
	 * @brief Construct a new VART ML Runner from a snapshot path.
	 *
	 * @param[in] path Snapshot directory path.
	 * @param[in] network_name Name of the network (unused, kept for backward compatibility).
	 * @param[in] output_names An optional vector of names to change outputs order.
	 * @param[in] input_format An optional field to force the input shape format of all inputs.
	 * @param[in] output_format An optional field to force the output shape format of all outputs.
	 * @param[in] npu_only If true, run only NPU nodes.
	 */
	VartMLRunner(const std::string&                             snapshot_dir,
	             const std::string&                             network_name,
	             const std::optional<std::vector<std::string>>& output_names,
	             const std::optional<std::string>&              input_format,
	             const std::optional<std::string>&              output_format,
	             bool                                           npu_only = false);

	/**
	 * @brief Destroy the VART ML Runner.
	 */
	~VartMLRunner() = default;

	/**
	 * @brief Allocate ddr buffers capable of holding given number of images.
	 *
	 * @details The buffers will be allocated using the shapes of the model's input tensors.
	 *
	 * @details The buffers are allocated by the runner and released by its destructor.
	 *
	 * @param nb_images Number of images in the given buffer. If given 0, fallback to model's batch size.
	 *
	 * @return Array of pointers to the newly created buffers.
	 */
	std::vector<py::array> alloc_ddr_bufs(void);

	/**
	 * @brief Get the names of the model's input tensors.
	 *
	 * @return Array of names.
	 */
	std::vector<std::string> get_input_names(void);

	/**
	 * @brief Get the names of the model's output tensors.
	 *
	 * @return Array of names.
	 */
	std::vector<std::string> get_output_names(void);

	/**
	 * @brief Get the shapes of the model's input tensors.
	 *
	 * @return Array of shapes.
	 */
	std::vector<std::vector<uint32_t>> get_input_shapes(void);

	/**
	 * @brief Get the native shapes of the model's input tensors.
	 *
	 * @return Array of shapes.
	 */
	std::vector<std::vector<uint32_t>> get_input_native_shape(void);

	/**
	 * @brief Get the shape formats of the model's input tensors.
	 *
	 * @return Array of shape formats.
	 */
	std::vector<std::string> get_input_shape_formats(void);

	/**
	 * @brief Get the shapes of the model's output tensors.
	 *
	 * @return Array of shapes.
	 */
	std::vector<std::vector<uint32_t>> get_output_shapes(void);

	/**
	 * @brief Get the shape formats of the model's output tensors.
	 *
	 * @return Array of shape formats.
	 */
	std::vector<std::string> get_output_shape_formats(void);

	/**
	 * @brief Get the native shapes formats of the model's input tensors.
	 *
	 * @return Array of shapes.
	 */
	std::vector<std::string> get_input_native_shape_formats(void);

	/**
	 * @brief Get the native shapes formats of the model's output tensors.
	 *
	 * @return Array of shapes.
	 */
	std::vector<std::string> get_output_native_shape_formats(void);

	/**
	 * @brief Get the native strides of the model's input tensors.
	 *
	 * @return Array of strides.
	 */
	std::vector<std::vector<uint32_t>> get_input_native_strides(void);

	/**
	 * @brief Get the data types of the model's input tensors.
	 *
	 * @return Array of numpy data types.
	 */
	std::vector<py::dtype> get_input_types(void);

	/**
	 * @brief Get the native data types of the model's input tensors.
	 *
	 * @return Array of numpy data types.
	 */
	std::vector<py::dtype> get_input_native_types(void);

	/**
	 * @brief Set the input buffers as native.
	 */
	void set_input_native(void);

	/**
	 * @brief Set the output buffers as native.
	 */
	void set_output_native(void);

	/**
	 * @brief Set the input buffers as native and in ddr.
	 */
	void set_input_zero_copy(void);

	/**
	 * @brief Set the output buffers as native and in ddr.
	 */
	void set_output_zero_copy(void);

	/**
	 * @brief Get the quantization coeffs of the model's input tensors.
	 *
	 * @return Array of quantization coeffs.
	 */
	std::vector<float> get_input_coeffs(void);

	/**
	 * @brief Get the quantization coeffs of the model's output tensors.
	 *
	 * @return Array of quantization coeffs.
	 */
	std::vector<float> get_output_coeffs(void);

	/**
	 * @brief Execute the model with one or more inputs.
	 *
	 * @param inputs Array of input buffers (ndarray or types convertible via numpy.asarray, e.g.
	 * torch.Tensor).
	 *
	 * @return An array of output buffers.
	 */
	std::vector<py::array> execute(const std::vector<py::array> inputs);

	/**
	 * @brief Return the architecture of the FPGA.
	 *
	 * @return std::string Architecture of the FPGA.
	 */
	std::string architecture(void);

  private:
	void init_out_arrays(size_t batch_size = 0, bool native = false);

	int check_inputs(const std::vector<py::array> inputs);

	int alloc_tensors_ddr_bufs(const std::vector<vart::NpuTensorInfo>& tensors,
	                           std::vector<py::array>&                 ddr_bufs,
	                           std::vector<void*>*                     ddr_ptrs);

	size_t batchSize_;
	bool   zero_copy_in_     = false;
	bool   zero_copy_out_    = false;
	bool   native_in_        = false;
	bool   native_out_       = false;
	bool   dispatch_buffers_ = false;
};

#endif
