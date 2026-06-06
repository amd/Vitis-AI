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

#pragma once
#include <onnxruntime_cxx_api.h>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Loads raw float16 data from a binary file into a vector.
 *
 * This function reads a binary file containing raw float16 values
 * and loads the data into the provided vector. Supports batch loading.
 *
 * @param filename The path to the binary file containing raw float16 data.
 * @param data Reference to a vector where the loaded float16 data will be stored.
 * @param shape Reference to a vector representing the shape of the data.
 * @return true if the data was loaded successfully, false otherwise.
 */
inline bool load_raw_float16(const std::string& filename,
                             std::vector<Ort::Float16_t>& data,
                             std::vector<int64_t>& shape) {
  std::ifstream f(filename, std::ios::binary);
  if (!f)
    return false;

  /* Check for empty shape */
  if (shape.empty()) {
    std::cerr << "Error: Empty shape provided for input file: " << filename << std::endl;
    return false;
  }

  /* First, determine the file size to understand available data */
  f.seekg(0, std::ios::end);
  std::streampos file_pos = f.tellg();

  /* Check for tellg() error */
  if (file_pos == static_cast<std::streampos>(-1)) {
    std::cerr << "Error: Failed to determine file size for: " << filename << std::endl;
    return false;
  }

  size_t file_size_bytes = static_cast<size_t>(file_pos);

  /* Check for empty file */
  if (file_size_bytes == 0) {
    std::cerr << "Warning: Input file is empty: " << filename << std::endl;
    return false;
  }

  /* Check for alignment with element size */
  if (file_size_bytes % sizeof(uint16_t) != 0) {
    std::cerr << "Warning: File size (" << file_size_bytes << " bytes) is not a multiple of element size ("
              << sizeof(uint16_t) << " bytes) for file: " << filename << std::endl;
  }

  size_t file_size_elements = file_size_bytes / sizeof(uint16_t);
  f.seekg(0, std::ios::beg);

  /* Calculate single element size (excluding batch dimension) and handle dynamic dimensions */
  size_t single_element_size = 1;
  constexpr size_t SIZE_MAX_SAFE = SIZE_MAX / 2; /* Conservative overflow threshold */

  for (size_t i = 1; i < shape.size(); ++i) {
    if (shape[i] <= 0) {
      shape[i] = 1; /* Set dynamic dimensions to 1 */
    }

    /* Check for potential overflow before multiplication */
    if (single_element_size > 0 && static_cast<size_t>(shape[i]) > SIZE_MAX_SAFE / single_element_size) {
      std::cerr << "Error: Tensor size overflow detected for file: " << filename << std::endl;
      return false;
    }

    single_element_size *= static_cast<size_t>(shape[i]);
  }

  /* Handle batch dimension (dimension 0) separately */
  if (shape[0] <= 0) {
    shape[0] = 1;
  }

  /* Determine how many complete elements are in the file */
  size_t elements_in_file = (single_element_size > 0) ? file_size_elements / single_element_size : 0;

  /* Load all available data from file, regardless of model batch size */
  size_t data_to_load = file_size_elements;

  /* Update batch dimension to reflect actual elements in file if originally > 1 */
  if (elements_in_file > 0 && single_element_size > 0) {
    size_t calculated_batch = file_size_elements / single_element_size;
    if (calculated_batch > 0) {
      shape[0] = static_cast<int64_t>(calculated_batch);
    }
  }

  data.resize(data_to_load);
  f.read(reinterpret_cast<char*>(data.data()), data_to_load * sizeof(uint16_t));

  /* Verify read was successful */
  if (!f) {
    std::cerr << "Error: Failed to read data from file: " << filename << std::endl;
    return false;
  }

  return true;
}

/**
 * @brief Saves raw float16 data to a binary file.
 *
 * This function writes the contents of a float16 vector to a binary file in raw
 * format.
 *
 * @param filename The path to the binary file where the float16 data will be
 * saved.
 * @param data The vector containing float16 data to be written to the file.
 * @return true if the data was saved successfully, false otherwise.
 */
inline bool save_raw_float16(const std::string& filename, const std::vector<Ort::Float16_t>& data) {
  /* Check for empty data */
  if (data.empty()) {
    std::cerr << "Warning: Attempting to save empty data to: " << filename << std::endl;
    return false;
  }

  std::ofstream f(filename, std::ios::binary);
  if (!f)
    return false;
  f.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(uint16_t));

  /* Verify write was successful */
  if (!f) {
    std::cerr << "Error: Failed to write data to file: " << filename << std::endl;
    return false;
  }

  return true;
}

/**
 * @brief Converts an ONNX tensor element data type enum to its corresponding
 * string representation.
 *
 * This function takes an ONNXTensorElementDataType enumeration value and
 * returns a human-readable string that describes the data type (e.g.,
 * "float32", "int64").
 *
 * @param type The ONNXTensorElementDataType enum value to convert.
 * @return A string representing the data type.
 */
inline std::string onnx_data_type_to_string(ONNXTensorElementDataType type) {
  static const std::unordered_map<ONNXTensorElementDataType, std::string> type_map = {
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, "float32"},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8, "uint8"},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8, "int8"},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16, "uint16"},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16, "int16"},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32, "int32"},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, "int64"},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_STRING, "string"},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL, "bool"},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16, "float16"},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE, "float64"},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32, "uint32"},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64, "uint64"},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX64, "complex64"},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX128, "complex128"},
      {ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16, "bfloat16"}};
  auto it = type_map.find(type);
  return it != type_map.end() ? it->second : "unknown";
}
