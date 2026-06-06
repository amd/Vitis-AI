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

#include <boost/algorithm/string.hpp>

using namespace vart;

/* Forward declaration */
Ort::Value create_input_tensor(AppLogLevel log_level, const InferTensorInfo& tensor_info, VideoFrame& frame);

/**
 * @brief Utility function to convert shape vector to string.
 * @param shape Vector of shape dimensions.
 * @return String representation of the shape.
 */

string get_shape_string(const vector<unsigned int>& shape) {
  string shape_str = "(";
  for (size_t i = 0; i < shape.size(); ++i) {
    shape_str += to_string(shape[i]);
    if (i < shape.size() - 1) {
      shape_str += ", ";
    }
  }
  shape_str += ")";
  return shape_str;
}

/*
 * Convert MemoryLayout to string
 */
static string get_memory_layout_string(const MemoryLayout& layout) {
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
    default:
      return "UNKNOWN";
  }
}

/**
 * @brief Utility function to map ONNX data types to internal DataType enum.
 * @param onnx_type ONNX data type.
 * @return Corresponding internal DataType enum value.
 */
DataType map_onnx_data_type(ONNXTensorElementDataType onnx_type) {
  switch (onnx_type) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:
      return DataType::INT8;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:
      return DataType::UINT8;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16:
      return DataType::INT16;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16:
      return DataType::UINT16;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16:
      return DataType::BF16;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16:
      return DataType::FP16;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:
      return DataType::INT32;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32:
      return DataType::UINT32;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
      return DataType::FLOAT32;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
      return DataType::INT64;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64:
      return DataType::UINT64;
    default:
      return DataType::UNKNOWN;
  }
}

/**
 * @brief Utility function to get tensor size in bytes from ONNX Tensor info.
 * @param ort_tensor_info ONNX Tensor type and shape info.
 * @param model_batch_size Batch size to use when resolving dynamic dimensions.
 * @return Size of the tensor in bytes.
 */
size_t get_tensor_size_in_bytes(Ort::ConstTensorTypeAndShapeInfo ort_tensor_info, int64_t model_batch_size) {
  auto element_type = ort_tensor_info.GetElementType();
  size_t size_in_bytes = 0;
  auto shape = ort_tensor_info.GetShape();

  /* Change dynamic batch (-1) to model_batch_size and calculate tensor size */
  size_t size = 1;
  for (auto& dim : shape) {
    if (dim <= 0)
      dim = model_batch_size;
    size *= dim;
  }

  switch (element_type) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
      size_in_bytes = size * sizeof(float32_t);
      break;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:
      size_in_bytes = size * sizeof(int8_t);
      break;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16:
      size_in_bytes = size * sizeof(uint16_t);  // ONNX uses uint16 to represent FP16
      break;
    // TODO Handle other formats as well
    default:
      break;
  }
  return size_in_bytes;
}

/* Helper function to read underlying hardware batch size from VitisAI config file.
 * Priority: dp_size, then device_batch_size, else default 1.
 * Only called when ONNX model has dynamic batch size (<= 0). */
int64_t read_device_batch_size_from_config(const std::string& vitisai_config_file, AppLogLevel log_level) {
  int64_t device_batch_size = 1;  // Default value
  try {
    boost::property_tree::ptree vitisai_pt;
    boost::property_tree::read_json(vitisai_config_file, vitisai_pt);

    // Navigate through the JSON structure to find hardware batch size.
    for (const auto& pass : vitisai_pt.get_child("passes")) {
      if (pass.second.get<std::string>("name") == "vaiml_partition") {
        auto vaiml_config = pass.second.get_child_optional("vaiml_config");
        if (vaiml_config) {
          auto dp_size_opt = vaiml_config->get_optional<int64_t>("dp_size");
          if (dp_size_opt) {
            if (*dp_size_opt > 0) {
              device_batch_size = *dp_size_opt;
              APP_LOG(AppLogLevel::INFO, log_level, "Read dp_size from VitisAI config: %ld", device_batch_size);
            } else {
              device_batch_size = 1;
              APP_LOG(AppLogLevel::WARNING, log_level, "Invalid dp_size '%ld'. Using default batch size: %ld",
                      *dp_size_opt, device_batch_size);
            }
            break;
          }

          auto device_batch_size_opt = vaiml_config->get_optional<int64_t>("device_batch_size");
          if (device_batch_size_opt) {
            if (*device_batch_size_opt > 0) {
              device_batch_size = *device_batch_size_opt;
              APP_LOG(AppLogLevel::INFO, log_level, "Read device_batch_size from VitisAI config: %ld",
                      device_batch_size);
            } else {
              device_batch_size = 1;
              APP_LOG(AppLogLevel::WARNING, log_level, "Invalid device_batch_size '%ld'. Using default batch size: %ld",
                      *device_batch_size_opt, device_batch_size);
            }
            break;
          }
        }
      }
    }
  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::WARNING, log_level,
            "Could not read device_batch_size/dp_size from VitisAI config (%s), using default: %ld", e.what(),
            device_batch_size);
  }
  return device_batch_size;
}

/**
 * @brief Create a batched input tensor from multiple video frames.
 * @param log_level Application log level for logging.
 * @param tensor_info Information about the tensor to be created.
 * @param frames Vector of video frames to be batched.
 * @param start_idx Starting index in the frames vector.
 * @param batch_size Number of frames to include in the batch.
 * @param shape Target tensor shape including batch dimension.
 * @return Created batched ONNX Runtime tensor.
 */
Ort::Value create_batched_input_tensor(AppLogLevel log_level,
                                       const InferTensorInfo& tensor_info,
                                       vector<shared_ptr<VideoFrame>>& frames,
                                       size_t start_idx,
                                       size_t batch_size,
                                       const vector<int64_t>& shape) {
  // Validate inputs early
  if (frames.empty() || batch_size == 0 || start_idx + batch_size > frames.size()) {
    throw std::invalid_argument("Invalid input parameters");
  }

  // Calculate dimensions
  size_t total_elements = 1;
  for (int64_t dim : shape) {
    total_elements *= dim;
  }
  const size_t elements_per_frame = total_elements / batch_size;

  // Determine data type
  const bool is_fp16 = (tensor_info.meta.data_type == DataType::FP16);
  const size_t element_size = is_fp16 ? sizeof(uint16_t) : sizeof(float32_t);

  APP_LOG(AppLogLevel::DEBUG, log_level,
          "Creating batched tensor: %ld frames, %ld elements per frame, %ld total elements", batch_size,
          elements_per_frame, total_elements);

  // Allocate memory
  std::unique_ptr<uint8_t[]> batched_data(new uint8_t[total_elements * element_size]);

  // Process frames
  for (size_t batch_idx = 0; batch_idx < batch_size; batch_idx++) {
    size_t frame_idx = start_idx + batch_idx;

    if (frame_idx >= frames.size()) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Frame index %ld out of range", frame_idx);
      throw std::runtime_error("Frame index out of range");
    }

    VideoFrameMapInfo map_info;
    try {
      map_info = frames[frame_idx]->map(DataMapFlags::READ);

      // Copy data
      const uint8_t* src = reinterpret_cast<const uint8_t*>(map_info.planes[0].data);
      uint8_t* dst = batched_data.get() + (batch_idx * elements_per_frame * element_size);
      std::memcpy(dst, src, elements_per_frame * element_size);

      frames[frame_idx]->unmap();

      APP_LOG(AppLogLevel::DEBUG, log_level, "Copied frame %ld data to batch position %ld", frame_idx, batch_idx);
    } catch (const exception& e) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Failed to process frame %ld: %s", frame_idx, e.what());
      throw;
    }
  }
  // Create tensor
  Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

  if (is_fp16) {
    auto* fp16_ptr = reinterpret_cast<Ort::Float16_t*>(batched_data.release());
    return Ort::Value::CreateTensor<Ort::Float16_t>(memory_info, fp16_ptr, total_elements, shape.data(), shape.size());
  } else {
    auto* fp32_ptr = reinterpret_cast<float32_t*>(batched_data.release());
    return Ort::Value::CreateTensor<float32_t>(memory_info, fp32_ptr, total_elements, shape.data(), shape.size());
  }
}

/**
 * @brief Delete the inference context and free resources.
 * @param ctx Pointer to the application context.
 * @return None.
 */
void delete_inference_context(PipelineContext* pipeline_ctx) {
  InferModelConf* model_info = &pipeline_ctx->model_info;

  /* Clear tensor configurations */
  if (model_info != NULL) {
    /* Clear input configuration */
    for (size_t i = 0; i < model_info->num_in_tensors; i++) {
      model_info->in_tensors_info[i].meta.shape.clear();
    }
    for (size_t i = 0; i < model_info->num_in_tensors; i++) {
      model_info->in_tensors_info[i].meta.shape_i64.clear();
    }

    model_info->in_tensors_info.clear();
    model_info->input_tensors.clear();

    /* Clear output configuration */
    for (size_t i = 0; i < model_info->num_out_tensors; i++) {
      model_info->out_tensors_info[i].meta.shape.clear();
    }
    for (size_t i = 0; i < model_info->num_out_tensors; i++) {
      model_info->out_tensors_info[i].meta.shape_i64.clear();
    }
    model_info->out_tensors_info.clear();
    model_info->output_tensors.clear();
  }

  /* Clear onnx session */
  pipeline_ctx->ort_session.reset();
}

/**
 * @brief Create an input tensor from a video frame.
 * @param log_level Application log level for logging.
 * @param tensor_info Information about the tensor to be created.
 * @param frame Video frame to be used as input data.
 * @return Created ONNX Runtime tensor.
 */
Ort::Value create_input_tensor(AppLogLevel log_level, const InferTensorInfo& tensor_info, VideoFrame& frame) {
  size_t element_count = tensor_info.meta.size;
  vector<int64_t> shape = tensor_info.meta.shape_i64;
  ONNXTensorElementDataType data_type = tensor_info.meta.type;
  float32_t* float_data = nullptr;
  VideoFrameMapInfo map_info;

  Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  try {
    map_info = frame.map(DataMapFlags::READ);
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Failed to map memory : %s", e.what());
    throw std::runtime_error(e.what());
  }

  {
    if ((map_info.fmt == VideoFormat::RGBP_FLOAT || map_info.fmt == VideoFormat::RGB_FLOAT ||
         map_info.fmt == VideoFormat::BGR_FLOAT) &&
        data_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
      APP_LOG(AppLogLevel::ERROR, log_level,
              "Video Format data type is not compatible with Input tensor "
              "data_type");
      throw std::runtime_error(
          "Video Format data type is not compatible"
          "with Input tensor data_type");
    }
    if ((map_info.fmt == VideoFormat::RGBP || map_info.fmt == VideoFormat::RGB || map_info.fmt == VideoFormat::BGR) &&
        data_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8) {
      APP_LOG(AppLogLevel::ERROR, log_level,
              "Video Format data type is not compatible with Input tensor "
              "data_type");
      throw std::runtime_error(
          "Video Format data type is not compatible"
          "with Input tensor data_type");
    }

    if (map_info.fmt == VideoFormat::RGBP_FLOAT && tensor_info.meta.memory_layout == MemoryLayout::NCHW) {
      /* Formats with float data that directly match the tensor's format */
      float_data = reinterpret_cast<float32_t*>(map_info.planes[0].data);
    } else if ((map_info.fmt == VideoFormat::RGB_FLOAT || map_info.fmt == VideoFormat::BGR_FLOAT) &&
               tensor_info.meta.memory_layout == MemoryLayout::NHWC) {
      /* Formats with float data that directly match the tensor's format */
      float_data = reinterpret_cast<float32_t*>(map_info.planes[0].data);
    } else {
      // TODO get string form of mapinfo.fmt
      APP_LOG(AppLogLevel::ERROR, log_level, "Video Format %d is not compatible with shape format %s",
              static_cast<int>(map_info.fmt), get_memory_layout_string(tensor_info.meta.memory_layout).c_str());
      std::stringstream ss;
      ss << "Video Format " << static_cast<int>(map_info.fmt) << " is not compatible with shape format "
         << get_memory_layout_string(tensor_info.meta.memory_layout);
      throw std::runtime_error(ss.str());
    }
  }
  /* Create the tensor */
  Ort::Value tensor = Ort::Value::CreateTensor(memory_info, float_data, element_count, shape.data(), shape.size());

  frame.unmap();
  return tensor;
}

/* Helper function to convert shape vector to string (e.g., "1x3x224x224") */
static string vector_to_string(const std::vector<uint32_t>& vec) {
  ostringstream oss;
  for (size_t i = 0; i < vec.size(); ++i) {
    oss << vec[i];
    if (i != vec.size() - 1) {
      oss << "x";
    }
  }
  return oss.str();
}

/* Convert DataType to string for filename */
static string get_data_type_string(const DataType& data_type) {
  switch (data_type) {
    case DataType::INT8:
      return "int8";
    case DataType::UINT8:
      return "uint8";
    case DataType::INT16:
      return "int16";
    case DataType::UINT16:
      return "uint16";
    case DataType::BF16:
      return "bf16";
    case DataType::FP16:
      return "fp16";
    case DataType::INT32:
      return "int32";
    case DataType::UINT32:
      return "uint32";
    case DataType::FLOAT32:
      return "fp32";
    case DataType::INT64:
      return "int64";
    case DataType::UINT64:
      return "uint64";
    default:
      return "unknown";
  }
}

/*
 * Get element size in bytes for ONNX tensor element type
 */
static size_t get_onnx_element_size(ONNXTensorElementDataType element_type) {
  switch (element_type) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
      return sizeof(float32_t);
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE:
      return sizeof(double);
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:
      return sizeof(int8_t);
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16:
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16:
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16:
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16:
      return sizeof(int16_t);
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32:
      return sizeof(int32_t);
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64:
      return sizeof(int64_t);
    default:
      return 0;
  }
}

/**
 * @brief Dump tensor values to a binary file for debugging (supports any data type).
 *        Uses naming convention consistent with x_plus_ml_vart app.
 *        File is truncated on first frame, appended on subsequent frames.
 * @param tensor The ONNX Runtime tensor to dump.
 * @param tensor_info The tensor metadata (name, shape, data type).
 * @param tensor_index Index of the tensor (for multi-tensor models).
 * @param instance_id Inference instance ID (pipeline ID) for filename.
 * @param output_dir Directory path to write output files.
 * @param frame_index Index of the current frame (0 = first frame).
 * @param iteration_number Current iteration number for multi-iteration runs.
 * @param max_iterations Maximum number of iterations (for filename formatting).
 * @param current_batch_size Number of valid frames in the current batch (excludes padding).
 * @param is_input True if dumping input tensor, false for output tensor.
 * @param log_level Application log level for logging.
 */
void dump_tensor_to_file(const Ort::Value& tensor,
                         const InferTensorInfo& tensor_info,
                         size_t tensor_index,
                         int instance_id,
                         const std::string& output_dir,
                         int frame_index,
                         int64_t iteration_number,
                         int64_t max_iterations,
                         uint32_t current_batch_size,
                         bool is_input,
                         AppLogLevel log_level) {
  auto type_info = tensor.GetTensorTypeAndShapeInfo();
  auto element_type = type_info.GetElementType();

  /* Get shape and compute per-frame element count - handle dynamic dimensions by treating <=0 as 1 */
  auto shape = type_info.GetShape();
  int64_t tensor_batch_size = (shape.size() > 0) ? shape[0] : 1;
  if (tensor_batch_size <= 0)
    tensor_batch_size = 1;

  size_t element_count = 1;
  for (auto& dim : shape) {
    if (dim <= 0)
      dim = 1;
    element_count *= dim;
  }

  size_t element_size = get_onnx_element_size(element_type);
  if (element_size == 0) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Unsupported tensor element type: %d", static_cast<int>(element_type));
    return;
  }

  const size_t elements_per_frame = element_count / static_cast<size_t>(tensor_batch_size);
  const size_t size_in_bytes = elements_per_frame * current_batch_size * element_size;
  const void* raw_data = tensor.GetTensorRawData();

  /* Build filename matching x_plus_ml_vart naming convention */
  string safe_name = tensor_info.meta.name;

  /* Replace '/' with '-' */
  replace(safe_name.begin(), safe_name.end(), '/', '-');

  /* Remove leading '-' if present */
  if (!safe_name.empty() && safe_name.front() == '-') {
    safe_name.erase(0, 1);
  }

  string file_name;
  string infer_prefix = "infer" + to_string(instance_id);

  if (is_input) {
    /* Input tensor naming: infer{N}_input-{tensor_name}.bin */
    if (max_iterations > 1) {
      file_name =
          output_dir + "/iter" + to_string(iteration_number) + "_" + infer_prefix + "_input-" + safe_name + ".bin";
    } else {
      file_name = output_dir + "/" + infer_prefix + "_input-" + safe_name + ".bin";
    }
  } else {
    /* Output tensor naming: infer{N}_out{i}-{data_type}_{shape}_{tensor_name}.bin */
    string append_str =
        get_data_type_string(tensor_info.meta.data_type) + "_" + vector_to_string(tensor_info.meta.shape) + "_";

    if (max_iterations > 1) {
      file_name = output_dir + "/iter" + to_string(iteration_number) + "_" + infer_prefix + "_out" +
                  to_string(tensor_index) + "-" + append_str + safe_name + ".bin";
    } else {
      file_name =
          output_dir + "/" + infer_prefix + "_out" + to_string(tensor_index) + "-" + append_str + safe_name + ".bin";
    }
  }

  /* Create/truncate file on first frame, append on subsequent frames */
  ios_base::openmode mode = ((frame_index == 0) ? (ios::binary | ios::trunc) : (ios::binary | ios::app));
  std::ofstream outfile(file_name, mode);

  if (!outfile.is_open()) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Failed to open file: %s", file_name.c_str());
    return;
  }

  outfile.write(reinterpret_cast<const char*>(raw_data), size_in_bytes);

  if (!outfile.good()) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Error writing to file: %s", file_name.c_str());
  }
  outfile.close();

  APP_LOG(AppLogLevel::DEBUG, log_level, "Dumped %s tensor %zu frame %d to %s", is_input ? "input" : "output",
          tensor_index, frame_index, file_name.c_str());
}

enum class device_execution_provider { cpu, vitisai, unknown };

/**
 * @brief Converts a string to device_execution_provider enum (case-insensitive).
 * @param mode Execution provider as a string (e.g., "CPU", "VitisAI").
 * @return Corresponding "device_execution_provider" enum value, or unknown if not recognized.
 */
device_execution_provider get_execution_provider(const std::string& mode) {
  static const std::unordered_map<std::string, device_execution_provider> mode_map = {
      {"CPU", device_execution_provider::cpu}, {"VITISAI", device_execution_provider::vitisai}};
  auto it = mode_map.find(boost::algorithm::to_upper_copy(mode));
  return (it != mode_map.end()) ? it->second : device_execution_provider::unknown;
}

/**
 * @brief Configures the ONNX Runtime session to use the Vitis AI Execution Provider.
 *
 * This helper function sets up the session options for the Vitis AI execution provider
 * by extracting required and optional configuration parameters from the provided property tree.
 * It appends the Vitis AI execution provider to the ONNX Runtime session options with the
 * specified options.
 *
 * @param session_options Reference to the ONNX Runtime session options to configure.
 * @param config Property tree containing the inference configuration parameters.
 * @param log_level Application log level for logging.
 *
 * Required configuration keys under "inference-config.execution-provider-options":
 *   - config_file: Path to the Vitis AI configuration file.
 *   - cache_dir: Directory for caching compiled models.
 *   - target: Target device for execution.
 *   - cache_key: Key for cache identification.
 *
 * Optional configuration keys:
 *   - encryption_key
 *   - ai_analyzer_visualization
 *   - ai_analyzer_profiling
 *
 * Logs the usage of the Vitis AI Execution Provider.
 */
static void setup_vitis_ai_execution_provider(Ort::SessionOptions& session_options,
                                              const pt::ptree& config,
                                              AppLogLevel log_level) {
  try {
    auto options = std::unordered_map<std::string, std::string>{};
    options["config_file"] = config.get<std::string>("inference-config.execution-provider-options.config_file");
    options["cache_dir"] = config.get<std::string>("inference-config.execution-provider-options.cache_dir");
    options["target"] = config.get<std::string>("inference-config.execution-provider-options.target");
    options["cache_key"] = config.get<std::string>("inference-config.execution-provider-options.cache_key");

    // Parse optional options
    std::vector<std::pair<std::string, std::string>> option_keys = {
        {"encryption_key", "inference-config.execution-provider-options.encryption_key"},
        {"ai_analyzer_visualization", "inference-config.execution-provider-options.ai_analyzer_visualization"},
        {"ai_analyzer_profiling", "inference-config.execution-provider-options.ai_analyzer_profiling"}};

    for (const auto& [option_name, config_key] : option_keys) {
      if (auto opt = config.get_optional<std::string>(config_key)) {
        options[option_name] = *opt;
      }
    }
    session_options.AppendExecutionProvider_VitisAI(options);
    APP_LOG(AppLogLevel::INFO, log_level, "Using Vitis AI Execution Provider (VitisAI)");
  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Failed to setup Vitis AI Execution Provider: %s", e.what());
    throw;  // Rethrow to allow caller to handle the error
  }
}

/**
 * @brief Initializes the ONNX Runtime inference context for a pipeline.
 *
 * This function sets up the ONNX Runtime environment and session for the given pipeline,
 * configures the execution provider (CPU or Vitis AI) based on the JSON configuration,
 * and prepares input and output tensor metadata.
 *
 * The function only supports models with a single input tensor, a single output tensor,
 * and batch size of 1. If the model does not meet these constraints, initialization fails.
 *
 * @param pipeline_ctx Pointer to the pipeline context to initialize.
 * @param log_level Application log level for logging.
 * @param json_str JSON configuration string specifying model and execution provider options.
 * @return true if initialization is successful, false otherwise.
 */
bool create_inference_context(PipelineContext* pipeline_ctx,
                              AppLogLevel log_level,
                              const string& json_str,
                              Ort::Env* ort_env) {
  InferModelConf* model_info = &pipeline_ctx->model_info;

  pt::ptree config;
  istringstream iss(json_str);
  pt::read_json(iss, config);

  /* Initialize ONNX Runtime session.*/
  try {
    std::basic_string<ORTCHAR_T> model_file = pipeline_ctx->model_path;

    /* Read execution mode from JSON, default to "VitisAI"*/
    std::string execution_provider = "VitisAI";
    if (auto opt = config.get_optional<std::string>("inference-config.execution-provider")) {
      execution_provider = *opt;
    }

    /* Determine the execution mode.*/
    device_execution_provider current_execution_provider = get_execution_provider(execution_provider);

    Ort::SessionOptions session_options;

    switch (current_execution_provider) {
      case device_execution_provider::cpu:
        APP_LOG(AppLogLevel::INFO, log_level, "Using CPU Execution Provider");
        break;
      case device_execution_provider::vitisai:
        APP_LOG(AppLogLevel::INFO, log_level, "Using VitisAI Execution Provider");
        setup_vitis_ai_execution_provider(session_options, config, log_level);

        /* Store VitisAI config file path for batch processing */
        pipeline_ctx->vitisai_config_file_path =
            config.get<std::string>("inference-config.execution-provider-options.config_file");
        break;
      default:
        std::string err_msg = "Unknown execution provider: " + execution_provider + ". Valid providers: CPU, VitisAI";
        APP_LOG(AppLogLevel::ERROR, log_level, "%s", err_msg.c_str());
        throw std::runtime_error(err_msg);
    }

    /* Create an Onnx Session and store it */
    pipeline_ctx->ort_session = std::make_unique<Ort::Session>(*ort_env, model_file.c_str(), session_options);
    Ort::Session* ort_session = pipeline_ctx->ort_session.get();
    if (!ort_session) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Failed to create ONNX Runtime session");
      return false;
    }

    /* Configure Input Tensors */
    model_info->num_in_tensors = ort_session->GetInputCount();

    APP_LOG(AppLogLevel::INFO, log_level, "Num of input tensors %ld", model_info->num_in_tensors);

    if (model_info->num_in_tensors > 1) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Num of input tensors %ld .", model_info->num_in_tensors);
      APP_LOG(AppLogLevel::ERROR, log_level, "Not supported for more than 1 input tensor.");
      delete_inference_context(pipeline_ctx);
      return false;
    }

    /* Read device_batch_size from VitisAI config once upfront (used for dynamic batch resolution in both input and
     * output tensors) */
    int64_t device_batch_size = 1;  // Default value
    if (!pipeline_ctx->vitisai_config_file_path.empty()) {
      device_batch_size = read_device_batch_size_from_config(pipeline_ctx->vitisai_config_file_path, log_level);
    }

    model_info->in_tensors_info.resize(model_info->num_in_tensors);
    model_info->input_names.resize(model_info->num_in_tensors);
    for (size_t i = 0; i < model_info->num_in_tensors; i++) {
      model_info->in_tensors_info[i].meta.name = ort_session->GetInputNameAllocated(i, model_info->allocator).get();
      model_info->input_names[i] = model_info->in_tensors_info[i].meta.name.c_str();
      auto type_info = ort_session->GetInputTypeInfo(i);
      auto ort_tensor_info = type_info.GetTensorTypeAndShapeInfo();
      auto shape = ort_tensor_info.GetShape();

      /* Print input shape from model (before any modifications) */
      std::ostringstream shape_before_oss;
      shape_before_oss << "Input shape from model: [";
      for (size_t j = 0; j < shape.size(); ++j) {
        shape_before_oss << shape[j];
        if (j < shape.size() - 1)
          shape_before_oss << ", ";
      }
      shape_before_oss << "]";
      APP_LOG(AppLogLevel::INFO, log_level, "%s", shape_before_oss.str().c_str());

      /* Store original model batch size before modification */
      int64_t model_batch_size = shape[0];
      APP_LOG(AppLogLevel::DEBUG, log_level, "Original model batch size before modification: %ld", model_batch_size);

      /* Use device_batch_size if model has dynamic batch */
      if (model_batch_size <= 0) {
        model_batch_size = device_batch_size;
        APP_LOG(AppLogLevel::INFO, log_level, "Using device_batch_size from config: %ld", device_batch_size);
      }

      /* Set dynamic batch dimensions */
      size_t size = 1;
      for (auto& dim : shape) {
        if (dim <= 0)
          dim = model_batch_size;
        size *= dim;
      }

      /* Print input shape after setting dynamic batch dimensions */
      std::ostringstream shape_after_oss;
      shape_after_oss << "Input shape after setting dynamic batch dimensions: [";
      for (size_t j = 0; j < shape.size(); ++j) {
        shape_after_oss << shape[j];
        if (j < shape.size() - 1)
          shape_after_oss << ", ";
      }
      shape_after_oss << "]";
      APP_LOG(AppLogLevel::INFO, log_level, "%s", shape_after_oss.str().c_str());

      model_info->in_tensors_info[i].meta.shape_i64 = shape;

      /* Store shape dimensions as uint32_t in shape for post-processing */
      std::vector<uint32_t> shape_u32(shape.size());
      std::transform(shape.begin(), shape.end(), shape_u32.begin(), [](int64_t v) { return static_cast<uint32_t>(v); });
      model_info->in_tensors_info[i].meta.shape = std::move(shape_u32);
      model_info->in_tensors_info[i].meta.size = size;
      model_info->in_tensors_info[i].meta.type = ort_tensor_info.GetElementType();
      model_info->in_tensors_info[i].meta.data_type = map_onnx_data_type(model_info->in_tensors_info[i].meta.type);
      model_info->in_tensors_info[i].meta.size_in_bytes = get_tensor_size_in_bytes(ort_tensor_info, model_batch_size);
      model_info->in_tensors_info[i].quantization_factor = DEFAULT_QUANT_FACTOR;
    }

    if (model_info->in_tensors_info[0].meta.memory_layout == MemoryLayout::NCHW) {
      model_info->batch_size = model_info->in_tensors_info[0].meta.shape_i64[0];    // N
      model_info->channels = model_info->in_tensors_info[0].meta.shape_i64[1];      // C
      model_info->model_height = model_info->in_tensors_info[0].meta.shape_i64[2];  // H
      model_info->model_width = model_info->in_tensors_info[0].meta.shape_i64[3];   // W
    } else if (model_info->in_tensors_info[0].meta.memory_layout == MemoryLayout::NHWC) {
      model_info->batch_size = model_info->in_tensors_info[0].meta.shape_i64[0];    // N
      model_info->model_height = model_info->in_tensors_info[0].meta.shape_i64[1];  // H
      model_info->model_width = model_info->in_tensors_info[0].meta.shape_i64[2];   // W
      model_info->channels = model_info->in_tensors_info[0].meta.shape_i64[3];      // C
    } else if (model_info->in_tensors_info[0].meta.memory_layout == MemoryLayout::NHW) {
      model_info->batch_size = model_info->in_tensors_info[0].meta.shape_i64[0];    // N
      model_info->model_height = model_info->in_tensors_info[0].meta.shape_i64[1];  // H
      model_info->model_width = model_info->in_tensors_info[0].meta.shape_i64[2];   // W
      model_info->channels = 1;                                                     // Default 1
    } else {
      APP_LOG(AppLogLevel::ERROR, log_level, "Unsupported memory_layout %s.",
              get_memory_layout_string(model_info->in_tensors_info[0].meta.memory_layout).c_str());
    }

    APP_LOG(AppLogLevel::INFO, log_level, "Model batch size: %ld", model_info->batch_size);

    /* Debug log when device_batch_size differs from resolved model batch size */
    if (device_batch_size != model_info->batch_size) {
      APP_LOG(AppLogLevel::DEBUG, log_level,
              "Device batch size (%ld) differs from model batch size (%ld) - using model batch size", device_batch_size,
              model_info->batch_size);
    }

    /* Configure Output Tensors */
    model_info->num_out_tensors = ort_session->GetOutputCount();

    APP_LOG(AppLogLevel::INFO, log_level, "Num of output tensors %ld", model_info->num_out_tensors);

    if (model_info->num_out_tensors > 1) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Num of output tensors %ld .", model_info->num_out_tensors);
      APP_LOG(AppLogLevel::ERROR, log_level, "Not supported for more than 1 output tensor.");
      delete_inference_context(pipeline_ctx);
      return false;
    }

    model_info->out_tensors_info.resize(model_info->num_out_tensors);
    model_info->output_names.resize(model_info->num_out_tensors);
    for (size_t i = 0; i < model_info->num_out_tensors; i++) {
      model_info->out_tensors_info[i].meta.name = ort_session->GetOutputNameAllocated(i, model_info->allocator).get();
      model_info->output_names[i] = model_info->out_tensors_info[i].meta.name.c_str();
      auto type_info = ort_session->GetOutputTypeInfo(i);
      auto ort_tensor_info = type_info.GetTensorTypeAndShapeInfo();
      auto shape = ort_tensor_info.GetShape();

      /* Print output shape for debugging */
      APP_LOG(AppLogLevel::DEBUG, log_level, "Output tensor %ld shape: [", i);
      for (size_t j = 0; j < shape.size(); ++j) {
        APP_LOG(AppLogLevel::DEBUG, log_level, "%ld%s", shape[j], (j < shape.size() - 1) ? ", " : "");
      }
      APP_LOG(AppLogLevel::DEBUG, log_level, "]");

      /* Store original model batch size before modification */
      int64_t model_batch_size = (shape.size() > 0) ? shape[0] : 1;
      APP_LOG(AppLogLevel::DEBUG, log_level, "Original output batch size before modification: %ld", model_batch_size);

      /* Use device_batch_size if model has dynamic batch */
      if (model_batch_size <= 0) {
        model_batch_size = device_batch_size;
        APP_LOG(AppLogLevel::INFO, log_level, "Using device_batch_size from config for output: %ld", device_batch_size);
      }

      /* Set dynamic batch dimensions for output tensors */
      size_t size = 1;
      for (auto& dim : shape) {
        if (dim <= 0)
          dim = model_batch_size;
        size *= dim;
      }

      /* Print output shape after setting dynamic batch dimensions */
      APP_LOG(AppLogLevel::DEBUG, log_level, "Output shape after setting dynamic batch dimensions: [");
      for (size_t j = 0; j < shape.size(); ++j) {
        APP_LOG(AppLogLevel::DEBUG, log_level, "%ld%s", shape[j], (j < shape.size() - 1) ? ", " : "");
      }
      APP_LOG(AppLogLevel::DEBUG, log_level, "]");

      model_info->out_tensors_info[i].meta.shape_i64 = shape;

      /* Store shape dimensions as uint32_t in shape for post-processing */
      std::vector<uint32_t> shape_u32(shape.size());
      std::transform(shape.begin(), shape.end(), shape_u32.begin(), [](int64_t v) { return static_cast<uint32_t>(v); });
      model_info->out_tensors_info[i].meta.shape = std::move(shape_u32);
      model_info->out_tensors_info[i].meta.size = size;
      model_info->out_tensors_info[i].meta.type = ort_tensor_info.GetElementType();
      model_info->out_tensors_info[i].meta.data_type = map_onnx_data_type(model_info->out_tensors_info[i].meta.type);
      model_info->out_tensors_info[i].meta.size_in_bytes = get_tensor_size_in_bytes(ort_tensor_info, model_batch_size);
      /* TODO create a tensor in runtime wrapped with data required by postprocessing */
      auto tensor = Ort::Value::CreateTensor(
          model_info->allocator, model_info->out_tensors_info[i].meta.shape_i64.data(),
          model_info->out_tensors_info[i].meta.shape_i64.size(), model_info->out_tensors_info[i].meta.type);
      model_info->output_tensors.emplace_back(std::move(tensor));
    }

  } catch (const Ort::Exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "ONNX Runtime error: %s", e.what());
    delete_inference_context(pipeline_ctx);
    return false;
  }

  return true;
}

/**
 * @brief Perform inference on a batch of video frames and populate output
 * tensors.
 * @param ctx Pointer to the application context.
 * @param current_batch_size Number of valid frames in the current batch.
 * @param inputs Vector of shared pointers to input video frames.
 * @param inference_out_tensors_memory Reference to a vector of vectors of
 * shared pointers to output memory buffers for NPU output tensors.
 * @return True if inference is successful, false otherwise.
 */

bool infer_process_frames(PipelineContext* pipeline_ctx,
                          AppLogLevel log_level,
                          uint32_t current_batch_size,
                          vector<shared_ptr<VideoFrame>> inputs,
                          vector<vector<shared_ptr<vart::Memory>>>& inference_out_tensors_memory,
                          int frame_index,
                          int64_t iteration_number,
                          int64_t max_iterations) {
  /* Note: inference_out_tensors_memory is used in the output copying section */
  InferModelConf* model_info = &pipeline_ctx->model_info;

  APP_LOG(AppLogLevel::DEBUG, log_level, "input batch_size %d", current_batch_size);

  /* Clear input tensors from previous call */
  model_info->input_tensors.clear();

  /* Enhanced batch processing - support any batch size */
  if (current_batch_size == 0) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Invalid batch size: 0");
    return false;
  }

  /* Multiple input_tensors not supported */
  if (model_info->num_in_tensors > 1) {
    APP_LOG(AppLogLevel::ERROR, log_level, "More than 1 input_tensor is not supported");
    delete_inference_context(pipeline_ctx);
    return false;
  }

  /* Handle partial batches for VitisAI - pad to model batch size
   * Note: padding duplicates the last valid frame to keep the input tensor fully initialized. */
  uint32_t padded_batch_size = current_batch_size;
  vector<shared_ptr<VideoFrame>> padded_inputs = inputs;
  padded_inputs.resize(
      current_batch_size);  // Ensure padded_inputs has the same number of elements as current_batch_size

  if (current_batch_size < static_cast<uint32_t>(model_info->batch_size)) {
    padded_batch_size = static_cast<uint32_t>(model_info->batch_size);
    APP_LOG(AppLogLevel::INFO, log_level, "Partial batch detected: %d frames. Padding to model batch size: %d",
            current_batch_size, padded_batch_size);

    /* Pad with duplicates of the last valid frame */
    for (uint32_t i = current_batch_size; i < padded_batch_size; i++) {
      padded_inputs.push_back(inputs[current_batch_size - 1]);
    }
  }

  /* Use padded_batch_size for input shape so the runtime sees a full batch */
  for (size_t i = 0; i < model_info->num_in_tensors; i++) {
    try {
      /* Update shape for padded batch size */
      vector<int64_t> batch_shape = model_info->in_tensors_info[i].meta.shape_i64;
      batch_shape[0] = padded_batch_size;

      APP_LOG(AppLogLevel::DEBUG, log_level, "Creating input tensor for %d frames (padded from %d)", padded_batch_size,
              current_batch_size);

      Ort::Value batched_tensor =
          create_batched_input_tensor(log_level, model_info->in_tensors_info[i], padded_inputs, 0, /* start_idx */
                                      padded_batch_size, batch_shape);

      model_info->input_tensors.emplace_back(std::move(batched_tensor));
    } catch (const std::exception& e) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Could not create input tensor %ld. Reason %s", i, e.what());
      return false;
    }
  }

  /* Recreate output tensors with padded_batch_size so outputs align with the padded input */
  model_info->output_tensors.clear();
  for (size_t i = 0; i < model_info->num_out_tensors; i++) {
    vector<int64_t> out_shape = model_info->out_tensors_info[i].meta.shape_i64;
    out_shape[0] = padded_batch_size; /* Match input batch size */
    auto tensor = Ort::Value::CreateTensor(model_info->allocator, out_shape.data(), out_shape.size(),
                                           model_info->out_tensors_info[i].meta.type);
    model_info->output_tensors.emplace_back(std::move(tensor));
  }

  try {
    auto start = std::chrono::high_resolution_clock::now();
    /* Run the model */
    pipeline_ctx->ort_session->Run(Ort::RunOptions{nullptr}, model_info->input_names.data(),
                                   model_info->input_tensors.data(), model_info->num_in_tensors,
                                   model_info->output_names.data(), model_info->output_tensors.data(),
                                   model_info->num_out_tensors);
    auto end = std::chrono::high_resolution_clock::now();
    if (pipeline_ctx->is_benchmark_enabled) {
      pipeline_ctx->total_infer_time =
          pipeline_ctx->total_infer_time + std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    }
  } catch (const Ort::Exception& exception) {
    std::cout << "ERROR running model inference: " << exception.what() << std::endl;
    return false;
  }

  auto output_type_info = model_info->output_tensors[0].GetTensorTypeAndShapeInfo();
  bool is_output_fp16 = (output_type_info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16);

  /* Copy tensor data to vart memory - distribute to individual frame memories
   * Only copy outputs for the original current_batch_size frames; outputs for padded frames are ignored. */
  for (size_t frame_idx = 0; frame_idx < current_batch_size; frame_idx++) {
    for (size_t tensor_idx = 0; tensor_idx < model_info->num_out_tensors; tensor_idx++) {
      /* Calculate offset for this frame in the batched output */
      size_t elements_per_frame = model_info->out_tensors_info[tensor_idx].meta.size / model_info->batch_size;
      size_t offset = frame_idx * elements_per_frame;
      const size_t bytes_to_copy = elements_per_frame * (is_output_fp16 ? sizeof(uint16_t) : sizeof(float32_t));

      APP_LOG(AppLogLevel::DEBUG, log_level, "Copying output tensor %ld frame %ld: %ld elements from offset %ld",
              tensor_idx, frame_idx, elements_per_frame, offset);

      // Map output memory
      void* mapped_memory;
      try {
        const void* const_mapped = inference_out_tensors_memory[frame_idx][tensor_idx]->map(DataMapFlags::WRITE);
        mapped_memory = const_cast<void*>(const_mapped);
      } catch (const exception& e) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Failed to map memory: %s", e.what());
        throw;
      }

      // Get source data pointer
      void* tensor_data =
          is_output_fp16
              ? static_cast<void*>(model_info->output_tensors[tensor_idx].GetTensorMutableData<uint16_t>() + offset)
              : static_cast<void*>(model_info->output_tensors[tensor_idx].GetTensorMutableData<float32_t>() + offset);

      // Copy data
      std::memcpy(mapped_memory, tensor_data, bytes_to_copy);

      // Unmap memory
      inference_out_tensors_memory[frame_idx][tensor_idx]->unmap();
    }
  }

  /* Dump tensors if required */
  if (pipeline_ctx->dump_all_inputs) {
    for (size_t i = 0; i < model_info->num_in_tensors; i++) {
      dump_tensor_to_file(model_info->input_tensors[i], model_info->in_tensors_info[i], i, pipeline_ctx->pipeline_id,
                          pipeline_ctx->output_dir_path, frame_index, iteration_number, max_iterations,
                          current_batch_size, true, /* is_input */
                          log_level);
    }
    for (size_t i = 0; i < model_info->num_out_tensors; i++) {
      dump_tensor_to_file(model_info->output_tensors[i], model_info->out_tensors_info[i], i, pipeline_ctx->pipeline_id,
                          pipeline_ctx->output_dir_path, frame_index, iteration_number, max_iterations,
                          current_batch_size, false, /* is_input */
                          log_level);
    }
  }

  auto input_type_info = model_info->input_tensors[0].GetTensorTypeAndShapeInfo();
  bool is_input_fp16 = (input_type_info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16);
  /* Free the memory for input tensors created with malloc in create_batched_input_tensor */
  for (size_t i = 0; i < model_info->num_in_tensors; i++) {
    void* data_ptr = is_input_fp16 ? static_cast<void*>(model_info->input_tensors[i].GetTensorMutableData<uint16_t>())
                                   : static_cast<void*>(model_info->input_tensors[i].GetTensorMutableData<float32_t>());

    if (data_ptr) {
      APP_LOG(AppLogLevel::DEBUG, log_level, "Freeing input tensor memory for tensor %ld", i);
      free(data_ptr);
    }
  }

  model_info->input_tensors.clear();

  return true;
}
