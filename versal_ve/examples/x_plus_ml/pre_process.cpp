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

/* Sets the Region of Interest to perform PanScan cropping */
static void set_roi_pan_scan(vart::PreProcessOp &preprocess_op) {
  float current_aspect_ratio =
      float(preprocess_op.in_roi.width) / preprocess_op.in_roi.height;
  float target_aspect_ratio =
      float(preprocess_op.out_roi.width) / preprocess_op.out_roi.height;
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

bool create_preprocess_context(AppContext *ctx) {
  AppLogLevel log_level = ctx->log_level;

  /* Prepare pre-processor context */
  /* Pre-process will take care of scale and csc as well as normalization and
   * quantization */
  string preprocess_json_config =
      extract_component_json(ctx->json_str, "preprocess-config");
  if (preprocess_json_config.empty()) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                "Failed to parse pre_process config");
    return false;
  } else {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Preprocess Config: %s",
                preprocess_json_config.c_str());
  }
  ctx->pre_process = new vart::PreProcess(DEFAULT_PREPROCESS_TYPE,
                                          preprocess_json_config, ctx->device);
  if (!ctx->pre_process) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                "Unable to create pre-process context");
    return false;
  }

  /* Extract height, width and quantization_factor from model info */
  ctx->preprocess_info.height = ctx->model_info.model_height;
  ctx->preprocess_info.width = ctx->model_info.model_width;
  /* Assumption input tensor is always one */
  ctx->preprocess_info.qt_fctr = (ctx->model_info.in_tensors[0].quantization_factor > 0 ? ctx->model_info.in_tensors[0].quantization_factor : 1);
  // print debug log info
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
            "PreProcessInfo (output: %dx%d, qt_fctr: %f)", ctx->preprocess_info.width, ctx->preprocess_info.height, ctx->preprocess_info.qt_fctr);
  ctx->pre_process->set_preprocess_info(ctx->preprocess_info);

  return true;
}

bool preprocess_process_frame(AppContext *ctx,
                              shared_ptr<vart::VideoFrame> input_frame,
                              shared_ptr<vart::VideoFrame> output_frame) {
  AppLogLevel log_level = ctx->log_level;
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
  if (ctx->do_pan_scan) {
    set_roi_pan_scan(preprocess_op);
  }

  preprocess_ops.push_back(preprocess_op);

  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Run Pre-process for %dx%d -> %dx%d",
              preprocess_op.in_roi.width, preprocess_op.in_roi.height,
              preprocess_op.out_roi.width, preprocess_op.out_roi.height);
  try {
    ctx->pre_process->process(preprocess_ops);
  } catch (const exception &e) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Failed to do pre-process %s",
                e.what());
    return false;
  }
  return true;
}
