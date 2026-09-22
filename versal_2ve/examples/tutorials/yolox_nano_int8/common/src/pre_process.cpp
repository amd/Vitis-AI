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

/**
 * @brief Set the Region of Interest (ROI) for PanScan cropping
 * @param preprocess_op Pre-processing operation
 */
static void set_roi_pan_scan(vart::PreProcessOp& preprocess_op) {
  float current_aspect_ratio =
      static_cast<float>(preprocess_op.in_roi.width) / static_cast<float>(preprocess_op.in_roi.height);
  float target_aspect_ratio =
      static_cast<float>(preprocess_op.out_roi.width) / static_cast<float>(preprocess_op.out_roi.height);
  int x, y, width, height;
  x = preprocess_op.in_roi.x;
  y = preprocess_op.in_roi.y;
  width = preprocess_op.in_roi.width;
  height = preprocess_op.in_roi.height;

  /* Target aspect rato is greater so crop from top and bottom */
  if (current_aspect_ratio < target_aspect_ratio) {
    width = preprocess_op.in_roi.width;
    height = float(preprocess_op.in_roi.width) * target_aspect_ratio;
    x = 0;
    y = (preprocess_op.in_roi.height - height) / 2;

    /* Target aspect rato is smaller so crop from left and right */
  } else {
    width = float(preprocess_op.in_roi.height) * target_aspect_ratio;
    height = preprocess_op.in_roi.height;

    x = (preprocess_op.in_roi.width - width) / 2;
    y = 0;
  }

  preprocess_op.in_roi.x = x;
  preprocess_op.in_roi.y = y;
  preprocess_op.in_roi.width = width;
  preprocess_op.in_roi.height = height;
}

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

  /* If PanScan enabled set roi for Pan Scan cropping */
  if (pipeline_ctx->do_pan_scan) {
    set_roi_pan_scan(preprocess_op);
  }

  preprocess_ops.push_back(preprocess_op);

  APP_LOG(AppLogLevel::DEBUG, log_level, "Pre-process input Roi: x=%d y=%d width=%d height=%d", preprocess_op.in_roi.x,
          preprocess_op.in_roi.y, preprocess_op.in_roi.width, preprocess_op.in_roi.height);
  APP_LOG(AppLogLevel::DEBUG, log_level, "Pre-process output Roi: x=%d y=%d width=%d height=%d",
          preprocess_op.out_roi.x, preprocess_op.out_roi.y, preprocess_op.out_roi.width, preprocess_op.out_roi.height);
  APP_LOG(AppLogLevel::DEBUG, log_level, "Run Pre-process for %dx%d -> %dx%d", preprocess_op.in_roi.width,
          preprocess_op.in_roi.height, preprocess_op.out_roi.width, preprocess_op.out_roi.height);
  try {
    auto start = std::chrono::high_resolution_clock::now();
    pipeline_ctx->pre_process->process(preprocess_ops);
    auto end = std::chrono::high_resolution_clock::now();
    if (pipeline_ctx->is_benchmark_enabled) {
      pipeline_ctx->total_preprocess_time += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    }
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Failed to do pre-process %s", e.what());
    return false;
  }
  return true;
}
