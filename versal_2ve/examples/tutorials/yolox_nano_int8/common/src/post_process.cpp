/*
 * Copyright (C) 2024-2026 Advanced Micro Devices, Inc.
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
 * EVENT SHALL "AMD" BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 * OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. Except as contained in this notice, the name of the AMD shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from AMD.
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
#include <numeric>
#include <string>
#include "x_plus_ml_app.hpp"

using namespace vart;

static std::string build_shape_string(const std::vector<uint32_t>& shape) {
  std::string shape_str;
  size_t len = 0;
  for (size_t i = 0; i < shape.size(); ++i) {
    len += std::to_string(shape[i]).size();
    if (i + 1 < shape.size())
      len += 1;
  }
  shape_str.reserve(len);
  for (size_t i = 0; i < shape.size(); ++i) {
    shape_str += std::to_string(shape[i]);
    if (i + 1 < shape.size())
      shape_str += "x";
  }
  return shape_str;
}

static bool parse_scale_factors(const std::string& json_str, std::vector<float>& scale_factors) {
  try {
    pt::ptree config;
    std::istringstream iss(json_str);
    pt::read_json(iss, config);

    for (const auto& value : config.get_child("quant-scale-factors")) {
      scale_factors.push_back(value.second.get_value<float>());
    }
  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::INFO, AppLogLevel::ERROR, "Failed to parse postprocess-config.quant-scale-factors: %s",
            e.what());
    return false;
  }
  return true;
}

bool create_postprocess_context(PipelineContext* pipeline_ctx,
                                AppLogLevel log_level,
                                vart::PostProcessType postprocess_type,
                                const string& json_str,
                                const shared_ptr<vart::Device>& device) {
  bool use_user_provided_scale_factors = false;
  /* Prepare post-processor context */
  /* post-process return the results of each frame in a batch */
  string postprocess_json_config(extract_component_json(json_str, "postprocess-config"));
  if (postprocess_json_config.empty()) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Failed to parse post_process config");
    return false;
  } else {
    APP_LOG(AppLogLevel::DEBUG, log_level, "Post process Config: \n\n\n %s", postprocess_json_config.c_str());
  }

  std::vector<float> quant_scale_factors = {};
  /* Parse user provided quantization scale factors */
  if (!parse_scale_factors(postprocess_json_config, quant_scale_factors)) {
    APP_LOG(AppLogLevel::INFO, log_level,
            "User didn't provide postprocess scale factors, "
            " considering Infer provided scale factors");
  }

  /* Validate number of user provided quantization scale factors */
  if (quant_scale_factors.empty()) {
    APP_LOG(AppLogLevel::INFO, log_level,
            "User provided empty postprocess scale factors, "
            " considering Infer provided scale factors");
  } else if (quant_scale_factors.size() != pipeline_ctx->model_info.num_out_tensors) {
    APP_LOG(AppLogLevel::ERROR, log_level,
            "User provided postprocess scale factors size (%zu) "
            "does not match model output tensors (%zu)",
            quant_scale_factors.size(), pipeline_ctx->model_info.num_out_tensors);
    return false;
  } else {
    use_user_provided_scale_factors = true;
  }

  pipeline_ctx->post_process = new vart::PostProcess(postprocess_type, postprocess_json_config, device);
  if (!pipeline_ctx->post_process) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Unable to create post-process context");
    return false;
  }

  /* Model info required for post-processing */
  APP_LOG(AppLogLevel::DEBUG, log_level, "Post process Info:");
  std::vector<TensorInfo> tensor_info;
  for (size_t i = 0; i < pipeline_ctx->model_info.num_in_tensors; ++i) {
    TensorInfo tinfo = {};
    tinfo.direction = TensorDataDirection::INPUT;  // input tensor
    tinfo.name = pipeline_ctx->model_info.in_tensors_info[i].meta.name;
    tinfo.scale_coeff = pipeline_ctx->preprocess_info.qt_fctr;  // Assumption input tensor is always one
    tinfo.size = pipeline_ctx->model_info.in_tensors_info[i].meta.size_in_bytes;
    APP_LOG(AppLogLevel::DEBUG, log_level, "Input Tensor%ld name: %s", i, tinfo.name.c_str());
    APP_LOG(AppLogLevel::DEBUG, log_level, "Input Tensor%ld size: %u", i, tinfo.size);
    APP_LOG(AppLogLevel::DEBUG, log_level, "Input Tensor%ld scale coeff: %f", i, tinfo.scale_coeff);
    std::string tensor_shape(build_shape_string(pipeline_ctx->model_info.in_tensors_info[i].meta.shape));
    APP_LOG(AppLogLevel::DEBUG, log_level, "Input Tensor%ld shape: %s", i, tensor_shape.c_str());
    tinfo.shape = pipeline_ctx->model_info.in_tensors_info[i].meta.shape;

    tinfo.data_type = map_data_type(pipeline_ctx->model_info.in_tensors_info[i].meta.data_type);
    if (TensorDataType::UNKNOWN == tinfo.data_type) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Input tensor%ld datatype not supported for Post Processing", i);
      return false;
    }

    tensor_info.emplace_back(std::move(tinfo));
  }

  for (size_t j = 0; j < pipeline_ctx->model_info.num_out_tensors; ++j) {
    TensorInfo tinfo = {};
    tinfo.direction = TensorDataDirection::OUTPUT;  // output tensor
    tinfo.name = pipeline_ctx->model_info.out_tensors_info[j].meta.name;
    tinfo.scale_coeff = (use_user_provided_scale_factors)
                            ? quant_scale_factors[j]
                            : pipeline_ctx->model_info.out_tensors_info[j].quantization_factor;
    tinfo.size = pipeline_ctx->model_info.out_tensors_info[j].meta.size_in_bytes;
    APP_LOG(AppLogLevel::DEBUG, log_level, "Output Tensor%ld name: %s", j, tinfo.name.c_str());
    APP_LOG(AppLogLevel::DEBUG, log_level, "Output Tensor%ld size: %u", j, tinfo.size);
    APP_LOG(AppLogLevel::DEBUG, log_level, "Output Tensor%ld scale coeff: %f", j, tinfo.scale_coeff);
    std::string tensor_shape(build_shape_string(pipeline_ctx->model_info.out_tensors_info[j].meta.shape));
    APP_LOG(AppLogLevel::DEBUG, log_level, "Output Tensor%ld shape: %s", j, tensor_shape.c_str());
    tinfo.shape = pipeline_ctx->model_info.out_tensors_info[j].meta.shape;

    tinfo.data_type = map_data_type(pipeline_ctx->model_info.out_tensors_info[j].meta.data_type);
    if (TensorDataType::UNKNOWN == tinfo.data_type) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Output tensor%ld datatype not supported for Post Processing", j);
      return false;
    }

    tensor_info.emplace_back(std::move(tinfo));
  }
  pipeline_ctx->post_process->set_config(tensor_info, pipeline_ctx->model_info.batch_size);

  return true;
}

const char* to_string(vart::PostProcessType type) {
  switch (type) {
    case vart::PostProcessType::RESNET50:
      return "RESNET50";
    case vart::PostProcessType::YOLOV2:
      return "YOLOV2";
    case vart::PostProcessType::SSDRESNET34:
      return "SSDRESNET34";
    case vart::PostProcessType::SOFTMAX:
      return "SOFTMAX";
    case vart::PostProcessType::TOPK:
      return "TOPK";
    case vart::PostProcessType::NMS:
      return "NMS";
    case vart::PostProcessType::ARGMAX:
      return "ARGMAX";
    case vart::PostProcessType::THRESHOLD:
      return "THRESHOLD";
    case vart::PostProcessType::LABEL_MAPPING:
      return "LABEL_MAPPING";
    case vart::PostProcessType::NORMALIZATION:
      return "NORMALIZATION";
    case vart::PostProcessType::ANCHOR_ADJUSTMENT:
      return "ANCHOR_ADJUSTMENT";
    case vart::PostProcessType::CALIBRATION_TEMPERATURE:
      return "CALIBRATION_TEMPERATURE";
    case vart::PostProcessType::CALIBRATION_PLATT:
      return "CALIBRATION_PLATT";
    case vart::PostProcessType::BIAS_CORRECTION:
      return "BIAS_CORRECTION";
    case vart::PostProcessType::OUTLIER_DETECTION:
      return "OUTLIER_DETECTION";
    case vart::PostProcessType::UNCERTAINTY_ESTIMATION:
      return "UNCERTAINTY_ESTIMATION";
    case vart::PostProcessType::SOFT_NMS:
      return "SOFT_NMS";
    case vart::PostProcessType::DISTANCE_IOU_NMS:
      return "DISTANCE_IOU_NMS";
    case vart::PostProcessType::CLASSWISE_NMS:
      return "CLASSWISE_NMS";
    case vart::PostProcessType::OBJECT_COUNT:
      return "OBJECT_COUNT";
    case vart::PostProcessType::SOFTMAXSEG:
      return "SOFTMAXSEG";
    case vart::PostProcessType::SIGMOIDSEG:
      return "SIGMOIDSEG";
    case vart::PostProcessType::ARGMAXSEG:
      return "ARGMAXSEG";
    default:
      return "UNKNOWN";
  }
}

/* Perform post-processing on the output tensor data to interpret the
 * inference results */
vector<vector<shared_ptr<InferResult>>> postprocess_process_frames(
    PipelineContext* pipeline_ctx,
    AppLogLevel log_level,
    uint32_t current_batch,
    std::vector<std::vector<std::shared_ptr<vart::Memory>>>& npu_out_tensors_memory,
    int64_t num_frame_processed) {
  unsigned int total_valid_tensor = pipeline_ctx->model_info.num_out_tensors * current_batch;

  // Start printing with 1 not 0 for number of frames processed
  int64_t batch_frame_number = 1;

  std::vector<int8_t*> tensor(total_valid_tensor);
  for (unsigned int j = 0; j < current_batch; ++j) {
    for (unsigned int i = 0; i < pipeline_ctx->model_info.num_out_tensors; ++i) {
      unsigned int index = j * pipeline_ctx->model_info.num_out_tensors + i;

      // Map the memory for the current tensor
      const unsigned char* mapped_memory = npu_out_tensors_memory[j][i]->map(vart::DataMapFlags::READ);

      // Assign the pointer to the tensor vector
      tensor[index] = const_cast<int8_t*>(reinterpret_cast<const int8_t*>(mapped_memory));
    }
  }

  vector<vector<shared_ptr<InferResult>>> results;
  try {
    /* Post-process the entire batch in one go and return the results as a
     * vector of detections/outputs for each frame in the batch. */
    /* The 'results' vector holds the results for multiple frames, with each
     * inner vector corresponding to the results for a single frame in the
     * batch. */
    auto start = chrono::high_resolution_clock::now();
    results = pipeline_ctx->post_process->process(std::move(tensor), current_batch);
    auto end = chrono::high_resolution_clock::now();
    if (pipeline_ctx->is_benchmark_enabled) {
      pipeline_ctx->total_postprocess_time +=
          std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    }
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Caught exception while post-processing : %s", e.what());
    return {{shared_ptr<InferResult>(nullptr)}};
  }
  APP_LOG(AppLogLevel::RESULT, log_level, "Results for Pipeline %d :", pipeline_ctx->pipeline_id);
  /* Log the output */
  for (auto itr : results) {
    APP_LOG(AppLogLevel::RESULT, log_level, "Post processing batch %ld frame number: %ld", num_frame_processed,
            batch_frame_number++);
    for (auto result_itr : itr) {
      InferResultData* base_infer_result = result_itr->get_infer_result();
      if (base_infer_result->result_type == vart::InferResultType::CLASSIFICATION) {
        ClassificationResData* infer_result = static_cast<ClassificationResData*>(base_infer_result);
        int size = infer_result->label.size();
        for (int idx = 0; idx < size; idx++) {
          APP_LOG(AppLogLevel::RESULT, log_level, "Classification Label : %s (confidence %lf)",
                  infer_result->label[idx].c_str(), infer_result->confidence[idx]);
        }
      } else if (base_infer_result->result_type == vart::InferResultType::DETECTION) {
        DetectionResData* infer_result = static_cast<DetectionResData*>(base_infer_result);
        APP_LOG(AppLogLevel::RESULT, log_level,
                "Detection bbox  x : %u y : %u width  : %u height : %u and "
                "label : %s (confidence %lf)",
                infer_result->x, infer_result->y, infer_result->width, infer_result->height,
                infer_result->label.c_str(), infer_result->confidence);
      } else if (base_infer_result->result_type == vart::InferResultType::SEGMENTATION) {
        SegmentationResData* infer_result = static_cast<SegmentationResData*>(base_infer_result);
        for (size_t idx = 0; idx < infer_result->numOutputs; ++idx) {
          APP_LOG(AppLogLevel::RESULT, log_level, "Segmentation result (width: %u, height: %u)",
                  infer_result->width[idx], infer_result->height[idx]);
        }
      }
    }
  }
  APP_LOG(AppLogLevel::RESULT, log_level, "===========================");
  // Unmap the memory after processing
  for (unsigned int j = 0; j < current_batch; ++j) {
    for (unsigned int i = 0; i < pipeline_ctx->model_info.num_out_tensors; ++i) {
      npu_out_tensors_memory[j][i]->unmap();
    }
  }

  return results;
}
