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

#include "SimpleUtilityTimer.hpp"
#include "x_plus_ml_app.hpp"

using namespace vart;

/**
 * @brief Map internal DataType enum to Vart TensorDataType enum.
 * @param d_type Internal DataType enum value.
 * @return Corresponding Vart TensorDataType enum value.
 */
TensorDataType map_data_type(DataType d_type) {
  switch (d_type) {
    case DataType::INT8:
      return TensorDataType::INT8;
    case DataType::FLOAT32:
      return TensorDataType::FLOAT32;
    case DataType::FP16:
      return TensorDataType::FP16;
    case DataType::BF16:
      return TensorDataType::BF16;
    default:
      return TensorDataType::UNKNOWN;
  }
}

/**
 * @brief Map AppVideoInputFormat enum to Vart VideoFormat enum.
 * @param fmt AppVideoInputFormat enum value.
 * @return Corresponding Vart VideoFormat enum value.
 */
static vart::VideoFormat get_vart_video_format(const string& fmt) {
  if (fmt == "RGBX") {
    return VideoFormat::RGBx;
  } else if (fmt == "BGRX") {
    return VideoFormat::BGRx;
  } else if (fmt == "RGB") {
    return VideoFormat::RGB;
  } else if (fmt == "BGR") {
    return VideoFormat::BGR;
  } else if (fmt == "Y_UV8_420") {
    return VideoFormat::Y_UV8_420;
  } else if (fmt == "RGBP") {
    return VideoFormat::RGBP;
  } else if (fmt == "BGR_FLOAT") {
    return VideoFormat::BGR_FLOAT;
  } else if (fmt == "RGB_FLOAT") {
    return VideoFormat::RGB_FLOAT;
  } else if (fmt == "RGBP_FLOAT") {
    return VideoFormat::RGBP_FLOAT;
  } else if ((fmt == "RGBx_BF16") || (fmt == "RGBX_BF16")) {
    return VideoFormat::RGBx_BF16;
  } else if (fmt == "RGB_FP16") {
    return VideoFormat::RGB_FP16;
  } else if (fmt == "BGR_FP16") {
    return VideoFormat::BGR_FP16;
  } else if (fmt == "RGB_BF16") {
    return VideoFormat::RGB_BF16;
  } else if (fmt == "BGR_BF16") {
    return VideoFormat::BGR_BF16;
  } else if (fmt == "RGBP_FP16") {
    return VideoFormat::RGBP_FP16;
  } else if (fmt == "RGBP_BF16") {
    return VideoFormat::RGBP_BF16;
  } else if (fmt == "BGRP") {
    return VideoFormat::BGRP;
  } else if (fmt == "BGRP_FP16") {
    return VideoFormat::BGRP_FP16;
  } else if (fmt == "BGRP_BF16") {
    return VideoFormat::BGRP_BF16;
  } else {
    return VideoFormat::UNKNOWN;
  }
}

/**
 * @brief Map Vart VideoFormat to AppVideoInputFormat in string format
 * @param app_fmt AppVideoInputFormat enum value.
 * @return Corresponding Vart VideoFormat enum value.
 * As JPEG and MP4 decoder return BGR output, the function return accordingly
 */
static string map_input_fmt_to_vart_fmt_string(AppVideoInputFormat app_fmt) {
  string fmt_str;
  switch (app_fmt) {
    case APP_VIDEO_INPUT_FORMAT_JPEG:
      fmt_str = "JPEG";
      break;
    case APP_VIDEO_INPUT_FORMAT_BGR:
      fmt_str = "BGR";
      break;
    case APP_VIDEO_INPUT_FORMAT_NV12:
      fmt_str = "NV12";
      break;
    default:
      fmt_str = "UNKNOWN";
      break;
  }
  return fmt_str;
}

/**
 * @brief Print help text for the command-line options
 * @param pn Program name
 */
static void print_help_text(char* pn) {
  std::cout << "Usage: " << pn << " [OPTIONS]" << std::endl;

  std::cout << "  --app-config\t\tConfig file path (mandatory)" << std::endl;
  std::cout << "  --input-file\t\tInput image file path (mandatory)" << std::endl;
  std::cout << "  --runs\t\tNumber of iterations app should run "
               "(optional, default "
               "is 1)"
            << std::endl;
  std::cout << "  --benchmark\t\tBenchmark the metrics of all components "
               "(optional, default is false)"
            << std::endl;
  std::cout << "  --log-level\t\tApplication log level to print logs "
               "(optional, default is WARNING)."
            << std::endl;
  std::cout << "\t\t\tAccepted log levels: 1 for ERROR, 2 for WARNING, 3 for "
               "INFERENCE RESULT, "
               "4 for FIXME, 5 for INFO, 6 for DEBUG."
            << std::endl;
  std::cout << "\t\t\tLogs at the provided level and all levels below will be "
               "printed."
            << std::endl;
  std::cout << "  --dim\t\t\tInput video dimensions in "
               "WIDTHxHEIGHT format, e.g., 1280x720 "
               "(mandatory for raw video input like NV12 and BGR)"
            << std::endl;
  std::cout << "  --frames\t\tNumber of frames to process (optional, default is all frames)" << std::endl;
  std::cout << "  --enable-timing-text-output\t\tEnable utiltimer instrumentation and print a human-readable "
               "per-component timing summary to the console after the run completes"
            << std::endl;
  std::cout << "  --enable-chrome-trace-json <file>\tEnable utiltimer instrumentation and write a Chrome trace JSON "
               "file to <file> after the run completes. View with chrome://tracing in Chrome browser"
            << std::endl;
  std::cout << "  --help\t\tPrint this help and exit" << std::endl;
}

/**
 * @brief Extract a specific component's JSON configuration from the overall
 * JSON string.
 * @param json_string The overall JSON configuration string.
 * @param component The component whose configuration needs to be extracted.
 * @return A string containing the JSON configuration for the specified
 * component, or an empty string if the component is not found or an error
 * occurs.
 */
string extract_component_json(const string& json_string, const string& component) {
  try {
    pt::ptree config;
    istringstream iss(json_string);
    pt::read_json(iss, config);
    pt::ptree specificConfig = config.get_child(component);
    ostringstream oss;
    pt::write_json(oss, specificConfig);
    return oss.str();
  } catch (const exception& e) {
    cerr << "Error parsing " << component << " config: " << e.what() << endl;
    return "{}";
  }
}

/**
 * @brief Map memory layout string to internal MemoryLayout enum.
 * @param layout Memory layout string.
 * @return Corresponding internal MemoryLayout enum value.
 */
static MemoryLayout map_memory_layout(const string& layout) {
  if (layout == "NHW") {
    return MemoryLayout::NHW;
  } else if (layout == "NHWC") {
    return MemoryLayout::NHWC;
  } else if (layout == "NCHW") {
    return MemoryLayout::NCHW;
  } else if (layout == "NHWC4") {
    return MemoryLayout::NHWC4;
  } else if (layout == "NC4HW4") {
    return MemoryLayout::NC4HW4;
  } else if (layout == "NC8HW8") {
    return MemoryLayout::NC8HW8;
  } else if (layout == "HCWNC4") {
    return MemoryLayout::HCWNC4;
  } else if (layout == "HCWNC8") {
    return MemoryLayout::HCWNC8;
  } else {
    return MemoryLayout::UNKNOWN;
  }
}

/**
 * @brief Parse JSON configuration for a specific pipeline.
 * @param ctx Pointer to the application context.
 * @param pipeline Pointer to the pipeline context.
 * @param pipeline_config The JSON configuration for this pipeline.
 * @return true if parsing is successful, false otherwise.
 */
static bool parse_pipeline_json_config(AppContext* ctx, PipelineContext* pipeline, const pt::ptree& pipeline_config) {
  PreProcessInfo* preprocess_info = &pipeline->preprocess_info;
  AppLogLevel log_level = ctx->log_level;

  try {
    /* Extract Pre-process info from "preprocess-config" */
    if (pipeline_config.get_child_optional("preprocess-config")) {
      pipeline->preprocess_enable = true;
      preprocess_info->mean_r = pipeline_config.get<float>("preprocess-config.mean-r");
      preprocess_info->mean_g = pipeline_config.get<float>("preprocess-config.mean-g");
      preprocess_info->mean_b = pipeline_config.get<float>("preprocess-config.mean-b");
      preprocess_info->scale_r = pipeline_config.get<float>("preprocess-config.scale-r");
      preprocess_info->scale_g = pipeline_config.get<float>("preprocess-config.scale-g");
      preprocess_info->scale_b = pipeline_config.get<float>("preprocess-config.scale-b");
      string preprocess_out_fmt_str = pipeline_config.get<string>("preprocess-config.colour-format");
      preprocess_info->colour_format = get_vart_video_format(preprocess_out_fmt_str);

      if (preprocess_info->colour_format == VideoFormat::UNKNOWN) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Unknown preprocessing Video Format: %s",
                preprocess_out_fmt_str.c_str());
        return false;
      }

      /* Get maintain_aspect_ratio value and perform corresponding resizing
       * technique based on the resizing-type value provided */
      bool maintain_aspect_ratio = pipeline_config.get<bool>("preprocess-config.maintain-aspect-ratio", false);
      if (maintain_aspect_ratio) {
        if (!pipeline_config.get_child("preprocess-config").count("resizing-type")) {
          APP_LOG(AppLogLevel::ERROR, log_level,
                  "Please provide resizing-type to maintain-aspect-ratio. "
                  "Valid values are LETTERBOX / PANSCAN");
          return false;
        }
        string resizing_type_str = pipeline_config.get<string>("preprocess-config.resizing-type");
        if (resizing_type_str.compare(0, 7, "PANSCAN") == 0) {
          preprocess_info->preprocess_type = PreProcessType::DEFAULT;
          pipeline->do_pan_scan = true;
        } else if (resizing_type_str.compare(0, 9, "LETTERBOX") == 0) {
          preprocess_info->preprocess_type = PreProcessType::LETTERBOX;
          preprocess_info->symmetric_padding = pipeline_config.get<bool>("preprocess-config.symmetric-padding", false);
        } else {
          APP_LOG(AppLogLevel::ERROR, log_level, "Unknown resizing-type: %s. Valid values are LETTERBOX / PANSCAN",
                  resizing_type_str.c_str());
          return false;
        }
      } else {
        /* Use default preprocess type if maintain-aspect-ratio is not provided
         */
        preprocess_info->preprocess_type = PreProcessType::DEFAULT;
      }

      /* Read the input and output memory bank indices for pre-processing module
       */
      pipeline->ppe_mbank_in = pipeline_config.get<uint8_t>("preprocess-config.in-mem-bank");
      pipeline->ppe_mbank_out = pipeline_config.get<uint8_t>("preprocess-config.out-mem-bank");
    }

    /* Extract Inference config */
    if (pipeline_config.get_child_optional("inference-config")) {
      pipeline->model_path = pipeline_config.get<std::string>("inference-config.model-file");
      if (pipeline->model_path.empty()) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Model file path is empty in inference-config for pipeline %d",
                pipeline->pipeline_id);
        return false;
      }
      try {
        auto inputs = pipeline_config.get_child("inference-config.inputs-config");
        pipeline->model_info.in_tensors_info.resize(inputs.size());
        size_t idx = 0;
        for (const auto& item : inputs) {
          string memory_layout = item.second.get<std::string>("memory-layout");
          APP_LOG(AppLogLevel::DEBUG, log_level, "memory-layout: %s", memory_layout.c_str());
          pipeline->model_info.in_tensors_info[idx].meta.memory_layout = map_memory_layout(memory_layout);
          if (pipeline->model_info.in_tensors_info[idx].meta.memory_layout == MemoryLayout::UNKNOWN) {
            APP_LOG(AppLogLevel::ERROR, log_level, "Unknown Memory Layout:  %s in inputs-config for pipeline %d",
                    memory_layout.c_str(), pipeline->pipeline_id);
            return false;
          }
          idx++;
        }
      } catch (const pt::ptree_error& e) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Could not get inputs-config from inference-config for pipeline %d",
                pipeline->pipeline_id);
        return false;
      }
    }

    /* Extract Post-process type from "postprocess-config" */
    if (pipeline_config.get_child_optional("postprocess-config")) {
      pipeline->postprocess_enable = true;
      string postprocess_type_str = pipeline_config.get<string>("postprocess-config.type");
      APP_LOG(AppLogLevel::DEBUG, log_level, "postprocess-config.type: %s for pipeline %d",
              postprocess_type_str.c_str(), pipeline->pipeline_id);

      /* Check if the postprocess_type_str is a supported type */
      if (postprocess_type_str == "RESNET50") {
        pipeline->postprocess_type = vart::PostProcessType::RESNET50;
      } else if (postprocess_type_str == "RESNET18") {
        pipeline->postprocess_type = vart::PostProcessType::RESNET50;  // RESNET18 uses same postprocess as RESNET50
      } else if (postprocess_type_str == "YOLOV2") {
        pipeline->postprocess_type = vart::PostProcessType::YOLOV2;
      } else if (postprocess_type_str == "SSDRESNET34") {
        pipeline->postprocess_type = vart::PostProcessType::SSDRESNET34;
      } else if (postprocess_type_str == "SOFTMAX") {
        pipeline->postprocess_type = vart::PostProcessType::SOFTMAX;
      } else if (postprocess_type_str == "TOPK") {
        pipeline->postprocess_type = vart::PostProcessType::TOPK;
      } else if (postprocess_type_str == "NMS") {
        pipeline->postprocess_type = vart::PostProcessType::NMS;
      } else if (postprocess_type_str == "ARGMAX") {
        pipeline->postprocess_type = vart::PostProcessType::ARGMAX;
      } else if (postprocess_type_str == "THRESHOLD") {
        pipeline->postprocess_type = vart::PostProcessType::THRESHOLD;
      } else if (postprocess_type_str == "LABEL_MAPPING") {
        pipeline->postprocess_type = vart::PostProcessType::LABEL_MAPPING;
      } else if (postprocess_type_str == "NORMALIZATION") {
        pipeline->postprocess_type = vart::PostProcessType::NORMALIZATION;
      } else if (postprocess_type_str == "ANCHOR_ADJUSTMENT") {
        pipeline->postprocess_type = vart::PostProcessType::ANCHOR_ADJUSTMENT;
      } else if (postprocess_type_str == "CALIBRATION_TEMPERATURE") {
        pipeline->postprocess_type = vart::PostProcessType::CALIBRATION_TEMPERATURE;
      } else if (postprocess_type_str == "CALIBRATION_PLATT") {
        pipeline->postprocess_type = vart::PostProcessType::CALIBRATION_PLATT;
      } else if (postprocess_type_str == "BIAS_CORRECTION") {
        pipeline->postprocess_type = vart::PostProcessType::BIAS_CORRECTION;
      } else if (postprocess_type_str == "OUTLIER_DETECTION") {
        pipeline->postprocess_type = vart::PostProcessType::OUTLIER_DETECTION;
      } else if (postprocess_type_str == "UNCERTAINTY_ESTIMATION") {
        pipeline->postprocess_type = vart::PostProcessType::UNCERTAINTY_ESTIMATION;
      } else if (postprocess_type_str == "SOFT_NMS") {
        pipeline->postprocess_type = vart::PostProcessType::SOFT_NMS;
      } else if (postprocess_type_str == "DISTANCE_IOU_NMS") {
        pipeline->postprocess_type = vart::PostProcessType::DISTANCE_IOU_NMS;
      } else if (postprocess_type_str == "CLASSWISE_NMS") {
        pipeline->postprocess_type = vart::PostProcessType::CLASSWISE_NMS;
      } else if (postprocess_type_str == "OBJECT_COUNT") {
        pipeline->postprocess_type = vart::PostProcessType::OBJECT_COUNT;
      } else if (postprocess_type_str == "SOFTMAXSEG") {
        pipeline->postprocess_type = vart::PostProcessType::SOFTMAXSEG;
      } else if (postprocess_type_str == "SIGMOIDSEG") {
        pipeline->postprocess_type = vart::PostProcessType::SIGMOIDSEG;
      } else if (postprocess_type_str == "ARGMAXSEG") {
        pipeline->postprocess_type = vart::PostProcessType::ARGMAXSEG;
      } else {
        APP_LOG(AppLogLevel::ERROR, log_level,
                "Invalid postprocess_type_str: %s for pipeline %d. Supported "
                "types: RESNET50, RESNET18",
                postprocess_type_str.c_str(), pipeline->pipeline_id);
        return false;
      }
      pipeline->postprocess_type_str = std::move(postprocess_type_str);
    }

    if (pipeline_config.get_child_optional("metaconvert-config"))
      pipeline->metaconvert_enable = true;

    /* Debug logging for configuration values */
    if (pipeline->preprocess_enable) {
      APP_LOG(AppLogLevel::DEBUG, log_level, "mean-r: %f", preprocess_info->mean_r);
      APP_LOG(AppLogLevel::DEBUG, log_level, "mean-g: %f", preprocess_info->mean_g);
      APP_LOG(AppLogLevel::DEBUG, log_level, "mean-b: %f", preprocess_info->mean_b);
      APP_LOG(AppLogLevel::DEBUG, log_level, "scale-r: %f", preprocess_info->scale_r);
      APP_LOG(AppLogLevel::DEBUG, log_level, "scale-g: %f", preprocess_info->scale_g);
      APP_LOG(AppLogLevel::DEBUG, log_level, "scale-b: %f", preprocess_info->scale_b);

      bool maintain_aspect_ratio =
          (preprocess_info->preprocess_type == PreProcessType::LETTERBOX || pipeline->do_pan_scan);
      APP_LOG(AppLogLevel::DEBUG, log_level, "maintain-aspect-ratio: %d", maintain_aspect_ratio);

      if (maintain_aspect_ratio) {
        string resizing_type_str = pipeline->do_pan_scan ? "PANSCAN" : "LETTERBOX";
        APP_LOG(AppLogLevel::DEBUG, log_level, "resizing-type: %s", resizing_type_str.c_str());
      }

      if (preprocess_info->preprocess_type == PreProcessType::LETTERBOX) {
        APP_LOG(AppLogLevel::DEBUG, log_level, "symmetric-padding: %d", preprocess_info->symmetric_padding);
      }
    }

    if (pipeline->postprocess_enable) {
      APP_LOG(AppLogLevel::DEBUG, log_level, "type: %s", pipeline->postprocess_type_str.c_str());
    }

  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Error reading pipeline %d config: Reason: %s", pipeline->pipeline_id,
            e.what());
    return false;
  }

  return true;
}

/**
 * @brief Load and parse a model-specific JSON configuration file.
 * @param config_file_path Path to the model-specific JSON file.
 * @param pipeline_config Reference to ptree to store the parsed configuration.
 * @param log_level Application log level.
 * @return true if parsing is successful, false otherwise.
 */
static bool load_model_config_file(const string& config_file_path, pt::ptree& pipeline_config, AppLogLevel log_level) {
  APP_LOG(AppLogLevel::DEBUG, log_level, "Loading model config file: %s", config_file_path.c_str());

  ifstream config_file(config_file_path);
  if (!config_file.is_open()) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Error opening model config file: %s", config_file_path.c_str());
    return false;
  }

  try {
    pt::read_json(config_file, pipeline_config);
    APP_LOG(AppLogLevel::DEBUG, log_level, "Successfully loaded model config: %s", config_file_path.c_str());
    return true;
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Error parsing model config file %s: %s", config_file_path.c_str(),
            e.what());
    return false;
  }
}

/**
 * @brief Parses the JSON configuration file and populates the application
 * context and pipeline contexts.
 * @param ctx Pointer to the application context to populate.
 * @return true if parsing is successful, false otherwise.
 */
static bool parse_json_config(AppContext* ctx) {
  if (!ctx || ctx->config_json_path.empty())
    return false;

  AppLogLevel log_level = ctx->log_level;
  APP_LOG(AppLogLevel::DEBUG, log_level, "Parsing configuration json file: %s", ctx->config_json_path.c_str());

  /* Read the top-level JSON configuration file */
  ifstream fileStream(ctx->config_json_path);
  if (!fileStream.is_open()) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Error opening top-level config file: %s", ctx->config_json_path.c_str());
    return false;
  }

  /* Read the entire file into ctx->json_str for global access */
  ctx->json_str = string((istreambuf_iterator<char>(fileStream)), istreambuf_iterator<char>());
  fileStream.close();

  try {
    pt::ptree top_level_config;
    istringstream iss(ctx->json_str);
    pt::read_json(iss, top_level_config);

    /* Extract global application-level config */
    ctx->xclbin_location = top_level_config.get<string>("xclbin-location");
    APP_LOG(AppLogLevel::DEBUG, log_level, "xclbin-location: %s", ctx->xclbin_location.c_str());

    ctx->device_idx = top_level_config.get<int>("device-index", DEFAULT_DEVICE_INDEX);
    APP_LOG(AppLogLevel::DEBUG, log_level, "device-index: %d", ctx->device_idx);

    /* Parse models configuration */
    ctx->num_active_pipelines = 0;

    if (top_level_config.get_child_optional("models-config")) {
      /* New structure: models-config array with config-path references */
      auto models_config = top_level_config.get_child("models-config");

      /* Count models and resize pipelines vector */
      ctx->num_models = models_config.size();
      ctx->pipelines.resize(ctx->num_models);

      /* Initialize all pipeline contexts */
      for (int i = 0; i < ctx->num_models; ++i) {
        init_pipeline_context(&ctx->pipelines[i], i);
      }

      int pipeline_idx = 0;
      for (const auto& model_entry : models_config) {
        /* Get the config file path for this model */
        string model_config_path = model_entry.second.get<string>("config-path");
        APP_LOG(AppLogLevel::DEBUG, log_level, "Loading config for pipeline %d from: %s", pipeline_idx,
                model_config_path.c_str());

        /* Load the model-specific configuration file */
        pt::ptree pipeline_config;
        if (!load_model_config_file(model_config_path, pipeline_config, log_level)) {
          APP_LOG(AppLogLevel::ERROR, log_level, "Failed to load config for pipeline %d", pipeline_idx);
          return false;
        }

        /* Parse the model configuration and populate pipeline context */
        ctx->pipelines[pipeline_idx].pipeline_id = pipeline_idx;

        /* Store the individual model's JSON string for context creation */
        ostringstream model_json_stream;
        pt::write_json(model_json_stream, pipeline_config);
        ctx->pipelines[pipeline_idx].pipeline_config_json = model_json_stream.str();

        if (!parse_pipeline_json_config(ctx, &ctx->pipelines[pipeline_idx], pipeline_config)) {
          APP_LOG(AppLogLevel::ERROR, log_level, "Failed to parse pipeline %d config", pipeline_idx);
          return false;
        }

        pipeline_idx++;
        ctx->num_active_pipelines++;
      }
    }

    if (ctx->num_active_pipelines == 0) {
      APP_LOG(AppLogLevel::ERROR, log_level, "No valid pipelines configured");
      return false;
    }

    APP_LOG(AppLogLevel::INFO, log_level, "Successfully parsed %d pipeline(s)", ctx->num_active_pipelines);

    /* Log detailed configuration information for each pipeline */
    for (int pipeline_idx = 0; pipeline_idx < ctx->num_active_pipelines; pipeline_idx++) {
      PipelineContext* pipeline = &ctx->pipelines[pipeline_idx];
      APP_LOG(AppLogLevel::DEBUG, log_level, "=== Pipeline %d Configuration ===", pipeline_idx);

      if (pipeline->postprocess_enable) {
        APP_LOG(AppLogLevel::DEBUG, log_level, "postprocess type: %s", pipeline->postprocess_type_str.c_str());
      }
    }
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Error reading config: Reason: %s", e.what());
    return false;
  }
  return true;
}

/**
 * @brief Transforms the post-processed prediction back to the original
 * resolution of input frame
 * @param ctx Pointer to the application context.
 * @param pipeline Pointer to the pipeline context.
 * @param root_res Vector of shared pointers to the root inference results.
 * @return true if transformation is successful, false otherwise.
 */
bool transform_infer_result(AppContext* ctx,
                            PipelineContext* pipeline,
                            vector<shared_ptr<vart::InferResult>>& root_res) {
  const vector<shared_ptr<vart::InferResult>>& result = (root_res.back())->get_children();

  AppLogLevel log_level = ctx->log_level;
  InferResScaleInfo info = {};
  info.input_frame_width = pipeline->input_width;
  info.input_frame_height = pipeline->input_height;
  info.model_input_width = pipeline->model_info.model_width;
  info.model_input_height = pipeline->model_info.model_height;

  APP_LOG(AppLogLevel::INFO, log_level, "Results after transform:");
  for (auto& itr : result) {
    try {
      itr->transform(info);
      InferResultData* base_infer_result = itr->get_infer_result();
      if (base_infer_result->result_type == vart::InferResultType::DETECTION) {
        DetectionResData* infer_result = static_cast<DetectionResData*>(base_infer_result);
        APP_LOG(AppLogLevel::INFO, log_level,
                "Detection bbox  x : %u y : %u width  : %u height : %u and "
                "label : %s",
                infer_result->x, infer_result->y, infer_result->width, infer_result->height,
                infer_result->label.c_str());
      } else if (base_infer_result->result_type == vart::InferResultType::SEGMENTATION) {
        SegmentationResData* infer_result = static_cast<SegmentationResData*>(base_infer_result);
        for (size_t i = 0; i < infer_result->numOutputs; ++i) {
          APP_LOG(AppLogLevel::INFO, log_level,
                  "Segmentation map with width : %d height : %d and %zu pixels is created", infer_result->width[i],
                  infer_result->height[i], infer_result->segmentationMap[i]->size());
        }
      }
    } catch (const exception& e) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Error in transform function: %s", e.what());
      return false;
    }
  }
  return true;
}

/**
 * @brief Draw inference result on the input video buffer using overlay
 * @param ctx Pointer to the application context.
 * @param pipeline Pointer to the pipeline context.
 * @param root_result Pointer to the root inference result.
 * @param input_frame Pointer to the input video frame.
 * @return true if drawing is successful, false otherwise.
 */
bool draw_infer_result(AppContext* ctx,
                       PipelineContext* pipeline,
                       const shared_ptr<vart::InferResult>& root_result,
                       shared_ptr<vart::VideoFrame> input_frame) {
  AppLogLevel log_level = ctx->log_level;
  shared_ptr<OverlayShapeInfo> shape_info = {};

  APP_LOG(AppLogLevel::DEBUG, log_level, "Convert inference metadata to overlay metadata");
  try {
    shape_info = pipeline->meta_convert->prepare_overlay_meta(root_result);
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Error in prepare overlay metadata: %s", e.what());
    return false;
  }

  APP_LOG(AppLogLevel::DEBUG, log_level, "Draw overlay data on frame");
  try {
    pipeline->overlay->draw_overlay(*input_frame, *shape_info);
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Error in drawing overlay: %s", e.what());
    return false;
  }

  return true;
}

/**
 * @brief Reads and processes user inputs from command line arguments.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param ctx Pointer to the application context to populate.
 * @return 0 on success, -1 on failure.
 */
int read_user_inputs(int argc, char* argv[], AppContext* ctx) {
  opterr = 0;
  int opt;
  int option_index = 0;
  static struct option long_options[] = {

      {"help", no_argument, 0, 0},
      {"app-config", required_argument, 0, 1},
      {"runs", required_argument, 0, 2},
      {"log-level", required_argument, 0, 3},
      {"dump-all", no_argument, 0, 4},
      {"benchmark", no_argument, 0, 5},
      {"input-file", required_argument, 0, 6},
      {"dim", required_argument, 0, 7},
      {"frames", required_argument, 0, 8},
      {"enable-timing-text-output", no_argument, 0, 9},
      {"enable-chrome-trace-json", required_argument, 0, 10},
      {0, 0, 0, 0}};

  /* Parse command-line options */
  while ((opt = getopt_long(argc, argv, "", long_options, &option_index)) != -1) {
    switch (opt) {
      case 0:  // --help
        print_help_text(argv[0]);
        return -1;
      case 1:  // --config-file
        ctx->config_json_path = optarg;
        break;
      case 2:  // --runs
        ctx->max_iterations = atoi(optarg);
        if (ctx->max_iterations <= 0) {
          APP_LOG(AppLogLevel::ERROR, ctx->log_level,
                  "Invalid number of runs. Enter valid "
                  "positive number");
          return -1;
        }
        break;
      case 3:  // --log-level
        ctx->log_level = static_cast<AppLogLevel>(atoi(optarg));
        break;
      case 4:  // --dump-all
        ctx->dump_all_inputs = true;
        break;
      case 5:  // --benchmark
        ctx->is_benchmark_enabled = true;
        break;
      case 6:  // --input-file
        ctx->input_file_path = optarg;
        break;
      case 7:  // --dim
        if (sscanf(optarg, "%ux%u", &ctx->input_width, &ctx->input_height) != 2) {
          APP_LOG(AppLogLevel::ERROR, ctx->log_level, "Invalid input dimension size format. Use WIDTHxHEIGHT.");
        }
        break;
      case 8:  // --frames
        ctx->num_frame_to_process = atoi(optarg);
        if (ctx->num_frame_to_process <= 0) {
          APP_LOG(AppLogLevel::ERROR, ctx->log_level,
                  "Invalid number of frames to process. Enter valid "
                  "positive number");
          return -1;
        }
        break;
      case 9:  // --enable-timing-text-output
        ctx->timing_enable = true;
        ctx->text_output_enable = true;
        break;
      case 10:  // --enable-chrome-trace-json <filename>
        ctx->timing_enable = true;
        ctx->chrome_trace_json_enable = true;
        ctx->chrome_trace_json_file = optarg;
        break;
      case '?':  // Unknown option
        std::cerr << "Unknown option: " << argv[optind - 1] << std::endl;
        print_help_text(argv[0]);
        return -1;
      default:
        print_help_text(argv[0]);
        return -1;
    }
  }
  if (ctx->config_json_path.empty()) {
    APP_LOG(AppLogLevel::ERROR, ctx->log_level, "Invalid argument(s)");
    print_help_text(argv[0]);
    return -1;
  }

  if (ctx->input_file_path.empty()) {
    APP_LOG(AppLogLevel::ERROR, ctx->log_level, "Input file path is not provided");
    print_help_text(argv[0]);
    return -1;
  }

  return 0;
}

/**
 * @brief Main function for the application.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, non-zero on failure.
 */
int main(int argc, char* argv[]) {
  AppReadStatus read_status = APP_READ_SUCCESS;
  int64_t num_frame_processed = 0;
  int ret = 0;
  /* Declare timers*/
  std::chrono::high_resolution_clock::time_point start, end;

  /* Vector to hold output tensors for post-processing - one per pipeline */
  vector<vector<vector<shared_ptr<vart::Memory>>>> npu_out_tensors_memory;

  AppContext ctx = {};

  /* Initialize handle parameters */
  init_app_context(&ctx);

  /* Read user inputs */
  ret = read_user_inputs(argc, argv, &ctx);
  if (ret) {
    return ret;
  }

  /* Set log level based on handle configuration */
  AppLogLevel log_level = ctx.log_level;

  /* parse configurations from json file */
  if (!parse_json_config(&ctx)) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Json parsing failed");
    return -1;
  }

  /* Enable utiltimer if any timing output is requested */
  if (ctx.timing_enable) {
    utiltimer::enable(std::cout);
  }

  /* Resize tensor memory vectors based on actual number of models */
  npu_out_tensors_memory.resize(ctx.num_active_pipelines);

  /* Device is required for all Vart APIs, this load xclbin of device only if
   * not already loaded */
  ctx.device = vart::Device::get_device_hdl(ctx.device_idx, ctx.xclbin_location);
  if (ctx.device == nullptr) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Failed to get device handle");
    goto killall;
  }

  /* make onnx environment alive throughout the session */
  try {
    ctx.ort_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_ERROR, "x_plus_ml_ort");
  } catch (const Ort::Exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Failed to create ONNX Runtime environment: %s", e.what());
    goto killall;
  }

  /* Initialize all pipelines: setup files, contexts, and memory */
  for (int pipeline_idx = 0; pipeline_idx < ctx.num_active_pipelines; pipeline_idx++) {
    /* Assign input file paths and configuration */
    ctx.pipelines[pipeline_idx].input_file_path = ctx.input_file_path;
    ctx.pipelines[pipeline_idx].input_width = ctx.input_width;
    ctx.pipelines[pipeline_idx].input_height = ctx.input_height;
    ctx.pipelines[pipeline_idx].dump_all_inputs = ctx.dump_all_inputs;
    ctx.pipelines[pipeline_idx].is_benchmark_enabled = ctx.is_benchmark_enabled;

    /* Open input and output files */
    if (open_files(&ctx.pipelines[pipeline_idx], ctx.pipelines[pipeline_idx].output_dir_path, ctx.max_iterations,
                   ctx.iteration_counter, ctx.log_level, ctx.dump_all_inputs) != true) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Failed to open files for pipeline %d", pipeline_idx);
      goto killall;
    }

    /* Create application context for this pipeline */
    if (create_all_context(&ctx.pipelines[pipeline_idx], ctx.log_level, ctx.device,
                           ctx.pipelines[pipeline_idx].pipeline_config_json, ctx.ort_env.get()) != true) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Application context creation failed for pipeline %d", pipeline_idx);
      goto killall;
    }

    /* Create tensor memory for this pipeline */
    if (create_out_tensor_memory(&ctx.pipelines[pipeline_idx], log_level, ctx.device,
                                 npu_out_tensors_memory[pipeline_idx]) != true) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Unable to create tensor out memory for pipeline %d", pipeline_idx);
      goto killall;
    }
  }

  std::cout << " Running for " << ctx.max_iterations << " iterations " << std::endl;

  /* Print batch processing information for each pipeline */
  for (int pipeline_idx = 0; pipeline_idx < ctx.num_active_pipelines; pipeline_idx++) {
    int64_t model_batch_size = ctx.pipelines[pipeline_idx].model_info.batch_size;
    std::cout << "Pipeline " << pipeline_idx << " - Model supports batch size: " << model_batch_size << std::endl;
  }

  /* Main loop for video processing until end-of-file */
  while (read_status != APP_EOF) {
    /* Number of frames to read in the current iteration per pipeline */
    vector<uint32_t> frames_to_read_per_pipeline(ctx.num_active_pipelines, 0);
    vector<uint32_t> frame_read_per_pipeline(ctx.num_active_pipelines, 0);
    vector<vector<vector<shared_ptr<InferResult>>>> inference_results(ctx.num_active_pipelines);

    /* Maintain arrays of input frames per pipeline until overlaying predictions
     * on them and then dump the results into files. */
    vector<vector<shared_ptr<vart::VideoFrame>>> input_frames(ctx.num_active_pipelines);
    vector<vector<shared_ptr<vart::VideoFrame>>> preprocess_out_frames(ctx.num_active_pipelines);

    for (int i = 0; i < ctx.num_active_pipelines; ++i) {
      input_frames[i].clear();
      preprocess_out_frames[i].clear();
      inference_results[i].clear();
    }

    /* Calculate frames to read and allocate frame vectors for each pipeline */
    for (int pipeline_idx = 0; pipeline_idx < ctx.num_active_pipelines; pipeline_idx++) {
      /* Calculate frames to read based on batch sizes */
      if (ctx.num_frame_to_process != APP_PROCESS_ALL_FRAMES) {
        /* Calculate the remaining frames to read in this iteration, considering
         * the batch size and the frames already processed */
        int64_t remaining_frames_to_process =
            (ctx.pipelines[pipeline_idx].frames_processed >= ctx.num_frame_to_process)
                ? 0
                : (ctx.num_frame_to_process - ctx.pipelines[pipeline_idx].frames_processed);
        frames_to_read_per_pipeline[pipeline_idx] =
            (remaining_frames_to_process < ctx.pipelines[pipeline_idx].model_info.batch_size)
                ? static_cast<uint32_t>(remaining_frames_to_process)
                : ctx.pipelines[pipeline_idx].model_info.batch_size;
      } else {
        /* If processing all frames, set frames_to_read equal to the batch size */
        frames_to_read_per_pipeline[pipeline_idx] = ctx.pipelines[pipeline_idx].model_info.batch_size;
      }

      /* Print batch processing info for this iteration */
      if (ctx.iteration_counter == 0) {
        std::cout << "Pipeline " << pipeline_idx << " - Available images in input: ";
        if (ctx.num_frame_to_process != APP_PROCESS_ALL_FRAMES) {
          std::cout << ctx.num_frame_to_process << std::endl;
          int32_t calculated_iterations =
              static_cast<int32_t>((ctx.num_frame_to_process + ctx.pipelines[pipeline_idx].model_info.batch_size - 1) /
                                   ctx.pipelines[pipeline_idx].model_info.batch_size);
          std::cout << "Pipeline " << pipeline_idx << " - Number of iterations needed: " << calculated_iterations
                    << std::endl;
        } else {
          std::cout << "ALL (processing until EOF)" << std::endl;
        }
      }

      /* Allocate frame vectors for this pipeline */
      input_frames[pipeline_idx].resize(frames_to_read_per_pipeline[pipeline_idx]);
      preprocess_out_frames[pipeline_idx].resize(frames_to_read_per_pipeline[pipeline_idx]);
    }

    APP_LOG(AppLogLevel::DEBUG, log_level, "***** Start of new iteration *****");

    /* Process frames for each pipeline */
    for (int pipeline_idx = 0; pipeline_idx < ctx.num_active_pipelines; pipeline_idx++) {
      APP_LOG(AppLogLevel::DEBUG, log_level, "Processing pipeline %d: frames to read = %d", pipeline_idx,
              frames_to_read_per_pipeline[pipeline_idx]);

      /* Loop over the frames to be read for this pipeline's batch */
      for (uint32_t frm_idx = 0; frm_idx < frames_to_read_per_pipeline[pipeline_idx]; frm_idx++) {
        /* Acquire a buffer from the pre-process input pool */
        try {
          input_frames[pipeline_idx][frm_idx] = ctx.pipelines[pipeline_idx].in_pool->acquire_frame();
        } catch (const std::runtime_error& e) {
          const bool shutting_down = std::string(e.what()).find("shutting down") != std::string::npos;
          APP_LOG(AppLogLevel::ERROR, log_level, "%s while acquiring buffer from pre-process pool for pipeline %d: %s",
                  shutting_down ? "Pool shutdown" : "Timeout", pipeline_idx, e.what());
          goto killall;
        }
        APP_LOG(AppLogLevel::DEBUG, log_level, "Acquired buffer from input pool for pipeline %d, frame index %d",
                pipeline_idx, frm_idx);

        if (ctx.pipelines[pipeline_idx].preprocess_enable) {
          /* Acquire a frame from the pre-process output pool */
          try {
            preprocess_out_frames[pipeline_idx][frm_idx] =
                ctx.pipelines[pipeline_idx].preprocess_out_pool->acquire_frame();
          } catch (const std::runtime_error& e) {
            const bool shutting_down = std::string(e.what()).find("shutting down") != std::string::npos;
            APP_LOG(AppLogLevel::ERROR, log_level,
                    "%s while acquiring buffer from output pre-process pool for pipeline %d: %s",
                    shutting_down ? "Pool shutdown" : "Timeout", pipeline_idx, e.what());
            goto killall;
          }
          APP_LOG(AppLogLevel::DEBUG, log_level, "Acquired frame from output pool for pipeline %d", pipeline_idx);
        } else {
          /* As preprocess data is read from file, preprocess output frame is
           * same as of read input */
          preprocess_out_frames[pipeline_idx][frm_idx] = input_frames[pipeline_idx][frm_idx];
          APP_LOG(AppLogLevel::DEBUG, log_level, "Preprocess output frame is same as input for pipeline %d",
                  pipeline_idx);
        }

        /* Read input data directly to pre-process input buffer */
        read_status =
            read_input(&ctx.pipelines[pipeline_idx], ctx.log_level, input_frames[pipeline_idx][frm_idx].get());
        if (read_status == APP_READ_SUCCESS) {
#ifdef DUMP_INPUTS
          if (ctx.dump_all_inputs) {
            /* dump the input frame to pre-process */
            APP_LOG(AppLogLevel::DEBUG, log_level, "Dumping input for pre-process for pipeline %d", pipeline_idx);
            if (dump_video_frame(ctx.log_level, ctx.pipelines[pipeline_idx].dump_input_fp,
                                 input_frames[pipeline_idx][frm_idx]) != true) {
              input_frames[pipeline_idx][frm_idx].reset();
              preprocess_out_frames[pipeline_idx][frm_idx].reset();
              APP_LOG(AppLogLevel::ERROR, log_level, "Failed to dump input for pipeline %d", pipeline_idx);
              goto killall;
            }
          }
#endif
          if (ctx.pipelines[pipeline_idx].preprocess_enable) {
            utiltimer::start("main preprocess_process_frame pipeline_" + std::to_string(pipeline_idx));
            /* Perform the pre-process step */
            if (preprocess_process_frame(&ctx.pipelines[pipeline_idx], ctx.log_level,
                                         input_frames[pipeline_idx][frm_idx],
                                         preprocess_out_frames[pipeline_idx][frm_idx]) != true) {
              input_frames[pipeline_idx][frm_idx].reset();
              preprocess_out_frames[pipeline_idx][frm_idx].reset();
              goto killall;
            }
            utiltimer::stop("main preprocess_process_frame pipeline_" + std::to_string(pipeline_idx));
          }

#ifdef DUMP_INPUTS
          if (ctx.dump_all_inputs) {
            /* dump the pre-processed frame to a file */
            APP_LOG(AppLogLevel::DEBUG, log_level, "Dumping output of pre-process for pipeline %d", pipeline_idx);
            if (dump_video_frame(ctx.log_level, ctx.pipelines[pipeline_idx].dump_infer_input_fp,
                                 preprocess_out_frames[pipeline_idx][frm_idx]) != true) {
              APP_LOG(AppLogLevel::ERROR, log_level, "Failed to dump pre-process output for pipeline %d", pipeline_idx);
              input_frames[pipeline_idx][frm_idx].reset();
              preprocess_out_frames[pipeline_idx][frm_idx].reset();
              goto killall;
            }
          }
#endif
          /* Increment the counter for frames read if data read successfully */
          frame_read_per_pipeline[pipeline_idx]++;
          /*Single frame formats should be read only one frame*/
          if (ctx.pipelines[pipeline_idx].input_fmt == APP_VIDEO_INPUT_FORMAT_JPEG) {
            read_status = APP_EOF;
          }
        } else if (read_status == APP_READ_FAILED) {
          APP_LOG(AppLogLevel::WARNING, log_level, "Failed to read input data for pipeline %d", pipeline_idx);
          input_frames[pipeline_idx][frm_idx].reset();
          if (ctx.pipelines[pipeline_idx].preprocess_enable)
            preprocess_out_frames[pipeline_idx][frm_idx].reset();
          /* Set the read_status to APP_EOF and exit the loop */
          read_status = APP_EOF;
          break;
        }

        else if (read_status == APP_EOF) {
          APP_LOG(AppLogLevel::DEBUG, log_level, "Got APP_EOF for pipeline %d", pipeline_idx);

          input_frames[pipeline_idx][frm_idx].reset();
          if (ctx.pipelines[pipeline_idx].preprocess_enable)
            preprocess_out_frames[pipeline_idx][frm_idx].reset();
        }
      }  // end of frame loop for this pipeline

      /* Don't break out of pipeline loop here - let each pipeline process
       * independently */
    }  // end of pipeline loop

    /* Report frame counts for each pipeline */
    uint32_t total_frames_read = 0;
    for (int pipeline_idx = 0; pipeline_idx < ctx.num_active_pipelines; pipeline_idx++) {
      APP_LOG(AppLogLevel::DEBUG, log_level, "Pipeline %d: %d frames read from input file in this iteration",
              pipeline_idx, frame_read_per_pipeline[pipeline_idx]);
      total_frames_read += frame_read_per_pipeline[pipeline_idx];
    }

    /* Check if no frames were read, and exit the loop if so */
    if (total_frames_read <= 0 && read_status == APP_EOF) {
      std::cout << "Reached end of file for iteration " << ctx.iteration_counter + 1 << std::endl;
      goto next_iteration;
    }

    /* Perform inference, postprocess on the batch of frames for each pipeline
     */
    for (int pipeline_idx = 0; pipeline_idx < ctx.num_active_pipelines; pipeline_idx++) {
      if (frame_read_per_pipeline[pipeline_idx] > 0) {
        /* Step 1: Perform inference on the batch of frames for this pipeline */
        APP_LOG(AppLogLevel::DEBUG, log_level, "Do infer for %d frames in pipeline %d",
                frame_read_per_pipeline[pipeline_idx], pipeline_idx);

        bool inference_success = false;
        try {
          utiltimer::start("main infer_process_frames pipeline_" + std::to_string(pipeline_idx));
          inference_success = infer_process_frames(
              &ctx.pipelines[pipeline_idx], ctx.log_level, frame_read_per_pipeline[pipeline_idx],
              preprocess_out_frames[pipeline_idx], npu_out_tensors_memory[pipeline_idx],
              ctx.pipelines[pipeline_idx].frames_processed, ctx.iteration_counter, ctx.max_iterations);
          utiltimer::stop("main infer_process_frames pipeline_" + std::to_string(pipeline_idx));
        } catch (const std::exception& e) {
          APP_LOG(AppLogLevel::ERROR, log_level, "Exception during inference for pipeline %d: %s", pipeline_idx,
                  e.what());
          inference_success = false;
        }

        if (!inference_success) {
          APP_LOG(AppLogLevel::ERROR, log_level, "Failed to do inference for pipeline %d", pipeline_idx);
          for (uint32_t i = 0; i < frame_read_per_pipeline[pipeline_idx]; i++) {
            /* Release acquired pre-process input buffers on error */
            input_frames[pipeline_idx][i].reset();
            if (ctx.pipelines[pipeline_idx].preprocess_enable)
              preprocess_out_frames[pipeline_idx][i].reset();
          }
          goto killall;
        }

        /* Step 2: Perform postprocessing (if enabled) */
        if (ctx.pipelines[pipeline_idx].postprocess_enable) {
          /* Perform the post-processing step on the inference predictions
           * The full batch is processed once in this step. After this call,
           * inference_results holds the results for each frame in batch */
          APP_LOG(AppLogLevel::DEBUG, log_level, "Do post process for %d frames in pipeline %d",
                  frame_read_per_pipeline[pipeline_idx], pipeline_idx);
          utiltimer::start("main postprocess_process_frames pipeline_" + std::to_string(pipeline_idx));
          inference_results[pipeline_idx] = postprocess_process_frames(
              &ctx.pipelines[pipeline_idx], ctx.log_level, frame_read_per_pipeline[pipeline_idx],
              npu_out_tensors_memory[pipeline_idx], ctx.pipelines[pipeline_idx].frames_processed);
          utiltimer::stop("main postprocess_process_frames pipeline_" + std::to_string(pipeline_idx));
        }
      }
    }

    /* Process postprocessing results for each pipeline */
    for (int pipeline_idx = 0; pipeline_idx < ctx.num_active_pipelines; pipeline_idx++) {
      if (ctx.pipelines[pipeline_idx].postprocess_enable && frame_read_per_pipeline[pipeline_idx] > 0) {
        vector<shared_ptr<InferResult>> root_res;
        /* Perform operation per frame for this pipeline */
        for (uint32_t i = 0; i < frame_read_per_pipeline[pipeline_idx]; i++) {
          root_res.push_back(make_shared<InferResult>(InferResultType::ROOT));
          if (inference_results[pipeline_idx].size())
            (root_res.back())->add_children(inference_results[pipeline_idx][i]);
          else
            APP_LOG(AppLogLevel::WARNING, log_level, "No infer result for current frame in pipeline %d", pipeline_idx);
          /* The predictions need to scaled/transformed to match the original
           * input before drawing */
          utiltimer::start("main transform_infer_result pipeline_" + std::to_string(pipeline_idx));
          if (transform_infer_result(&ctx, &ctx.pipelines[pipeline_idx], root_res) != true) {
            APP_LOG(AppLogLevel::ERROR, log_level, "Failed to do transform for pipeline %d", pipeline_idx);
            for (uint32_t j = 0; j < frame_read_per_pipeline[pipeline_idx]; j++) {
              input_frames[pipeline_idx][j].reset();
              if (ctx.pipelines[pipeline_idx].preprocess_enable)
                preprocess_out_frames[pipeline_idx][j].reset();
            }
            goto killall;
          }
          utiltimer::stop("main transform_infer_result pipeline_" + std::to_string(pipeline_idx));

          if (!ctx.pipelines[pipeline_idx].out_file_path.empty() && ctx.pipelines[pipeline_idx].output_file.is_open()) {
            /* Draw predictions on the input frame */
            APP_LOG(AppLogLevel::DEBUG, log_level, "Draw Prediction for frame %d in pipeline %d", i, pipeline_idx);
            start = chrono::high_resolution_clock::now();
            utiltimer::start("main draw_infer_result pipeline_" + std::to_string(pipeline_idx));
            if (draw_infer_result(&ctx, &ctx.pipelines[pipeline_idx], root_res.back(), input_frames[pipeline_idx][i]) !=
                true) {
              APP_LOG(AppLogLevel::ERROR, log_level, "Failed to do drawing on input for pipeline %d", pipeline_idx);
              for (uint32_t j = 0; j < frame_read_per_pipeline[pipeline_idx]; j++) {
                input_frames[pipeline_idx][j].reset();
                if (ctx.pipelines[pipeline_idx].preprocess_enable)
                  preprocess_out_frames[pipeline_idx][j].reset();
              }
              goto killall;
            }
            utiltimer::stop("main draw_infer_result pipeline_" + std::to_string(pipeline_idx));
            end = chrono::high_resolution_clock::now();
            if (ctx.is_benchmark_enabled) {
              ctx.pipelines[pipeline_idx].total_overlay_time +=
                  chrono::duration_cast<chrono::microseconds>(end - start).count();
            }

            /*Dont dump if benchmark enabled*/
            if (!ctx.is_benchmark_enabled) {
              /* Dump the video frame with prediction drawing */
              APP_LOG(AppLogLevel::DEBUG, log_level, "Dump output frame %s",
                      ctx.pipelines[pipeline_idx].out_file_path.c_str());
              if (ctx.pipelines[pipeline_idx].input_fmt == APP_VIDEO_INPUT_FORMAT_JPEG) {
                /* For JPEG input, dump as image files */
                if (!dump_video_frame_as_jpeg(ctx.log_level, ctx.pipelines[pipeline_idx].output_file,
                                              input_frames[pipeline_idx][i])) {
                  APP_LOG(AppLogLevel::ERROR, log_level,
                          "Failed to dump output frame %d as jpeg image file in pipeline %d", i, pipeline_idx);
                  input_frames[pipeline_idx][i].reset();
                  preprocess_out_frames[pipeline_idx][i].reset();
                  goto killall;
                }
              } else {
                /* For other video formats, dump as video frames */
                if (!dump_video_frame(ctx.log_level, ctx.pipelines[pipeline_idx].output_file,
                                      input_frames[pipeline_idx][i])) {
                  APP_LOG(AppLogLevel::ERROR, log_level, "Failed to dump output frame %d to file in pipeline %d", i,
                          pipeline_idx);
                  input_frames[pipeline_idx][i].reset();
                  preprocess_out_frames[pipeline_idx][i].reset();
                  goto killall;
                }
              }
            }
          }
        }
      }
    }

    /* Update per-pipeline and global processed counters */
    for (int pipeline_idx = 0; pipeline_idx < ctx.num_active_pipelines; pipeline_idx++) {
      ctx.pipelines[pipeline_idx].frames_processed += frame_read_per_pipeline[pipeline_idx];
    }

    APP_LOG(AppLogLevel::INFO, log_level, "num_frame_processed %ld", num_frame_processed);

    /* If configured to stop after N frames PER PIPELINE, check each pipeline */
    if (ctx.num_frame_to_process != APP_PROCESS_ALL_FRAMES) {
      bool all_pipelines_done = true;
      for (int pipeline_idx = 0; pipeline_idx < ctx.num_active_pipelines; pipeline_idx++) {
        if (ctx.pipelines[pipeline_idx].frames_processed < ctx.num_frame_to_process) {
          all_pipelines_done = false;
          break;
        }
      }
      if (all_pipelines_done) {
        APP_LOG(AppLogLevel::INFO, log_level, "Required frames processed for all pipelines");
        read_status = APP_EOF;
      }
    }
    /* Clear inference results and release buffers for each pipeline */
    for (int pipeline_idx = 0; pipeline_idx < ctx.num_active_pipelines; pipeline_idx++) {
      /* Clear inference results */
      if (ctx.pipelines[pipeline_idx].postprocess_enable)
        inference_results[pipeline_idx].clear();

      /* Release buffers */
      for (uint32_t i = 0; i < frame_read_per_pipeline[pipeline_idx]; i++) {
        APP_LOG(AppLogLevel::DEBUG, log_level, "release in/out frames %d for pipeline %d", i, pipeline_idx);
        input_frames[pipeline_idx][i].reset();
        if (ctx.pipelines[pipeline_idx].preprocess_enable)
          preprocess_out_frames[pipeline_idx][i].reset();
      }
    }

  next_iteration:
    if (read_status == APP_EOF) {
      ctx.iteration_counter++;
      /* Reset frame counters for next iterartion */
      for (int pipeline_idx = 0; pipeline_idx < ctx.num_active_pipelines; pipeline_idx++) {
        /*close previous output files*/
        close_files(&ctx.pipelines[pipeline_idx], ctx.log_level);
        /* Update global processed counter */
        num_frame_processed += static_cast<int64_t>(ctx.pipelines[pipeline_idx].frames_processed);
        ctx.pipelines[pipeline_idx].frames_processed = 0;
        /* Re-open input and output files for next iteration */
        if (ctx.iteration_counter < ctx.max_iterations) {
          APP_LOG(AppLogLevel::INFO, log_level, "Re-opening files for pipeline %d for next iteration", pipeline_idx);
          open_files(&ctx.pipelines[pipeline_idx], ctx.pipelines[pipeline_idx].output_dir_path, ctx.max_iterations,
                     ctx.iteration_counter, ctx.log_level, ctx.dump_all_inputs);
        }
      }

      if (ctx.iteration_counter >= ctx.max_iterations) {
        /*Break the loop after reaching maximum iterations*/
        read_status = APP_EOF;
      } else {
        /* Continue for next iteration */
        read_status = APP_READ_SUCCESS;
      }
      if (ctx.max_iterations > 1) {
        cout << "Completed " << ctx.iteration_counter << "/" << ctx.max_iterations << " iteration(s)" << endl;
      }
    }

  }  // end of while loop

killall:
  // If utiltimer is enabled, write human readable results if enabled.
  if (ctx.text_output_enable) {
    std::cout << "UtilTimer Output" << std::endl;
    utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, std::cout);
  }

  // If utiltimer is enabled, write chrome trace results if enabled.
  if (ctx.chrome_trace_json_enable) {
    std::ofstream trace_file(ctx.chrome_trace_json_file);
    if (trace_file.is_open()) {
      utiltimer::print(utiltimer::TimerOutputFormat::CHROME_TRACE_JSON, trace_file);
      if (trace_file.fail()) {
        std::cerr << "Error: Failed to write trace data to " << ctx.chrome_trace_json_file << std::endl;
      } else {
        trace_file.close();
        std::cout << "Chrome trace saved to " << ctx.chrome_trace_json_file << std::endl;
        std::cout << "View it by opening chrome://tracing in Chrome browser and loading file" << std::endl;
      }
    } else {
      std::cerr << "Error: Could not open " << ctx.chrome_trace_json_file << " for writing (check permissions and path)"
                << std::endl;
    }
  }

  cout << "Total number of samples processed on all pipelines: " << num_frame_processed << endl;
  /* Sum of per-pipeline average frame latency (microseconds) for overall summary */
  float overall_time = 0;
  double sum_pipeline_fps = 0.0;
  uint32_t frames_processed_per_pipe = num_frame_processed / ctx.num_active_pipelines;
  cout << "Total number of samples processed on per pipeline: " << frames_processed_per_pipe << endl;

  /* Display output information per pipeline */
  for (int pipeline_idx = 0; pipeline_idx < ctx.num_active_pipelines; pipeline_idx++) {
    if (!ctx.is_benchmark_enabled && !ctx.pipelines[pipeline_idx].out_file_path.empty()) {
      cout << "Pipeline " << pipeline_idx << " outputs dumped with " << ctx.pipelines[pipeline_idx].input_width << "x"
           << ctx.pipelines[pipeline_idx].input_height << " resolution and "
           << map_input_fmt_to_vart_fmt_string(ctx.pipelines[pipeline_idx].input_fmt) << " format" << endl;
    }
  }

  if (ctx.is_benchmark_enabled && frames_processed_per_pipe) {
    cout << "----------------------------------------\n";
    cout << "Performance metrics per pipeline:\n";

    uint32_t num_inference_runs = (frames_processed_per_pipe + ctx.pipelines[0].model_info.batch_size - 1) /
                                  ctx.pipelines[0].model_info.batch_size;  // Assuming same batch size for all pipelines
    for (int pipeline_idx = 0; pipeline_idx < ctx.num_active_pipelines; pipeline_idx++) {
      cout << "Pipeline " << pipeline_idx << ":\n";

      if (ctx.pipelines[pipeline_idx].preprocess_enable) {
        cout << "  Average time for Pre-process : "
             << (ctx.pipelines[pipeline_idx].total_preprocess_time / 1000.0) / frames_processed_per_pipe << " ms\n";
      }
      cout << "  Average time for Inference : "
           << (ctx.pipelines[pipeline_idx].total_infer_time / 1000.0) / num_inference_runs << " ms\n";
      if (ctx.pipelines[pipeline_idx].postprocess_enable) {
        cout << "  Average time for Post-process : "
             << (ctx.pipelines[pipeline_idx].total_postprocess_time / 1000.0) / frames_processed_per_pipe << " ms\n";
      }
      if (!ctx.pipelines[pipeline_idx].out_file_path.empty()) {
        cout << "  Average time for Overlay : "
             << (ctx.pipelines[pipeline_idx].total_overlay_time / 1000.0) / frames_processed_per_pipe << " ms\n";
      }

      /* Amortize inference over output frames so totals/FPS are per frame when batch_size > 1.
       * (Inference line above stays ms/batch via num_inference_runs.) */
      ctx.pipelines[pipeline_idx].total_time =
          ctx.pipelines[pipeline_idx].total_preprocess_time / frames_processed_per_pipe +
          ctx.pipelines[pipeline_idx].total_infer_time / frames_processed_per_pipe +
          ctx.pipelines[pipeline_idx].total_postprocess_time / frames_processed_per_pipe +
          ctx.pipelines[pipeline_idx].total_overlay_time / frames_processed_per_pipe;

      const double pipeline_fps = 1000000.0 / static_cast<double>(ctx.pipelines[pipeline_idx].total_time);
      sum_pipeline_fps += pipeline_fps;

      cout << "  Average  total time for Pipeline " << pipeline_idx << ": "
           << (ctx.pipelines[pipeline_idx].total_time / 1000.0) << " ms\n";
      cout << "  Average  FPS for Pipeline " << pipeline_idx << ": " << pipeline_fps << " fps\n";
      overall_time = overall_time + ctx.pipelines[pipeline_idx].total_time;
    }
  }
  if (ctx.is_benchmark_enabled) {
    cout << "==========================================================" << endl;
    if (frames_processed_per_pipe != 0 && overall_time != 0) {
      const double avg_latency_ms = (overall_time / 1000.0) / static_cast<double>(ctx.num_active_pipelines);
      cout << "Average overall time for all the pipelines "
           << ": " << avg_latency_ms << " ms\n";
      cout << "Average  Overal FPS for all the pipelines "
           << ": " << sum_pipeline_fps << " fps\n";
    } else {
      if (frames_processed_per_pipe == 0) {
        cout << "Warning: frames_processed_per_pipe is zero. Skipping calculations.\n";
      }
      if (overall_time == 0) {
        cout << "Warning: overall_time is zero. Skipping calculations.\n";
      }
    }
  }
  if (ctx.log_level < AppLogLevel::RESULT)
    cout << "To view results on console, enable logs using the option "
            "--log-level "
         << static_cast<int>(AppLogLevel::RESULT) << endl;

  /* Log a warning and reset all resources */
  APP_LOG(AppLogLevel::INFO, log_level, "Resetting all");
  destroy_all_context(&ctx);
  for (int pipeline_idx = 0; pipeline_idx < ctx.num_active_pipelines; pipeline_idx++) {
    close_files(&ctx.pipelines[pipeline_idx], log_level);
  }
  return 0;
}
