/*
 * Copyright (C) 2024-2025 Advanced Micro Devices, Inc.
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

/**
 * @file inference.cpp
 * @brief This file contains the implementation of inference functions.
 *
 */

#include "x_plus_ml_app.hpp"
#include <optional>

#include <vart/vart_memory_impl_vvas.hpp>
#include <vart/vart_videoframe_impl_xrt.hpp>


static std::unordered_map<TensorType, std::string> tensor_type_to_string_map = { { TensorType::CPU, "CPU" },
	                                                                             { TensorType::HW, "HW" } };
static string vector_to_string(const vector<unsigned int> &vec) {
  string vec_str = "(";
  for (size_t i = 0; i < vec.size(); ++i) {
    vec_str += to_string(vec[i]);
    if (i < vec.size() - 1) {
      vec_str += ", ";
    }
  }
  vec_str += ")";
  return vec_str;
}

/*
 * Convert vart::DataType to string
 */
static string get_data_type_string(const vart::DataType& data_type) {
  switch (data_type) {
    case vart::DataType::INT8:
      return "int8";
    case vart::DataType::UINT8:
      return "uint8";
    case vart::DataType::INT16:
      return "int16";
    case vart::DataType::UINT16:
      return "uint16";
    case vart::DataType::BF16:
      return "bf16";
    case vart::DataType::FP16:
      return "fp16";
    case vart::DataType::FLOAT32:
      return "fp32";
    default:
      return "UNKNOWN";
  }
}

static void print_tensor_info(const std::vector<InferTensorInfo>& infos, AppLogLevel log_level) {
  for (size_t i = 0; i < infos.size(); ++i) {
    string shape_str = vector_to_string(infos[i].meta.shape);
    string stride_str = vector_to_string(infos[i].meta.strides);
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Tensor[%ld] name %s", i, infos[i].meta.name.c_str());
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Tensor[%ld] data_type %s", i, get_data_type_string(infos[i].meta.data_type).c_str());
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Tensor[%ld] tensor_type %s", i, tensor_type_to_string_map[infos[i].meta.tensor_type].c_str());
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Tensor[%ld] size %ld", i, infos[i].meta.size_in_bytes);
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Tensor[%ld] shape %s", i, shape_str.c_str());
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Tensor[%ld] stride %s", i, stride_str.c_str());
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Tensor[%ld] quantization_factor %f", i, infos[i].quantization_factor);
  }
}

bool create_inference_context(AppContext *ctx) {
  /* Check if AppContext is valid */
  if (!ctx) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, APP_LOG_LEVEL_ERROR, "AppContext is NULL");
    return false;
  }

  AppLogLevel log_level = ctx->log_level;

  InferModelConf *model_info = &ctx->model_info;

  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
              "create_inference_context: snap_path=%s exec_cpu_subgraph=%d use_native_output_format=%d",
              ctx->snap_path.c_str(), ctx->exec_cpu_subgraph, ctx->use_native_output_format);

  /* Add all the needed run options */
  std::unordered_map<std::string, std::any> runner_options;
  runner_options["npu_only"] = !ctx->exec_cpu_subgraph;
  runner_options["in_shape_format"] = std::string("NHWC");
  // runner_options["out_shape_format"] = std::string("default");

  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
              "create_inference_context: calling VAIML RunnerFactory::create_runner (npu_only=%d)",
              !ctx->exec_cpu_subgraph);

  /* Create a NpuRunner instance for inference */
  ctx->runner = vart::RunnerFactory::create_runner(vart::RunnerType::VAIML, ctx->snap_path, runner_options);
  if (!ctx->runner) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, APP_LOG_LEVEL_ERROR, "create runner failed");
    return false;
  }

  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
              "create_inference_context: runner created successfully, querying tensors_info");

  /* Get input and output tensors */
  auto input_tensors = ctx->runner->get_tensors_info(vart::TensorDirection::INPUT, vart::TensorType::HW);
  auto output_tensors = ctx->runner->get_tensors_info(vart::TensorDirection::OUTPUT, vart::TensorType::HW);

  if (ctx->exec_cpu_subgraph || !ctx->use_native_output_format) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
                "create_inference_context: querying CPU output tensors instead of HW");
    output_tensors = ctx->runner->get_tensors_info(vart::TensorDirection::OUTPUT, vart::TensorType::CPU);
  }

  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
              "create_inference_context: got %lu input tensors, %lu output tensors",
              input_tensors.size(), output_tensors.size());

  if (input_tensors.empty() || output_tensors.empty()) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, APP_LOG_LEVEL_ERROR,
                "Couldn't get input and/or output tensors (inputs=%lu, outputs=%lu)",
                input_tensors.size(), output_tensors.size());
    return false;
  }

  const uint32_t *in_shape = input_tensors[0].shape.data();

  /* Set model's information based on the tensors */
  /* Assumption we always has one input tensor and of size H*W*C */
  model_info->batch_size = ctx->runner->get_batch_size();
  /* fix for https://jira.xilinx.com/browse/AIESW-12056 */
  model_info->model_height = in_shape[1];
  model_info->model_width = in_shape[2];
  model_info->num_in_tensors = input_tensors.size();
  model_info->num_out_tensors = output_tensors.size();

  /* Validate the number of input tensors, as per assumption */
  if (model_info->num_in_tensors != 1) {
    APP_LOG_MESSAGE(
        APP_LOG_LEVEL_ERROR, log_level,
        "infer support input tensor 1, current snapshot has input tensor = %lu",
        model_info->num_in_tensors);
    return false;
  }

  /* Resize tensor vectors to accommodate num_*_tensors elements */
  model_info->in_tensors.resize(model_info->num_in_tensors);
  model_info->out_tensors.resize(model_info->num_out_tensors);

  /* Set tensor metadata */
  for (size_t index = 0; index < input_tensors.size(); index++) {
    float scale = 1.0f;
    try {
      auto qp = ctx->runner->get_quant_parameters(input_tensors[index].name);
      scale = qp.scale;
    } catch (const std::exception &e) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_WARNING, log_level,
                  "Failed to get quant params for input tensor %s: %s, defaulting scale=1.0",
                  input_tensors[index].name.c_str(), e.what());
    }
    model_info->in_tensors[index].quantization_factor = scale;
    model_info->in_tensors[index].meta = input_tensors[index];
  }

  for (size_t index = 0; index < output_tensors.size(); index++) {
    float scale = 1.0f;
    if (!ctx->exec_cpu_subgraph) {
      try {
        auto qp = ctx->runner->get_quant_parameters(output_tensors[index].name);
        scale = qp.scale;
      } catch (const std::exception &e) {
        APP_LOG_MESSAGE(APP_LOG_LEVEL_WARNING, log_level,
                    "Failed to get quant params for output tensor %s: %s, defaulting scale=1.0",
                    output_tensors[index].name.c_str(), e.what());
      }
    }
    /* When CPU subgraph is executed, unquantization is done inside VART */
    model_info->out_tensors[index].quantization_factor = ctx->exec_cpu_subgraph ? 1.0f : scale;
    model_info->out_tensors[index].meta = output_tensors[index];
  }

  /* Log model and tensor information for debugging */
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Number of Batches: %d",
              model_info->batch_size);
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Number of Inputs: %ld",
              model_info->num_in_tensors);
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Number of Outputs: %ld",
              model_info->num_out_tensors);
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Model width: %d",
              model_info->model_width);
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Model height: %d",
              model_info->model_height);

  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "INPUT Tensor Info");
  print_tensor_info(model_info->in_tensors, log_level);

  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "OUTPUT Tensor Info");
  print_tensor_info(model_info->out_tensors, log_level);

  return true;
}

void dump_infer_input_to_file(AppContext *ctx, const void *data, size_t size,
                              string name) {
  AppLogLevel log_level = ctx->log_level;
  string file_name = "/tmp/infer_input_" + name + "_snap_" + to_string(ctx->snap_id) + ".bin";
  ofstream file(file_name, ios::binary);
  if (file.is_open()) {
    file.write(reinterpret_cast<const char *>(data), size);
    file.close();
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Dumped infer input %s",
                file_name.c_str());
  } else {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Error opening file: %s",
                file_name.c_str());
  }
}

void dump_tensors_to_files(
    AppContext *ctx,
    const vector<vector<shared_ptr<vart::Memory>>> &npu_out_tensors_memory,
    const string &apend_str) {
  AppLogLevel log_level = ctx->log_level;
  for (size_t b = 0; b < npu_out_tensors_memory.size(); ++b) {
    for (size_t i = 0; i < npu_out_tensors_memory[b].size(); ++i) {
      /* Create a file name for each tensor */
      string file_name = "/tmp/infer_out_tensor_" + to_string(b) + "_" +
                         to_string(i) + "_snap_" + to_string(ctx->snap_id) + "_" + apend_str + ".bin";

      ofstream file(file_name, ios::out | ios::binary);

      if (!file) {
        APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Error opening file: %s",
                    file_name.c_str());
        continue;
      }

      const unsigned char *mapped_memory =
          npu_out_tensors_memory[b][i]->map(vart::DataMapFlags::READ);

      file.write(reinterpret_cast<const char *>(mapped_memory),
                 ctx->model_info.out_tensors[i].meta.size_in_bytes);

      file.close();

      APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Dumped tensor %ld_%ld to %s", b,
                  i, file_name.c_str());

      npu_out_tensors_memory[b][i]->unmap();
    }
  }
}


/**
 * @brief Get the underlying xrt::bo pointer from a vart::VideoFrame object.
 * @param frame Shared pointer to the vart::VideoFrame object.
 * @return Pointer to the underlying xrt::bo object, or nullptr if not found.
 */
void* get_bo_from_videoframe(const shared_ptr<vart::VideoFrame>& frame) {
  auto frame_impl = frame->get_pimpl_handle();
  auto frame_vxrt = std::dynamic_pointer_cast<VideoFrameImplXRT>(frame_impl);
  if (!frame_vxrt) {
    return nullptr;
  }
  void* bo = frame_vxrt->get_videoframe_bo();
  return bo;
}

/**
 * @brief Get the underlying xrt::bo pointer from a vart::Memory object.
 * @param mem Shared pointer to the vart::Memory object.
 * @return Pointer to the underlying xrt::bo object, or nullptr if not found.
 */
void* get_bo_from_memory(const shared_ptr<vart::Memory>& mem) {
  auto mem_impl = mem->get_pimpl_handle();
  auto mem_vvas = std::dynamic_pointer_cast<MemoryImplVvas>(mem_impl);
  if (!mem_vvas) {
    return nullptr;
  }
  void* bo = mem_vvas->get_memory_bo();
  return bo;
}


/* Perform inference, on full batch
 * and populates the tensor array with the resulting inference data.
 * The current_batch_size parameter indicates the number of valid frames in
 * current batch
 */
bool infer_process_frames(
    AppContext *ctx, uint32_t current_batch_size,
    vector<shared_ptr<vart::VideoFrame>> input_frames,
    vector<vector<shared_ptr<vart::Memory>>> &npu_out_tensors_memory) {
  InferModelConf *model_info = &ctx->model_info;
  AppLogLevel log_level = ctx->log_level;
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "input batch_size %d",
              current_batch_size);
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "model batch_size %d",
              ctx->model_info.batch_size);
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "get_in_size = %ld",
              ctx->model_info.in_tensors[0].meta.size_in_bytes);
  if (current_batch_size > model_info->batch_size) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                "received more frames than batch size (%d) of the model",
                model_info->batch_size);
    return false;
  }

  if (ctx->model_info.num_in_tensors > 1) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                "Num of input tensor greater than 1 is not supported");
    return false;
  }

  std::vector<std::vector<vart::NpuTensor>> batch_in_nputensors;  //[batch][tensors]
  batch_in_nputensors.reserve(current_batch_size);

  // Process each batch element
  for (uint32_t b = 0; b < current_batch_size; b++) {
    std::vector<vart::NpuTensor> input_tensors;
    void* inputbuf_ptr = nullptr;
    /* get the BO from the videoframe */
    auto bo = get_bo_from_videoframe(input_frames[b]);
    if (nullptr == bo) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,"Failed to get BO from input videoframe[%u]", b);
      return false;
    }
    inputbuf_ptr = bo;

    vart::NpuTensor in_tensor(model_info->in_tensors[0].meta, inputbuf_ptr, vart::MemoryType::XRT_BO);
    input_tensors.push_back(std::move(in_tensor));

    if (ctx->dump_all_inputs || ctx->dump_npu_output) {
      const vart::VideoFrameMapInfo* map_info = nullptr;
      try {
        map_info = &input_frames[b]->map(vart::DataMapFlags::READ);
      } catch (const exception& e) {
        APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,"Failed to map memory : %s", e.what());
        return false;
      }
      dump_infer_input_to_file(ctx, map_info->planes[0].data, map_info->planes[0].size,
                               model_info->in_tensors[0].meta.name);
    }
    batch_in_nputensors.push_back(std::move(input_tensors));
  }


  std::vector<std::vector<vart::NpuTensor>> batch_out_nputensors; //[batch][tensors]
  batch_out_nputensors.reserve(current_batch_size);
  for (unsigned int b = 0; b < current_batch_size; ++b) {
    auto tensors = npu_out_tensors_memory[b];
    if (tensors.empty()) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,"Failed to acquire output tensors for batch %u", b);
      return false;
    }

    std::vector<vart::NpuTensor> output_tensors;
    for (uint32_t t = 0; t < model_info->num_out_tensors; t++) {
      void* outputbuf_ptr = nullptr;
      auto mem_type = vart::MemoryType::XRT_BO;
      if(!ctx->exec_cpu_subgraph) {
        auto bo = get_bo_from_memory(tensors[t]);
        if (nullptr == bo) {
          APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,"Failed to get BO from output videoframe");
          return false;
        }
        outputbuf_ptr = bo;
      } else {
        const unsigned char *mapped_memory = npu_out_tensors_memory[b][t]->map(vart::DataMapFlags::WRITE);
        outputbuf_ptr = const_cast<void *>(reinterpret_cast<const void *>(mapped_memory));
        mem_type = vart::MemoryType::USER_POINTER_NON_CMA;
      }
      vart::NpuTensor out_tensor(model_info->out_tensors[t].meta, outputbuf_ptr, mem_type);
      output_tensors.push_back(std::move(out_tensor));
    }
    batch_out_nputensors.push_back(std::move(output_tensors));
  }

  if (ctx->dump_all_inputs)
    dump_tensors_to_files(ctx, npu_out_tensors_memory, "before_infer_execute");

  /* Execute NPU Runner */
  auto ret = ctx->runner->execute(batch_in_nputensors, batch_out_nputensors);
  if (vart::StatusCode::SUCCESS != ret) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,"Inference execution failed");
    return false;
  }

  if (ctx->dump_all_inputs)
    dump_tensors_to_files(ctx, npu_out_tensors_memory, "after_infer_execute");

  if (ctx->dump_all_inputs || ctx->dump_npu_output) {
    for (uint32_t b = 0; b < current_batch_size; b++) {
      input_frames[b]->unmap();
    }
  }

  return true;
}
