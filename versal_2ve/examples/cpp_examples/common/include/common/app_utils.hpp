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
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

/**
 * @brief Loads raw float data from a binary file into a vector.
 *
 * This function reads a binary file containing raw float values and loads the
 * data into the provided vector. The shape of the data can also be set or
 * inferred as needed.
 *
 * @param filename The path to the binary file containing raw float data.
 * @param data Reference to a vector where the loaded float data will be stored.
 * @param shape Reference to a vector representing the shape of the data. This
 * may be set or updated by the function.
 * @return true if the data was loaded successfully, false otherwise.
 */
inline bool load_raw_float(const std::string& filename, std::vector<float>& data, std::vector<int64_t>& shape) {
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
  if (file_size_bytes % sizeof(float) != 0) {
    std::cerr << "Warning: File size (" << file_size_bytes << " bytes) is not a multiple of element size ("
              << sizeof(float) << " bytes) for file: " << filename << std::endl;
  }

  size_t file_size_floats = file_size_bytes / sizeof(float);
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
  size_t elements_in_file = (single_element_size > 0) ? file_size_floats / single_element_size : 0;

  /* Load all available data from file, regardless of model batch size
   * The application will handle batching during inference */
  size_t data_to_load = file_size_floats;

  /* Update batch dimension to reflect actual elements in file if originally > 1 */
  if (elements_in_file > 0 && single_element_size > 0) {
    size_t calculated_batch = file_size_floats / single_element_size;
    if (calculated_batch > 0) {
      shape[0] = static_cast<int64_t>(calculated_batch);
    }
  }

  data.resize(data_to_load);
  f.read(reinterpret_cast<char*>(data.data()), data_to_load * sizeof(float));

  /* Verify read was successful */
  if (!f) {
    std::cerr << "Error: Failed to read data from file: " << filename << std::endl;
    return false;
  }

  return true;
}

/**
 * @brief Saves raw float data to a binary file.
 *
 * This function writes the contents of a float vector to a binary file in raw
 * format.
 *
 * @param filename The path to the binary file where the float data will be
 * saved.
 * @param data The vector containing float data to be written to the file.
 * @return true if the data was saved successfully, false otherwise.
 */
inline bool save_raw_float(const std::string& filename, const std::vector<float>& data) {
  /* Check for empty data */
  if (data.empty()) {
    std::cerr << "Warning: Attempting to save empty data to: " << filename << std::endl;
    return false;
  }

  std::ofstream f(filename, std::ios::binary);
  if (!f)
    return false;
  f.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(float));

  /* Verify write was successful */
  if (!f) {
    std::cerr << "Error: Failed to write data to file: " << filename << std::endl;
    return false;
  }

  return true;
}

/**
 * @brief Loads raw int8 data from a binary file into a vector.
 *
 * This function reads a binary file containing raw int8 values and loads the
 * data into the provided vector. Supports batch loading.
 *
 * @param filename The path to the binary file containing raw int8 data.
 * @param data Reference to a vector where the loaded int8 data will be stored.
 * @param shape Reference to a vector representing the shape of the data.
 * @return true if the data was loaded successfully, false otherwise.
 */
inline bool load_raw_int8(const std::string& filename, std::vector<int8_t>& data, std::vector<int64_t>& shape) {
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

  /* Check for alignment with element size (always 1 for int8, but kept for consistency) */
  if (file_size_bytes % sizeof(int8_t) != 0) {
    std::cerr << "Warning: File size (" << file_size_bytes << " bytes) is not a multiple of element size ("
              << sizeof(int8_t) << " bytes) for file: " << filename << std::endl;
  }

  size_t file_size_elements = file_size_bytes / sizeof(int8_t);
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
  f.read(reinterpret_cast<char*>(data.data()), data_to_load * sizeof(int8_t));

  /* Verify read was successful */
  if (!f) {
    std::cerr << "Error: Failed to read data from file: " << filename << std::endl;
    return false;
  }

  return true;
}

/**
 * @brief Saves raw int8 data to a binary file.
 *
 * This function writes the contents of an int8 vector to a binary file in raw
 * format.
 *
 * @param filename The path to the binary file where the int8 data will be
 * saved.
 * @param data The vector containing int8 data to be written to the file.
 * @return true if the data was saved successfully, false otherwise.
 */
inline bool save_raw_int8(const std::string& filename, const std::vector<int8_t>& data) {
  /* Check for empty data */
  if (data.empty()) {
    std::cerr << "Warning: Attempting to save empty data to: " << filename << std::endl;
    return false;
  }

  std::ofstream f(filename, std::ios::binary);
  if (!f)
    return false;
  f.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(int8_t));

  /* Verify write was successful */
  if (!f) {
    std::cerr << "Error: Failed to write data to file: " << filename << std::endl;
    return false;
  }

  return true;
}

/**
 * @brief Converts a vector representing a tensor shape to a string.
 *
 * This function takes a vector of int64_t values representing the dimensions of
 * a tensor and returns a string in the format "dim1xdim2x...xdimN" (e.g.,
 * "1x224x224x3").
 *
 * @param shape The vector containing the dimensions of the tensor.
 * @return A string representing the shape in "dim1xdim2x...xdimN" format.
 */
inline std::string shape_to_string(const std::vector<int64_t>& shape) {
  std::ostringstream oss;
  for (size_t i = 0; i < shape.size(); ++i) {
    oss << shape[i];
    if (i + 1 < shape.size())
      oss << "x";
  }
  return oss.str();
}

/**
 * @brief Extracts the file extension from a given filename and converts it to
 * lowercase.
 * @param filename The input filename.
 * @return The file extension in lowercase, or an empty string if not found.
 */
inline std::string get_file_extension_lowercase(const std::string& filename) {
  size_t dotIndex = filename.find_last_of(".");
  if (dotIndex != std::string::npos && dotIndex + 1 < filename.size()) {
    /* Return the substring after the last dot as the file extension */
    std::string ext = filename.substr(dotIndex + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext;
  }
  /* Return an empty string if no extension is found */
  return "";
}
