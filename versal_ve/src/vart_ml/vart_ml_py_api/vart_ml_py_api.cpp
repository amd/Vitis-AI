/**
 * @file vart_ml_py_api.cpp
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
#include <any>
#include <cassert>
#include <cstring>
#include <map>
#include <vector>

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "io/io.h"
#include "onnx_runner/onnx_runner.h"
#include "utils/error.h"
#include "utils/fpga_info.h"
#include "utils/log.h"
#include "vart_ml_py_api.h"
#include "vart_ml_runner/vart_ml_runner.hpp"
#include "vart_ml_runner/vart_npu_tensor_impl.hpp"

#define PY_C_CONTIGUOUS py::detail::npy_api::constants::NPY_ARRAY_C_CONTIGUOUS_

// Accept ndarray, torch.Tensor, and other array-likes via numpy.asarray (no libtorch link).
static inline py::array numpy_asarray(py::handle h)
{
	// If handle is already a py::array, return it.
	if (py::isinstance<py::array>(h))
		return py::reinterpret_borrow<py::array>(h);

	static const py::object np = py::module_::import("numpy");
	return np.attr("asarray")(py::reinterpret_borrow<py::object>(h)).cast<py::array>();
}

static inline std::vector<py::array> vartml_dispatch_execute(VartMLRunner& self, py::handle inputs)
{
	// Handle tuple/list of inputs.
	if (py::isinstance<py::tuple>(inputs) || py::isinstance<py::list>(inputs))
	{
		py::sequence seq = py::reinterpret_borrow<py::sequence>(inputs);

		std::vector<py::array> arrs;
		arrs.reserve(static_cast<size_t>(seq.size()));
		for (py::handle item : seq)
			arrs.push_back(numpy_asarray(item));

		return self.execute(arrs);
	}

	return self.execute({ numpy_asarray(inputs) });
}

struct DDRBufferDeleter
{
	void*               parent_buf;
	std::vector<void*>  bufs;
	vart::VartMLRunner* runner;
};

VartMLRunner::VartMLRunner(const std::string&                             snapshot_dir,
                           const std::string&                             network_name,
                           const std::optional<std::vector<std::string>>& output_names,
                           const std::optional<std::string>&              input_format,
                           const std::optional<std::string>&              output_format,
                           bool                                           npu_only)
    : batchSize_()
{
	// NpuRunner gets network name from iriz graph, so its safe to discard user input.
	(void)network_name;

	dispatch_buffers_ = check_user_config("debug.py_iobuf_allddrs");

	// Set npu_only and output_names options.
	std::unordered_map<std::string, std::any> runner_options;
	runner_options["npu_only"] = npu_only;
	if (output_names.has_value() && !output_names.value().empty())
		runner_options["output_names"] = output_names.value();

	if (input_format.has_value() && !input_format.value().empty() && input_format.value() != "from_snapshot")
		runner_options["in_shape_format"] = input_format.value();

	if (output_format.has_value() && !output_format.value().empty()
	    && output_format.value() != "from_snapshot")
		runner_options["out_shape_format"] = output_format.value();

	// Always set arm_op_views in the py api.
	runner_options["arm_op_views"] = true;

	// Only async execution needs multiple threads. Each thread allocates its own IO buffers in DDR,
	// so defaulting to hardware_concurrency() wastes DDR on unused copies.
	runner_options["nb_threads"] = (size_t)1;

	vartMLRunner_ = std::make_unique<vart::VartMLRunner>(snapshot_dir, runner_options);
	batchSize_    = vartMLRunner_->get_batch_size();

	// Set ptrs and py arrays to their definitive size.
	inputs_ptr_.resize(vartMLRunner_->get_num_input_tensors() * batchSize_, nullptr);
	outputs_ptr_.resize(vartMLRunner_->get_num_output_tensors() * batchSize_, nullptr);
	outputs_py_.resize(vartMLRunner_->get_num_output_tensors());
}

int VartMLRunner::alloc_tensors_ddr_bufs(const std::vector<vart::NpuTensorInfo>& tensors,
                                         std::vector<py::array>&                 ddr_bufs,
                                         std::vector<void*>*                     ddr_ptrs)
{
	uint8_t nb_ddrs = std::any_cast<uint8_t>(vartMLRunner_->get_property("nb_ddrs"));

	for (size_t i = 0; i < tensors.size(); i++)
	{
		void* buf = vartMLRunner_->malloc_buffer(tensors[i].size_in_bytes * batchSize_, i % nb_ddrs);

		std::vector<void*> bufs;
		bufs.reserve(batchSize_);
		if (ddr_ptrs != nullptr)
		{
			// If input buffers need to be dispatched across DDRs, alloc such buffers.
			if (dispatch_buffers_)
				for (size_t b = 0; b < batchSize_; b++)
				{
					bufs.push_back(vartMLRunner_->malloc_buffer(tensors[i].size_in_bytes, (i + b) % nb_ddrs));

					(*ddr_ptrs)[b * tensors.size() + i] = bufs.back();
				}
			else
				for (size_t b = 0; b < batchSize_; b++)
				{
					bufs.push_back(vartMLRunner_->malloc_sub(
					    buf, b * tensors[i].size_in_bytes, tensors[i].size_in_bytes));

					(*ddr_ptrs)[b * tensors.size() + i] = bufs.back();
				}
		}

		// Use native shape.
		std::vector<ssize_t> shape(tensors[i].shape.begin(), tensors[i].shape.end());

		// Use native strides.
		std::vector<ssize_t> strides(tensors[i].strides.begin(), tensors[i].strides.end());

		size_t data_size = vart::get_data_type_size(tensors[i].data_type);
		std::for_each(strides.begin(), strides.end(), [data_size](ssize_t& stride) { stride *= data_size; });

		// Allocation based on memory data type.
		py::dtype out_py_type;
		switch (tensors[i].data_type)
		{
		case vart::DataType::INT8:
			out_py_type = py::dtype::of<int8_t>();
			break;

		case vart::DataType::BOOLEAN:
		case vart::DataType::UINT8:
			out_py_type = py::dtype::of<uint8_t>();
			break;

		case vart::DataType::INT16:
			out_py_type = py::dtype::of<int16_t>();
			break;

		case vart::DataType::UINT16:
		case vart::DataType::BF16:
		case vart::DataType::FP16:
			out_py_type = py::dtype::of<uint16_t>();
			break;

		case vart::DataType::INT32:
			out_py_type = py::dtype::of<int32_t>();
			break;

		case vart::DataType::UINT32:
			out_py_type = py::dtype::of<uint32_t>();
			break;

		case vart::DataType::FLOAT32:
			out_py_type = py::dtype::of<float>();
			break;

		case vart::DataType::INT64:
			out_py_type = py::dtype::of<int64_t>();
			break;

		case vart::DataType::UINT64:
			out_py_type = py::dtype::of<uint64_t>();
			break;

		case vart::DataType::UNKNOWN:
			throw std::runtime_error("Tensor of unknown type.");
		}

		auto*       deleter = new DDRBufferDeleter{ buf, bufs, vartMLRunner_.get() };
		py::capsule cap(deleter, [](void* p) {
			auto* d = static_cast<DDRBufferDeleter*>(p);
			for (void* b : d->bufs)
				d->runner->free_buffer(b);
			d->runner->free_buffer(d->parent_buf);
			delete d;
		});
		ddr_bufs[i] = py::array(out_py_type, shape, strides, buf, cap);
	}

	return vart_ml_error::SUCCESS;
}

std::vector<py::array> VartMLRunner::alloc_ddr_bufs()
{
	auto input_tensors = vartMLRunner_->get_tensors_info(vart::TensorDirection::INPUT, vart::TensorType::HW);

	std::vector<py::array> ddr_bufs(input_tensors.size());

	int err = this->alloc_tensors_ddr_bufs(input_tensors, ddr_bufs, &inputs_ptr_);
	if (err)
		throw std::runtime_error(vart_ml_error::exception_message(err));

	return ddr_bufs;
}

std::vector<std::string> VartMLRunner::get_input_names()
{
	auto input_tensors = vartMLRunner_->get_tensors_info(vart::TensorDirection::INPUT, vart::TensorType::CPU);

	std::vector<std::string> input_names;
	input_names.reserve(input_tensors.size());

	for (const auto& input_tensor : input_tensors)
		input_names.push_back(input_tensor.name);

	return input_names;
}

std::vector<std::string> VartMLRunner::get_output_names()
{
	auto output_tensors =
	    vartMLRunner_->get_tensors_info(vart::TensorDirection::OUTPUT, vart::TensorType::CPU);

	std::vector<std::string> output_names;
	output_names.reserve(output_tensors.size());

	for (const auto& output_tensor : output_tensors)
		output_names.push_back(output_tensor.name);

	return output_names;
}

std::vector<std::vector<uint32_t>> VartMLRunner::get_input_shapes()
{
	auto input_tensors = vartMLRunner_->get_tensors_info(vart::TensorDirection::INPUT, vart::TensorType::CPU);

	std::vector<std::vector<uint32_t>> input_shapes;
	input_shapes.reserve(input_tensors.size());

	for (const auto& input_tensor : input_tensors)
		input_shapes.push_back(input_tensor.shape);

	return input_shapes;
}

std::vector<std::string> VartMLRunner::get_input_shape_formats()
{
	auto input_tensors = vartMLRunner_->get_tensors_info(vart::TensorDirection::INPUT, vart::TensorType::CPU);

	std::vector<std::string> input_shape_formats;
	input_shape_formats.reserve(input_tensors.size());

	for (const auto& input_tensor : input_tensors)
		input_shape_formats.push_back(std::string(vart::to_string(input_tensor.memory_layout)));

	return input_shape_formats;
}

std::vector<std::vector<uint32_t>> VartMLRunner::get_output_shapes()
{
	auto output_tensors =
	    vartMLRunner_->get_tensors_info(vart::TensorDirection::OUTPUT, vart::TensorType::CPU);

	std::vector<std::vector<uint32_t>> output_shapes;
	output_shapes.reserve(output_tensors.size());

	for (const auto& output_tensor : output_tensors)
		output_shapes.push_back(output_tensor.shape);

	return output_shapes;
}

std::vector<std::string> VartMLRunner::get_output_shape_formats()
{
	auto output_tensors =
	    vartMLRunner_->get_tensors_info(vart::TensorDirection::OUTPUT, vart::TensorType::CPU);

	std::vector<std::string> output_shape_formats;
	output_shape_formats.reserve(output_tensors.size());

	for (const auto& output_tensor : output_tensors)
		output_shape_formats.push_back(std::string(vart::to_string(output_tensor.memory_layout)));

	return output_shape_formats;
}

std::vector<std::vector<uint32_t>> VartMLRunner::get_input_native_shape()
{
	auto input_tensors = vartMLRunner_->get_tensors_info(vart::TensorDirection::INPUT, vart::TensorType::HW);

	std::vector<std::vector<uint32_t>> input_native_shape;
	input_native_shape.reserve(input_tensors.size());

	for (const auto& input_tensor : input_tensors)
		input_native_shape.push_back(input_tensor.shape);

	return input_native_shape;
}

std::vector<std::string> VartMLRunner::get_input_native_shape_formats()
{
	auto input_tensors = vartMLRunner_->get_tensors_info(vart::TensorDirection::INPUT, vart::TensorType::HW);

	std::vector<std::string> input_native_shape_formats;
	input_native_shape_formats.reserve(input_tensors.size());

	for (const auto& input_tensor : input_tensors)
		input_native_shape_formats.push_back(std::string(vart::to_string(input_tensor.memory_layout)));

	return input_native_shape_formats;
}

std::vector<std::string> VartMLRunner::get_output_native_shape_formats()
{
	auto output_tensors =
	    vartMLRunner_->get_tensors_info(vart::TensorDirection::OUTPUT, vart::TensorType::HW);

	std::vector<std::string> output_native_shape_formats;
	output_native_shape_formats.reserve(output_tensors.size());

	for (const auto& output_tensor : output_tensors)
		output_native_shape_formats.push_back(std::string(vart::to_string(output_tensor.memory_layout)));

	return output_native_shape_formats;
}

std::vector<std::vector<uint32_t>> VartMLRunner::get_input_native_strides()
{
	auto input_tensors = vartMLRunner_->get_tensors_info(vart::TensorDirection::INPUT, vart::TensorType::HW);

	std::vector<std::vector<uint32_t>> input_native_strides;
	input_native_strides.reserve(input_tensors.size());

	for (const auto& input_tensor : input_tensors)
	{
		size_t data_size = vart::get_data_type_size(input_tensor.data_type);

		std::vector<uint32_t> strides = input_tensor.strides;
		std::for_each(strides.begin(), strides.end(), [data_size](uint32_t& stride) { stride *= data_size; });

		input_native_strides.push_back(strides);
	}

	return input_native_strides;
}

std::vector<py::dtype> VartMLRunner::get_input_types()
{
	auto input_tensors = vartMLRunner_->get_tensors_info(vart::TensorDirection::INPUT, vart::TensorType::CPU);

	std::vector<py::dtype> input_types;
	input_types.reserve(input_tensors.size());

	for (const auto& input_tensor : input_tensors)
	{
		switch (input_tensor.data_type)
		{
		case vart::DataType::INT8:
			input_types.push_back(py::dtype("int8"));
			break;

		case vart::DataType::BOOLEAN:
		case vart::DataType::UINT8:
			input_types.push_back(py::dtype("uint8"));
			break;

		case vart::DataType::INT16:
			input_types.push_back(py::dtype("int16"));
			break;

		case vart::DataType::UINT16:
		case vart::DataType::BF16:
		case vart::DataType::FP16:
			input_types.push_back(py::dtype("uint16"));
			break;

		case vart::DataType::INT32:
			input_types.push_back(py::dtype("int32"));
			break;

		case vart::DataType::UINT32:
			input_types.push_back(py::dtype("uint32"));
			break;

		case vart::DataType::FLOAT32:
			input_types.push_back(py::dtype("float32"));
			break;

		case vart::DataType::INT64:
			input_types.push_back(py::dtype("int64"));
			break;

		case vart::DataType::UINT64:
			input_types.push_back(py::dtype("uint64"));
			break;

		case vart::DataType::UNKNOWN:
			throw std::runtime_error("Tensor of unknown type.");
		}
	}

	return input_types;
}

std::vector<py::dtype> VartMLRunner::get_input_native_types()
{
	auto input_tensors = vartMLRunner_->get_tensors_info(vart::TensorDirection::INPUT, vart::TensorType::HW);

	std::vector<py::dtype> input_types;
	input_types.reserve(input_tensors.size());

	for (const auto& input_tensor : input_tensors)
	{
		vart::DataType dtype = input_tensor.data_type;
		switch (dtype)
		{
		case vart::DataType::INT8:
			input_types.push_back(py::dtype("int8"));
			break;

		case vart::DataType::BOOLEAN:
		case vart::DataType::UINT8:
			input_types.push_back(py::dtype("uint8"));
			break;

		case vart::DataType::INT16:
			input_types.push_back(py::dtype("int16"));
			break;

		case vart::DataType::UINT16:
		case vart::DataType::BF16:
		case vart::DataType::FP16:
			input_types.push_back(py::dtype("uint16"));
			break;

		case vart::DataType::INT32:
			input_types.push_back(py::dtype("int32"));
			break;

		case vart::DataType::UINT32:
			input_types.push_back(py::dtype("uint32"));
			break;

		case vart::DataType::FLOAT32:
			input_types.push_back(py::dtype("float32"));
			break;

		case vart::DataType::INT64:
			input_types.push_back(py::dtype("int64"));
			break;

		case vart::DataType::UINT64:
			input_types.push_back(py::dtype("uint64"));
			break;

		case vart::DataType::UNKNOWN:
			throw std::runtime_error("Tensor of unknown type.");
		}
	}

	return input_types;
}

void VartMLRunner::set_input_native() { native_in_ = true; }

void VartMLRunner::set_output_native() { native_out_ = true; }

void VartMLRunner::set_input_zero_copy() { zero_copy_in_ = true; }

void VartMLRunner::set_output_zero_copy() { zero_copy_out_ = true; }

std::vector<float> VartMLRunner::get_input_coeffs()
{
	auto input_tensors = vartMLRunner_->get_tensors_info(vart::TensorDirection::INPUT, vart::TensorType::CPU);

	std::vector<float> input_coeffs;
	input_coeffs.reserve(input_tensors.size());

	for (const auto& input_tensor : input_tensors)
		input_coeffs.push_back(vartMLRunner_->get_quant_parameters(input_tensor.name).scale);

	return input_coeffs;
}

std::vector<float> VartMLRunner::get_output_coeffs()
{
	auto output_tensors =
	    vartMLRunner_->get_tensors_info(vart::TensorDirection::OUTPUT, vart::TensorType::CPU);

	std::vector<float> output_coeffs;
	output_coeffs.reserve(output_tensors.size());

	for (const auto& output_tensor : output_tensors)
		output_coeffs.push_back(vartMLRunner_->get_quant_parameters(output_tensor.name).scale);

	return output_coeffs;
}

std::vector<py::array> VartMLRunner::execute(const std::vector<py::array> inputs)
{
	// Check that buffers dispatching is only enabled with zero copy.
	assert(!dispatch_buffers_ || (zero_copy_in_ && zero_copy_out_));

	int err = check_inputs(inputs);
	if (err)
		throw std::runtime_error(vart_ml_error::exception_message(err));

	vart::TensorType input_tensor_type  = vart::TensorType::CPU;
	vart::TensorType output_tensor_type = vart::TensorType::CPU;

	vart::MemoryType memory_type_in  = vart::MemoryType::USER_POINTER_CMA;
	vart::MemoryType memory_type_out = vart::MemoryType::USER_POINTER_CMA;

	// Get the batch size of the first input and consider this is the batch size of all inputs / outputs.
	size_t batch_size = inputs[0].request().shape[0];

	if (batch_size > vartMLRunner_->get_batch_size())
		throw std::runtime_error("Input batch size (" + std::to_string(batch_size)
		                         + ") exceeds model batch size ("
		                         + std::to_string(vartMLRunner_->get_batch_size()) + ").");

	batchSize_ = batch_size;

	// Put input buffers in a C array while converting to batch_first format.
	for (size_t i = 0; i < vartMLRunner_->get_num_input_tensors(); i++)
	{
		py::array input = inputs[i];

		std::vector<uint32_t> input_strides;
		for (ssize_t d = 0; d < input.ndim(); d++)
			input_strides.push_back((uint32_t)input.strides()[d]);

		// If this input has inconsistent batch size, issue a warning.
		if ((size_t)input.request().shape[0] != batch_size)
		{
			vart_ml_log(LOG_WARN,
			            "Inconsistent batch sizes across inputs. Input 0 has `%lu' and input %lu has `%lu'.",
			            batch_size,
			            i,
			            input.request().shape[0]);
		}

		// Given inputs may not be contiguous. If they are not, copy as contiguous to a new array. If the
		// input's strides are in descending order, they have already been dealt with by setting the
		// strides of the tensor.
		if ((PY_C_CONTIGUOUS != (input.flags() & PY_C_CONTIGUOUS))
		    && !std::is_sorted(input_strides.crbegin(), input_strides.crend()))
		{
			if (input.dtype().is(pybind11::dtype::of<int8_t>()))
				input = input.cast<py::array_t<int8_t, py::array::c_style | py::array::forcecast>>();
			else if (input.dtype().is(pybind11::dtype::of<uint8_t>()))
				input = input.cast<py::array_t<uint8_t, py::array::c_style | py::array::forcecast>>();
			else if (input.dtype().is(pybind11::dtype::of<uint16_t>()))
				input = input.cast<py::array_t<uint16_t, py::array::c_style | py::array::forcecast>>();
			else if (input.dtype().is(pybind11::dtype::of<float>()))
				input = input.cast<py::array_t<float, py::array::c_style | py::array::forcecast>>();
			else if (input.dtype().is(pybind11::dtype::of<int32_t>()))
				input = input.cast<py::array_t<int32_t, py::array::c_style | py::array::forcecast>>();
			else if (input.dtype().is(pybind11::dtype::of<uint32_t>()))
				input = input.cast<py::array_t<uint32_t, py::array::c_style | py::array::forcecast>>();
			else if (input.dtype().is(pybind11::dtype::of<int64_t>()))
				input = input.cast<py::array_t<int64_t, py::array::c_style | py::array::forcecast>>();
			else
				throw std::invalid_argument("Data type not supported");
		}

		const uint8_t* data_ptr  = (const uint8_t*)input.data();
		size_t         elem_size = input_strides[0];

		// If input buffers need to be dispatched across DDRs, we need to copy user inputs to such buffers.
		if (dispatch_buffers_)
			for (size_t b = 0; b < batch_size; b++)
				memcpy(inputs_ptr_[b * vartMLRunner_->get_num_input_tensors() + i],
				       data_ptr + (b * elem_size),
				       elem_size);
		else
			for (size_t b = 0; b < batch_size; b++)
				inputs_ptr_[b * vartMLRunner_->get_num_input_tensors() + i] =
				    const_cast<uint8_t*>(data_ptr + (b * elem_size));
	}

	if (native_in_)
	{
		memory_type_in    = vart::MemoryType::USER_POINTER_NON_CMA;
		input_tensor_type = vart::TensorType::HW;
	}
	else if (zero_copy_in_)
	{
		if (batch_size != batchSize_)
			throw std::runtime_error("Batch can not be incomplete in zero copy mode");
		memory_type_in    = vart::MemoryType::XRT_BO;
		input_tensor_type = vart::TensorType::HW;
	}

	if (native_out_)
	{
		memory_type_out    = vart::MemoryType::USER_POINTER_NON_CMA;
		output_tensor_type = vart::TensorType::HW;
	}
	else if (zero_copy_out_)
	{
		memory_type_out    = vart::MemoryType::XRT_BO;
		output_tensor_type = vart::TensorType::HW;
	}

	// Initialize out arrays.
	if (zero_copy_out_)
	{
		if (alloc_tensors_ddr_bufs(
		        vartMLRunner_->get_tensors_info(vart::TensorDirection::OUTPUT, vart::TensorType::HW),
		        outputs_py_,
		        &outputs_ptr_))
			throw std::runtime_error("Error during output tensors allocation\n");
	}
	else
		init_out_arrays(batch_size, native_out_);

	auto input_tensors  = vartMLRunner_->get_tensors_info(vart::TensorDirection::INPUT, input_tensor_type);
	auto output_tensors = vartMLRunner_->get_tensors_info(vart::TensorDirection::OUTPUT, output_tensor_type);

	/* Prepare run tensors */
	std::vector<std::vector<vart::NpuTensor>> runInputTensors(batch_size);
	std::vector<std::vector<vart::NpuTensor>> runOutputTensors(batch_size);

	for (size_t b = 0; b < batch_size; b++)
	{
		for (size_t i = 0; i < input_tensors.size(); i++)
		{
			runInputTensors[b].push_back(vart::NpuTensor(
			    input_tensors[i], (void*)inputs_ptr_[b * input_tensors.size() + i], memory_type_in));

			auto input_tensors_cpu =
			    vartMLRunner_->get_tensors_info(vart::TensorDirection::INPUT, vart::TensorType::CPU)[i];

			if (input_tensors[i].size_in_bytes == input_tensors_cpu.size_in_bytes
			    && batch_size == vartMLRunner_->get_batch_size())
				vart::NpuTensorPrivAccess::get_impl(runInputTensors[b][i])->is_packed_ = !dispatch_buffers_;
		}

		for (size_t i = 0; i < output_tensors.size(); i++)
		{
			runOutputTensors[b].push_back(vart::NpuTensor(
			    output_tensors[i], outputs_ptr_[b * output_tensors.size() + i], memory_type_out));

			auto output_tensors_cpu =
			    vartMLRunner_->get_tensors_info(vart::TensorDirection::OUTPUT, vart::TensorType::CPU)[i];

			if (output_tensors[i].size_in_bytes == output_tensors_cpu.size_in_bytes
			    && batch_size == vartMLRunner_->get_batch_size())
				vart::NpuTensorPrivAccess::get_impl(runOutputTensors[b][i])->is_packed_ = !dispatch_buffers_;
		}
	}

	// Start model execution.
	vart::StatusCode status = vartMLRunner_->execute(runInputTensors, runOutputTensors);
	if (status != vart::StatusCode::SUCCESS)
		throw std::runtime_error("Error during runner execution\n");

	// If output buffers are dispatched across DDRs, copy data to user outputs.
	if (dispatch_buffers_)
		for (size_t i = 0; i < output_tensors.size(); i++)
			for (size_t b = 0; b < batch_size; b++)
				memcpy((uint8_t*)outputs_py_[i].data()
				           + b * output_tensors[i].strides[0]
				                 * vart::get_data_type_size(output_tensors[i].data_type),
				       outputs_ptr_[b * output_tensors.size() + i],
				       output_tensors[i].strides[0] * vart::get_data_type_size(output_tensors[i].data_type));

	return outputs_py_;
}

void VartMLRunner::init_out_arrays(size_t batch_size, bool native)
{
	// If a batch size was specified, change it.
	if (batch_size != 0)
		batchSize_ = batch_size;

	// Reset all pointers to nullptr in case there are incomplete batches.
	std::fill(outputs_ptr_.begin(), outputs_ptr_.end(), nullptr);

	auto output_tensors = vartMLRunner_->get_tensors_info(
	    vart::TensorDirection::OUTPUT, native ? vart::TensorType::HW : vart::TensorType::CPU);

	for (size_t i = 0; i < vartMLRunner_->get_num_output_tensors(); i++)
	{
		// Get output's info.
		vart::DataType        out_data_type  = output_tensors[i].data_type;
		std::vector<uint32_t> output_shape   = output_tensors[i].shape;
		std::vector<uint32_t> output_strides = output_tensors[i].strides;
		size_t                img_size       = output_tensors[i].size_in_bytes;

		// Force batch size of outputs to input batch size. If given inputs is complete, this has no
		// effect.
		output_shape[0] = batchSize_;

		size_t data_size = vart::get_data_type_size(out_data_type);
		std::for_each(output_strides.begin(), output_strides.end(), [data_size](uint32_t& stride) {
			stride *= data_size;
		});

		py::dtype out_py_type;
		if (out_data_type == vart::DataType::INT8)
			out_py_type = py::dtype::of<int8_t>();
		else if (out_data_type == vart::DataType::UINT8)
			out_py_type = py::dtype::of<uint8_t>();
		else if (out_data_type == vart::DataType::BF16)
			out_py_type = py::dtype::of<uint16_t>();
		else if (out_data_type == vart::DataType::FLOAT32)
			out_py_type = py::dtype::of<float>();
		else if (out_data_type == vart::DataType::INT32)
			out_py_type = py::dtype::of<int32_t>();
		else if (out_data_type == vart::DataType::UINT32)
			out_py_type = py::dtype::of<uint32_t>();
		else if (out_data_type == vart::DataType::INT64)
			out_py_type = py::dtype::of<int64_t>();
		else if (out_data_type == vart::DataType::UINT64)
			out_py_type = py::dtype::of<uint64_t>();
		else
			throw std::invalid_argument("Data type not supported");

		// Create the py array.
		void*       base_array = new uint8_t[batchSize_ * img_size];
		py::capsule cap(base_array, [](void* x) { delete[] (uint8_t*)x; });
		outputs_py_[i] = py::array(out_py_type, output_shape, output_strides, base_array, cap);

		// And store a pointer to each of its batch elements.
		for (size_t b = 0; b < batchSize_; b++)
			outputs_ptr_[b * vartMLRunner_->get_num_output_tensors() + i] =
			    (void*)((uint8_t*)outputs_py_[i].data() + b * output_strides[0]);
	}
}

int VartMLRunner::check_inputs(const std::vector<py::array> inputs)
{
	auto input_tensors = vartMLRunner_->get_tensors_info(
	    vart::TensorDirection::INPUT,
	    (native_in_ || zero_copy_in_) ? vart::TensorType::HW : vart::TensorType::CPU);

	// Check that there is a correct number of inputs.
	if (input_tensors.size() > inputs.size())
		return vart_ml_log_err_msg(CONFIG_UNEXPECTED_ARG_VALUE,
		                           "Invalid number of inputs. Expected %lu. Got %lu.\n",
		                           input_tensors.size(),
		                           inputs.size());

	for (size_t i = 0; i < input_tensors.size(); i++)
	{
		// Get data type of input and convert it to VART ML's DataType.
		vart::DataType in_data_type;
		if (inputs[i].dtype().is(pybind11::dtype::of<int8_t>()))
			in_data_type = vart::DataType::INT8;
		else if (inputs[i].dtype().is(pybind11::dtype::of<float>()))
			in_data_type = vart::DataType::FLOAT32;
		else if (inputs[i].dtype().is(pybind11::dtype::of<uint8_t>()))
			in_data_type = vart::DataType::UINT8;
		else if (inputs[i].dtype().is(pybind11::dtype::of<uint16_t>()))
			in_data_type = vart::DataType::BF16;
		else if (inputs[i].dtype().is(pybind11::dtype::of<int32_t>()))
			in_data_type = vart::DataType::INT32;
		else if (inputs[i].dtype().is(pybind11::dtype::of<uint32_t>()))
			in_data_type = vart::DataType::UINT32;
		else if (inputs[i].dtype().is(pybind11::dtype::of<int64_t>()))
			in_data_type = vart::DataType::INT64;
		else if (inputs[i].dtype().is(pybind11::dtype::of<uint64_t>()))
			in_data_type = vart::DataType::UINT64;
		else
			return vart_ml_log_err_msg(CONFIG_UNEXPECTED_ARG_VALUE,
			                           "Unsupported data type for input %lu (%s).\n",
			                           i,
			                           input_tensors[i].name.c_str());

		// Check that the data type is correct. If it is not, try to set the data type of the tensor as
		// conversion can be made later on, if possible.
		vart::DataType expected_data_type = input_tensors[i].data_type;
		if (in_data_type != expected_data_type)
		{
			float factor =
			    (float)vart::get_data_type_size(in_data_type) / (float)get_data_type_size(expected_data_type);

			input_tensors[i].data_type = in_data_type;
			input_tensors[i].size_in_bytes *= factor;
		}

		// Put the input strides in a vector.
		std::vector<uint32_t> input_strides;
		for (ssize_t d = 0; d < inputs[i].ndim(); d++)
			input_strides.push_back((uint32_t)inputs[i].strides()[d]
			                        / vart::get_data_type_size(in_data_type));

		// Check that the strides are correct. Strides that are not sorted in descending order are not
		// supported by C API yet. If input with such strides is given, ignore it here and it will be handled
		// later on while checking for contiguity.
		if (input_strides != input_tensors[i].strides
		    && std::is_sorted(input_strides.crbegin(), input_strides.crend()))
			return vart_ml_log_err_msg(CONFIG_UNEXPECTED_ARG_VALUE,
			                           "[VART]  Invalid strides for input %lu (%s).\n",
			                           i,
			                           input_tensors[i].name.c_str());
	}

	return vart_ml_error::SUCCESS;
}

std::string VartMLRunner::architecture()
{
	FpgaArchitecture arch;

	int err = npu_get_architecture(&arch);
	if (err)
		throw std::runtime_error(vart_ml_error::exception_message(err));

	return std::string(stringFromArch(arch));
}

PYBIND11_MODULE(vart_ml, m)
{
	m.doc() = "VART ML Python library"; // optional module docstring

	auto m_userConfig = m.def_submodule("UserConfig", "UserConfig submodule for VART ML");
	m_userConfig.def("reset", []() {})
	    .def("getRunSessionDir",
	         []() {
		         const char* info;
		         int         err = get_user_config("runSession.directory", &info);
		         if (err)
			         throw std::runtime_error(vart_ml_error::exception_message(err));
		         return info;
	         })
	    .def("get",
	         [](const std::string& param_) {
		         const char* info;
		         int         err = get_user_config(param_.c_str(), &info);
		         if (err)
			         throw std::runtime_error(vart_ml_error::exception_message(err));
		         return info;
	         })
	    .def("use",
	         [](const std::string& param_) {
		         const char* info;
		         int         err = get_user_config(param_.c_str(), &info);
		         if (err)
			         throw std::runtime_error(vart_ml_error::exception_message(err));
		         return strcmp(info, "true") == 0;
	         })
	    .def("getDefaultValue",
	         [](const std::string& param_ __attribute__((unused))) { return ""; }) // not implemented
	    .def("isDefaultValue", [](const std::string& param_ __attribute__((unused))) {
		    auto ret = false;
		    // not implemented
		    return ret;
	    });

	py::class_<VartMLRunner> vartMLRunner(m, "VartMLRunner");
	vartMLRunner
	    .def(py::init<std::string,
	                  std::string,
	                  std::optional<std::vector<std::string>>,
	                  std::optional<std::string>,
	                  std::optional<std::string>,
	                  bool>(),
	         py::arg("snapshot_dir")  = std::string(),
	         py::arg("network_name")  = std::string("wrp_network"),
	         py::arg("output_names")  = std::nullopt,
	         py::arg("input_format")  = std::nullopt,
	         py::arg("output_format") = std::nullopt,
	         py::arg("npu_only")      = false)
	    .def("alloc_ddr_bufs", &VartMLRunner::alloc_ddr_bufs)
	    .def("set_input_native", &VartMLRunner::set_input_native)
	    .def("set_output_native", &VartMLRunner::set_output_native)
	    .def("set_input_zero_copy", &VartMLRunner::set_input_zero_copy)
	    .def("set_output_zero_copy", &VartMLRunner::set_output_zero_copy)
	    .def(
	        "__call__",
	        [](VartMLRunner& self, py::handle inputs) { return vartml_dispatch_execute(self, inputs); },
	        py::arg("inputs"))
	    .def(
	        "run",
	        [](VartMLRunner& self, py::handle inputs) { return vartml_dispatch_execute(self, inputs); },
	        py::arg("inputs"))
	    .def_property_readonly("input_names", &VartMLRunner::get_input_names)
	    .def_property_readonly("output_names", &VartMLRunner::get_output_names)
	    .def_property_readonly("input_shapes", &VartMLRunner::get_input_shapes)
	    .def_property_readonly("input_shape_formats", &VartMLRunner::get_input_shape_formats)
	    .def_property_readonly("output_shapes", &VartMLRunner::get_output_shapes)
	    .def_property_readonly("output_shape_formats", &VartMLRunner::get_output_shape_formats)
	    .def_property_readonly("input_native_shape", &VartMLRunner::get_input_native_shape)
	    .def_property_readonly("input_native_shape_formats", &VartMLRunner::get_input_native_shape_formats)
	    .def_property_readonly("output_native_shape_formats", &VartMLRunner::get_output_native_shape_formats)
	    .def_property_readonly("input_native_strides", &VartMLRunner::get_input_native_strides)
	    .def_property_readonly("input_coeffs", &VartMLRunner::get_input_coeffs)
	    .def_property_readonly("output_coeffs", &VartMLRunner::get_output_coeffs)
	    .def_property_readonly("input_types", &VartMLRunner::get_input_types)
	    .def_property_readonly("input_native_types", &VartMLRunner::get_input_native_types)
	    .def_property_readonly("architecture", &VartMLRunner::architecture);
}
