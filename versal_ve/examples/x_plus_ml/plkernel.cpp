/*
 * Copyright (C) 2024 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software
 * is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
 * KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
 * EVENT SHALL XILINX BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 * OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. Except as contained in this notice, the name of the Xilinx shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from Xilinx.
 */

#include "x_plus_ml_app.hpp"
/* Function to convert NHWC to NCHW on CPU */
void nhwc_to_nchw(const float *input, float *output, int batch, int height,
                  int width, int channels) {
  for (int b = 0; b < batch; ++b) {
    for (int h = 0; h < height; ++h) {
      for (int w = 0; w < width; ++w) {
        for (int c = 0; c < channels; ++c) {
          output[b * channels * height * width + c * height * width +
                 h * width + w] =
              input[b * height * width * channels + h * width * channels +
                    w * channels + c];
        }
      }
    }
  }
}

void sync_tensors(
    AppContext *ctx,
    vector<vector<shared_ptr<vart::Memory>>> &npu_out_tensors_memory,
    vector<vector<shared_ptr<vart::Memory>>> &pl_out_tensors_memory) {
  AppLogLevel log_level = ctx->log_level;
  LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Syncing NPU out tensor memory");
  for (size_t b = 0; b < npu_out_tensors_memory.size(); ++b) {
    for (size_t i = 0; i < npu_out_tensors_memory[b].size(); ++i) {
      npu_out_tensors_memory[b][i]->map(vart::DataMapFlags::READ);
      npu_out_tensors_memory[b][i]->unmap();
    }
  }

  LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Syncing PL out tensor memory");
  for (size_t b = 0; b < pl_out_tensors_memory.size(); ++b) {
    for (size_t i = 0; i < pl_out_tensors_memory[b].size(); ++i) {
      pl_out_tensors_memory[b][i]->map(vart::DataMapFlags::READ);
      pl_out_tensors_memory[b][i]->unmap();
    }
  }
}


bool create_plkernel_context(AppContext *ctx) {
  AppLogLevel log_level = ctx->log_level;
  string kernel_name = ctx->plkernel_conf.plkernel_name;
  string json_data = "{}";
  int pl_tensor_index = 0;

  LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Create context for PL %s kernel",
              kernel_name.c_str());
  ctx->plkernel = new vart::PLKernel(vart::PLKernelImplType::PL_KERNEL_XRT,
                                     kernel_name, json_data, ctx->device);

  vector<ArgumentInfo> arg_info_list;
  ctx->plkernel->get_config(arg_info_list);

  for (const auto &arg_info : arg_info_list) {
    LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Argument Name: %s",
                arg_info.arg_name.c_str());
    LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Argument Index: %d",
                arg_info.arg_index);
    LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Argument size: %ld",
                arg_info.arg_size);
    LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Memory Index: %d",
                arg_info.mem_index);
  }

  /* Create mapping of in_tensors to model_info.out_tensors based on tensor
   * names */
  for (size_t i = 0; i < ctx->plkernel_conf.in_tensors.size(); ++i) {
    auto &tensor_info = ctx->plkernel_conf.in_tensors[i];
    for (size_t j = 0; j < ctx->model_info.out_tensors.size(); ++j) {
      const auto &model_tensor = ctx->model_info.out_tensors[j];
      if (tensor_info.name == model_tensor.name) {
        ctx->tensor_mapping[tensor_info.name] = j;
        LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
                    "Mapped PL input tensor '%s' (index %zu) to NPU output "
                    "tensor '%s' (index %zu)",
                    tensor_info.name.c_str(), i, model_tensor.name.c_str(), j);
        break;
      }
    }
  }

  /* Update ddr memory index input tensors */
  for (auto &tensor_info : ctx->plkernel_conf.in_tensors) {
    tensor_info.mem_index = arg_info_list[pl_tensor_index].mem_index;
    pl_tensor_index++;
  }

  /* Update ddr memory index output tensors */
  for (auto &tensor_info : ctx->plkernel_conf.out_tensors) {
    tensor_info.mem_index = arg_info_list[pl_tensor_index].mem_index;
    pl_tensor_index++;
  }

  /* Update ddr memory index for const arguments */
  for (auto &tensor_info : ctx->plkernel_conf.in_constants) {
    tensor_info.mem_index = arg_info_list[pl_tensor_index].mem_index;
    pl_tensor_index++;
  }

  return true;
}

bool plkernel_process_frames(
    AppContext *ctx, uint32_t current_batch,
    vector<vector<shared_ptr<vart::Memory>>> &npu_out_tensors_memory,
    vector<vector<shared_ptr<vart::Memory>>> &pl_out_tensors_memory,
    vector<shared_ptr<vart::Memory>> &pl_in_constant_memory) {
  AppLogLevel log_level = ctx->log_level;
  LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
              "PL process called with current_batch = %d", current_batch);

  sync_tensors(ctx, npu_out_tensors_memory, pl_out_tensors_memory);

  for (unsigned int b = 0; b < current_batch; ++b) {
    LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Processing batch %u", b);

    /* Create the memory vector for the current batch, as the NPU output is
      * not one to one map with PL input */
    vector<shared_ptr<vart::Memory>> memory_vector;
    for (const auto &tensor_info : ctx->plkernel_conf.in_tensors) {
      /* Use the global index to find the corresponding NPU output tensor */
      int npu_mem_index = ctx->tensor_mapping[tensor_info.name];
      memory_vector.push_back(npu_out_tensors_memory[b][npu_mem_index]);
      LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
                  "NPU tensor index %d mapped to PL input tensor '%s'",
                  npu_mem_index, tensor_info.name.c_str());
    }

    for (size_t i = 0; i < ctx->plkernel_conf.out_tensors.size(); ++i) {
      memory_vector.push_back(pl_out_tensors_memory[b][i]);
    }

    for (size_t i = 0; i < ctx->plkernel_conf.in_constants.size(); ++i) {
      memory_vector.push_back(pl_in_constant_memory[i]);
    }

    /* Process tail for one inference */
    ctx->plkernel->process(memory_vector);
    ctx->plkernel->wait(10);
  }

  return true;
}
