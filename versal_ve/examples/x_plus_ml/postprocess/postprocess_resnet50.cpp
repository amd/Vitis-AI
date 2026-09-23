/*
 *
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

#include <dlfcn.h>
#include <vvas_core/vvas_common.h>
#include <vvas_core/vvas_postprocess.h>
#include <vvas_utils/vvas_utils.h>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <iostream>
#include <filesystem>
#include <fstream>

#ifdef __cplusplus
extern "C" {

#endif

#define NUM_IMAGENET_CLASSES 1000

#define DEFAULT_TOPK 1
typedef struct {
  uint32_t topk;
  uint32_t num_tensors;
  bool disable_softmax; // This is needed to disable softmax computation, if already
                        // applied in the model
  std::vector<std::string> labels;
  std::vector<float> unquant_outbuf;
  VvasTensorInfo** info;
} resnetPriv;

void* postprocess_init(char* json_conf,
                       VvasTensorInfo** t_info,
                       uint32_t num_valid_tensors);
VvasReturnType postprocess_deinit(void* pp_private);
VvasReturnType postprocess_run(void* pp_private,
                               VvasMemory** tensor_memory,
                               uint32_t cur_batch_size,
                               VvasList** res);

static std::vector<std::pair<size_t, float>> get_topk(std::vector<float>& buf,
                                                      size_t k) {
  std::vector<std::pair<size_t, float>> tmp;
  size_t i = 0;
  for (auto x : buf)
    tmp.push_back(std::make_pair(i++, x));
  std::sort(tmp.begin(), tmp.end(),
            [](auto& l, auto r) { return l.second > r.second; });
  tmp.resize(k);
  return tmp;
}

static std::vector<std::string> read_labels(const std::string& label_loc) {
  std::vector<std::string> categories;
  std::string line;

  // Check if path is empty
  if (label_loc.empty()) {
    std::cerr << "Error: Label file path is empty" << std::endl;
    return categories;
  }

  // Check if path exists
  if (!std::filesystem::exists(label_loc)) {
    std::cerr << "Error: Label file does not exist: " << label_loc << std::endl;
    return categories;
  }

  // Check if path is a directory
  if (std::filesystem::is_directory(label_loc)) {
    std::cerr << "Error: Label path is a directory, expected a file: " << label_loc << std::endl;
    return categories;
  }

  // Open the file
  std::ifstream catfile(label_loc.c_str());
  if (!catfile.is_open()) {
    std::cerr << "Error: Failed to open label file: " << label_loc << std::endl;
    return categories;
  }

  // Read labels line by line
  while (std::getline(catfile, line)) {
    // Skip empty lines
    if (!line.empty()) {
      categories.push_back(line);
    }
  }

  return categories;
}

static void softmax(std::vector<float>& buf) {
  float sum = 0.0f;
  for (auto& x : buf) {
    x = expf(x);
    sum += x;
  }
  if (sum > 0.0f) {
    for (auto& x : buf)
      x /= sum;
  }
}

struct float16 {
    uint16_t data; // Stores the 16-bit float16 value
};

/* The c++ build system is not happy with this unholy cast!!
*  Disabling warnings for now. */
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wstrict-aliasing\"")
_Pragma("GCC diagnostic ignored \"-Wuninitialized\"")

float bfloat16_to_float(float16 bf16_val) {
    uint32_t float_bits = static_cast<uint32_t>(bf16_val.data) << 16;
    return *reinterpret_cast<float*>(&float_bits);
}

float fp16_to_float(float16 fp16_val) {
    uint16_t sign_fp16 = (fp16_val.data >> 15) & 0x01;
    uint16_t exponent_fp16 = (fp16_val.data >> 10) & 0x1F;
    uint16_t fraction_fp16 = fp16_val.data & 0x03FF;

    uint32_t sign_fp32 = sign_fp16 << 31;
    uint32_t exponent_fp32;
    uint32_t fraction_fp32 = fraction_fp16 << (23-10);

    if (exponent_fp16 == 0) {
      exponent_fp32 = 0;
    } else if (exponent_fp16 == 0x1F) {
      exponent_fp32 = 0xFF << 23;
    } else {
      exponent_fp32 = (exponent_fp16 + (127-15)) << 23;
    }

    uint32_t fp32 = sign_fp32 | exponent_fp32 | fraction_fp32;

    float result;
    memcpy(&result, &fp32, sizeof(result));

    return result;
}

_Pragma("GCC diagnostic pop")

VvasReturnType postprocess_run(void* pp_private,
                               VvasMemory** tensor_memory,
                               uint32_t cur_batch_size,
                               VvasList** res) {
  resnetPriv* pp_handle = NULL;

  if (!pp_private) {
    std::cerr << "post process handle is not valid" << std::endl;
    return VVAS_RET_ERROR;
  }
  pp_handle = (resnetPriv*)pp_private;

  if (!res) {
    std::cerr << "No memory allocated to VvasList" << std::endl;
    return VVAS_RET_ERROR;
  }

  if (!tensor_memory) {
    std::cerr << "Tensor data is not valid" << std::endl;
    return VVAS_RET_ERROR;
  }

  std::vector<std::vector<float>> results;
  for (uint32_t i = 0; i < cur_batch_size; i++) {
    /* Each batch receives its own set of tensors.
     * So, the total number of tensors received corresponds to the entire batch
     * size. The first tensor corresponds to first frame, the next corresponds
     * to second frame ans so on.
     */

    /* Tensor has padding in Native format, hence considering only the valid
     * data */

    if (!tensor_memory[i]) {
      std::cerr << "Tensor memory is not valid" << std::endl;
      return VVAS_RET_ERROR;
    }

    VvasMemoryMapInfo map_info = {};
    if (VVAS_RET_SUCCESS !=
        vvas_memory_map(tensor_memory[i], VVAS_DATA_MAP_READ, &map_info)) {
      std::cerr << "Failed to map tensor memory for read" << std::endl;
      return VVAS_RET_ERROR;
    }

    if (pp_handle->info[0]->data_type == VVAS_TENSOR_DATA_TYPE_INT8) {
      int8_t* data_ptr = reinterpret_cast<int8_t*>(map_info.data);
      for (size_t j = 0; j < NUM_IMAGENET_CLASSES; j++) {
        float unquantized_value =
            (float)(data_ptr[j] / pp_handle->info[0]->scale_coeff);
        pp_handle->unquant_outbuf.push_back(unquantized_value);
      }
    } else if (pp_handle->info[0]->data_type == VVAS_TENSOR_DATA_TYPE_FLOAT32) {
      float* data_ptr = reinterpret_cast<float*>(map_info.data);
      for (size_t j = 0; j < NUM_IMAGENET_CLASSES; j++) {
        pp_handle->unquant_outbuf.push_back(data_ptr[j]);
      }
    } else if (pp_handle->info[0]->data_type == VVAS_TENSOR_DATA_TYPE_BF16) {
      float16* data_ptr = reinterpret_cast<float16*>(map_info.data);
      for (size_t j = 0; j < NUM_IMAGENET_CLASSES; j++) {
        pp_handle->unquant_outbuf.push_back(bfloat16_to_float(data_ptr[j]));
      }
    } else if (pp_handle->info[0]->data_type == VVAS_TENSOR_DATA_TYPE_FP16) {
      float16* data_ptr = reinterpret_cast<float16*>(map_info.data);
      for (size_t j = 0; j < NUM_IMAGENET_CLASSES; j++) {
        pp_handle->unquant_outbuf.push_back(fp16_to_float(data_ptr[j]));
      }
    } else {
      std::cerr << "Unknown VvasTensorDataType" << std::endl;
      return VVAS_RET_ERROR;
    }

    /* Apply softmax if not disabled */
    if (!pp_handle->disable_softmax)
      softmax(pp_handle->unquant_outbuf);

    results.push_back(pp_handle->unquant_outbuf);
    /* Clear vector for next iteration */
    pp_handle->unquant_outbuf.clear();
    vvas_memory_unmap(tensor_memory[i], &map_info);
  }

  for (uint32_t b = 0; b < cur_batch_size; b++) {
    std::vector<std::pair<size_t, float>> top =
        get_topk(results[b], pp_handle->topk);
    VvasInferResult* infer_result = NULL;
    infer_result = vvas_infer_result_classification_create();
    VvasList** classification = (VvasList**)&infer_result->data;

    for (uint32_t i = 0; i < pp_handle->topk; i++) {
      VvasInferClassification* c = NULL;
      if (top[i].first >= NUM_IMAGENET_CLASSES) {
        std::cerr << "Wrong tensor format. Classification label index is "
                     "beyond the limit : "
                     "Expected index is within 1000 but getting  : "
                  << top[i].first << std::endl;
        throw std::runtime_error(
            "Wrong tensor format. Classification label index is beyond the "
            "limit : "
            "Expected index is within 1000 but getting  : " +
            std::to_string(top[i].first));
      }

      c = vvas_inferclassification_new();
      c->id = i;
      c->probability = top[i].second;
      c->label = strdup(pp_handle->labels[top[i].first].substr(0, 50).c_str());
      *classification = vvas_list_append(*classification, c);
    }
    infer_result->infer_result_type = VVAS_INFER_RESULT_CLASSIFICATION;
    res[b] = vvas_list_append(res[b], infer_result);
  }

  return VVAS_RET_SUCCESS;
}

void* postprocess_init(char* json_conf,
                       VvasTensorInfo** t_info,
                       uint32_t num_valid_tensors) {
  if (1 != num_valid_tensors) {
    std::cerr << "Invalid num of tensors: " << num_valid_tensors
              << ", expected: 1" << std::endl;
    return NULL;
  }

  resnetPriv* pp_private = new resnetPriv;
  std::string label_file_path;
  boost::property_tree::ptree pt;
  std::istringstream json_stream(json_conf);
  size_t data_type_size = 1;

  // copy tensors information
  pp_private->info =
      (VvasTensorInfo**)calloc(num_valid_tensors, sizeof(VvasTensorInfo*));
  if (!pp_private->info) {
    std::cerr << "Failed to allocate memory" << std::endl;
    delete pp_private;
    return NULL;
  }

  if (!t_info[0]) {
    std::cerr << "Tensor information is missing" << std::endl;
    goto error;
  }

  if (t_info[0]->data_type == VVAS_TENSOR_DATA_TYPE_INT8) {
    data_type_size = sizeof(int8_t);
  } else if (t_info[0]->data_type == VVAS_TENSOR_DATA_TYPE_FLOAT32) {
    data_type_size = sizeof(float);
  } else if (t_info[0]->data_type == VVAS_TENSOR_DATA_TYPE_BF16) {
    data_type_size = sizeof(uint16_t);
  } else if (t_info[0]->data_type == VVAS_TENSOR_DATA_TYPE_FP16) {
    data_type_size = sizeof(uint16_t);
  } else {
    std::cerr << "Unknown VvasTesnorType " << std::endl;
    goto error;
  }

  if(t_info[0]->size < (NUM_IMAGENET_CLASSES * data_type_size)) {
    std::cerr << "Invalid tensor size: " << t_info[0]->size << ", expected tensors size (minimum): " << (NUM_IMAGENET_CLASSES * data_type_size);
  }

  pp_private->info[0] = (VvasTensorInfo*)calloc(1, sizeof(VvasTensorInfo));
  if (!pp_private->info) {
    std::cerr << "Failed to allocate memory" << std::endl;
    goto error;
  }

  pp_private->info[0]->size = t_info[0]->size;
  pp_private->info[0]->scale_coeff = t_info[0]->scale_coeff;
  pp_private->info[0]->valid_shapes = t_info[0]->valid_shapes;
  pp_private->info[0]->data_type = t_info[0]->data_type;
  for (uint32_t j = 0; j < t_info[0]->valid_shapes; j++)
    pp_private->info[0]->shape[j] = t_info[0]->shape[j];

  pp_private->num_tensors = num_valid_tensors;
  try {
    boost::property_tree::read_json(json_stream, pt);
  } catch (const std::exception& e) {
    std::cerr << "Error reading JSON : " << e.what() << std::endl;
    goto error;
  }
  pp_private->topk = pt.get<int>("topk", DEFAULT_TOPK);
  try {
    /* Not taking the default path, as we want to make sure that user
     * passes label file path in JSON.
     */
    label_file_path = pt.get<std::string>("label-file-path");
  } catch (const std::exception& e) {
    std::cerr << "Specify label file path in JSON config : " << e.what()
              << std::endl;
    goto error;
  }
  /* read disable-softmax flag, if not present consider it as false */
  pp_private->disable_softmax = pt.get<bool>("disable-softmax", false);

  /* Read the given label file and populate the labels */
  pp_private->labels = read_labels(label_file_path);
  if (pp_private->labels.empty()) {
    std::cerr << "Failed to read labels from: " << label_file_path << std::endl;
    goto error;
  }

  pp_private->unquant_outbuf.reserve(NUM_IMAGENET_CLASSES);

  return pp_private;

error:
  if (pp_private->info[0] != NULL) {
    free(pp_private->info[0]);
    pp_private->info[0] = NULL;
  }

  if (pp_private->info != NULL) {
    free(pp_private->info);
    pp_private->info = NULL;
  }
  delete pp_private;
  pp_private = NULL;
  return NULL;
}

VvasReturnType postprocess_deinit(void* pp_private) {
  resnetPriv* pp_handle = (resnetPriv*)pp_private;
  if (pp_handle) {
    if (pp_handle->info) {
      for (uint32_t i = 0; i < pp_handle->num_tensors; i++) {
        if (pp_handle->info[i]) {
          free(pp_handle->info[i]);
        }
      }
      free(pp_handle->info);
    }
    delete pp_handle;
  }
  return VVAS_RET_SUCCESS;
}

#ifdef __cplusplus
}

#endif /* \
        */
