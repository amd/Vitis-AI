/**
 * @file onnx_runner.cpp
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

#include <cassert>
#include <onnxruntime/onnxruntime_c_api.h>

#include "onnx_runner.h"
#include "utils/fpga_info.h"
#include "utils/log.h"

const OrtApi* g_ort = NULL;

// Check an OrtStatus and log error.
static int ort_check_status(OrtStatus* status)
{
	if (status != NULL)
	{
		const char* msg = g_ort->GetErrorMessage(status);
		int err = vart_ml_log_err_msg(vart_ml_error::LIBRARY_ERROR_ONNXRT, "OnnxRuntime error: %s\n", msg);
		g_ort->ReleaseStatus(status);
		return err;
	}

	return vart_ml_error::SUCCESS;
}

static int size_of_ort_tensor_type(ONNXTensorElementDataType t, size_t* size)
{
	switch (t)
	{
	case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
		*size = sizeof(float);
		break;

	case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:
		*size = sizeof(int8_t);
		break;

	case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:
		*size = sizeof(uint8_t);
		break;

	case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:
		*size = sizeof(int32_t);
		break;

	case ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16:
		*size = sizeof(uint16_t);
		break;

	case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32:
		*size = sizeof(uint32_t);
		break;

	case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
		*size = sizeof(int64_t);
		break;

	case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64:
		*size = sizeof(uint64_t);
		break;

	default:
		return vart_ml_log_err_msg(vart_ml_error::CONFIG_UNSUPPORTED_FEATURE, "Unsupported data type.");
	}

	return vart_ml_error::SUCCESS;
}

static int onnx_get_transput_info(const void*           session,
                                  void*                 allocator,
                                  enum transput_type    transput_type,
                                  size_t                index,
                                  struct transput_info* transput_info)
{
	int err;

	// Get transput name.
	if (transput_type == INPUT)
		err = ort_check_status(g_ort->SessionGetInputName(
		    (OrtSession*)session, index, (OrtAllocator*)allocator, &transput_info->name));
	else
		err = ort_check_status(g_ort->SessionGetOutputName(
		    (OrtSession*)session, index, (OrtAllocator*)allocator, &transput_info->name));
	if (err)
		return err;

	// Get transput type info.
	OrtTypeInfo* transput_type_info;
	if (transput_type == INPUT)
		err = ort_check_status(
		    g_ort->SessionGetInputTypeInfo((OrtSession*)session, index, &transput_type_info));
	else
		err = ort_check_status(
		    g_ort->SessionGetOutputTypeInfo((OrtSession*)session, index, &transput_type_info));
	if (err)
		return err;

	// Get tensor type info.
	const OrtTensorTypeAndShapeInfo* tensor_type_info;
	err = ort_check_status(g_ort->CastTypeInfoToTensorInfo(transput_type_info, &tensor_type_info));
	if (err)
		goto clean;

	// Get tensor shape len.
	err = ort_check_status(g_ort->GetDimensionsCount(tensor_type_info, &transput_info->shape_len));
	if (err)
		goto clean;

	// Get tensor shape.
	transput_info->shape = (int64_t*)malloc(transput_info->shape_len * sizeof(int64_t));
	if (transput_info->shape == NULL)
	{
		err = vart_ml_error::SYSTEM_ERROR_MEM_ALLOC_FAILURE;
		goto clean;
	}
	err = ort_check_status(
	    g_ort->GetDimensions(tensor_type_info, transput_info->shape, transput_info->shape_len));
	if (err)
		goto clean_shape;

	// Get tensor data type.
	err = ort_check_status(
	    g_ort->GetTensorElementType(tensor_type_info, (ONNXTensorElementDataType*)&transput_info->data_type));
	if (err)
		goto clean_shape;

	// Get tensor element size.
	err = size_of_ort_tensor_type((ONNXTensorElementDataType)transput_info->data_type, &transput_info->size);
	if (err)
		goto clean_shape;

	for (size_t i = 0; i < transput_info->shape_len; i++)
	{
		// If shape contains a variable dimension, set size to 0.
		if ((transput_info->variable_dim = (transput_info->shape[i] < 0)))
		{
			transput_info->size = 0;
			break;
		}

		transput_info->size *= transput_info->shape[i];
	}

	goto clean;

clean_shape:
	free(transput_info->shape);
	transput_info->shape = NULL;
clean:
	g_ort->ReleaseTypeInfo(transput_type_info);
	return err;
}

static int onnx_destroy_transput_info(void* allocator, struct transput_info transput_info)
{
	free(transput_info.shape);
	return ort_check_status(g_ort->AllocatorFree((OrtAllocator*)allocator, transput_info.name));
}

int onnx_init(struct onnx_context_global* onnx_context_global)
{
	int err;

	g_ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);

	// Create OrtEnv with log level roughly corresponding to Vart ML logs.
	err = ort_check_status(
	    g_ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "wrp_network", (OrtEnv**)&onnx_context_global->env));
	if (err != vart_ml_error::SUCCESS)
		return err;

	err = ort_check_status(g_ort->CreateCpuMemoryInfo(
	    OrtArenaAllocator, OrtMemTypeDefault, (OrtMemoryInfo**)&onnx_context_global->memory_info));

	return err;
}

int onnx_init_node(const char*                model_path,
                   struct onnx_context_global onnx_context_global,
                   struct onnx_context_node*  onnx_context_node,
                   bool                       disable_cpu_mem_arena)
{
	int err;

	vart_ml_log(LOG_DBG, "Parsing ONNX node at `%s'...", model_path);

	OrtSessionOptions* session_options = NULL;
	bool               need_session_options =
	    disable_cpu_mem_arena || check_user_config("workaround.disableOnnxGraphOptimizations");

	if (need_session_options)
	{
		err = ort_check_status(g_ort->CreateSessionOptions(&session_options));
		if (err)
			return err;

		if (disable_cpu_mem_arena)
		{
			err = ort_check_status(g_ort->DisableCpuMemArena(session_options));
			if (err)
			{
				g_ort->ReleaseSessionOptions(session_options);
				return err;
			}
		}

		// Workaround to disable graph optimizations to avoid potential segfaults in graph transformers.
		// XXX: Try removing this when upgrading OnnxRuntime.
		if (check_user_config("workaround.disableOnnxGraphOptimizations"))
		{
			err = ort_check_status(g_ort->SetSessionGraphOptimizationLevel(session_options, ORT_DISABLE_ALL));
			if (err)
			{
				g_ort->ReleaseSessionOptions(session_options);
				return err;
			}
		}
	}

	err = ort_check_status(g_ort->CreateSession((OrtEnv*)onnx_context_global.env,
	                                            model_path,
	                                            session_options,
	                                            (OrtSession**)&onnx_context_node->session));
	if (session_options)
		g_ort->ReleaseSessionOptions(session_options);
	if (err)
		return err;

	err = ort_check_status(g_ort->CreateAllocator((OrtSession*)onnx_context_node->session,
	                                              (OrtMemoryInfo*)onnx_context_global.memory_info,
	                                              (OrtAllocator**)&onnx_context_node->allocator));
	if (err)
		return err;

	// Set base values for presence of variable dimensions.
	onnx_context_node->variable_dim_in  = false;
	onnx_context_node->variable_dim_out = false;

	// Get number of inputs.
	err = ort_check_status(g_ort->SessionGetInputCount((OrtSession*)onnx_context_node->session,
	                                                   &onnx_context_node->input_count));
	if (err)
		return err;

	// And initialize transputs info accordingly.
	onnx_context_node->input_infos =
	    (struct transput_info*)malloc(onnx_context_node->input_count * sizeof(struct transput_info));
	if (onnx_context_node->input_infos == NULL)
		return vart_ml_error::SYSTEM_ERROR_MEM_ALLOC_FAILURE;
	for (size_t i = 0; (i < onnx_context_node->input_count) && !err; i++)
	{
		err = onnx_get_transput_info((OrtSession*)onnx_context_node->session,
		                             (OrtAllocator*)onnx_context_node->allocator,
		                             INPUT,
		                             i,
		                             onnx_context_node->input_infos + i);
		onnx_context_node->variable_dim_in |= onnx_context_node->input_infos[i].variable_dim;
	}

	if (err)
		return err;

	// Get number of outputs.
	err = ort_check_status(g_ort->SessionGetOutputCount((OrtSession*)onnx_context_node->session,
	                                                    &onnx_context_node->output_count));
	if (err)
		return err;

	// And initialize transput info accordingly.
	onnx_context_node->output_infos =
	    (struct transput_info*)malloc(onnx_context_node->output_count * sizeof(struct transput_info));
	if (onnx_context_node->output_infos == NULL)
		return vart_ml_error::SYSTEM_ERROR_MEM_ALLOC_FAILURE;
	for (size_t i = 0; (i < onnx_context_node->output_count) && !err; i++)
	{
		err = onnx_get_transput_info((OrtSession*)onnx_context_node->session,
		                             (OrtAllocator*)onnx_context_node->allocator,
		                             OUTPUT,
		                             i,
		                             onnx_context_node->output_infos + i);
		onnx_context_node->variable_dim_out |= onnx_context_node->output_infos[i].variable_dim;
	}

	vart_ml_log(LOG_DBG, " Done.\n", model_path);

	return err;
}

void onnx_destroy(struct onnx_context_global onnx_context_global)
{
	g_ort->ReleaseMemoryInfo((OrtMemoryInfo*)onnx_context_global.memory_info);
	g_ort->ReleaseEnv((OrtEnv*)onnx_context_global.env);
}

int onnx_destroy_node(struct onnx_context_node onnx_context_node)
{
	int err = vart_ml_error::SUCCESS;

	for (size_t i = 0; i < onnx_context_node.input_count; i++)
		err = onnx_destroy_transput_info(onnx_context_node.allocator, onnx_context_node.input_infos[i]);
	free(onnx_context_node.input_infos);

	for (size_t i = 0; i < onnx_context_node.output_count; i++)
		err = onnx_destroy_transput_info(onnx_context_node.allocator, onnx_context_node.output_infos[i]);
	free(onnx_context_node.output_infos);

	g_ort->ReleaseAllocator((OrtAllocator*)onnx_context_node.allocator);
	g_ort->ReleaseSession((OrtSession*)onnx_context_node.session);

	return err;
}

int onnx_run_inference(struct onnx_context_node onnx_context_node,
                       const void*              memory_info,
                       const char**             input_names,
                       const int64_t**          input_shapes,
                       void**                   inbufs,
                       const char**             output_names,
                       const int64_t**          output_shapes,
                       void**                   outbufs)
{
	int err;

	OrtIoBinding* io_binding;
	err = ort_check_status(g_ort->CreateIoBinding((OrtSession*)onnx_context_node.session, &io_binding));
	if (err)
		return err;

	OrtValue** input_tensors = (OrtValue**)malloc(onnx_context_node.input_count * sizeof(OrtValue*));
	if (input_tensors == NULL)
		return vart_ml_error::SYSTEM_ERROR_MEM_ALLOC_FAILURE;
	for (size_t user_idx = 0; user_idx < onnx_context_node.input_count; user_idx++)
	{
		// If the user specified input names, they may not be in the same order as the inputs read in the ONNX
		// model. Get the index of the ith name in inputs_infos and use it to refer to the input.
		size_t onnx_idx = user_idx;
		if (input_names != nullptr)
			for (onnx_idx = 0; onnx_idx < onnx_context_node.input_count; onnx_idx++)
				if (strcmp(input_names[user_idx], onnx_context_node.input_infos[onnx_idx].name) == 0)
					break;

		// If the user specified input shapes, use it and compute corresponding size.
		const int64_t* input_shape = onnx_context_node.input_infos[onnx_idx].shape;
		size_t         input_size  = onnx_context_node.input_infos[onnx_idx].size;
		if ((input_shapes != NULL) && (input_shapes[user_idx] != NULL))
		{
			input_shape = input_shapes[user_idx];
			err         = size_of_ort_tensor_type(
                (ONNXTensorElementDataType)onnx_context_node.input_infos[onnx_idx].data_type, &input_size);
			if (err)
				return err;

			for (size_t s = 0; s < onnx_context_node.input_infos[onnx_idx].shape_len; s++)
				input_size *= input_shape[s];
		}
		// If size is 0, there is a variable dimension in the model's shape. The user should have specified a
		// shape.
		else if (input_size == 0)
			return vart_ml_log_err_msg(
			    vart_ml_error::CONFIG_BAD_ARGUMENT,
			    "Unspecified shape for input tensor %s. Tensor has a variable shape.\n",
			    onnx_context_node.input_infos[onnx_idx].name);

		// Create tensor.
		err = ort_check_status(g_ort->CreateTensorWithDataAsOrtValue(
		    (OrtMemoryInfo*)memory_info,
		    inbufs[user_idx],
		    input_size,
		    input_shape,
		    onnx_context_node.input_infos[onnx_idx].shape_len,
		    (ONNXTensorElementDataType)onnx_context_node.input_infos[onnx_idx].data_type,
		    input_tensors + user_idx));
		if (err)
			return err;

		// Bind the tensor.
		err = ort_check_status(g_ort->BindInput(
		    io_binding, onnx_context_node.input_infos[onnx_idx].name, input_tensors[user_idx]));
		if (err)
			return err;
	}

	// Create an array of output tensors with outbufs as values.
	OrtValue** output_tensors = (OrtValue**)malloc(onnx_context_node.output_count * sizeof(OrtValue*));
	if (output_tensors == NULL)
		return vart_ml_error::SYSTEM_ERROR_MEM_ALLOC_FAILURE;
	for (size_t user_idx = 0; user_idx < onnx_context_node.output_count; user_idx++)
	{
		// If the user specified output names, they may not be in the same order as the outputs read in the
		// ONNX model. Get the index of the ith name in outputs_infos and use it to refer to the output.
		size_t onnx_idx = user_idx;
		if (output_names != nullptr)
			for (onnx_idx = 0; onnx_idx < onnx_context_node.output_count; onnx_idx++)
				if (strcmp(output_names[user_idx], onnx_context_node.output_infos[onnx_idx].name) == 0)
					break;

		// If the user specified output shapes, use it and compute corresponding size.
		const int64_t* output_shape = onnx_context_node.output_infos[onnx_idx].shape;
		size_t         output_size  = onnx_context_node.output_infos[onnx_idx].size;
		if ((output_shapes != NULL) && (output_shapes[user_idx] != NULL))
		{
			output_shape = output_shapes[user_idx];
			err          = size_of_ort_tensor_type(
                (ONNXTensorElementDataType)onnx_context_node.output_infos[onnx_idx].data_type, &output_size);
			if (err)
				return err;

			for (size_t s = 0; s < onnx_context_node.output_infos[onnx_idx].shape_len; s++)
				output_size *= output_shape[s];
		}
		// If size is 0, there is a variable dimension in the model's shape. The user should have specified a
		// shape.
		else if (output_size == 0)
			return vart_ml_log_err_msg(
			    vart_ml_error::CONFIG_BAD_ARGUMENT,
			    "Unspecified shape for output tensor %s. Tensor has a variable shape.\n",
			    onnx_context_node.output_infos[onnx_idx].name);

		// Create tensor.
		err = ort_check_status(g_ort->CreateTensorWithDataAsOrtValue(
		    (OrtMemoryInfo*)memory_info,
		    outbufs[user_idx],
		    output_size,
		    output_shape,
		    onnx_context_node.output_infos[onnx_idx].shape_len,
		    (ONNXTensorElementDataType)onnx_context_node.output_infos[onnx_idx].data_type,
		    output_tensors + user_idx));
		if (err)
			return err;

		// Bind the tensor.
		err = ort_check_status(g_ort->BindOutput(
		    io_binding, onnx_context_node.output_infos[onnx_idx].name, output_tensors[user_idx]));
		if (err)
			return err;
	}

	// Run the inference.
	err = ort_check_status(g_ort->RunWithBinding((OrtSession*)onnx_context_node.session, NULL, io_binding));
	if (err)
		return err;

	// Clean up.
	// Note: ReleaseValue does not free buffer within tensor when it was given from outside.
	for (size_t i = 0; i < onnx_context_node.input_count; i++)
		g_ort->ReleaseValue(input_tensors[i]);
	for (size_t i = 0; i < onnx_context_node.output_count; i++)
		g_ort->ReleaseValue(output_tensors[i]);
	free(input_tensors);
	free(output_tensors);
	g_ort->ReleaseIoBinding(io_binding);

	return vart_ml_error::SUCCESS;
}
