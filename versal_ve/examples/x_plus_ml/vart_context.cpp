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
 * @file vart_context.cpp
 * @brief This file has the methods for creating VART modules context required
 * for the x_plus_ml application.
 *
 */

#include "x_plus_ml_app.hpp"
#include <filesystem>

static constexpr uint32_t RGB_COLOR_VALUE_ZERO = 0;

/* Return the size of frame required for VideoFormat */
static size_t get_video_frame_size(vart::VideoFormat fmt, size_t width,
                                   size_t height) {
  size_t size;

  switch (fmt) {
  case vart::VideoFormat::BGR:
  case vart::VideoFormat::RGB:
    size = (width * height) * 3;
    break;
  case vart::VideoFormat::BGR_FLOAT:
    size = (width * height) * 3 * 4;
    break;
  case vart::VideoFormat::Y_UV8_420:
    size = static_cast<size_t>(static_cast<double>(width * height) * 1.5);
    break;
  case vart::VideoFormat::BGRx:
  case vart::VideoFormat::RGBx:
    size = (width * height) * 4;
    break;
  case vart::VideoFormat::BGRx_BF16:
  case vart::VideoFormat::RGBx_BF16:
    size = (width * height) * 4 * 2;
    break;
  default:
    size = 0;
    break;
  }
  return size;
}

/* Map AppVideoInputFormat to Vart VideoFormat
 * As JPEG and MP4 decoder return BGR output, the function return accordingly
 */
static vart::VideoFormat get_video_frame_format(AppVideoInputFormat app_fmt) {
  vart::VideoFormat fmt;
  switch (app_fmt) {
  /* For mp4 and jpeg, decoder is returning bgr for now */
  case APP_VIDEO_INPUT_FORMAT_MP4:
  case APP_VIDEO_INPUT_FORMAT_JPEG:
  case APP_VIDEO_INPUT_FORMAT_BGR:
    fmt = vart::VideoFormat::BGR;
    break;
  case APP_VIDEO_INPUT_FORMAT_BGR_FLOAT:
    fmt = vart::VideoFormat::BGR_FLOAT;
    break;
  case APP_VIDEO_INPUT_FORMAT_NV12:
    fmt = vart::VideoFormat::Y_UV8_420;
    break;
  case APP_VIDEO_INPUT_FORMAT_RGB:
    fmt = vart::VideoFormat::RGB;
    break;
  case APP_VIDEO_INPUT_FORMAT_RGBX:
    fmt = vart::VideoFormat::RGBx;
    break;
  case APP_VIDEO_INPUT_FORMAT_BGRX:
    fmt = vart::VideoFormat::BGRx;
    break;
  default:
    fmt = vart::VideoFormat::UNKNOWN;
    break;
  }
  return fmt;
}

/* Reset inference information */
static void reset_infer_info(InferModelConf *model_info) {
  model_info->model_width = 0;
  model_info->model_height = 0;
  model_info->batch_size = 0;
  model_info->num_in_tensors = 0;
  model_info->num_out_tensors = 0;
}

/* Initialize the parameters and handles in the AppContext structure
 */
void init_app_context(AppContext *ctx) {
  /* Initialize handle parameters */
  ctx->log_level = APP_LOG_LEVEL_WARNING;
#ifdef DUMP_INPUTS
  ctx->dump_input_path.clear();
  ctx->dump_infer_input_path.clear();
#endif
  ctx->out_file_path.clear();
  ctx->input_file_path.clear();
  ctx->input_height = 0;
  ctx->input_width = 0;
  ctx->iteration_counter = 0;
  ctx->max_iterations = 1;
  ctx->device_idx = DEFAULT_DEVICE_INDEX;

  ctx->device = nullptr;
  ctx->pre_process = nullptr;
  ctx->post_process = nullptr;
  ctx->meta_convert = nullptr;
  ctx->overlay = nullptr;
  ctx->preprocess_out_pool = nullptr;
  ctx->in_pool = nullptr;
  ctx->vid_capture = nullptr;

  ctx->num_frame_to_process = APP_PROCESS_ALL_FRAMES;

  /* By default set panscan cropping to false */
  ctx->do_pan_scan = false;

  ctx->preprocess_enable = false;
  ctx->postprocess_enable = false;
  ctx->metaconvert_enable = false;
  ctx->dump_all_inputs = false;
  ctx->dump_npu_output = false;

  /* Initialize inference configuration and information */
  reset_infer_info(&ctx->model_info);
}

static string vector_to_string(const vector<uint32_t> &vec) {
  ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < vec.size(); ++i) {
    oss << vec[i];
    if (i != vec.size() - 1) {
      oss << ", ";
    }
  }
  oss << "]";
  return oss.str();
}

bool create_out_tensor_memory(
    AppContext *ctx,
    vector<vector<shared_ptr<vart::Memory>>> &npu_out_tensors_memory) {
  size_t mem_index_size = ctx->post_process_mem_banks.size();
  AppLogLevel log_level = ctx->log_level;

  int mem_index = DEFAULT_FRAME_MEMBANK;

  npu_out_tensors_memory.resize(ctx->model_info.batch_size);
  for (unsigned int j = 0u; j < ctx->model_info.batch_size; j++) {
    for (unsigned int i = 0u; i < ctx->model_info.num_out_tensors; ++i) {
      if (mem_index_size) {
        auto idx = (j * ctx->model_info.num_out_tensors + i) % mem_index_size;
        mem_index = ctx->post_process_mem_banks[idx];
      }

      APP_LOG_MESSAGE(
        APP_LOG_LEVEL_DEBUG, log_level,
        "Creating NPU out tensor memory with shape: %s size %ld on mem bank %d",
        vector_to_string(ctx->model_info.out_tensors[i].meta.shape).c_str(),
        ctx->model_info.out_tensors[i].meta.size_in_bytes, mem_index);

      npu_out_tensors_memory[j].push_back(make_shared<vart::Memory>(
          vart::MemoryImplType::XRT, ctx->model_info.out_tensors[i].meta.size_in_bytes,
          mem_index, ctx->device));
    }
  }
  return true;
}

static bool reset_video_frame(shared_ptr<vart::VideoFrame> video_frame, AppLogLevel &log_level) {
  const vart::VideoFrameMapInfo *map_info = nullptr;
  try {
    map_info = &video_frame->map(vart::DataMapFlags::READ);
  } catch (const exception &e) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Failed to map memory : %s",
                e.what());
    return false;
  }
  memset(map_info->planes[0].data, RGB_COLOR_VALUE_ZERO, map_info->size);
  /* Unmap video frame data */
  video_frame->unmap();
  return true;
}


bool create_input_buffer_pool(AppContext *ctx) {
  AppLogLevel log_level = ctx->log_level;
  /* Get input vinfo */
  vart::VideoInfo in_vinfo;
  memset(&in_vinfo, 0, sizeof(in_vinfo));
  vart::VideoFormat fmt = get_video_frame_format(ctx->input_fmt);
  size_t buf_size = get_video_frame_size(fmt, ctx->input_width, ctx->input_height);
  if (ctx->preprocess_enable) {
    ctx->pre_process->get_input_vinfo(ctx->input_height, ctx->input_width,
                                      fmt, in_vinfo);
  } else {
    in_vinfo.height = ctx->input_height;
    in_vinfo.width = ctx->input_width;
    in_vinfo.fmt = fmt;
    in_vinfo.alignment.padding_left = 0;
    in_vinfo.alignment.padding_right = 0;
    in_vinfo.alignment.padding_top = 0;
    in_vinfo.alignment.padding_bottom = 0;
    in_vinfo.n_planes = 1;
    for (uint32_t idx = 0; idx < in_vinfo.n_planes; idx++) {
      in_vinfo.alignment.stride_align[idx] = 1;
    }
  }
  /* Create input pool */
  /* The number of buffers in the pool should be equal to or greater than the
  * model's batch size.
  * Setting it equal to the batch size is sufficient because the application
  * runs in a single thread,
  * and the buffers are freed before the next iteration of the loop. */
  ctx->in_pool = new VideoFramePool(ctx->model_info.batch_size,
                                  DEFAULT_PREPROCESS_POOL_TYPE, buf_size,
                                  ctx->ppe_mbank_in, in_vinfo, ctx->device);
  if (ctx->in_pool == nullptr) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "got nullptr while creating input frame pool");
    return false;
  }
  return true;
}

bool create_preprocess_output_pool(AppContext *ctx) {
  AppLogLevel log_level = ctx->log_level;
  
  /* Get output vinfo */
  vart::VideoInfo out_vinfo;
  out_vinfo = ctx->pre_process->get_output_vinfo();
  size_t buf_size =  get_video_frame_size(out_vinfo.fmt, ctx->model_info.model_width,
                          ctx->model_info.model_height);

  /* Create output pool */
  /* The number of buffers in the pool should be equal to or greater than
  * the model's batch size. Setting it equal to the batch size is
  * sufficient because the application runs in a single thread, and the
  * buffers are freed before the next iteration of the loop. */
  ctx->preprocess_out_pool = new VideoFramePool(
      ctx->model_info.batch_size, DEFAULT_PREPROCESS_POOL_TYPE, buf_size,
      ctx->ppe_mbank_out, out_vinfo, ctx->device);
  if (ctx->preprocess_out_pool == nullptr) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Got nullptr while creating pre-process output frame pool");
    return false;
  }

  /* AIESW-11055: Fix for inconsistent outputs across the boards and across the reboots */
  for (uint32_t i=0; i < ctx->model_info.batch_size; i++) {
    shared_ptr<vart::VideoFrame> frame = ctx->preprocess_out_pool->acquire_frame();
    if(!reset_video_frame(frame, log_level)) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Unable to reset pre-process output frame");
      return false;
    }
    ctx->preprocess_out_pool->release_frame(frame);
  }
  return true;
}


bool create_metaconvert_context(AppContext *ctx) {
  /* Prepare meta convert context */
  /* Convert the inference results obtained after post-processing into a
    * structured overlay data format. This structured overlay data is then
    * utilized to draw the results onto an input image
    */
  string metaconvert_config =
      extract_component_json(ctx->json_str, "metaconvert-config");
  AppLogLevel log_level = ctx->log_level;
  if (metaconvert_config.empty()) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                "Failed to parse metaconvert config");
    return false;
  } else {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "metaconvert Config: \n\n\n%s",
                metaconvert_config.c_str());
  }
  vart::InferResultType infer_result_type = {};
  if (ctx->use_vvas_pp_api) {
    /* resnet50 -> classification, yolox -> detection (app-side vvas C-API path). */
    infer_result_type =
        (ctx->vvas_pp_result_kind == xplusml::VvasPPResultKind::CLASSIFICATION)
            ? vart::InferResultType::CLASSIFICATION
            : vart::InferResultType::DETECTION;
  } else if (ctx->postprocess_type == vart::PostProcessType::YOLOV2)
    infer_result_type = vart::InferResultType::DETECTION;
  else if (ctx->postprocess_type == vart::PostProcessType::SSDRESNET34)
    infer_result_type = vart::InferResultType::DETECTION;
  else {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Unsupported post process type");
    throw;
  }
  ctx->meta_convert = new vart::MetaConvert(infer_result_type, metaconvert_config, ctx->device);
  if (!ctx->meta_convert) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Unable to create meta convert context");
    throw;
  }

  ctx->overlay = new vart::Overlay(DEFAULT_OVERLAY_TYPE, ctx->device);
  if (!ctx->overlay) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Unable to create overlay context");
    throw;
  }
  return true;
}

bool create_all_context(AppContext *ctx) {
  AppLogLevel log_level = ctx->log_level;

  try {

    /* Device is required for all Vart APIs, this load xclbin of device only if
     * not already loaded */
    ctx->device =
        vart::Device::get_device_hdl(ctx->device_idx, ctx->xclbin_location);
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
                "Device::get_device_hdl returned device=%p", static_cast<void*>(ctx->device.get()));
    if (ctx->device == nullptr) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Failed to get device handle");
      throw std::runtime_error("get_device_hdl returned nullptr");
    }

    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Creating inference context");
    /* Prepare infer context and configuration */
    if (!create_inference_context(ctx)) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Failed to create infer context");
      throw std::runtime_error("create_inference_context failed");
    }

    if (ctx->preprocess_enable) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Creating preprocess context");
      if (!create_preprocess_context(ctx)) {
        APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Unable to create pre-process context");
        throw std::runtime_error("create_preprocess_context failed");
      }
    }
    
    /* Create input pool which will be given as input to either preprocess of npu based on preprocess_enable flag */
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Creating input buffer pool");
    if( !create_input_buffer_pool(ctx)) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Unable to create input buffer pool");
      throw std::runtime_error("create_input_buffer_pool failed");
    }

    /* Create output pool for pre-process which will be the input to npu */
    if (ctx->preprocess_enable) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Creating preprocess output pool");
      if (!create_preprocess_output_pool(ctx)) {
        APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Unable to create pre-process output pool");
        throw std::runtime_error("create_preprocess_output_pool failed");
      }
    }

    if (ctx->postprocess_enable) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Creating postprocess context");
      if (!create_postprocess_context(ctx)) {
        APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Unable to create post-process context");
        throw std::runtime_error("create_postprocess_context failed");
      }
    }

    if (ctx->metaconvert_enable) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Creating metaconvert context");
      if (!create_metaconvert_context(ctx)) {
        APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Unable to create meta convert context");
        throw std::runtime_error("create_metaconvert_context failed");
      }
    }
    return true;
  } catch (const std::exception &e) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Exception caught in create_all_context: %s", e.what());
    destroy_all_context(ctx);
    return false;
  } catch (...) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Unknown exception in create_all_context");
    destroy_all_context(ctx);
    return false;
  }
}

/* Reset all components in the AppContext structure */
void destroy_all_context(AppContext *ctx) {
  /* Release and delete video capture object if it exists */
  if (ctx->vid_capture != nullptr) {
    if (ctx->vid_capture->isOpened())
      ctx->vid_capture->release();
    delete ctx->vid_capture;
  }
#ifdef DUMP_INPUTS
  /* Reset the string objects to empty strings */
  ctx->dump_input_path.clear();
  ctx->dump_infer_input_path.clear();
#endif
  ctx->out_file_path.clear();
  /* Free memory and reset pointers */
  if (ctx->pre_process) {
    delete (ctx->pre_process);
    ctx->pre_process = NULL;
  }

  if (ctx->in_pool) {
    delete (ctx->in_pool);
    ctx->in_pool = NULL;
  }

  if (ctx->preprocess_enable) {
    if (ctx->preprocess_out_pool) {
      delete (ctx->preprocess_out_pool);
      ctx->preprocess_out_pool = NULL;
    }
  }

  if (ctx->vvas_pp_bridge) {
    delete (ctx->vvas_pp_bridge);
    ctx->vvas_pp_bridge = NULL;
  }

  if (ctx->post_process) {
    ctx->post_process = NULL;
  }

  if (ctx->meta_convert) {
    delete (ctx->meta_convert);
    ctx->meta_convert = NULL;
  }

  if (ctx->overlay) {
    delete (ctx->overlay);
    ctx->overlay = NULL;
  }

  if (ctx->runner) {
    /* TODO ctx->runner.reset(); */
    // APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, ctx->log_level,
    //             "Resetting runner in destroy_all_context");
    // ctx->runner.reset();
  }

  if (ctx->device) {
    ctx->device.reset();
  }

  /* Reset inference information */
  reset_infer_info(&ctx->model_info);
}
