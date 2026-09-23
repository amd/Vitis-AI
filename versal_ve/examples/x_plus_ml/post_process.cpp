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
 * @file post_process.cpp
 * @brief This file contains the implementation of post-processing functions for
 * interpreting the inference results.
 *
 * If user wants to integrate his new custom post-processing implementaiton
 * which he/she has developed, it can be done by changing the way
 * vart::PostProcess class object is instantiated. One can use it's other
 * constructor signature which accpets the shared pointer to the user's
 * implementation instance.
 *
 * Ex: Lets say the new custom post-processing implementation is named as
 * PostProcessImplCustom, then the instantiaion will be like below:
 *
 * ctx->post_process = new
 * vart::PostProcess(std::make_shared<PostProcessImplCustom>());
 *
 * Note : 1) On how to implement the custom post-processing, please refer to the
 * VART documentation. 2) If the user's custom post-processing implementation,
 * lets say PostProcessImplCustom, is producing a result type other than the
 * default provide result types(vart::InferResultType), then user also need to
 * consider creating a new inference result type and implement the same.
 *
 */

#include "x_plus_ml_app.hpp"

bool create_postprocess_context(AppContext *ctx) {
  AppLogLevel log_level = ctx->log_level;

  /* Prepare post-processor context */
  /* post-process return the results of each frame in a batch */
  string postprocess_json_config =
      extract_component_json(ctx->json_str, "postprocess-config");
  if (postprocess_json_config.empty()) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                "Failed to parse post_process config");
    return false;
  } else {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Post process Config: \n\n\n %s",
                postprocess_json_config.c_str());
  }
  /* App-side vvas_core postprocess C-API path (resnet50, yolox): build the bridge that
   * dlopens the x_plus_ml-built .so by path. No vart::PostProcess / vart_x
   * dispatch involved. */
  if (ctx->use_vvas_pp_api) {
    try {
      ctx->vvas_pp_bridge = new xplusml::VvasPostProcessBridge(
          postprocess_json_config, ctx->vvas_pp_lib_path,
          ctx->vvas_pp_result_kind, ctx->device_idx, ctx->xclbin_location);
    } catch (const exception &e) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                  "Unable to create vvas post-process context: %s", e.what());
      return false;
    }
  } else {
    ctx->post_process = new vart::PostProcess(
        ctx->postprocess_type, postprocess_json_config, ctx->device);
    if (!ctx->post_process) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                  "Unable to create post-process context");
      return false;
    }
  }

  /* Model info required for post-processing */
  std::vector<TensorInfo> tensor_info;
  auto append_tensor_info = [&](const InferTensorInfo& src, TensorDataDirection direction) {
    TensorInfo tinfo = {};
    tinfo.scale_coeff = src.quantization_factor;
    tinfo.size = src.meta.size_in_bytes;
    tinfo.name = src.meta.name;
    tinfo.direction = direction;

    switch (src.meta.data_type) {
      case vart::DataType::INT8:
        tinfo.data_type = TensorDataType::INT8;
        break;
      case vart::DataType::UINT8:
        tinfo.data_type = TensorDataType::INT8;
        break;
      case vart::DataType::BF16:
        tinfo.data_type = TensorDataType::BF16;
        break;
      case vart::DataType::FLOAT32:
        tinfo.data_type = TensorDataType::FLOAT32;
        break;
      default:
        APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Format %d isn't supported in vart::PostProcess", static_cast<int>(src.meta.data_type));
        return false;
    }

    for (size_t idx = 0; idx < src.meta.shape.size(); idx++) {
      tinfo.shape.push_back(src.meta.shape[idx]);
    }
    tensor_info.push_back(std::move(tinfo));
    return true;
  };

  for (size_t j = 0; j < ctx->model_info.num_in_tensors; ++j) {
    if (!append_tensor_info(ctx->model_info.in_tensors[j], TensorDataDirection::INPUT)) {
      return false;
    }
  }

  for (size_t j = 0; j < ctx->model_info.num_out_tensors; ++j) {
    if (!append_tensor_info(ctx->model_info.out_tensors[j], TensorDataDirection::OUTPUT)) {
      return false;
    }
  }
  if (ctx->use_vvas_pp_api) {
    try {
      ctx->vvas_pp_bridge->set_config(tensor_info, ctx->model_info.batch_size);
    } catch (const exception &e) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                  "vvas set_config failed: %s", e.what());
      return false;
    }
  } else {
    ctx->post_process->set_config(tensor_info, ctx->model_info.batch_size);
  }

  return true;
}

/* Perform post-processing on the output tensor data to interpret the
 * inference results */
vector<vector<shared_ptr<InferResult>>> postprocess_process_frames(
    AppContext *ctx, uint32_t current_batch,
    std::vector<std::vector<std::shared_ptr<vart::Memory>>>
        &npu_out_tensors_memory,
    int64_t num_frame_processed) {
  AppLogLevel log_level = ctx->log_level;
  unsigned int total_valid_tensor =
      ctx->model_info.num_out_tensors * current_batch;

  // Start printing with 1 not 0 for number of frames processed
  num_frame_processed = num_frame_processed + 1;

  vector<vector<shared_ptr<InferResult>>> results;

  /* The vvas_core postprocess C-API path (resnet50, yolox) always feeds mapped host
   * pointers to the model .so (it re-wraps them as VvasMemory via
   * vvas_memory_alloc_from_data). It cannot consume XRT vart::Memory directly
   * (that needs vart_x-internal handles), so map the output tensors here even
   * in native (zero-copy) mode. vart::Memory::map() works for XRT memory too. */
  if (2 != ctx->use_native_output_format || ctx->use_vvas_pp_api) {
    /* Non-Native/native format, No Zero Copy, feed virtual pointers to the Post Processing */
    std::vector<int8_t *> tensor(total_valid_tensor);
    for (unsigned int j = 0; j < current_batch; ++j) {
      for (unsigned int i = 0; i < ctx->model_info.num_out_tensors; ++i) {
        unsigned int index = j * ctx->model_info.num_out_tensors + i;
        // Map the memory for the current tensor
        const unsigned char *mapped_memory =
            npu_out_tensors_memory[j][i]->map(vart::DataMapFlags::READ);

        // Assign the pointer to the tensor vector
        tensor[index] =
            const_cast<int8_t *>(reinterpret_cast<const int8_t *>(mapped_memory));
      }
    }
    try {
      /* Post-process the entire batch in one go and return the results as a
       * vector of detections/outputs for each frame in the batch. */
      /* The 'results' vector holds the results for multiple frames, with each
       * inner vector corresponding to the results for a single frame in the
       * batch. */
      if (ctx->use_vvas_pp_api) {
        results = ctx->vvas_pp_bridge->process(tensor, current_batch);
      } else {
        results = ctx->post_process->process(tensor, current_batch);
      }
    } catch (const exception &e) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                  "Caught exception while post-processing with int pointers: %s", e.what());
      return {{shared_ptr<InferResult>(nullptr)}};
    }
  } else {
    /* Native output mode, Zero Copy, feed XRT vart::Memory to the Post Processing */
    try {
      /* Post-process the entire batch in one go and return the results as a
       * vector of detections/outputs for each frame in the batch. */
      /* The 'results' vector holds the results for multiple frames, with each
       * inner vector corresponding to the results for a single frame in the
       * batch. */
       results = ctx->post_process->process(npu_out_tensors_memory, current_batch);
    } catch (const exception &e) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                  "Caught exception while post-processing with vart::Memory: %s", e.what());
      return {{shared_ptr<InferResult>(nullptr)}};
    }
  }

  /* Log the output */
  for (auto& itr : results) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_RESULT, log_level, "Results for frame number %ld",
                num_frame_processed++);
    for (auto& result_itr : itr) {
      InferResultData *base_infer_result = result_itr->get_infer_result();
      if (base_infer_result->result_type ==
          vart::InferResultType::CLASSIFICATION) {
        ClassificationResData *infer_result =
            static_cast<ClassificationResData *>(base_infer_result);
        int size = infer_result->label.size();
        for (int idx = 0; idx < size; idx++) {
          APP_LOG_MESSAGE(APP_LOG_LEVEL_RESULT, log_level,
                      "Classification Label : %s (confidence %lf)",
                      infer_result->label[idx].c_str(),
                      infer_result->confidence[idx]);
        }
      } else if (base_infer_result->result_type ==
                 vart::InferResultType::DETECTION) {
        DetectionResData *infer_result =
            static_cast<DetectionResData *>(base_infer_result);
        APP_LOG_MESSAGE(APP_LOG_LEVEL_RESULT, log_level,
                    "Detection bbox  x : %u y : %u width  : %u height : %u and "
                    "label : %s (confidence %lf)",
                    infer_result->x, infer_result->y, infer_result->width,
                    infer_result->height, infer_result->label.c_str(),
                    infer_result->confidence);
      }
    }
  }

  if (2 != ctx->use_native_output_format || ctx->use_vvas_pp_api) {
    // Unmap the memory after processing (vvas C-API path maps even in native mode)
    for (unsigned int j = 0; j < current_batch; ++j) {
      for (unsigned int i = 0; i < ctx->model_info.num_out_tensors; ++i) {
        npu_out_tensors_memory[j][i]->unmap();
      }
    }
  }

  return results;
}
