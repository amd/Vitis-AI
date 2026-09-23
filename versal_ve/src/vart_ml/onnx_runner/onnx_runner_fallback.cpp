/**
 * @file onnx_runner_fallback.cpp
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

#include "onnx_runner.h"
#include "utils/log.h"

static int missing_library_error(const char* func)
{
	return vart_ml_log_err_msg(
	    LIBRARY_MISSING,
	    "%s: Missing Onnxruntime library. Please check the compilation logs for further information.\n",
	    func);
}

int onnx_init(struct onnx_context_global*) { return missing_library_error(__func__); }

int onnx_init_node(const char*, struct onnx_context_global, struct onnx_context_node*, bool)
{
	return missing_library_error(__func__);
}

void onnx_destroy(struct onnx_context_global) { missing_library_error(__func__); }

int onnx_destroy_node(struct onnx_context_node) { return missing_library_error(__func__); }

int onnx_run_inference(struct onnx_context_node,
                       const void*,
                       const char**,
                       const int64_t**,
                       void**,
                       const char**,
                       const int64_t**,
                       void**)
{
	return missing_library_error(__func__);
}
