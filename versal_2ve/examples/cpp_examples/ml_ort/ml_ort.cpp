/*
 * Copyright (C) 2025-2026 Advanced Micro Devices, Inc.
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

#include <getopt.h>
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <any>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <unordered_set>
#include <vector>
#include "common/app_utils.hpp"   //Contains utility functions for the app
#include "common/onnx_utils.hpp"  //ONNX-specific utility functions

/* Helper function to read underlying hardware batch size from VitisAI config file.
 * Priority: dp_size, then device_batch_size, else default 1.
 * Only called when ONNX model has dynamic batch size (<= 0). */
int64_t read_device_batch_size_from_config(const std::string& vitisai_config_file) {
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
            } else {
              std::cerr << "Warning: Invalid dp_size (" << *dp_size_opt << "). Using default batch size: 1"
                        << std::endl;
              device_batch_size = 1;
            }
            break;
          }

          auto device_batch_size_opt = vaiml_config->get_optional<int64_t>("device_batch_size");
          if (device_batch_size_opt) {
            if (*device_batch_size_opt > 0) {
              device_batch_size = *device_batch_size_opt;
            } else {
              std::cerr << "Warning: Invalid device_batch_size (" << *device_batch_size_opt
                        << "). Using default batch size: 1" << std::endl;
              device_batch_size = 1;
            }
            break;
          }
        }
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "Warning: Could not read dp_size/device_batch_size from VitisAI config (" << e.what()
              << "), using default: " << device_batch_size << std::endl;
  }
  return device_batch_size;
}

/* Print help text for the command-line options */
namespace {
void print_help_text(const char* pn) {
  std::cout << "Usage: " << pn << " [OPTIONS]" << std::endl;
  std::cout << "  --app-config	Application configuration (mandatory)" << std::endl;
  std::cout << "  --runs         Run the model for n runs (optional)" << std::endl;
  std::cout << "  --benchmark	 benchmark the model for n runs (optional)" << std::endl;
  std::cout << "  --help	Print this help and exit" << std::endl;
  std::cout << pn << " --app-config json_configs/ml_ort_config.json " << std::endl;
}
}  // namespace

/* ===== Main ===== */
/*
 * Enhanced ML ORT Application with Dynamic Batch Processing
 *
 * Features:
 * - Supports any batch size defined by the ONNX model (1, 2, 3, 4, 5, 6, 7, 8, 9, etc.)
 * - Automatically detects model batch size from ONNX metadata
 * - Intelligently handles cases where input images < model batch size
 * - Creates optimal tensors based on available data and model requirements
 * - Accumulates outputs across multiple inference iterations
 *
 * Batch Processing Logic:
 * - Model batch 1: Creates 1-image tensors, calls ONNX once per image
 * - Model batch 6: Creates 6-image tensors, calls ONNX once per 6 images
 * - Model batch 9: Creates 9-image tensors, calls ONNX once per 9 images
 * - If input has fewer images than batch size, creates partial tensors
 *
 * Example scenarios:
 * - 10 images + batch-6 model = 2 iterations (6 + 4 images)
 * - 3 images + batch-6 model = 1 iteration (3 images only)
 * - 18 images + batch-9 model = 2 iterations (9 + 9 images)
 */
int main(int argc, char* argv[]) {
  try {
    std::string model_path;
    std::string input_file;
    std::string output_file;
    std::string app_config;
    uint32_t n_runs = 1;
    bool benchmark = false;
    bool dump = true;
    double total_inference_time_us = 0.0;  // Accumulate session.Run() times

    opterr = 0;
    int32_t opt = 0;
    int32_t option_index = 0;
    const std::array<struct option, 5> long_options = {{{"app-config", required_argument, nullptr, 0},
                                                        {"benchmark", no_argument, nullptr, 1},
                                                        {"runs", required_argument, nullptr, 2},
                                                        {"help", no_argument, nullptr, 3},
                                                        {nullptr, 0, nullptr, 0}}};

    /* Parse command-line options */
    while (true) {
      opt = getopt_long(argc, argv, "-", std::data(long_options), &option_index);
      if (opt == -1) {
        break;
      }
      switch (opt) {
        case 0:  // --config-file
          app_config = optarg;
          break;
        case 1:  // --benchmark
          benchmark = true;
          break;
        case 2:  // --runs
          try {
            uint64_t temp = std::stoul(optarg);
            if (temp == 0) {
              std::cerr << "Error: --runs must be greater than 0" << std::endl;
              return 1;
            }
            n_runs = static_cast<uint32_t>(temp);
          } catch (const std::invalid_argument&) {
            std::cerr << "Error: Invalid number for --runs: " << optarg << std::endl;
            return 1;
          } catch (const std::out_of_range&) {
            std::cerr << "Error: --runs value is out of range: " << optarg << std::endl;
            return 1;
          }
          break;
        case 63:  // '?' - Unknown option (getopt_long convention)
          std::cerr << "Unknown option: " << argv[optind - 1] << std::endl;
          print_help_text(argv[0]);
          return -1;
        case 3:  // --help
        default:
          print_help_text(argv[0]);
          return -1;
      }
    }

    /* Add check for mandatory options */
    if (app_config.empty()) {
      std::cerr << "Argument to app-config not provided" << std::endl;
      print_help_text(argv[0]);
      return -1;
    }

    /* Env + session */
    Ort::Env env(ORT_LOGGING_LEVEL_ERROR, "ml_ort");
    Ort::SessionOptions session_options;

    /* Parse app config */
    boost::property_tree::ptree app_pt;
    boost::property_tree::read_json(app_config, app_pt);

    /* Get the inference-config from app config */
    auto& model_cfg_pt = app_pt.get_child("inference-config");

    /* Get model path */
    model_path = model_cfg_pt.get<std::string>("model-file");

    /* Parse Execution Provider options from user config */
    std::unordered_map<std::string, std::string> options;

    /* Vitis AI EP config file */
    options["config_file"] = model_cfg_pt.get<std::string>("execution-provider-options.config_file");

    /* Target device */
    options["target"] = model_cfg_pt.get<std::string>("execution-provider-options.target");

    /* Cache dir that was used to compile the model */
    options["cache_dir"] = model_cfg_pt.get<std::string>(
        "execution-provider-options.cache_"
        "dir");

    /* Cache key that was used to compile the model */
    options["cache_key"] = model_cfg_pt.get<std::string>(
        "execution-provider-options.cache_"
        "key");

    /* Parse and set optional options if any */
    std::vector<std::pair<std::string, std::string>> option_keys = {
        {"encryption_key", "execution-provider-options.encryption_key"},
        {"ai_analyzer_visualization", "execution-provider-options.ai_analyzer_visualization"},
        {"ai_analyzer_profiling", "execution-provider-options.ai_analyzer_profiling"}};

    for (const auto& [option_name, config_key] : option_keys) {
      if (auto opt_value = model_cfg_pt.get_optional<std::string>(config_key)) {
        options[option_name] = *opt_value;
      }
    }

    /* ORT Session with VitisAIExecutionProvider */
    Ort::Session session{nullptr};

    try {
      (void)session_options.AppendExecutionProvider("VitisAI", options);
      session = Ort::Session(env, model_path.c_str(), session_options);
    } catch (const Ort::Exception& e) {
      std::cerr << "Failed to append VitisAI execution provider: " << e.what() << std::endl;
      throw;
    }

    /* Create input tensors  */

    std::vector<const char*> input_names;
    std::vector<Ort::AllocatedStringPtr> input_name_ptrs;
    std::vector<Ort::Value> input_tensors;
    std::vector<std::any> input_datas;
    std::vector<ONNXTensorElementDataType> input_data_types;  // Track data type for each input

    /* fetch ifms config */
    auto ifms = model_cfg_pt.get_child("ifms-config");
    size_t num_inputs = session.GetInputCount();
    Ort::AllocatorWithDefaultOptions allocator;

    /* Validate input count */
    if (num_inputs == 0) {
      std::cerr << "Error: Model has no inputs" << std::endl;
      return 1;
    }

    /* Store all input names of model */
    for (size_t i = 0; i < num_inputs; ++i) {
      auto& name_ptr = input_name_ptrs.emplace_back(session.GetInputNameAllocated(i, allocator));
      input_names.push_back(name_ptr.get());
    }

    /* Print input information with data types */
    std::cout << "\nGraph Input Node Name/Shape/Type (" << num_inputs << ")" << std::endl;
    for (size_t i = 0; i < num_inputs; ++i) {
      auto input_type_info = session.GetInputTypeInfo(i);
      auto tensor_info = input_type_info.GetTensorTypeAndShapeInfo();
      std::vector<int64_t> shape = tensor_info.GetShape();
      ONNXTensorElementDataType dtype = tensor_info.GetElementType();
      std::string dtype_str = onnx_data_type_to_string(dtype);
      std::string shape_str = shape_to_string(shape);
      std::cout << "         " << input_names[i] << " : [" << shape_str << "] (" << dtype_str << ")" << std::endl;
    }
    std::cout << std::endl;

    /* Check that the number of IFMs matches the model's input count */
    if (static_cast<size_t>(std::distance(ifms.begin(), ifms.end())) != num_inputs) {
      std::cerr << "Expected " << num_inputs << " objects in the ifms-config array, but found "
                << std::distance(ifms.begin(), ifms.end()) << " instead." << std::endl;
      return 1;
    }

    /* Declare model_batch_size at broader scope for later use */
    int64_t model_batch_size = 1;

    /* Pre-allocate in session-input order so downstream batch processing
     * (which indexes by session input slot) gets the correct data
     * regardless of the JSON authoring order. */
    input_datas.resize(num_inputs);
    input_data_types.resize(num_inputs);
    std::unordered_set<std::string> seen_names;

    /* Bind each ifms-config entry to a session input tensor by name.
     * Entries may appear in any order in the JSON array. */
    size_t ifm_idx = 0;
    for (const auto& ifm : ifms) {
      std::string ifm_name = ifm.second.get<std::string>("name", "");
      std::string ifm_file = ifm.second.get<std::string>("file", "");

      if (ifm_name.empty()) {
        std::cerr << "Error: ifms-config[" << ifm_idx
                  << "]: 'name' is missing or empty; it must match an ONNX session input tensor name." << std::endl;
        return 1;
      }
      if (ifm_file.empty()) {
        std::cerr << "Error: ifms-config[" << ifm_idx << "] (name='" << ifm_name << "'): 'file' is missing or empty."
                  << std::endl;
        return 1;
      }
      if (!seen_names.insert(ifm_name).second) {
        std::cerr << "Error: ifms-config[" << ifm_idx << "]: duplicate 'name'='" << ifm_name << "'." << std::endl;
        return 1;
      }

      /* Find the session input index by name */
      int32_t input_index = -1;

      for (size_t i = 0; i < num_inputs; ++i) {
        if (ifm_name == input_names[i]) {
          input_index = i;
          break;
        }
      }

      if (input_index == -1) {
        std::cerr << "Error: ifms-config[" << ifm_idx << "]: 'name'='" << ifm_name
                  << "' does not match any session input tensor." << std::endl;
        std::cerr << "  Session input tensor names:";
        for (const auto& name : input_names) {
          std::cerr << " '" << name << "'";
        }
        std::cerr << std::endl;
        return 1;
      }

      /* Get input shape and type */
      auto input_type_info = session.GetInputTypeInfo(input_index);
      auto tensor_info = input_type_info.GetTensorTypeAndShapeInfo();
      std::vector<int64_t> input_shape = tensor_info.GetShape();
      ONNXTensorElementDataType input_type = tensor_info.GetElementType();
      /* Print input shape */
      std::cout << "Input shape from model: [";
      for (size_t i = 0; i < input_shape.size(); ++i) {
        std::cout << input_shape[i];
        if (i < input_shape.size() - 1)
          std::cout << ", ";
      }
      std::cout << "]" << std::endl;

      /* Store original model batch size before modification */
      model_batch_size = input_shape[0];

      /* Only read VitisAI config if model has dynamic batch size */
      int64_t device_batch_size = 1;  // Default for non-dynamic case
      if (model_batch_size <= 0) {
        // Read hardware batch size (dp_size/device_batch_size) only when needed
        std::string vitisai_config_file = options["config_file"];
        device_batch_size = read_device_batch_size_from_config(vitisai_config_file);
        model_batch_size = device_batch_size;
      }

      /* Validate batch size */
      if (model_batch_size <= 0) {
        std::cerr << "Invalid model batch size: " << model_batch_size << std::endl;
        return 1;
      }

      /* Set dynamic batch dimensions */
      for (auto& dim : input_shape) {
        if (dim <= 0) {
          dim = (model_batch_size > 0) ? model_batch_size : device_batch_size;
        }
      }

      /* Print input shape */
      std::cout << "Input shape after setting dynamic batch dimensions: [";
      for (size_t i = 0; i < input_shape.size(); ++i) {
        std::cout << input_shape[i];
        if (i < input_shape.size() - 1)
          std::cout << ", ";
      }
      std::cout << "]" << std::endl;

      /* Store input data type at the session-input slot */
      input_data_types[input_index] = input_type;

      /* Load data and create tensor based on required data type (native format loading) */
      Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

      if (input_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
        /* Load float16 data directly from file (2 bytes per element) */
        std::vector<Ort::Float16_t> input_data_fp16;
        if (!load_raw_float16(ifm_file, input_data_fp16, input_shape)) {
          std::cerr << "Failed to load input file " << ifm_file << " as float16" << std::endl;
          return 1;
        }

        /* Store the float16 data at the session-input slot and create tensor */
        input_datas[input_index] = std::move(input_data_fp16);
        auto& stored_input_data = std::any_cast<std::vector<Ort::Float16_t>&>(input_datas[input_index]);
        (void)input_tensors.emplace_back(Ort::Value::CreateTensor<Ort::Float16_t>(
            mem_info, stored_input_data.data(), stored_input_data.size(), input_shape.data(), input_shape.size()));

        std::cout << "Bound ifms-config name='" << ifm_name << "' -> session input slot " << input_index
                  << " (float16, " << stored_input_data.size() << " elements)" << std::endl;
      } else if (input_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        /* Load float data directly from file (4 bytes per element) */
        std::vector<float> input_data;
        if (!load_raw_float(ifm_file, input_data, input_shape)) {
          std::cerr << "Failed to load input file " << ifm_file << " as float" << std::endl;
          return 1;
        }

        /* Store the float data at the session-input slot and create tensor */
        input_datas[input_index] = std::move(input_data);
        auto& stored_input_data = std::any_cast<std::vector<float>&>(input_datas[input_index]);
        (void)input_tensors.emplace_back(Ort::Value::CreateTensor<float>(
            mem_info, stored_input_data.data(), stored_input_data.size(), input_shape.data(), input_shape.size()));

        std::cout << "Bound ifms-config name='" << ifm_name << "' -> session input slot " << input_index << " (float, "
                  << stored_input_data.size() << " elements)" << std::endl;
      } else if (input_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8) {
        /* Load int8 data directly from file (1 byte per element) */
        std::vector<int8_t> input_data_int8;
        if (!load_raw_int8(ifm_file, input_data_int8, input_shape)) {
          std::cerr << "Failed to load input file " << ifm_file << " as int8" << std::endl;
          return 1;
        }

        /* Store the int8 data at the session-input slot and create tensor */
        input_datas[input_index] = std::move(input_data_int8);
        auto& stored_input_data = std::any_cast<std::vector<int8_t>&>(input_datas[input_index]);
        (void)input_tensors.emplace_back(Ort::Value::CreateTensor<int8_t>(
            mem_info, stored_input_data.data(), stored_input_data.size(), input_shape.data(), input_shape.size()));

        std::cout << "Bound ifms-config name='" << ifm_name << "' -> session input slot " << input_index << " (int8, "
                  << stored_input_data.size() << " elements)" << std::endl;
      } else {
        std::cerr << "Unsupported input data type for '" << ifm_name << "'. Expected float, float16, or int8."
                  << std::endl;
        return 1;
      }
      ++ifm_idx;
    }

    /* set output names */
    size_t num_outputs = session.GetOutputCount();
    std::vector<const char*> output_names;
    std::vector<Ort::AllocatedStringPtr> output_name_ptrs;
    for (size_t i = 0; i < num_outputs; i++) {
      auto& name_ptr = output_name_ptrs.emplace_back(session.GetOutputNameAllocated(i, allocator));
      output_names.push_back(name_ptr.get());
    }

    /* Validate output count */
    if (num_outputs == 0) {
      std::cerr << "Error: Model has no outputs" << std::endl;
      return 1;
    }

    /* Print output information with data types */
    std::cout << "Graph Output Node Name/Shape/Type (" << num_outputs << ")" << std::endl;
    for (size_t i = 0; i < num_outputs; i++) {
      auto output_type_info = session.GetOutputTypeInfo(i);
      auto tensor_info = output_type_info.GetTensorTypeAndShapeInfo();
      std::vector<int64_t> shape = tensor_info.GetShape();
      ONNXTensorElementDataType dtype = tensor_info.GetElementType();
      std::string dtype_str = onnx_data_type_to_string(dtype);
      /* Change dynamic batch (-1) to 1 for display */
      for (auto& dim : shape) {
        if (dim <= 0) {
          dim = 1;
        }
      }
      std::string shape_str = shape_to_string(shape);
      std::cout << "         " << output_names[i] << " : [" << shape_str << "] (" << dtype_str << ")" << std::endl;
    }
    std::cout << std::endl;

    /* disable dumping of files in benchmark mode */
    if (benchmark) {
      dump = false;
    }

    /* Compute per-input first dimensions, element sizes, and available
     * data counts. Different inputs may have different first dimensions
     * and different element sizes (product of all dims except the first,
     * i.e., the number of values that make up one sample for that input). */
    std::vector<int64_t> per_input_batch_dim(num_inputs);
    std::vector<size_t> per_input_element_size(num_inputs);
    std::vector<size_t> per_input_available_images(num_inputs);
    constexpr size_t SIZE_MAX_SAFE = SIZE_MAX / 2;

    /* For each input, query its shape from the ONNX session to extract
     * the first dimension, compute the element size (product of remaining
     * dims), and determine how many complete samples the loaded data holds. */
    for (size_t i = 0; i < num_inputs; ++i) {
      auto input_type_info = session.GetInputTypeInfo(i);
      auto tensor_info = input_type_info.GetTensorTypeAndShapeInfo();
      auto shape = tensor_info.GetShape();

      if (shape.empty()) {
        std::cerr << "Error: Input " << i << " ('" << input_names[i] << "') has empty shape" << std::endl;
        return 1;
      }

      per_input_batch_dim[i] = (shape[0] > 0) ? shape[0] : model_batch_size;

      size_t element_size = 1;
      for (size_t d = 1; d < shape.size(); ++d) {
        if (shape[d] > 0) {
          if (element_size > 0 && static_cast<size_t>(shape[d]) > SIZE_MAX_SAFE / element_size) {
            std::cerr << "Error: Tensor size overflow for input " << i << std::endl;
            return 1;
          }
          element_size *= static_cast<size_t>(shape[d]);
        }
      }
      if (element_size == 0) {
        std::cerr << "Error: Element size is zero for input " << i << std::endl;
        return 1;
      }
      per_input_element_size[i] = element_size;

      size_t data_size = 0;
      if (input_data_types[i] == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
        data_size = std::any_cast<std::vector<Ort::Float16_t>&>(input_datas[i]).size();
      } else if (input_data_types[i] == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        data_size = std::any_cast<std::vector<float>&>(input_datas[i]).size();
      } else if (input_data_types[i] == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8) {
        data_size = std::any_cast<std::vector<int8_t>&>(input_datas[i]).size();
      } else {
        std::cerr << "Error: Unsupported input data type for input " << i << std::endl;
        return 1;
      }
      per_input_available_images[i] = data_size / element_size;
    }

    /* Iteration count for each input is computed as:
     *   ceil(available_images / first_dim)
     * i.e., how many batches are needed to consume all loaded samples.
     * We compute this for input 0 first, then cross-check that every
     * other input yields the same iteration count. A mismatch means the
     * input data files are inconsistent with each other and we abort. */
    size_t available_images = per_input_available_images[0];
    if (available_images == 0) {
      std::cerr << "Error: No input data available (input 0 has zero images)" << std::endl;
      return 1;
    }

    int32_t num_iterations =
        static_cast<int32_t>((available_images + per_input_batch_dim[0] - 1) / per_input_batch_dim[0]);

    for (size_t i = 1; i < num_inputs; ++i) {
      int32_t input_iters =
          static_cast<int32_t>((per_input_available_images[i] + per_input_batch_dim[i] - 1) / per_input_batch_dim[i]);
      if (input_iters != num_iterations) {
        std::cerr << "Error: Input " << i << " ('" << input_names[i] << "') requires " << input_iters
                  << " iteration(s) (available=" << per_input_available_images[i]
                  << ", batch_dim=" << per_input_batch_dim[i] << "), but '" << input_names[0] << "' requires "
                  << num_iterations << ". All inputs must agree on the iteration count." << std::endl;
        return 1;
      }
    }

    std::cout << "Model batch dimensions per input:" << std::endl;
    for (size_t i = 0; i < num_inputs; ++i) {
      std::cout << "  " << input_names[i] << ": batch_dim=" << per_input_batch_dim[i]
                << ", available=" << per_input_available_images[i] << std::endl;
    }
    std::cout << "Number of iterations needed: " << num_iterations << std::endl;

    /* Batch processing strategy:
     * - Each input uses its own batch dimension and available-data count.
     * - All inputs must yield the same iteration count (proportional batches).
     * - Each iteration creates per-input tensors with appropriate batch sizes.
     */

    /* Get output data types for native format saving */
    std::vector<ONNXTensorElementDataType> output_data_types;
    for (size_t i = 0; i < num_outputs; i++) {
      auto output_type_info = session.GetOutputTypeInfo(i);
      auto tensor_info = output_type_info.GetTensorTypeAndShapeInfo();
      output_data_types.push_back(tensor_info.GetElementType());
    }

    /* Run the inference for n number of runs default is 1 */
    std::cout << "Running the inference for " << n_runs << " runs" << std::endl;
    for (size_t j = 0; j < n_runs; j++) {
      /* Vectors to accumulate all outputs across iterations */
      std::vector<std::any> accumulated_outputs(num_outputs);

      /* Initialize each output vector with the correct type */
      for (size_t i = 0; i < num_outputs; ++i) {
        if (output_data_types[i] == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
          accumulated_outputs[i] = std::vector<Ort::Float16_t>();
        } else if (output_data_types[i] == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
          accumulated_outputs[i] = std::vector<float>();
        } else if (output_data_types[i] == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8) {
          accumulated_outputs[i] = std::vector<int8_t>();
        } else {
          std::cerr << "Error: Unsupported output data type for output " << i << " during initialization" << std::endl;
          return 1;
        }
      }

      /* Process each batch iteration */
      /* Iterate over batches, slicing each input's loaded data independently.
       * For every input we use:
       *   input_batch_dim   – first dimension (samples per batch for this input)
       *   input_element_size – values per sample (product of remaining dims)
       *   input_available   – total samples available in loaded data
       * The offset into the flat data is  batch_iter * input_batch_dim * input_element_size,
       * and the last iteration may be a partial batch (fewer than input_batch_dim samples).
       * After building one tensor per input, we run inference and accumulate outputs. */
      for (int32_t batch_iter = 0; batch_iter < num_iterations; ++batch_iter) {
        std::vector<Ort::Value> batch_input_tensors;

        for (size_t input_idx = 0; input_idx < num_inputs; ++input_idx) {
          /* Get shape for this specific input */
          auto input_type_info = session.GetInputTypeInfo(input_idx);
          auto tensor_info = input_type_info.GetTensorTypeAndShapeInfo();
          std::vector<int64_t> input_specific_shape = tensor_info.GetShape();

          /* Validate shape is not empty */
          if (input_specific_shape.empty()) {
            std::cerr << "Error: Input " << input_idx << " has empty shape" << std::endl;
            return 1;
          }

          /* Use pre-computed per-input element size, batch dim, and available count */
          size_t input_element_size = per_input_element_size[input_idx];
          int64_t input_batch_dim = per_input_batch_dim[input_idx];
          size_t input_available = per_input_available_images[input_idx];

          /* Calculate offset for this batch iteration using this input's batch dimension */
          if (input_batch_dim > 0 && input_element_size > 0) {
            if (static_cast<size_t>(batch_iter) >
                SIZE_MAX_SAFE / static_cast<size_t>(input_batch_dim) / input_element_size) {
              std::cerr << "Error: Offset calculation overflow for input " << input_idx << std::endl;
              return 1;
            }
          }
          size_t offset = batch_iter * input_batch_dim * input_element_size;

          /* Determine actual batch size for this iteration using this input's own dimensions */
          size_t already_consumed = std::min(input_available, static_cast<size_t>(batch_iter * input_batch_dim));
          size_t remaining_images = input_available - already_consumed;
          size_t current_batch_size = std::min(static_cast<size_t>(input_batch_dim), remaining_images);

          /* Update shape for current batch size */
          std::vector<int64_t> current_shape = std::move(input_specific_shape);
          current_shape[0] = current_batch_size;

          /* Calculate tensor size for this batch using this input's element size */
          /* Check for overflow in tensor size calculation */
          if (current_batch_size > 0 && input_element_size > SIZE_MAX / current_batch_size) {
            std::cerr << "Error: Tensor size calculation overflow for input " << input_idx << std::endl;
            return 1;
          }
          size_t tensor_size = current_batch_size * input_element_size;

          std::cout << "Batch iteration " << batch_iter << ": input_idx=" << input_idx << " processing "
                    << current_batch_size << " images, offset=" << offset << ", tensor_size=" << tensor_size
                    << std::endl;

          /* Create tensor for this batch based on data type */
          Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
          ONNXTensorElementDataType dtype = input_data_types[input_idx];

          if (input_idx >= input_datas.size()) {
            std::cerr << "Error: Input index " << input_idx << " out of bounds for input data" << std::endl;
            return 1;
          }

          if (dtype == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
            auto& data = std::any_cast<std::vector<Ort::Float16_t>&>(input_datas[input_idx]);
            if (offset + tensor_size > data.size()) {
              std::cerr << "Error: Buffer overflow detected for input " << input_idx << " (offset=" << offset
                        << ", tensor_size=" << tensor_size << ", buffer_size=" << data.size() << ")" << std::endl;
              return 1;
            }
            Ort::Float16_t* input_data_ptr = data.data() + offset;
            batch_input_tensors.emplace_back(Ort::Value::CreateTensor<Ort::Float16_t>(
                mem_info, input_data_ptr, tensor_size, current_shape.data(), current_shape.size()));
          } else if (dtype == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            auto& data = std::any_cast<std::vector<float>&>(input_datas[input_idx]);
            if (offset + tensor_size > data.size()) {
              std::cerr << "Error: Buffer overflow detected for input " << input_idx << " (offset=" << offset
                        << ", tensor_size=" << tensor_size << ", buffer_size=" << data.size() << ")" << std::endl;
              return 1;
            }
            float* input_data_ptr = data.data() + offset;
            batch_input_tensors.emplace_back(Ort::Value::CreateTensor<float>(
                mem_info, input_data_ptr, tensor_size, current_shape.data(), current_shape.size()));
          } else if (dtype == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8) {
            auto& data = std::any_cast<std::vector<int8_t>&>(input_datas[input_idx]);

            if (offset + tensor_size > data.size()) {
              std::cerr << "Error: Buffer overflow detected for input " << input_idx << " (offset=" << offset
                        << ", tensor_size=" << tensor_size << ", buffer_size=" << data.size() << ")" << std::endl;
              return 1;
            }
            int8_t* input_data_ptr = data.data() + offset;
            batch_input_tensors.emplace_back(Ort::Value::CreateTensor<int8_t>(
                mem_info, input_data_ptr, tensor_size, current_shape.data(), current_shape.size()));
          } else {
            std::cerr << "Error: Unsupported input data type for input " << input_idx << " during batch processing"
                      << std::endl;
            return 1;
          }
        }

        /* Run inference for this batch */
        auto start_run = std::chrono::high_resolution_clock::now();
        auto output_tensors = session.Run(Ort::RunOptions{nullptr},
                                          input_names.data(),          // array of input names
                                          batch_input_tensors.data(),  // array of Ort::Value input tensors
                                          num_inputs,                  // number of inputs
                                          output_names.data(),         // array of output names
                                          num_outputs                  // number of outputs
        );
        auto end_run = std::chrono::high_resolution_clock::now();
        if (benchmark) {
          total_inference_time_us += std::chrono::duration_cast<std::chrono::microseconds>(end_run - start_run).count();
        }
        /* Accumulate outputs from this batch iteration (type-aware) */
        for (size_t i = 0; i < num_outputs; i++) {
          size_t output_size = output_tensors[i].GetTensorTypeAndShapeInfo().GetElementCount();

          /* Validate output size */
          if (output_size == 0) {
            std::cerr << "Warning: Output " << i << " has zero elements, skipping" << std::endl;
            continue;
          }

          /* Validate output index is within bounds */
          if (i >= output_data_types.size()) {
            std::cerr << "Error: Output index " << i << " out of bounds for output_data_types" << std::endl;
            return 1;
          }

          ONNXTensorElementDataType out_type = output_data_types[i];

          if (out_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
            const Ort::Float16_t* output_data = output_tensors[i].GetTensorData<Ort::Float16_t>();
            /* Validate pointer is not null */
            if (!output_data) {
              std::cerr << "Error: Failed to get float16 tensor data for output " << i << std::endl;
              return 1;
            }
            auto& acc_data = std::any_cast<std::vector<Ort::Float16_t>&>(accumulated_outputs[i]);
            if (batch_iter == 0) {
              acc_data.assign(output_data, output_data + output_size);
            } else {
              acc_data.insert(acc_data.end(), output_data, output_data + output_size);
            }
          } else if (out_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            const float* output_data = output_tensors[i].GetTensorData<float>();
            /* Validate pointer is not null */
            if (!output_data) {
              std::cerr << "Error: Failed to get float tensor data for output " << i << std::endl;
              return 1;
            }
            auto& acc_data = std::any_cast<std::vector<float>&>(accumulated_outputs[i]);
            if (batch_iter == 0) {
              acc_data.assign(output_data, output_data + output_size);
            } else {
              acc_data.insert(acc_data.end(), output_data, output_data + output_size);
            }
          } else if (out_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8) {
            const int8_t* output_data = output_tensors[i].GetTensorData<int8_t>();
            /* Validate pointer is not null */
            if (!output_data) {
              std::cerr << "Error: Failed to get int8 tensor data for output " << i << std::endl;
              return 1;
            }
            auto& acc_data = std::any_cast<std::vector<int8_t>&>(accumulated_outputs[i]);
            if (batch_iter == 0) {
              acc_data.assign(output_data, output_data + output_size);
            } else {
              acc_data.insert(acc_data.end(), output_data, output_data + output_size);
            }
          } else {
            std::cerr << "Error: Unsupported output data type for output " << i << " during accumulation" << std::endl;
            return 1;
          }
        }
      }

      /* Save accumulated outputs if dump is enabled */
      if (dump) {
        /* Save output (as raw binary) with the name matching:
         * infer{instance_id}_out{tensor_idx}-{dtype}_{shape}_{tensor_name}.bin */
        std::string output_dir = "outputs";
        for (size_t i = 0; i < num_outputs; i++) {
          auto output_name = session.GetOutputNameAllocated(i, allocator);

          /* Get output type info */
          Ort::TypeInfo type_info = session.GetOutputTypeInfo(i);
          auto tensor_info = type_info.GetTensorTypeAndShapeInfo();

          /* Get data type and shape */
          ONNXTensorElementDataType type = tensor_info.GetElementType();
          std::string dtype_str = onnx_data_type_to_string(type);
          std::vector<int64_t> shape = tensor_info.GetShape();

          /* Calculate actual accumulated batch size from the output data
           * This represents the total number of images processed across all batch iterations */
          size_t total_output_elements = 0;
          ONNXTensorElementDataType out_type = output_data_types[i];
          if (out_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
            auto& data = std::any_cast<std::vector<Ort::Float16_t>&>(accumulated_outputs[i]);
            total_output_elements = data.size();
          } else if (out_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            auto& data = std::any_cast<std::vector<float>&>(accumulated_outputs[i]);
            total_output_elements = data.size();
          } else if (out_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8) {
            auto& data = std::any_cast<std::vector<int8_t>&>(accumulated_outputs[i]);
            total_output_elements = data.size();
          }

          size_t single_output_element_size = 1;
          for (size_t dim_idx = 1; dim_idx < shape.size(); ++dim_idx) {
            if (shape[dim_idx] > 0) {
              single_output_element_size *= shape[dim_idx];
            }
          }
          size_t accumulated_batch_size =
              (single_output_element_size > 0) ? total_output_elements / single_output_element_size : available_images;

          /* Update shape to reflect accumulated output data */
          if (shape.size() > 0) {
            shape[0] = accumulated_batch_size;  // Set batch dimension to actual accumulated batch size
          }

          for (auto& dim : shape) {
            if (dim <= 0)
              dim = 1;
          }

          std::string shape_str = shape_to_string(shape);

          /* Construct output file name */
          std::string safe_name = std::string(output_name.get());
          // Replace '/' with '-'
          replace(safe_name.begin(), safe_name.end(), '/', '-');

          // Remove leading '-' if present
          if (!safe_name.empty() && safe_name.front() == '-') {
            safe_name.erase(0, 1);
          }

          std::string infer_prefix = "infer_"; /* No instance_id needed for single-instance ml_ort */
          std::string append_str = dtype_str + "_" + shape_str + "_";
          std::string base_filename = infer_prefix + "out" + std::to_string(i) + "-" + append_str + safe_name + ".bin";

          if (n_runs > 1) {
            /* Multiple runs includes iteration number in filename */
            output_file = output_dir + "/iter" + std::to_string(j) + "_" + base_filename;
          } else {
            /* Single run */
            output_file = output_dir + "/" + base_filename;
          }

          /* Create all parent directories for this specific output file */
          std::filesystem::path output_path(output_file);
          std::filesystem::create_directories(output_path.parent_path());

          /* Save accumulated output data in native format */
          bool save_success = false;
          if (out_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
            auto& data = std::any_cast<std::vector<Ort::Float16_t>&>(accumulated_outputs[i]);
            std::cout << "output_file name: " << output_file << " total_output_size: " << data.size() << std::endl;
            save_success = save_raw_float16(output_file, data);
          } else if (out_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            auto& data = std::any_cast<std::vector<float>&>(accumulated_outputs[i]);
            std::cout << "output_file name: " << output_file << " total_output_size: " << data.size() << std::endl;
            save_success = save_raw_float(output_file, data);
          } else if (out_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8) {
            auto& data = std::any_cast<std::vector<int8_t>&>(accumulated_outputs[i]);
            std::cout << "output_file name: " << output_file << " total_output_size: " << data.size() << std::endl;
            save_success = save_raw_int8(output_file, data);
          } else {
            std::cerr << "Unsupported output data type for output " << i << std::endl;
            return 1;
          }

          if (!save_success) {
            std::cerr << "Failed to save OFM" << std::endl;
            return 1;
          }
          std::cout << "OFM saved to " << output_file << std::endl;
        }
      }
    }
    /* Calculate and display average session.Run() time */
    if (benchmark) {
      int32_t total_iterations = static_cast<int32_t>(n_runs) * num_iterations;
      double avg_time_us = total_inference_time_us / static_cast<double>(total_iterations);

      /* convert micro to milli seconds */
      double avg_time_ms = avg_time_us / 1000;
      std::ostringstream time_str;
      time_str << std::fixed << std::setprecision(2) << avg_time_ms;
      std::cout << "Average inference time over " << n_runs << " runs: " << time_str.str() << " ms" << std::endl;
    }
    return 0;
  } catch (const std::exception& e) {
    try {
      std::cerr << "Error: " << e.what() << std::endl;
    } catch (...) {
    }
    return 1;
  } catch (...) {
    try {
      std::cerr << "Unknown error" << std::endl;
    } catch (...) {
    }
    return 2;
  }
}
