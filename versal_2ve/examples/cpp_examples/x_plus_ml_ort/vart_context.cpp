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
 * @file vart_context.cpp
 * @brief This file has the methods for creating VART modules context required
 * for the x_plus_ml application.
 *
 */

#include "x_plus_ml_app.hpp"

using namespace vart;

/**
 * @brief Get the size of a video frame based on its format, width, and height.
 * @param fmt The format of the video frame (e.g., BGR, RGB, Y_UV8_420).
 * @param width The width of the video frame.
 * @param height The height of the video frame.
 * @return The size of the video frame in bytes.
 */
static size_t get_video_frame_size(vart::VideoFormat fmt, size_t width, size_t height) {
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
    case vart::VideoFormat::RGBP_FP16:
    case vart::VideoFormat::RGBP_BF16:
      size = (width * height) * 3 * 2;
      break;
    default:
      size = 0;
      break;
  }
  return size;
}

/**
 * @brief Convert AppVideoInputFormat to vart::VideoFormat
 * @param app_fmt The AppVideoInputFormat to convert
 * @return The corresponding vart::VideoFormat
 */
static vart::VideoFormat get_video_frame_format(AppVideoInputFormat app_fmt) {
  vart::VideoFormat fmt;
  switch (app_fmt) {
    /* For jpeg is returning bgr for now */
    case APP_VIDEO_INPUT_FORMAT_JPEG:
    case APP_VIDEO_INPUT_FORMAT_BGR:
      fmt = vart::VideoFormat::BGR;
      break;
    case APP_VIDEO_INPUT_FORMAT_NV12:
      fmt = vart::VideoFormat::Y_UV8_420;
      break;
    default:
      fmt = vart::VideoFormat::UNKNOWN;
      break;
  }
  return fmt;
}

/**
 * @brief
 * Reset the fields of an InferModelConf structure to their default values.
 * @param model_info Pointer to the InferModelConf structure to reset.
 */
static void reset_infer_info(InferModelConf* model_info) {
  model_info->model_width = 0;
  model_info->model_height = 0;
  model_info->batch_size = 0;
  model_info->num_in_tensors = 0;
  model_info->num_out_tensors = 0;
}

/**
 * @brief Initialize a single pipeline context
 * @param pipeline_ctx Pointer to the PipelineContext structure to initialize
 * @param pipeline_id Pipeline identifier (0, 1, 2, etc.)
 */
void init_pipeline_context(PipelineContext* pipeline_ctx, int pipeline_id) {
  /* Initialize pipeline identifier */
  pipeline_ctx->pipeline_id = pipeline_id;

  /* Clear string paths */
  pipeline_ctx->model_path.clear();
  pipeline_ctx->pipeline_config_json.clear();
  pipeline_ctx->input_file_path.clear();
  pipeline_ctx->out_file_path.clear();
  pipeline_ctx->output_dir_path = DEFAULT_OUTPUT_DIR;

  pipeline_ctx->frames_processed = 0;

  /* Initialize file streams */
  pipeline_ctx->input_file = ifstream();
  pipeline_ctx->output_file = ofstream();

  /* Initialize input format and dimensions */
  // pipeline_ctx->input_fmt =
  // AppVideoInputFormat::APP_VIDEO_INPUT_FORMAT_UNKNOWN;
  pipeline_ctx->input_height = 0;
  pipeline_ctx->input_width = 0;

  /* By default set panscan cropping to false */
  pipeline_ctx->do_pan_scan = false;

#ifdef DUMP_INPUTS
  /* Initialize debug paths */
  pipeline_ctx->dump_input_path.clear();
  pipeline_ctx->dump_infer_input_path.clear();
  pipeline_ctx->dump_input_fp = ofstream();
  pipeline_ctx->dump_infer_input_fp = ofstream();
#endif

  /* Initialize pre-process context */
  pipeline_ctx->preprocess_enable = false;
  pipeline_ctx->pre_process = nullptr;
  memset(&pipeline_ctx->preprocess_info, 0, sizeof(pipeline_ctx->preprocess_info));
  pipeline_ctx->ppe_mbank_in = 0;
  pipeline_ctx->ppe_mbank_out = 0;
  pipeline_ctx->quant_scale_factor = 1.0f;
  pipeline_ctx->quant_scale_factor_conf_set = false;

  /* Initialize pools */
  pipeline_ctx->in_pool = nullptr;
  pipeline_ctx->preprocess_out_pool = nullptr;

  /* Initialize post-process context */
  pipeline_ctx->postprocess_enable = false;
  pipeline_ctx->post_process = nullptr;
  // pipeline_ctx->postprocess_type = vart::PostProcessType::UNKNOWN;

  /* Initialize model info */
  reset_infer_info(&pipeline_ctx->model_info);

  pipeline_ctx->tensor_mapping.clear();
  pipeline_ctx->dump_pl_results = true;
  pipeline_ctx->dump_all_inputs = false;

  /* Initialize meta convert context */
  pipeline_ctx->metaconvert_enable = false;
  pipeline_ctx->meta_convert = nullptr;

  /* Initialize overlay context */
  pipeline_ctx->overlay = nullptr;

  /* Initialize benchmarking statistics */
  pipeline_ctx->total_preprocess_time = 0.0f;
  pipeline_ctx->total_infer_time = 0.0f;
  pipeline_ctx->total_postprocess_time = 0.0f;
  pipeline_ctx->total_overlay_time = 0.0f;
}

void init_app_context(AppContext* ctx) {
  /* Initialize handle parameters */
  ctx->log_level = AppLogLevel::WARNING;

  ctx->num_frame_to_process = APP_PROCESS_ALL_FRAMES;

  /* File paths and formats */
  ctx->input_file_path.clear();
  ctx->config_json_path.clear();
  ctx->xclbin_location.clear();
  ctx->json_str.clear();

  /* Device context */
  ctx->device = nullptr;
  ctx->device_idx = DEFAULT_DEVICE_INDEX; /* Will be updated from JSON parsing */

  /* Pipeline management */
  ctx->num_active_pipelines = 0;
  ctx->num_models = 0; /* Will be set dynamically from JSON */
  ctx->dump_all_inputs = false;

  /* Iteration control */
  ctx->max_iterations = 1;
  ctx->iteration_counter = 0;
  ctx->is_benchmark_enabled = false;
  ctx->total_time = 0.0f;

  /* Initialize empty pipelines vector - will be resized when JSON is parsed */
  ctx->pipelines.clear();
}

/**
 * @brief Convert a vector of uint32_t to a string representation.
 * @param vec The vector of uint32_t to convert.
 * @return A string representation of the vector in the format
 */
string vector_to_string(const vector<uint32_t>& vec) {
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

/**
 * @brief Create output tensor memory for NPU and PL kernels based on the
 * pipeline context.
 * @param pipeline_ctx Pointer to the PipelineContext structure containing
 * configuration information.
 * @param log_level Application log level
 * @param device Device handle
 * @param npu_out_tensors_memory Reference to a vector of vectors to store the
 * NPU output tensor memory.
 * @param pl_out_tensors_memory Reference to a vector of vectors to store the
 * PL output tensor memory.
 * @return true if the memory creation is successful, false otherwise.
 */
bool create_out_tensor_memory(PipelineContext* pipeline_ctx,
                              AppLogLevel log_level,
                              shared_ptr<vart::Device> device,
                              vector<vector<shared_ptr<vart::Memory>>>& npu_out_tensors_memory) {
  int mem_index = 0;

  npu_out_tensors_memory.resize(pipeline_ctx->model_info.batch_size);
  for (unsigned int j = 0u; j < pipeline_ctx->model_info.batch_size; j++) {
    APP_LOG(AppLogLevel::DEBUG, log_level, "Processing batch %u for NPU out tensors", j);
    for (unsigned int i = 0u; i < pipeline_ctx->model_info.num_out_tensors; ++i) {
      mem_index = DEFAULT_FRAME_MEMBANK;

      npu_out_tensors_memory[j].push_back(make_shared<vart::Memory>(
          vart::MemoryImplType::NON_CMA, pipeline_ctx->model_info.out_tensors_info[i].meta.size_in_bytes, mem_index,
          device));

      APP_LOG(AppLogLevel::DEBUG, log_level, "Creating NPU out tensor memory with shape: %s size %ld on mem %d",
              vector_to_string(pipeline_ctx->model_info.out_tensors_info[i].meta.shape).c_str(),
              pipeline_ctx->model_info.out_tensors_info[i].meta.size_in_bytes, mem_index);
    }
  }

  return true;
}

/**
 * @brief Create and initialize all necessary contexts for a pipeline.
 * This includes inference, pre-process, post-process contexts.
 * @param pipeline_ctx Pointer to the PipelineContext structure for the specific
 * pipeline
 * @param log_level Application log level
 * @param device Device handle
 * @param preprocess_enable Whether preprocessing is enabled
 * @param postprocess_enable Whether postprocessing is enabled
 * @param metaconvert_enable Whether metaconvert is enabled
 * @param postprocess_type Post-processing type
 * @param json_str JSON configuration string
 * @param ppe_mbank_in Input memory bank for preprocessing
 * @param ppe_mbank_out Output memory bank for preprocessing
 * @param meta_convert Pointer to store meta convert context (output)
 * @param overlay Pointer to store overlay context (output)
 * @return true if all contexts are created successfully, false otherwise.
 */
bool create_all_context(PipelineContext* pipeline_ctx,
                        AppLogLevel log_level,
                        shared_ptr<vart::Device> device,
                        const string& json_str,
                        Ort::Env* ort_env) {
  try {
    /* Prepare infer context and configuration */
    if (!create_inference_context(pipeline_ctx, log_level, json_str, ort_env)) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Failed to create infer context");
      throw std::runtime_error("Failed to create infer context");
    }

    if (pipeline_ctx->preprocess_enable) {
      if (!create_preprocess_context(pipeline_ctx, log_level, json_str, device)) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Unable to create pre-process context");
        throw std::runtime_error("Unable to create pre-process context");
      }
    }

    /* Create input and output pool for pre-process */
    /* Here we already know input resolution and model required resolution */
    /* Get input vinfo */
    vart::VideoInfo in_vinfo;
    memset(&in_vinfo, 0, sizeof(in_vinfo));
    vart::VideoFormat fmt = get_video_frame_format(pipeline_ctx->input_fmt);
    size_t buf_size = get_video_frame_size(fmt, pipeline_ctx->input_width, pipeline_ctx->input_height);
    if (pipeline_ctx->preprocess_enable) {
      pipeline_ctx->pre_process->get_input_vinfo(pipeline_ctx->input_height, pipeline_ctx->input_width, fmt, in_vinfo);
    } else {
      in_vinfo.height = pipeline_ctx->input_height;
      in_vinfo.width = pipeline_ctx->input_width;
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

    pipeline_ctx->in_pool = new VideoFramePool(pipeline_ctx->model_info.batch_size, DEFAULT_PREPROCESS_POOL_TYPE,
                                               buf_size, pipeline_ctx->ppe_mbank_in, in_vinfo, device);
    if (pipeline_ctx->in_pool == nullptr) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Unable to create pre-process input frame pool");
      throw std::runtime_error("Unable to create pre-process input frame pool");
    }

    if (pipeline_ctx->preprocess_enable) {
      /* Get output vinfo */
      buf_size = 0;
      vart::VideoInfo out_vinfo;
      out_vinfo = pipeline_ctx->pre_process->get_output_vinfo();
      /* Inference support only BGR/RGB as input
       * TODO infer provide size as per model requirement but actually it works
       * on RGBA/BGRA, for zero copy, how we will get the RGBA/BGRA size along
       * with padding required by infer HW */
      buf_size = get_video_frame_size(out_vinfo.fmt, pipeline_ctx->model_info.model_width,
                                      pipeline_ctx->model_info.model_height);

      /* Create output pool */
      /* The number of buffers in the pool should be equal to or greater than
       * the model's batch size. Setting it equal to the batch size is
       * sufficient because the application runs in a single thread, and the
       * buffers are freed before the next iteration of the loop. */
      pipeline_ctx->preprocess_out_pool =
          new VideoFramePool(pipeline_ctx->model_info.batch_size, DEFAULT_PREPROCESS_POOL_TYPE, buf_size,
                             pipeline_ctx->ppe_mbank_out, out_vinfo, device);
      if (pipeline_ctx->preprocess_out_pool == nullptr) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Unable to create pre-process output frame pool");
        throw std::runtime_error("Unable to create pre-process output frame pool");
      }
    }

    if (pipeline_ctx->postprocess_enable) {
      if (!create_postprocess_context(pipeline_ctx, log_level, pipeline_ctx->postprocess_type, json_str, device)) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Unable to create post-process context");
        throw std::runtime_error("Unable to create post-process context");
      }
    }

    if (pipeline_ctx->metaconvert_enable) {
      /* Prepare meta convert context */
      /* Convert the inference results obtained after post-processing into a
       * structured overlay data format. This structured overlay data is then
       * utilized to draw the results onto an input image
       */
      string metaconvert_config = extract_component_json(json_str, "metaconvert-config");
      if (metaconvert_config.empty()) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Failed to parse metaconvert config");
        throw std::runtime_error("Failed to parse metaconvert config");
      } else {
        APP_LOG(AppLogLevel::DEBUG, log_level, "metaconvert Config: \n\n\n%s", metaconvert_config.c_str());
      }
      vart::InferResultType infer_result_type = {};

      switch (pipeline_ctx->postprocess_type) {
        case vart::PostProcessType::RESNET50:
          infer_result_type = vart::InferResultType::CLASSIFICATION;
          break;
        case vart::PostProcessType::YOLOV2:
        case vart::PostProcessType::SSDRESNET34:
          infer_result_type = vart::InferResultType::DETECTION;
          break;
        case vart::PostProcessType::SOFTMAX:
        case vart::PostProcessType::TOPK:
        case vart::PostProcessType::ARGMAX:
        case vart::PostProcessType::THRESHOLD:
        case vart::PostProcessType::LABEL_MAPPING:
        case vart::PostProcessType::NORMALIZATION:
        case vart::PostProcessType::CALIBRATION_TEMPERATURE:
        case vart::PostProcessType::CALIBRATION_PLATT:
        case vart::PostProcessType::BIAS_CORRECTION:
        case vart::PostProcessType::OUTLIER_DETECTION:
        case vart::PostProcessType::UNCERTAINTY_ESTIMATION:
          infer_result_type = vart::InferResultType::CLASSIFICATION;
          APP_LOG(AppLogLevel::RESULT, log_level,
                  "==== Post Process : Classification (%s) =====", to_string(pipeline_ctx->postprocess_type));
          break;
        case vart::PostProcessType::NMS:
        case vart::PostProcessType::ANCHOR_ADJUSTMENT:
        case vart::PostProcessType::SOFT_NMS:
        case vart::PostProcessType::DISTANCE_IOU_NMS:
        case vart::PostProcessType::CLASSWISE_NMS:
        case vart::PostProcessType::OBJECT_COUNT:
          infer_result_type = vart::InferResultType::DETECTION;
          APP_LOG(AppLogLevel::RESULT, log_level,
                  "==== Post Process : Detection (%s) =====", to_string(pipeline_ctx->postprocess_type));
          break;
        case vart::PostProcessType::SOFTMAXSEG:
        case vart::PostProcessType::SIGMOIDSEG:
        case vart::PostProcessType::ARGMAXSEG:
          infer_result_type = vart::InferResultType::SEGMENTATION;
          APP_LOG(AppLogLevel::RESULT, log_level,
                  "==== Post Process : SEGMENTATION (%s) =====", to_string(pipeline_ctx->postprocess_type));
          break;
        default:
          APP_LOG(AppLogLevel::ERROR, log_level, "Unsupported post process type");
          throw std::runtime_error("Unsupported post process type");
      }
      pipeline_ctx->meta_convert = new vart::MetaConvert(infer_result_type, metaconvert_config, device);
      if (!pipeline_ctx->meta_convert) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Unable to create meta convert context");
        throw std::runtime_error("Unable to create meta convert context");
      }

      pipeline_ctx->overlay = new vart::Overlay(DEFAULT_OVERLAY_TYPE, std::move(device));
      if (!pipeline_ctx->overlay) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Unable to create overlay context");
        throw std::runtime_error("Unable to create overlay context");
      }
    }

    return true;
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Exception caught: %s", e.what());
    // Pipeline-specific resources should be cleaned up by caller if needed
    return false;
  }
}

/**
 * @brief Reset a single pipeline context and free its resources
 * @param pipeline_ctx Pointer to the PipelineContext structure to reset
 */
void destroy_pipeline_context(PipelineContext* pipeline_ctx) {
  /* Close file streams */
  if (pipeline_ctx->input_file.is_open()) {
    pipeline_ctx->input_file.close();
  }
  if (pipeline_ctx->output_file.is_open()) {
    pipeline_ctx->output_file.close();
  }

#ifdef DUMP_INPUTS
  /* Close debug file streams */
  if (pipeline_ctx->dump_input_fp.is_open()) {
    pipeline_ctx->dump_input_fp.close();
  }
  if (pipeline_ctx->dump_infer_input_fp.is_open()) {
    pipeline_ctx->dump_infer_input_fp.close();
  }
#endif

  /* Free memory and reset pointers */
  if (pipeline_ctx->pre_process) {
    delete pipeline_ctx->pre_process;
    pipeline_ctx->pre_process = nullptr;
  }

  if (pipeline_ctx->in_pool) {
    delete pipeline_ctx->in_pool;
    pipeline_ctx->in_pool = nullptr;
  }

  if (pipeline_ctx->preprocess_out_pool) {
    delete pipeline_ctx->preprocess_out_pool;
    pipeline_ctx->preprocess_out_pool = nullptr;
  }

  if (pipeline_ctx->post_process) {
    delete pipeline_ctx->post_process;
    pipeline_ctx->post_process = nullptr;
  }

  if (pipeline_ctx->meta_convert) {
    delete pipeline_ctx->meta_convert;
    pipeline_ctx->meta_convert = nullptr;
  }

  if (pipeline_ctx->overlay) {
    delete pipeline_ctx->overlay;
    pipeline_ctx->overlay = nullptr;
  }

  /* Clear ONNX tensors and vectors before deleting session */
  pipeline_ctx->model_info.input_tensors.clear();
  pipeline_ctx->model_info.output_tensors.clear();
  pipeline_ctx->model_info.input_names.clear();
  pipeline_ctx->model_info.output_names.clear();
  pipeline_ctx->model_info.in_tensors_info.clear();
  pipeline_ctx->model_info.out_tensors_info.clear();

  pipeline_ctx->ort_session.reset();
  /* Reset inference information */
  reset_infer_info(&pipeline_ctx->model_info);

  /* Clear string paths */
  pipeline_ctx->model_path.clear();
  pipeline_ctx->pipeline_config_json.clear();
  pipeline_ctx->input_file_path.clear();
  pipeline_ctx->out_file_path.clear();

#ifdef DUMP_INPUTS
  pipeline_ctx->dump_input_path.clear();
  pipeline_ctx->dump_infer_input_path.clear();
#endif

  /* Clear tensor mapping */
  pipeline_ctx->tensor_mapping.clear();

  /* Reset all boolean flags and counters */
  pipeline_ctx->preprocess_enable = false;
  pipeline_ctx->postprocess_enable = false;
  pipeline_ctx->metaconvert_enable = false;
  pipeline_ctx->dump_pl_results = false;
  pipeline_ctx->dump_all_inputs = false;

  /* Reset timing statistics */
  pipeline_ctx->total_preprocess_time = 0.0f;
  pipeline_ctx->total_infer_time = 0.0f;
  pipeline_ctx->total_postprocess_time = 0.0f;
  pipeline_ctx->total_overlay_time = 0.0f;
}

/**
 * @brief Reset all components in the AppContext structure
 * @param ctx Pointer to the AppContext structure to reset
 */
void destroy_all_context(AppContext* ctx) {
  /* Destroy all pipeline contexts */
  for (size_t i = 0; i < ctx->pipelines.size(); ++i) {
    destroy_pipeline_context(&ctx->pipelines[i]);
  }
  ctx->pipelines.clear();

  ctx->ort_env.reset();
  /* Reset shared device context */
  if (ctx->device) {
    ctx->device.reset();
    ctx->device = nullptr;
  }

  /* Clear application-level string paths */
  ctx->input_file_path.clear();
  ctx->config_json_path.clear();
  ctx->xclbin_location.clear();
  ctx->json_str.clear();

  /* Reset application-level counters and flags */
  ctx->num_active_pipelines = 0;
  ctx->max_iterations = 1;
  ctx->iteration_counter = 0;
  ctx->is_benchmark_enabled = false;
  ctx->total_time = 0.0f;
  ctx->dump_all_inputs = false;
}
