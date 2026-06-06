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

#include <cmath>
#include <filesystem>
#include <iomanip>
#include "x_plus_ml_app.hpp"

namespace fs = std::filesystem;
using namespace vart;

/**
 * @brief Constructor for PipelineQueue with specified maximum size.
 * @param max_size Maximum number of elements the queue can hold.
 */
PipelineQueue::PipelineQueue(size_t max_size) : max_size_(max_size) {}

/**
 * @brief Pushes data to the pipeline queue with timeout.
 * @param data The PipelineData to push to the queue.
 * @return true if data was successfully pushed, false if timeout occurred.
 */
bool PipelineQueue::push(const PipelineData& data) {
  unique_lock<mutex> lock(mutex_);

  /* Wait if queue is full */
  if (condition_.wait_for(lock, PIPELINE_QUEUE_TIMEOUT,
                          [this] { return queue_.size() < max_size_ || finished_.load(); })) {
    if (!finished_.load()) {
      queue_.push(data);
      condition_.notify_one();
    }
    return true;
  } else {
    APP_LOG(AppLogLevel::ERROR, AppLogLevel::ERROR, "Pipeline queue timed out during push operation");
    return false;
  }
}

/**
 * @brief Pops data from the pipeline queue with timeout.
 * @param data Reference to PipelineData to store the popped data.
 * @return true if data was successfully popped, false if timeout occurred
 *         or queue is finished.
 */
bool PipelineQueue::pop(PipelineData& data) {
  unique_lock<mutex> lock(mutex_);

  /* Wait until queue has data or producer is finished */
  if (condition_.wait_for(lock, PIPELINE_QUEUE_TIMEOUT, [this] { return !queue_.empty() || finished_.load(); })) {
    if (queue_.empty()) {
      return false;  // No more data
    }

    data = queue_.front();
    queue_.pop();
    condition_.notify_one();  // Notify producer that space is available
    return true;
  } else {
    APP_LOG(AppLogLevel::ERROR, AppLogLevel::ERROR, "Pipeline queue timed out during pop operation");
    return false;
  }
}

/**
 * @brief Marks the queue as finished, signaling no more data will be produced.
 */
void PipelineQueue::finish() {
  lock_guard<mutex> lock(mutex_);
  finished_.store(true);
  condition_.notify_all();
}

/**
 * @brief Checks if the queue is finished.
 * @return true if the queue is finished, false otherwise.
 */
bool PipelineQueue::is_finished() {
  lock_guard<mutex> lock(mutex_);
  return finished_.load();
}

bool PipelineQueue::is_empty() {
  lock_guard<mutex> lock(mutex_);
  return queue_.empty();
}

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
  } else if (fmt == "RGBx_BF16") {
    return VideoFormat::RGBx_BF16;
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
    case APP_VIDEO_INPUT_FORMAT_MP4:
    case APP_VIDEO_INPUT_FORMAT_BGR:
      fmt_str = "BGR";
      break;
    case APP_VIDEO_INPUT_FORMAT_NV12:
      fmt_str = "NV12";
      break;
    case APP_VIDEO_INPUT_FORMAT_RGB:
      fmt_str = "RGB";
      break;
    case APP_VIDEO_INPUT_FORMAT_BGR_FLOAT:
      fmt_str = "BGR_FLOAT";
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
               "(optional, default is "
               "ERROR and WARNING)."
            << std::endl;
  std::cout << "\t\t\tAccepted log levels: 1 for ERROR, 2 for WARNING, 3 for "
               "INFERENCE RESULT, "
               "4 for FIXME, 5 for INFO, 6 for DEBUG."
            << std::endl;
  std::cout << "\t\t\tLogs at the provided level and all levels below will be "
               "printed."
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

    ctx->use_native_output_format = top_level_config.get<bool>("use-native-output-format", false);
    APP_LOG(AppLogLevel::DEBUG, log_level, "use-native-output-format: %d", ctx->use_native_output_format);

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
  info.model_input_width = pipeline->model_info.model_width;
  info.model_input_height = pipeline->model_info.model_height;
  info.input_frame_width = pipeline->input_width;
  info.input_frame_height = pipeline->input_height;

  APP_LOG(AppLogLevel::DEBUG, log_level,
          "Transform the post-processed prediction back to the original "
          "input frame");
  APP_LOG(AppLogLevel::DEBUG, log_level, "Width scaler factor %f",
          static_cast<float>(pipeline->input_width) / pipeline->model_info.model_width);
  APP_LOG(AppLogLevel::DEBUG, log_level, "Height scaler factor %f",
          static_cast<float>(pipeline->input_height) / pipeline->model_info.model_height);

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

      {"app-config", required_argument, 0, 0}, {"runs", required_argument, 0, 1},
      {"log-level", required_argument, 0, 2},  {"dump-all", no_argument, 0, 3},
      {"benchmark", no_argument, 0, 4},        {"help", no_argument, 0, 5},
      {"input-file", required_argument, 0, 6}, {0, 0, 0, 0}};

  /* Parse command-line options */
  while ((opt = getopt_long(argc, argv, "", long_options, &option_index)) != -1) {
    switch (opt) {
      case 0:  // --config-file
        ctx->config_json_path = optarg;
        break;
      case 1:  // --runs
        ctx->max_iterations = atoi(optarg);
        if (ctx->max_iterations <= 0) {
          APP_LOG(AppLogLevel::ERROR, ctx->log_level,
                  "Invalid number of runs. Enter valid "
                  "positive number");
          return -1;
        }
        break;
      case 2:  // --log-level
        ctx->log_level = static_cast<AppLogLevel>(atoi(optarg));
        break;
      case 3:  // --dump-all
        ctx->dump_all_inputs = true;
        break;
      case 4:  // --benchmark
        ctx->is_benchmark_enabled = true;
        break;
      case 5:  // --help
        print_help_text(argv[0]);
        return -1;
      case 6:  // --input-file
        ctx->input_file_path = optarg;
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
 * @brief Compares inference results between pipeline 1 and pipeline 2
 * @param pipeline1_results Reference to inference results from pipeline 1
 * (data.inference_results)
 * @param pipeline2_results Reference to inference results from pipeline 2
 * (final_results)
 * @param filename Input filename for logging
 * @param log_level Application log level
 */
void compare_inference_results(const vector<vector<shared_ptr<vart::InferResult>>>& pipeline1_results,
                               const vector<vector<shared_ptr<vart::InferResult>>>& pipeline2_results,
                               const string& filename,
                               AppLogLevel log_level) {
  APP_LOG(AppLogLevel::INFO, log_level, "=== Inference Results for %s ===", filename.c_str());

  APP_LOG(AppLogLevel::INFO, log_level, "Pipeline 1 results: %zu batches", pipeline1_results.size());
  APP_LOG(AppLogLevel::INFO, log_level, "Pipeline 2 results: %zu batches", pipeline2_results.size());

  size_t min_batches = min(pipeline1_results.size(), pipeline2_results.size());

  for (size_t batch_idx = 0; batch_idx < min_batches; batch_idx++) {
    APP_LOG(AppLogLevel::INFO, log_level, "Batch %zu:", batch_idx);
    APP_LOG(AppLogLevel::INFO, log_level, "  Pipeline 1: %zu results", pipeline1_results[batch_idx].size());
    APP_LOG(AppLogLevel::INFO, log_level, "  Pipeline 2: %zu results", pipeline2_results[batch_idx].size());

    // Compare individual results in this batch
    size_t min_results = min(pipeline1_results[batch_idx].size(), pipeline2_results[batch_idx].size());
    for (size_t result_idx = 0; result_idx < min_results; result_idx++) {
      auto& p1_result = pipeline1_results[batch_idx][result_idx];
      auto& p2_result = pipeline2_results[batch_idx][result_idx];

      if (p1_result && p2_result) {
        APP_LOG(AppLogLevel::DEBUG, log_level, "    Result %zu: Both pipelines have valid results", result_idx);

        // Get inference result data for comparison
        InferResultData* p1_data = p1_result->get_infer_result();
        InferResultData* p2_data = p2_result->get_infer_result();

        if (p1_data && p2_data) {
          APP_LOG(AppLogLevel::DEBUG, log_level, "      Pipeline 1 result type: %d",
                  static_cast<int>(p1_data->result_type));
          APP_LOG(AppLogLevel::DEBUG, log_level, "      Pipeline 2 result type: %d",
                  static_cast<int>(p2_data->result_type));

          // Compare classification results
          if (p1_data->result_type == vart::InferResultType::CLASSIFICATION &&
              p2_data->result_type == vart::InferResultType::CLASSIFICATION) {
            ClassificationResData* p1_cls = static_cast<ClassificationResData*>(p1_data);
            ClassificationResData* p2_cls = static_cast<ClassificationResData*>(p2_data);

            // Print top prediction results for both pipelines
            if (!p1_cls->label.empty() && !p1_cls->confidence.empty() && !p2_cls->label.empty() &&
                !p2_cls->confidence.empty()) {
              // Print P1 results in first line
              string p1_labels = "";
              for (size_t i = 0; i < p1_cls->label.size(); ++i) {
                if (i > 0)
                  p1_labels += ", ";
                p1_labels += p1_cls->label[i];
              }
              APP_LOG(AppLogLevel::INFO, log_level, "      P1: %s (%.6f)", p1_labels.c_str(), p1_cls->confidence[0]);

              // Print P2 results in second line
              string p2_labels = "";
              for (size_t i = 0; i < p2_cls->label.size(); ++i) {
                if (i > 0)
                  p2_labels += ", ";
                p2_labels += p2_cls->label[i];
              }
              APP_LOG(AppLogLevel::INFO, log_level, "      P2: %s (%.6f)", p2_labels.c_str(), p2_cls->confidence[0]);
            } else {
              APP_LOG(AppLogLevel::WARNING, log_level, "      Empty classification results detected");
            }
          } else {
            APP_LOG(AppLogLevel::WARNING, log_level, "      Non-classification results detected");
          }
        } else {
          APP_LOG(AppLogLevel::WARNING, log_level, "    Result %zu: One or both pipelines have null result data",
                  result_idx);
        }
      } else {
        APP_LOG(AppLogLevel::WARNING, log_level, "    Result %zu: One or both pipelines have null results", result_idx);
      }
    }
  }

  APP_LOG(AppLogLevel::INFO, log_level, "=== End of Inference Results for %s ===", filename.c_str());
}

/**
 * @brief Thread function for running the first pipeline (preprocessing + Model
 * 1)
 * @param app_ctx Reference to the application context
 */
void run_pipeline1_thread(AppContext& app_ctx) {
  AppLogLevel log_level = app_ctx.log_level;
  std::chrono::high_resolution_clock::time_point start, end;

  while (!app_ctx.input_pipeline_queue.is_finished() || !app_ctx.input_pipeline_queue.is_empty()) {
    PipelineData data(vector<shared_ptr<vart::VideoFrame>>(), vector<shared_ptr<vart::VideoFrame>>(),
                      vector<PredResult>(), "", -1, -1, 0);

    if (!app_ctx.input_pipeline_queue.pop(data)) {
      continue;  // Timeout or no data, continue loop
    }

    if (data.pipeline_id >= 0 && data.pipeline_id < app_ctx.num_active_pipelines) {
      PipelineContext& pipeline = app_ctx.pipelines[data.pipeline_id];

      try {
        // Process through pipeline 1 (first model)
        vector<vector<shared_ptr<vart::Memory>>> inference_out_tensors_memory;

        // Create output tensor memory
        if (!create_out_tensor_memory(&pipeline, log_level, app_ctx.device, inference_out_tensors_memory)) {
          APP_LOG(AppLogLevel::ERROR, log_level, "Failed to create output tensor memory for pipeline %d",
                  data.pipeline_id);
          continue;
        }

        // Run inference
        APP_LOG(AppLogLevel::DEBUG, log_level, "Do infer for %zu frames in pipeline %d", data.input_frames.size(),
                data.pipeline_id);
        start = chrono::high_resolution_clock::now();
        if (!infer_process_frames(&pipeline, log_level, data.preprocessed_frames.size(), data.preprocessed_frames,
                                  inference_out_tensors_memory, data.file_index, data.iteration_counter,
                                  app_ctx.max_iterations)) {
          APP_LOG(AppLogLevel::ERROR, log_level, "Inference failed for pipeline %d", data.pipeline_id);
          continue;
        }
        end = chrono::high_resolution_clock::now();

        /* Record this inference interval */
        {
          std::lock_guard<std::mutex> lock(app_ctx.timing_mutex);
          app_ctx.inference_intervals.emplace_back(
              chrono::duration_cast<chrono::microseconds>(start.time_since_epoch()).count(),
              chrono::duration_cast<chrono::microseconds>(end.time_since_epoch()).count());
        }
        // Post-process results if enabled
        vector<vector<shared_ptr<InferResult>>> inference_results;
        if (pipeline.postprocess_enable) {
          APP_LOG(AppLogLevel::DEBUG, log_level, "Do post process for %zu frames in pipeline %d",
                  data.input_frames.size(), data.pipeline_id);
          inference_results = postprocess_process_frames(&pipeline, log_level, data.preprocessed_frames.size(),
                                                         inference_out_tensors_memory, data.iteration_counter);

          // Process each frame's results.
          {
            vector<shared_ptr<InferResult>> root_res;

            for (size_t i = 0; i < data.input_frames.size(); i++) {
              root_res.push_back(make_shared<InferResult>(InferResultType::ROOT));
              if (inference_results.size() > i)
                root_res.back()->add_children(inference_results[i]);
              else
                APP_LOG(AppLogLevel::WARNING, log_level, "No infer result for current frame in pipeline %d",
                        data.pipeline_id);

              // Transform results to match original input resolution
              if (!transform_infer_result(&app_ctx, &pipeline, root_res)) {
                APP_LOG(AppLogLevel::ERROR, log_level, "Failed to transform results for pipeline %d", data.pipeline_id);
                continue;
              }

              // Draw inference results on frame if output is enabled
              if (!pipeline.out_file_path.empty()) {
                APP_LOG(AppLogLevel::DEBUG, log_level, "Draw Prediction for frame %zu in pipeline %d", i,
                        data.pipeline_id);
                start = chrono::high_resolution_clock::now();
                if (!draw_infer_result(&app_ctx, &pipeline, root_res.back(), data.input_frames[i])) {
                  APP_LOG(AppLogLevel::WARNING, log_level,
                          "Failed to overlay info on videoframe for pipeline "
                          "%d, continuing without overlay",
                          data.pipeline_id);
                }
                end = chrono::high_resolution_clock::now();
                if (app_ctx.is_benchmark_enabled) {
                  pipeline.total_overlay_time += chrono::duration_cast<chrono::microseconds>(end - start).count();
                }

                // Output frame if not benchmarking
                if (!app_ctx.is_benchmark_enabled && !pipeline.out_file_path.empty()) {
                  /* Dump the video frame with prediction drawing */
                  APP_LOG(AppLogLevel::DEBUG, log_level, "Dump output frame %s", pipeline.out_file_path.c_str());
                  if (pipeline.input_fmt == APP_VIDEO_INPUT_FORMAT_JPEG) {
                    /* For JPEG input, dump as image files */
                    if (!dump_video_frame_as_jpeg(log_level, pipeline.output_file, data.input_frames[i])) {
                      APP_LOG(AppLogLevel::ERROR, log_level,
                              "Failed to dump output frame %ld as jpeg image file in pipeline %d", i, data.pipeline_id);
                      /* Do not reset the frames here; let stack unwinding
                       * release them via `data`'s destructor in the catch
                       * block, so the buffer is not recycled (and possibly
                       * overwritten by another thread) while the failure
                       * is still being handled. */
                      throw runtime_error("Failed to dump output frame as jpeg image file");
                    }

                  } else {
                    /* For other formats, dump as raw video frames */
                    if (!dump_video_frame(log_level, pipeline.output_file, data.input_frames[i])) {
                      APP_LOG(AppLogLevel::ERROR, log_level, "Failed to dump output frame %ld in pipeline %d", i,
                              data.pipeline_id);
                      throw runtime_error("Failed to dump output frame");
                    }
                  }
                }
              }
            }
          }
        }

        // Create data for pipeline 2 (cascading)
        PipelineData data2(vector<shared_ptr<vart::VideoFrame>>(), vector<shared_ptr<vart::VideoFrame>>(),
                           vector<PredResult>(), "", -1, -1, 0);
        while (!app_ctx.input_pipeline_queue.pop(data2)) {
          continue;  // Timeout or no data, continue loop
        }

        PipelineData cascaded_data(data2.input_frames, data2.preprocessed_frames, inference_results, data2.filename,
                                   data2.file_index, data2.pipeline_id, data2.iteration_counter);

        // Push to cascaded queue for pipeline 2
        if (!app_ctx.cascaded_queue.push(cascaded_data)) {
          APP_LOG(AppLogLevel::ERROR, log_level, "Failed to push data to cascaded queue");
        }
        // Single pipeline: release buffers here since no cascading
        for (auto& frame : data.input_frames) {
          if (frame) {
            frame.reset();
          }
        }

        if (pipeline.preprocess_enable) {
          for (auto& frame : data.preprocessed_frames) {
            if (frame) {
              frame.reset();
            }
          }
        }
      } catch (const exception& e) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Exception in pipeline1 thread: %s", e.what());
      }
    }
  }

  // Finish cascaded queue when pipeline1 is done processing
  app_ctx.cascaded_queue.finish();
  APP_LOG(AppLogLevel::INFO, log_level, "Pipeline 1 thread finished");
}

/**
 * @brief Thread function for cascaded inference (uses data.pipeline_id from queue)
 * @param app_ctx Reference to the application context
 */
void run_pipeline2_thread(AppContext& app_ctx) {
  AppLogLevel log_level = app_ctx.log_level;
  std::chrono::high_resolution_clock::time_point start, end;

  while (!app_ctx.cascaded_queue.is_finished() || !app_ctx.cascaded_queue.is_empty()) {
    PipelineData data(vector<shared_ptr<vart::VideoFrame>>(), vector<shared_ptr<vart::VideoFrame>>(),
                      vector<PredResult>(), "", -1, -1, 0);

    if (!app_ctx.cascaded_queue.pop(data)) {
      continue;  // Timeout or no data, continue loop
    }

    // Cascaded thread uses prior job's inference_results; pipeline context
    // matches the cascaded batch's pipeline_id (same pattern as pipeline1_thread).
    APP_LOG(AppLogLevel::DEBUG, log_level, "Cascaded thread received %zu inference results from prior pipeline",
            data.inference_results.size());

    if (data.pipeline_id >= 0 && data.pipeline_id < app_ctx.num_active_pipelines) {
      PipelineContext& pipeline = app_ctx.pipelines[data.pipeline_id];

      try {
        // Process through the pipeline identified by cascaded data.pipeline_id
        vector<vector<shared_ptr<vart::Memory>>> inference_out_tensors_memory;

        // Create output tensor memory
        if (!create_out_tensor_memory(&pipeline, log_level, app_ctx.device, inference_out_tensors_memory)) {
          APP_LOG(AppLogLevel::ERROR, log_level, "Failed to create output tensor memory for pipeline %d",
                  data.pipeline_id);
          continue;
        }

        // Run inference with cascaded data
        APP_LOG(AppLogLevel::DEBUG, log_level, "Do infer for %zu frames in pipeline %d",
                data.preprocessed_frames.size(), data.pipeline_id);
        start = chrono::high_resolution_clock::now();
        if (!infer_process_frames(&pipeline, log_level, data.preprocessed_frames.size(), data.preprocessed_frames,
                                  inference_out_tensors_memory, data.file_index, data.iteration_counter,
                                  app_ctx.max_iterations)) {
          APP_LOG(AppLogLevel::ERROR, log_level, "Inference failed for pipeline %d", data.pipeline_id);
          continue;
        }
        end = chrono::high_resolution_clock::now();

        /* Record this inference interval */
        {
          std::lock_guard<std::mutex> lock(app_ctx.timing_mutex);
          app_ctx.inference_intervals.emplace_back(
              chrono::duration_cast<chrono::microseconds>(start.time_since_epoch()).count(),
              chrono::duration_cast<chrono::microseconds>(end.time_since_epoch()).count());
        }

        // Post-process final results if enabled
        vector<vector<shared_ptr<InferResult>>> final_results;
        if (pipeline.postprocess_enable) {
          APP_LOG(AppLogLevel::DEBUG, log_level, "Do post process for %zu frames in pipeline %d",
                  data.preprocessed_frames.size(), data.pipeline_id);
          start = chrono::high_resolution_clock::now();
          final_results = postprocess_process_frames(&pipeline, log_level, data.preprocessed_frames.size(),
                                                     inference_out_tensors_memory, data.iteration_counter);
          end = chrono::high_resolution_clock::now();
          if (app_ctx.is_benchmark_enabled) {
            pipeline.total_postprocess_time += chrono::duration_cast<chrono::microseconds>(end - start).count();
          }

          // Process each frame's final results.
          {
            vector<shared_ptr<InferResult>> root_res;

            for (size_t i = 0; i < data.input_frames.size(); i++) {
              root_res.push_back(make_shared<InferResult>(InferResultType::ROOT));
              if (final_results.size() > i)
                root_res.back()->add_children(final_results[i]);
              else
                APP_LOG(AppLogLevel::WARNING, log_level, "No infer result for current frame in pipeline %d",
                        data.pipeline_id);

              // Transform results to match original input resolution
              if (!transform_infer_result(&app_ctx, &pipeline, root_res)) {
                APP_LOG(AppLogLevel::ERROR, log_level, "Failed to transform results for pipeline %d", data.pipeline_id);
                continue;
              }

              // Draw inference results on frame if output is enabled
              if (!pipeline.out_file_path.empty()) {
                APP_LOG(AppLogLevel::DEBUG, log_level, "Draw Prediction for frame %zu in pipeline %d", i,
                        data.pipeline_id);
                start = chrono::high_resolution_clock::now();
                if (!draw_infer_result(&app_ctx, &pipeline, root_res.back(), data.input_frames[i])) {
                  APP_LOG(AppLogLevel::WARNING, log_level,
                          "Failed to overlay info on videoframe for pipeline "
                          "%d, continuing without overlay",
                          data.pipeline_id);
                }
                end = chrono::high_resolution_clock::now();
                if (app_ctx.is_benchmark_enabled) {
                  pipeline.total_overlay_time += chrono::duration_cast<chrono::microseconds>(end - start).count();
                }

                // Output final frame if not benchmarking
                if (!app_ctx.is_benchmark_enabled && !pipeline.out_file_path.empty()) {
                  /* Dump the video frame with prediction drawing */
                  APP_LOG(AppLogLevel::DEBUG, log_level, "Dump output frame %s", pipeline.out_file_path.c_str());
                  if (pipeline.input_fmt == APP_VIDEO_INPUT_FORMAT_JPEG) {
                    /* For JPEG input, dump as image files */
                    if (!dump_video_frame_as_jpeg(log_level, pipeline.output_file, data.input_frames[i])) {
                      APP_LOG(AppLogLevel::ERROR, log_level,
                              "Failed to dump output frame %ld as jpeg image file in pipeline %d", i, data.pipeline_id);
                      /* See pipeline1: do not reset the frames here. */
                      throw runtime_error("Failed to dump output frame as jpeg image file");
                    }

                  } else {
                    /* For other formats, dump as raw video frames */
                    if (!dump_video_frame(log_level, pipeline.output_file, data.input_frames[i])) {
                      APP_LOG(AppLogLevel::ERROR, log_level, "Failed to dump output frame %ld in pipeline %d", i,
                              data.pipeline_id);
                      throw runtime_error("Failed to dump output frame");
                    }
                  }
                }
              }
            }
          }
        }

        // Compare inference results between pipeline 1 and pipeline 2
        if (!data.inference_results.empty() && !final_results.empty()) {
          APP_LOG(AppLogLevel::DEBUG, log_level, "Comparing results: P1 has %zu batches, P2 has %zu batches",
                  data.inference_results.size(), final_results.size());
          compare_inference_results(data.inference_results, final_results, data.filename, log_level);
        } else {
          APP_LOG(AppLogLevel::DEBUG, log_level, "Skipping comparison - P1 results: %zu, P2 results: %zu",
                  data.inference_results.size(), final_results.size());
        }

        APP_LOG(AppLogLevel::DEBUG, log_level, "Processed file %s through pipeline %d", data.filename.c_str(),
                data.pipeline_id);

        for (auto& frame : data.input_frames) {
          if (frame) {
            frame.reset();
          }
        }

        if (pipeline.preprocess_enable) {
          for (auto& frame : data.preprocessed_frames) {
            if (frame) {
              frame.reset();
            }
          }
        }

      } catch (const exception& e) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Exception in pipeline2 thread: %s", e.what());
        {
          std::lock_guard<std::mutex> lock(app_ctx.mtx);
          app_ctx.thread_processed = true;
        }
        APP_LOG(AppLogLevel::DEBUG, log_level, "Notifying main thread after exception in pipeline 2");
        app_ctx.cv.notify_one();
      }
    }
    {
      std::lock_guard<std::mutex> lock(app_ctx.mtx);
      app_ctx.thread_processed = true;
    }
    APP_LOG(AppLogLevel::DEBUG, log_level, "Notifying main thread after processing frame %d in pipeline 2",
            data.file_index);
    app_ctx.cv.notify_one();
  }

  APP_LOG(AppLogLevel::INFO, log_level, "Pipeline 2 thread finished");
}

/**
 * @brief Calculate total time from overlapping intervals
 * @param intervals Vector of time intervals
 * @return Total inference duration in microseconds
 */
double calculate_total_inference_time_us(vector<TimeInterval>& intervals) {
  if (intervals.empty())
    return 0.0;
  sort(intervals.begin(), intervals.end());

  /* Merge overlapping intervals and calculate total time */
  double total_us = 0.0;
  double current_start = intervals[0].start;
  double current_end = intervals[0].end;

  for (size_t i = 1; i < intervals.size(); i++) {
    if (intervals[i].start <= current_end) {
      /* Overlapping - extend current interval */
      current_end = max(current_end, intervals[i].end);
    } else {
      /* Non-overlapping - add current interval to total and start new one */
      total_us += (current_end - current_start);
      current_start = intervals[i].start;
      current_end = intervals[i].end;
    }
  }

  /* Add the last interval */
  total_us += (current_end - current_start);
  return total_us;
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

  /* Declare threads */
  thread pipeline1_thread, pipeline2_thread;

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

  /* Resize tensor memory vectors based on actual number of models */
  npu_out_tensors_memory.resize(ctx.num_models);

  /* Device is required for all Vart APIs, this load xclbin of device only if
   * not already loaded */
  ctx.device = vart::Device::get_device_hdl(ctx.device_idx, ctx.xclbin_location);
  if (ctx.device == nullptr) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Failed to get device handle");
    goto killall;
  }

  /* make onnx environment alive throughout the session */
  try {
    ctx.ort_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_ERROR, "spatial_mt_ml_ort");
  } catch (const Ort::Exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Failed to create ONNX Runtime environment: %s", e.what());
    goto killall;
  }

  /* Initialize all pipelines: setup files, contexts, and memory */
  for (int pipeline_idx = 0; pipeline_idx < ctx.num_active_pipelines; pipeline_idx++) {
    /* Assign input file paths and configuration */
    ctx.pipelines[pipeline_idx].input_file_path = ctx.input_file_path;
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

  /* Start multithreaded processing */
  APP_LOG(AppLogLevel::INFO, log_level, "Starting multithreaded pipeline processing with %d pipelines",
          ctx.num_active_pipelines);

  // Start consumer threads
  pipeline2_thread = thread(run_pipeline2_thread, ref(ctx));
  pipeline1_thread = thread(run_pipeline1_thread, ref(ctx));

  /* Main loop for video processing until end-of-file */
  while (read_status != APP_EOF) {
    /* Number of frames to read in the current iteration per pipeline */
    vector<uint32_t> frames_to_read_per_pipeline(ctx.num_active_pipelines);
    vector<uint32_t> frame_read_per_pipeline(ctx.num_active_pipelines, 0);

    /* Maintain arrays of input frames per pipeline for reading and
     * preprocessing only */
    vector<vector<shared_ptr<vart::VideoFrame>>> input_frames(ctx.num_active_pipelines);
    vector<vector<shared_ptr<vart::VideoFrame>>> preprocess_out_frames(ctx.num_active_pipelines);

    /* Calculate frames to read and allocate frame vectors for each pipeline */
    for (int pipeline_idx = 0; pipeline_idx < ctx.num_active_pipelines; pipeline_idx++) {
      /* Calculate frames to read based on batch sizes */
      if (ctx.num_frame_to_process != APP_PROCESS_ALL_FRAMES) {
        /* Calculate the remaining frames to read in this iteration, considering
         * the batch size and the frames already processed */
        uint64_t remaining_frames_to_process = ctx.num_frame_to_process - num_frame_processed;
        frames_to_read_per_pipeline[pipeline_idx] =
            ((int64_t)remaining_frames_to_process < ctx.pipelines[pipeline_idx].model_info.batch_size)
                ? (remaining_frames_to_process)
                : ctx.pipelines[pipeline_idx].model_info.batch_size;
      } else {
        /* If processing all frames, set frames_to_read equal to the batch size
         */
        frames_to_read_per_pipeline[pipeline_idx] = ctx.pipelines[pipeline_idx].model_info.batch_size;
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

      /* Collect all frames for this pipeline's batch before pushing to queue */
      vector<shared_ptr<vart::VideoFrame>> batch_frames;
      batch_frames.reserve(frames_to_read_per_pipeline[pipeline_idx]);

      /* Loop over the frames to be read for this pipeline's batch */
      for (uint32_t frm_idx = 0; frm_idx < frames_to_read_per_pipeline[pipeline_idx]; frm_idx++) {
        /* Acquire a buffer from the pre-process input pool */
      read_more:
        try {
          input_frames[pipeline_idx][frm_idx] = ctx.pipelines[pipeline_idx].in_pool->acquire_frame();
        } catch (const std::runtime_error& e) {
          const bool shutting_down = std::string(e.what()).find("shutting down") != std::string::npos;
          APP_LOG(AppLogLevel::ERROR, log_level, "%s while acquiring frame from pre-process pool for pipeline %d: %s",
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
                    "%s while acquiring frame from output pre-process pool for pipeline %d: %s",
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
            /* Perform the pre-process step */
            if (preprocess_process_frame(&ctx.pipelines[pipeline_idx], ctx.log_level,
                                         input_frames[pipeline_idx][frm_idx],
                                         preprocess_out_frames[pipeline_idx][frm_idx]) != true) {
              input_frames[pipeline_idx][frm_idx].reset();
              preprocess_out_frames[pipeline_idx][frm_idx].reset();
              goto killall;
            }
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
          /* Collect preprocessed frame for batch processing */
          if (ctx.pipelines[pipeline_idx].preprocess_enable) {
            batch_frames.push_back(preprocess_out_frames[pipeline_idx][frm_idx]);
          } else {
            batch_frames.push_back(input_frames[pipeline_idx][frm_idx]);
          }

          /* Increment the counter for frames read if data read successfully */
          frame_read_per_pipeline[pipeline_idx]++;
        } else if (read_status == APP_READ_FAILED) {
          APP_LOG(AppLogLevel::WARNING, log_level, "Failed to read input data for pipeline %d", pipeline_idx);
          input_frames[pipeline_idx][frm_idx].reset();
          if (ctx.pipelines[pipeline_idx].preprocess_enable)
            preprocess_out_frames[pipeline_idx][frm_idx].reset();
          /* Set the read_status to APP_EOF and exit the loop */
          read_status = APP_EOF;
          break;
        }

        /* Handle iteration logic for single frame formats */
        if (((ctx.pipelines[pipeline_idx].input_fmt == APP_VIDEO_INPUT_FORMAT_JPEG ||
              ctx.pipelines[pipeline_idx].input_fmt == APP_VIDEO_INPUT_FORMAT_BGR_FLOAT) &&
             (APP_READ_SUCCESS == read_status)) ||
            (read_status == APP_EOF)) {
          /* For single frame formats like JPEG, each pipeline should process
           * the same frame Reset file pointer to beginning for next pipeline to
           * read the same frame */
          if (ctx.pipelines[pipeline_idx].input_fmt == APP_VIDEO_INPUT_FORMAT_NV12 ||
              ctx.pipelines[pipeline_idx].input_fmt == APP_VIDEO_INPUT_FORMAT_JPEG) {
            ctx.pipelines[pipeline_idx].input_file.clear();
            ctx.pipelines[pipeline_idx].input_file.seekg(0, ctx.pipelines[pipeline_idx].input_file.beg);
            APP_LOG(AppLogLevel::DEBUG, log_level, "Reset file pointer to beginning for pipeline %d", pipeline_idx);
          }
          if (APP_EOF == read_status) {
            APP_LOG(AppLogLevel::DEBUG, log_level, "Got APP_EOF for pipeline %d", pipeline_idx);
            APP_LOG(AppLogLevel::DEBUG, log_level, "Read more from start of file for pipeline %d", pipeline_idx);
            input_frames[pipeline_idx][frm_idx].reset();
            if (ctx.pipelines[pipeline_idx].preprocess_enable)
              preprocess_out_frames[pipeline_idx][frm_idx].reset();
            goto read_more;
          }
        }
      }  // end of frame loop for this pipeline

      /* Push entire batch to pipeline queue for multithreaded processing */
      if (!batch_frames.empty() && frame_read_per_pipeline[pipeline_idx] > 0) {
        PipelineData pipeline_data(input_frames[pipeline_idx], batch_frames, vector<PredResult>(),
                                   ctx.pipelines[pipeline_idx].input_file_path, num_frame_processed, pipeline_idx,
                                   ctx.iteration_counter);

        if (!ctx.input_pipeline_queue.push(pipeline_data)) {
          APP_LOG(AppLogLevel::WARNING, log_level, "Failed to push batch to pipeline queue for pipeline %d",
                  pipeline_idx);
        } else {
          APP_LOG(AppLogLevel::DEBUG, log_level, "Pushed batch of %zu frames to pipeline queue for pipeline %d",
                  batch_frames.size(), pipeline_idx);
        }
      }

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
    if (total_frames_read <= 0)
      break;

    /* All inference and postprocessing is now handled by threads - main loop
     * only reads and preprocesses */

    /* Log the number of processed frames */
    num_frame_processed += total_frames_read;
    APP_LOG(AppLogLevel::INFO, log_level, "num_frame_processed %ld", num_frame_processed);
    /* Check if the required number of frames has been processed */
    if (ctx.num_frame_to_process != APP_PROCESS_ALL_FRAMES && num_frame_processed >= ctx.num_frame_to_process) {
      APP_LOG(AppLogLevel::INFO, log_level, "Required frames processed");
      read_status = APP_EOF;
      /* no need to break as while(read_status != APP_EOF) take care of this */
    }

    /* Note: Frame buffers are now managed by threads after being pushed to
     * queues as batches */
    /* Main thread only tracks batch processing progress */
    for (int pipeline_idx = 0; pipeline_idx < ctx.num_active_pipelines; pipeline_idx++) {
      if (frame_read_per_pipeline[pipeline_idx] > 0) {
        APP_LOG(AppLogLevel::DEBUG, log_level,
                "Pipeline %d: Pushed 1 batch containing %d frames to "
                "processing queue",
                pipeline_idx, frame_read_per_pipeline[pipeline_idx]);
      }
    }

    std::cout << "Main thread waiting for processing to complete..." << std::endl;
    {
      std::unique_lock<std::mutex> lock(ctx.mtx);

      ctx.cv.wait(lock, [&ctx] { return ctx.thread_processed; });
      ctx.thread_processed = false;
    }
    std::cout << "Main thread processing completed for iteration " << ctx.iteration_counter << std::endl;

    ctx.iteration_counter++;
    /* Re-opening files for next iteration */
    if (!ctx.is_benchmark_enabled && ctx.iteration_counter < ctx.max_iterations) {
      for (int pipeline_idx = 0; pipeline_idx < ctx.num_active_pipelines; pipeline_idx++) {
        close_files(&ctx.pipelines[pipeline_idx], ctx.log_level);

        APP_LOG(AppLogLevel::INFO, log_level, "Re-opening files for pipeline %d for next iteration", pipeline_idx);
        open_files(&ctx.pipelines[pipeline_idx], ctx.pipelines[pipeline_idx].output_dir_path, ctx.max_iterations,
                   ctx.iteration_counter, ctx.log_level, ctx.dump_all_inputs);
      }
    }
    /* Check if max iterations reached and stop the loop*/
    if (ctx.iteration_counter >= ctx.max_iterations) {
      read_status = APP_EOF;
    }

    if (!ctx.is_benchmark_enabled && (ctx.max_iterations > 1)) {
      cout << "Completed " << ctx.iteration_counter << "/" << ctx.max_iterations << " iteration(s)" << endl;
    }
  }

killall:
  /* Clean up multithreading resources */
  APP_LOG(AppLogLevel::INFO, log_level, "Finishing multithreaded processing");

  // Signal input queue to finish - cascaded queue will be finished by pipeline1
  // thread
  ctx.input_pipeline_queue.finish();

  // Wait for threads to complete
  if (pipeline1_thread.joinable()) {
    pipeline1_thread.join();
  }

  // Ensure cascaded queue is finished (safety net)
  ctx.cascaded_queue.finish();

  if (pipeline2_thread.joinable()) {
    pipeline2_thread.join();
  }

  APP_LOG(AppLogLevel::INFO, log_level, "Multithreaded processing completed");

  cout << "Total number of samples processed on all pipelines: " << num_frame_processed << endl;
  /* To calulate average of all the pipelines */
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

    for (int pipeline_idx = 0; pipeline_idx < ctx.num_active_pipelines; pipeline_idx++) {
      cout << "Pipeline " << pipeline_idx << ":\n";

      if (ctx.pipelines[pipeline_idx].preprocess_enable) {
        cout << "  Average time for Pre-process : "
             << (ctx.pipelines[pipeline_idx].total_preprocess_time / 1000.0) / frames_processed_per_pipe << " ms\n";
      }
      cout << "  Average time for Inference : "
           << (ctx.pipelines[pipeline_idx].total_infer_time / 1000.0) / frames_processed_per_pipe << " ms\n";
      if (ctx.pipelines[pipeline_idx].postprocess_enable) {
        cout << "  Average time for Post-process : "
             << (ctx.pipelines[pipeline_idx].total_postprocess_time / 1000.0) / frames_processed_per_pipe << " ms\n";
      }
      if (!ctx.pipelines[pipeline_idx].out_file_path.empty()) {
        cout << "  Average time for Overlay : "
             << (ctx.pipelines[pipeline_idx].total_overlay_time / 1000.0) / frames_processed_per_pipe << " ms\n";
      }

      ctx.pipelines[pipeline_idx].total_time =
          ctx.pipelines[pipeline_idx].total_preprocess_time + ctx.pipelines[pipeline_idx].total_infer_time +
          ctx.pipelines[pipeline_idx].total_postprocess_time + ctx.pipelines[pipeline_idx].total_overlay_time;

      cout << "  Average  total time for Pipeline " << pipeline_idx << ": "
           << (ctx.pipelines[pipeline_idx].total_time / 1000.0) / frames_processed_per_pipe << " ms\n";
      cout << "  Average  FPS for Pipeline " << pipeline_idx << ": "
           << (frames_processed_per_pipe * 1000000.0) / ctx.pipelines[pipeline_idx].total_time << " fps\n";
    }
  }
  if (ctx.is_benchmark_enabled) {
    cout << "==========================================================" << endl;

    int processed_count = frames_processed_per_pipe;
    double total_inf_time_us = calculate_total_inference_time_us(ctx.inference_intervals);
    double total_inf_time_ms = total_inf_time_us / 1000.0;

    if (processed_count > 0 && total_inf_time_ms > 0.0) {
      double avg_inf_time_ms = total_inf_time_ms / processed_count;
      double inf_fps = (1000.0 * processed_count) / total_inf_time_ms;

      cout << "Total inference time: " << total_inf_time_ms << "ms for " << processed_count << " samples, average "
           << avg_inf_time_ms << "ms per sample. Total inference FPS: " << inf_fps << endl;
    } else {
      cout << "No valid samples processed or zero inference time detected" << endl;
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
