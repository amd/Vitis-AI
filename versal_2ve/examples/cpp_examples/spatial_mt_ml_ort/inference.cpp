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
 * @file inference.cpp
 * @brief This file contains the implementation of inference functions.
 *
 */

#include "x_plus_ml_app.hpp"

using namespace vart;

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
 * @return Size of the tensor in bytes.
 */
size_t get_tensor_size_in_bytes(Ort::ConstTensorTypeAndShapeInfo ort_tensor_info) {
  auto element_type = ort_tensor_info.GetElementType();
  size_t size_in_bytes = 0;
  auto shape = ort_tensor_info.GetShape();

  /* Change dynamic batch (-1) to 1 and calculate input size*/
  size_t size = 1;
  for (auto& dim : shape) {
    if (dim <= 0)
      dim = 1;
    size *= dim;
  }

  switch (element_type) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
      size_in_bytes = size * sizeof(float);
      break;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:
      size_in_bytes = size * sizeof(int8_t);
      break;
    // TODO Handle other formats as well
    default:
      break;
  }
  return size_in_bytes;
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
      return sizeof(float);
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
                         bool is_input,
                         AppLogLevel log_level) {
  auto type_info = tensor.GetTensorTypeAndShapeInfo();
  auto element_type = type_info.GetElementType();

  /* Get shape and compute element count - handle dynamic dimensions by treating <=0 as 1 */
  auto shape = type_info.GetShape();
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

  size_t size_in_bytes = element_count * element_size;
  const void* raw_data = tensor.GetTensorRawData();

  string safe_name = tensor_info.meta.name;
  replace(safe_name.begin(), safe_name.end(), '/', '-');
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

/**
 * @brief Create an inference context for ONNX Runtime.
 * @param pipeline_ctx Pointer to the pipeline context.
 * @param log_level Application log level for logging.
 * @param json_str JSON configuration string.
 * @return true if successful, false otherwise.
 */
bool create_inference_context(PipelineContext* pipeline_ctx,
                              AppLogLevel log_level,
                              const string& json_str,
                              Ort::Env* ort_env) {
  InferModelConf* model_info = &pipeline_ctx->model_info;

  pt::ptree config;
  istringstream iss(json_str);
  pt::read_json(iss, config);

  try {
    std::basic_string<ORTCHAR_T> model_file = pipeline_ctx->model_path;

    /* Parse Vitis AI specific options */
    Ort::SessionOptions session_options;
    auto options = std::unordered_map<std::string, std::string>{};
    options["config_file"] = config.get<std::string>("inference-config.execution-provider-options.config_file");
    options["cache_dir"] = config.get<std::string>("inference-config.execution-provider-options.cache_dir");
    options["target"] = config.get<std::string>("inference-config.execution-provider-options.target");
    options["cache_key"] = config.get<std::string>("inference-config.execution-provider-options.cache_key");
    // Parse optional options
    std::vector<std::pair<std::string, std::string>> option_keys = {
        {"encryption_key", "inference-config.execution-provider-options.encryption_key"},
        {"ai_analyzer_visualization",
         "inference-config.execution-provider-options.ai_analyzer_"
         "visualization"},
        {"ai_analyzer_profiling", "inference-config.execution-provider-options.ai_analyzer_profiling"}};

    for (const auto& [option_name, config_key] : option_keys) {
      if (auto opt = config.get_optional<std::string>(config_key)) {
        options[option_name] = *opt;
      }
    }
    session_options.AppendExecutionProvider_VitisAI(options);
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
    model_info->in_tensors_info.resize(model_info->num_in_tensors);
    model_info->input_names.resize(model_info->num_in_tensors);
    for (size_t i = 0; i < model_info->num_in_tensors; i++) {
      model_info->in_tensors_info[i].meta.name = ort_session->GetInputNameAllocated(i, model_info->allocator).get();
      model_info->input_names[i] = model_info->in_tensors_info[i].meta.name.c_str();
      auto type_info = ort_session->GetInputTypeInfo(i);
      auto ort_tensor_info = type_info.GetTensorTypeAndShapeInfo();
      auto shape = ort_tensor_info.GetShape();
      /* Change dynamic batch (-1) to 1 and calculate input size*/
      size_t size = 1;
      for (auto& dim : shape) {
        if (dim <= 0)
          dim = 1;
        size *= dim;
      }

      model_info->in_tensors_info[i].meta.shape_i64 = shape;

      /* Store shape dimensions as uint32_t in shape for post-processing */
      std::vector<uint32_t> shape_u32(shape.size());
      std::transform(shape.begin(), shape.end(), shape_u32.begin(), [](int64_t v) { return static_cast<uint32_t>(v); });
      model_info->in_tensors_info[i].meta.shape = std::move(shape_u32);
      model_info->in_tensors_info[i].meta.size = size;
      model_info->in_tensors_info[i].meta.type = ort_tensor_info.GetElementType();
      model_info->in_tensors_info[i].meta.data_type = map_onnx_data_type(model_info->in_tensors_info[i].meta.type);
      model_info->in_tensors_info[i].meta.size_in_bytes = get_tensor_size_in_bytes(ort_tensor_info);
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

    if (model_info->batch_size > 1) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Batch size is %ld", model_info->batch_size);
      APP_LOG(AppLogLevel::ERROR, log_level, "Not supported for more than batch size 1.");
      delete_inference_context(pipeline_ctx);
      return false;
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
      /* Change dynamic batch (-1) to 1 */
      size_t size = 1;
      for (auto& dim : shape) {
        if (dim <= 0)
          dim = 1;
        size *= dim;
      }
      model_info->out_tensors_info[i].meta.shape_i64 = shape;

      /* Store shape dimensions as uint32_t in shape for post-processing */
      std::vector<uint32_t> shape_u32(shape.size());
      std::transform(shape.begin(), shape.end(), shape_u32.begin(), [](int64_t v) { return static_cast<uint32_t>(v); });
      model_info->out_tensors_info[i].meta.shape = std::move(shape_u32);
      model_info->out_tensors_info[i].meta.size = size;
      model_info->out_tensors_info[i].meta.type = ort_tensor_info.GetElementType();
      model_info->out_tensors_info[i].meta.data_type = map_onnx_data_type(model_info->out_tensors_info[i].meta.type);
      model_info->out_tensors_info[i].meta.size_in_bytes = get_tensor_size_in_bytes(ort_tensor_info);
      // TODO create a tensor in runtime wrapped with data required by
      // postprocessing
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
  InferModelConf* model_info = &pipeline_ctx->model_info;
  VideoFrameMapInfo map_info;

  APP_LOG(AppLogLevel::DEBUG, log_level, "input batch_size %d", current_batch_size);
  if (current_batch_size > 1) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Batch size more than 1  is not supported");
    delete_inference_context(pipeline_ctx);
    return false;
  }

  /* Multiple input_tensors not supported */
  if (model_info->num_in_tensors > 1) {
    APP_LOG(AppLogLevel::ERROR, log_level, "More than 1 input_tensor is not supported");
    delete_inference_context(pipeline_ctx);
    return false;
  }

  for (size_t i = 0; i < model_info->num_in_tensors; i++) {
    try {
      Ort::Value tensor = create_input_tensor(log_level, model_info->in_tensors_info[i], *inputs[i]);
      model_info->input_tensors.emplace_back(std::move(tensor));

    } catch (const std::exception& e) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Could not create input tensor %ld. Reason %s", i, e.what());
    }
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
    exit(-1);
  }

  /* Copy tesnsor data to vart memory */
  for (size_t i = 0; i < model_info->num_out_tensors; i++) {
    float32_t* mapped_memory;
    try {
      mapped_memory = const_cast<float32_t*>(
          reinterpret_cast<const float32_t*>(inference_out_tensors_memory[0][i]->map(DataMapFlags::WRITE)));
    } catch (const exception& e) {
      APP_LOG(AppLogLevel::ERROR, AppLogLevel::ERROR, "Failed to map memory : %s", e.what());
      throw std::runtime_error(e.what());
    }

    float* tensor_data = model_info->output_tensors[i].GetTensorMutableData<float>();
    for (size_t j = 0; j < model_info->out_tensors_info[i].meta.size; j++) {
      mapped_memory[j] = tensor_data[j];
    }
    inference_out_tensors_memory[0][i]->unmap();
  }

  /* Dump tensors if required */
  if (pipeline_ctx->dump_all_inputs) {
    for (size_t i = 0; i < model_info->num_in_tensors; i++) {
      dump_tensor_to_file(model_info->input_tensors[i], model_info->in_tensors_info[i], i, pipeline_ctx->pipeline_id,
                          pipeline_ctx->output_dir_path, frame_index, iteration_number, max_iterations,
                          true, /* is_input */
                          log_level);
    }
    for (size_t i = 0; i < model_info->num_out_tensors; i++) {
      dump_tensor_to_file(model_info->output_tensors[i], model_info->out_tensors_info[i], i, pipeline_ctx->pipeline_id,
                          pipeline_ctx->output_dir_path, frame_index, iteration_number, max_iterations,
                          false, /* is_input */
                          log_level);
    }
  }

  /* Free the memory if required */
  for (size_t i = 0; i < model_info->num_in_tensors; i++) {
    try {
      map_info = inputs[i]->map(DataMapFlags::READ);
    } catch (const exception& e) {
      APP_LOG(AppLogLevel::ERROR, AppLogLevel::ERROR, "Failed to map memory : %s", e.what());
      throw std::runtime_error(e.what());
    }

    if (map_info.fmt == VideoFormat::RGBP || map_info.fmt == VideoFormat::RGB || map_info.fmt == VideoFormat::BGR) {
      /* Deallocate the malloc memory created during conversion */
      float32_t* data_ptr = model_info->input_tensors[i].GetTensorMutableData<float32_t>();

      if (data_ptr)
        free(data_ptr);
    }
    inputs[i]->unmap();
  }

  return true;
}
