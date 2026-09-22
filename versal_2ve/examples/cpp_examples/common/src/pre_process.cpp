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
 * EVENT SHALL "AMD" BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 * OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. Except as contained in this notice, the name of the AMD shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from AMD.
 */

/**
 * @file pre_process.cpp
 * @brief Implementation of the pre-processing functions for the x_plus_ml
 * application.
 *
 * This file contains the implementation of the pre-processing functions used in
 * the x_plus_ml application.
 *
 * If user wants to integrate his new custom pre-processing implementaiton which
 * he/she has developed, it can be done by changing the way vart::PreProcess
 * class object is instantiated. One can use it's other constructor signature
 * which accpets the shared pointer to the user's implementation instance.
 *
 * Ex: Lets say the new custom post-processing implementation is named as
 * PreProcessImplCustom, then the instantiaion will be like below:
 *
 * ctx->pre_process = new
 * vart::PreProcess(std::make_shared<PreProcessImplCustom>());
 *
 * Note : 1) On how to implement the custom pre-processing or any other VART
 * module, please refer to the VART documentation.
 *
 */

#include "x_plus_ml_app.hpp"

namespace {

string get_colour_space(vart::VideoFormat fmt) {
  switch (fmt) {
    case vart::VideoFormat::RGBx:
    case vart::VideoFormat::RGB:
    case vart::VideoFormat::RGBP:
    case vart::VideoFormat::RGBP_FLOAT:
    case vart::VideoFormat::RGBx_BF16:
    case vart::VideoFormat::RGBx_FP16:
    case vart::VideoFormat::RGB_FLOAT:
    case vart::VideoFormat::RGBP_BF16:
    case vart::VideoFormat::RGBP_FP16:
    case vart::VideoFormat::RGB_BF16:
    case vart::VideoFormat::RGB_FP16:
      return "RGB";

    case vart::VideoFormat::BGRx:
    case vart::VideoFormat::BGR:
    case vart::VideoFormat::BGR_FLOAT:
    case vart::VideoFormat::BGRx_BF16:
    case vart::VideoFormat::BGRx_FP16:
    case vart::VideoFormat::BGRP:
    case vart::VideoFormat::BGRP_FLOAT:
    case vart::VideoFormat::BGRP_FP16:
    case vart::VideoFormat::BGRP_BF16:
    case vart::VideoFormat::BGR_BF16:
    case vart::VideoFormat::BGR_FP16:
      return "BGR";

    default:
      return "";
  }
}

vart::VideoFormat derive_expected_colour_format(const string& colour_space,
                                                  MemoryLayout layout,
                                                  DataType dtype,
                                                  const std::vector<uint32_t>& shape) {
  using VF = vart::VideoFormat;

  const bool is_rgb = (colour_space == "RGB");
  const bool is_bgr = (colour_space == "BGR");
  if (!is_rgb && !is_bgr) {
    return VF::UNKNOWN;
  }

  VF result = VF::UNKNOWN;

  switch (layout) {
    case MemoryLayout::HCWNC4:
      switch (dtype) {
        case DataType::INT8:
        case DataType::UINT8:
          result = is_rgb ? VF::RGBx : VF::BGRx;
          break;
        case DataType::BF16:
          result = is_rgb ? VF::RGBx_BF16 : VF::BGRx_BF16;
          break;
        case DataType::FP16:
          result = is_rgb ? VF::RGBx_FP16 : VF::BGRx_FP16;
          break;
        default:
          result = VF::UNKNOWN;
          break;
      }
      break;

    case MemoryLayout::NCHW: {
      /* NCHW policy: support only C=3 (planar RGB/BGR). */
      if (shape.size() < 4) {
        result = VF::UNKNOWN;
        break;
      }
      const uint32_t channel_count = shape[1];
      switch (dtype) {
        case DataType::INT8:
        case DataType::UINT8:
          if (channel_count == 3) {
            result = is_rgb ? VF::RGBP : VF::BGRP;
          } else {
            result = VF::UNKNOWN;
          }
          break;
        case DataType::FLOAT32:
          if (channel_count == 3) {
            result = is_rgb ? VF::RGBP_FLOAT : VF::BGRP_FLOAT;
          } else {
            result = VF::UNKNOWN;
          }
          break;
        case DataType::BF16:
          if (channel_count == 3) {
            result = is_rgb ? VF::RGBP_BF16 : VF::BGRP_BF16;
          } else {
            result = VF::UNKNOWN;
          }
          break;
        case DataType::FP16:
          if (channel_count == 3) {
            result = is_rgb ? VF::RGBP_FP16 : VF::BGRP_FP16;
          } else {
            result = VF::UNKNOWN;
          }
          break;
        default:
          result = VF::UNKNOWN;
          break;
      }
      break;
    }

    case MemoryLayout::NHWC: {
      /* NHWC policy: support C=3 (RGB/BGR) and C=4 (RGBx/BGRx where supported). */
      if (shape.size() < 4) {
        result = VF::UNKNOWN;
        break;
      }
      const uint32_t channel_count = shape[3];
      switch (dtype) {
        case DataType::INT8:
        case DataType::UINT8:
          if (channel_count == 3) {
            result = is_rgb ? VF::RGB : VF::BGR;
          } else if (channel_count == 4) {
            result = is_rgb ? VF::RGBx : VF::BGRx;
          } else {
            result = VF::UNKNOWN;
          }
          break;
        case DataType::FLOAT32:
          /* channel_count == 4 (RGBx/BGRx FP32) is not supported/enabled in vart-x/preprocess. */
          if (channel_count == 3) {
            result = is_rgb ? VF::RGB_FLOAT : VF::BGR_FLOAT;
          } else {
            result = VF::UNKNOWN;
          }
          break;
        case DataType::BF16:
          if (channel_count == 3) {
            result = is_rgb ? VF::RGB_BF16 : VF::BGR_BF16;
          } else if (channel_count == 4) {
            result = is_rgb ? VF::RGBx_BF16 : VF::BGRx_BF16;
          } else {
            result = VF::UNKNOWN;
          }
          break;
        case DataType::FP16:
          if (channel_count == 3) {
            result = is_rgb ? VF::RGB_FP16 : VF::BGR_FP16;
          } else if (channel_count == 4) {
            result = is_rgb ? VF::RGBx_FP16 : VF::BGRx_FP16;
          } else {
            result = VF::UNKNOWN;
          }
          break;
        default:
          result = VF::UNKNOWN;
          break;
      }
      break;
    }

    default:
      result = VF::UNKNOWN;
      break;
  }

  return result;
}

const char* colour_format_to_string(vart::VideoFormat fmt) {
  switch (fmt) {
    case vart::VideoFormat::RGBx:
      return "RGBX";
    case vart::VideoFormat::BGRx:
      return "BGRX";
    case vart::VideoFormat::RGB:
      return "RGB";
    case vart::VideoFormat::BGR:
      return "BGR";
    case vart::VideoFormat::RGBP:
      return "RGBP";
    case vart::VideoFormat::BGRP:
      return "BGRP";
    case vart::VideoFormat::RGBP_FLOAT:
      return "RGBP_FLOAT";
    case vart::VideoFormat::BGRP_FLOAT:
      return "BGRP_FLOAT";
    case vart::VideoFormat::RGB_FLOAT:
      return "RGB_FLOAT";
    case vart::VideoFormat::BGR_FLOAT:
      return "BGR_FLOAT";
    case vart::VideoFormat::RGBx_BF16:
      return "RGBX_BF16";
    case vart::VideoFormat::BGRx_BF16:
      return "BGRX_BF16";
    case vart::VideoFormat::RGBx_FP16:
      return "RGBX_FP16";
    case vart::VideoFormat::BGRx_FP16:
      return "BGRX_FP16";
    case vart::VideoFormat::RGBP_BF16:
      return "RGBP_BF16";
    case vart::VideoFormat::RGBP_FP16:
      return "RGBP_FP16";
    case vart::VideoFormat::BGRP_BF16:
      return "BGRP_BF16";
    case vart::VideoFormat::BGRP_FP16:
      return "BGRP_FP16";
    case vart::VideoFormat::RGB_BF16:
      return "RGB_BF16";
    case vart::VideoFormat::RGB_FP16:
      return "RGB_FP16";
    case vart::VideoFormat::BGR_FP16:
      return "BGR_FP16";
    case vart::VideoFormat::BGR_BF16:
      return "BGR_BF16";
    case vart::VideoFormat::Y_UV8_420:
      return "Y_UV8_420";
    default:
      return "?";
  }
}

const char* tensor_data_type_to_string(DataType dtype) {
  switch (dtype) {
    case DataType::INT8:
      return "INT8";
    case DataType::UINT8:
      return "UINT8";
    case DataType::INT16:
      return "INT16";
    case DataType::UINT16:
      return "UINT16";
    case DataType::BF16:
      return "BF16";
    case DataType::FP16:
      return "FP16";
    case DataType::INT32:
      return "INT32";
    case DataType::UINT32:
      return "UINT32";
    case DataType::FLOAT32:
      return "FLOAT32";
    case DataType::INT64:
      return "INT64";
    case DataType::UINT64:
      return "UINT64";
    case DataType::UNKNOWN:
      return "UNKNOWN";
    default:
      return "UNKNOWN";
  }
}

const char* tensor_memory_layout_to_string(MemoryLayout layout) {
  switch (layout) {
    case MemoryLayout::NHW:
      return "NHW";
    case MemoryLayout::NHWC:
      return "NHWC";
    case MemoryLayout::NCHW:
      return "NCHW";
    case MemoryLayout::NHWC4:
      return "NHWC4";
    case MemoryLayout::NC4HW4:
      return "NC4HW4";
    case MemoryLayout::NC8HW8:
      return "NC8HW8";
    case MemoryLayout::HCWNC4:
      return "HCWNC4";
    case MemoryLayout::HCWNC8:
      return "HCWNC8";
    case MemoryLayout::GENERIC:
      return "GENERIC";
    case MemoryLayout::UNKNOWN:
      return "UNKNOWN";
    default:
      return "UNKNOWN";
  }
}

bool validate_preprocess_colour_format(PipelineContext* pipeline_ctx, AppLogLevel log_level) {
  if (pipeline_ctx->model_info.in_tensors_info.empty()) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Cannot validate colour-format without input tensor metadata");
    return false;
  }

  const TensorMeta& first_tensor = pipeline_ctx->model_info.in_tensors_info[0].meta;
  const vart::VideoFormat user_colour_format = pipeline_ctx->preprocess_info.colour_format;

  if (user_colour_format == vart::VideoFormat::UNKNOWN) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Invalid or missing colour-format for pipeline %d",
            pipeline_ctx->pipeline_id);
    return false;
  }

  const string colour_space = get_colour_space(user_colour_format);
  if (colour_space.empty()) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Cannot determine colour space for colour-format \"%s\" (pipeline %d)",
            colour_format_to_string(user_colour_format), pipeline_ctx->pipeline_id);
    return false;
  }

  const vart::VideoFormat expected_colour_format = derive_expected_colour_format(
      colour_space, first_tensor.memory_layout, first_tensor.data_type, first_tensor.shape);
  if (expected_colour_format == vart::VideoFormat::UNKNOWN) {
    APP_LOG(AppLogLevel::ERROR, log_level,
            "Cannot derive colour-format for pipeline %d: colour_space=%s, layout=%s, dtype=%s",
            pipeline_ctx->pipeline_id, colour_space.c_str(), tensor_memory_layout_to_string(first_tensor.memory_layout),
            tensor_data_type_to_string(first_tensor.data_type));
    return false;
  }

  if (expected_colour_format != user_colour_format) {
    APP_LOG(AppLogLevel::ERROR, log_level,
            "colour-format mismatch for pipeline %d: user specified \"%s\" but "
            "tensor metadata (layout=%s, dtype=%s) expects %s",
            pipeline_ctx->pipeline_id, colour_format_to_string(user_colour_format),
            tensor_memory_layout_to_string(first_tensor.memory_layout),
            tensor_data_type_to_string(first_tensor.data_type), colour_format_to_string(expected_colour_format));
    return false;
  }

  APP_LOG(AppLogLevel::INFO, log_level, "Validated colour-format \"%s\" for pipeline %d (layout=%s, dtype=%s)",
          colour_format_to_string(user_colour_format), pipeline_ctx->pipeline_id,
          tensor_memory_layout_to_string(first_tensor.memory_layout), tensor_data_type_to_string(first_tensor.data_type));
  return true;
}

}  // namespace

/**
 * @brief Create and initialize the pre-processing context
 * @param pipeline_ctx Pipeline context
 * @param log_level Application log level
 * @param json_str JSON configuration string
 * @param device Device handle
 * @return true if successful, false otherwise
 */
bool create_preprocess_context(PipelineContext* pipeline_ctx,
                               AppLogLevel log_level,
                               const string& json_str,
                               const shared_ptr<vart::Device>& device) {
  if (!validate_preprocess_colour_format(pipeline_ctx, log_level)) {
    return false;
  }

  /* Prepare pre-processor context */
  /* Pre-process will take care of scale and csc as well as normalization and
   * quantization */
  string preprocess_json_config = extract_component_json(json_str, "preprocess-config");
  if (preprocess_json_config.empty()) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Failed to parse pre_process config");
    return false;
  } else {
    APP_LOG(AppLogLevel::DEBUG, log_level, "Preprocess Config: %s", preprocess_json_config.c_str());
  }
  pipeline_ctx->pre_process = new vart::PreProcess(DEFAULT_PREPROCESS_TYPE, preprocess_json_config, device);
  if (!pipeline_ctx->pre_process) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Unable to create pre-process context");
    return false;
  }

  /* Extract height, width and quantization_factor from model info */
  pipeline_ctx->preprocess_info.height = pipeline_ctx->model_info.model_height;
  pipeline_ctx->preprocess_info.width = pipeline_ctx->model_info.model_width;
  /* Assumption input tensor is always one */
  pipeline_ctx->preprocess_info.qt_fctr = (pipeline_ctx->quant_scale_factor_conf_set)
                                              ? pipeline_ctx->quant_scale_factor
                                              : pipeline_ctx->model_info.in_tensors_info[0].quantization_factor;

  if (pipeline_ctx->quant_scale_factor_conf_set) {
    APP_LOG(AppLogLevel::INFO, log_level, "Using user provided quantization factor: %f",
            pipeline_ctx->quant_scale_factor);
  }

  APP_LOG(AppLogLevel::DEBUG, log_level, "Preprocess Info: %dx%d, Qt Fctr: %f, Format: %d",
          pipeline_ctx->preprocess_info.width, pipeline_ctx->preprocess_info.height,
          pipeline_ctx->preprocess_info.qt_fctr, static_cast<int>(pipeline_ctx->preprocess_info.colour_format));

  pipeline_ctx->pre_process->set_preprocess_info(pipeline_ctx->preprocess_info);
  APP_LOG(AppLogLevel::DEBUG, log_level, "Pre-processing context created");
  return true;
}

/**
 * @brief Process a video frame using the pre-processing context
 * @param pipeline_ctx Pipeline context
 * @param log_level Application log level
 * @param input_frame Input video frame
 * @param output_frame Output video frame
 * @return true if successful, false otherwise
 */
bool preprocess_process_frame(PipelineContext* pipeline_ctx,
                              AppLogLevel log_level,
                              shared_ptr<vart::VideoFrame> input_frame,
                              shared_ptr<vart::VideoFrame> output_frame) {
  vector<vart::PreProcessOp> preprocess_ops;
  vart::PreProcessOp preprocess_op;
  /* set the pre-process input ops */
  preprocess_op.in_roi.x = 0;
  preprocess_op.in_roi.y = 0;
  preprocess_op.in_roi.height = input_frame->get_video_info().height;
  preprocess_op.in_roi.width = input_frame->get_video_info().width;
  preprocess_op.in_frame = input_frame.get();
  /* set the pre-process output ops */
  preprocess_op.out_roi.x = 0;
  preprocess_op.out_roi.y = 0;
  preprocess_op.out_roi.height = output_frame->get_video_info().height;
  preprocess_op.out_roi.width = output_frame->get_video_info().width;
  preprocess_op.out_frame = output_frame.get();

  preprocess_ops.push_back(preprocess_op);

  APP_LOG(AppLogLevel::DEBUG, log_level, "Pre-process input Roi: x=%d y=%d width=%d height=%d", preprocess_op.in_roi.x,
          preprocess_op.in_roi.y, preprocess_op.in_roi.width, preprocess_op.in_roi.height);
  APP_LOG(AppLogLevel::DEBUG, log_level, "Pre-process output Roi: x=%d y=%d width=%d height=%d",
          preprocess_op.out_roi.x, preprocess_op.out_roi.y, preprocess_op.out_roi.width, preprocess_op.out_roi.height);
  APP_LOG(AppLogLevel::DEBUG, log_level, "Run Pre-process for %dx%d -> %dx%d", preprocess_op.in_roi.width,
          preprocess_op.in_roi.height, preprocess_op.out_roi.width, preprocess_op.out_roi.height);
  pipeline_ctx->scale_info = {};
  try {
    auto start = std::chrono::high_resolution_clock::now();
    vector<vart::PreProcessOpRes> ops_res;
    pipeline_ctx->pre_process->process(preprocess_ops, ops_res);
    auto end = std::chrono::high_resolution_clock::now();
    if (pipeline_ctx->is_benchmark_enabled) {
      pipeline_ctx->total_preprocess_time += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    }
    if (!ops_res.empty()) {
      const auto& in_vinfo = input_frame->get_video_info();
      const auto& out_vinfo = output_frame->get_video_info();
      /* Single PreProcessOp per call (preprocess_ops.size() == 1), so use ops_res[0] (ops_res[i] matches preprocess_ops[i]). */
      const auto& type_meta = ops_res[0].type_meta;
      /* Bind PreProcess geometry to InferResult transform (independent modules). */
      pipeline_ctx->scale_info.model_input_width = out_vinfo.width;
      pipeline_ctx->scale_info.model_input_height = out_vinfo.height;
      pipeline_ctx->scale_info.input_frame_width = in_vinfo.width;
      pipeline_ctx->scale_info.input_frame_height = in_vinfo.height;
      pipeline_ctx->scale_info.scale_x = type_meta.scale_x;
      pipeline_ctx->scale_info.scale_y = type_meta.scale_y;
      pipeline_ctx->scale_info.crop_x = type_meta.crop_x;
      pipeline_ctx->scale_info.crop_y = type_meta.crop_y;
      pipeline_ctx->scale_info.pad_x = type_meta.pad_x;
      pipeline_ctx->scale_info.pad_y = type_meta.pad_y;
    }
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Failed to do pre-process %s", e.what());
    return false;
  }
  return true;
}
