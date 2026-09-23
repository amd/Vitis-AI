/**
 * @file onnx_runner.h
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

#pragma once

#include <inttypes.h>
#include <stddef.h>

enum transput_type
{
	INPUT,
	OUTPUT,
};

struct onnx_context_global
{
	void* env;
	void* memory_info;
};

struct transput_info
{
	char*    name;
	int64_t* shape;
	size_t   shape_len;
	unsigned data_type;
	size_t   size;
	bool     variable_dim;
};

struct onnx_context_node
{
	void*                 session;
	void*                 allocator;
	struct transput_info* input_infos;
	size_t                input_count;
	struct transput_info* output_infos;
	size_t                output_count;
	bool                  variable_dim_in;
	bool                  variable_dim_out;
};

/**
 * @brief Initialize some global values for OnnxRuntime inference.
 *
 * @details This function needs to be called once at the beginning of the program.
 *
 * @param[out] onnx_context_global Struct with all objects allocated to run multiple nodes inference.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int onnx_init(struct onnx_context_global* onnx_context_global);

/**
 * @brief Initialize some values needed for a single Onnx node inference.
 *
 * @details This function needs to be called once for each different Onnx node.
 *
 * @param[in] model_path Path to an onnx model.
 * @param[in] onnx_context_global Initialized global context struct.
 * @param[out] onnx_context_node Struct with all objects allocated to run an inference.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int onnx_init_node(const char*                model_path,
                   struct onnx_context_global onnx_context_global,
                   struct onnx_context_node*  onnx_context_node,
                   bool                       disable_cpu_mem_arena);

/**
 * @brief Free memory allocated within an onnx_context_global with onnx_init.
 *
 * @param[in] onnx_context_global Struct with all objects allocated by OnnxRuntime.
 */
void onnx_destroy(struct onnx_context_global onnx_context_global);

/**
 * @brief Free memory allocated within an onnx_context_node with onnx_init_node.
 *
 * @param[in] onnx_context_node Struct with all objects allocated by OnnxRuntime.
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int onnx_destroy_node(struct onnx_context_node onnx_context_node);

/**
 * @brief Run the model in an OrtSession.
 *
 * @details Input and output buffers' order is defined by input_names and output_names respectively. If the
 * latters are omitted, input and output buffers must be given in the order defined by the onnx model.
 *
 * @details Input/output shapes of the buffers are optional as they are read in the onnx file. If a shape
 * contains variable dimensions (represented by -1), the argument is required.
 *
 * @param[in] onnx_context_node Struct with OnnxRuntime session and transputs info.
 * @param[in] memory_info OrtMemoryInfo for tensor creation.
 * @param[in] input_names An array of the inputs names (optional).
 * @param[in] input_shapes An array of the inputs shapes (optional).
 * @param[in] inbufs Two-dimensional array of input values (allocated by user).
 * @param[in] output_names An array of the outputs names (optional).
 * @param[in] output_shapes An array of the outputs shapes (optional).
 * @param[in] outbufs Two-dimensional array of output values (allocated by user).
 * @return int The Vart ML error (vart_ml_error_id) code of the operation.
 */
int onnx_run_inference(struct onnx_context_node onnx_context_node,
                       const void*              memory_info,
                       const char**             input_names,
                       const int64_t**          input_shapes,
                       void**                   inbufs,
                       const char**             output_names,
                       const int64_t**          output_shapes,
                       void**                   outbufs);
