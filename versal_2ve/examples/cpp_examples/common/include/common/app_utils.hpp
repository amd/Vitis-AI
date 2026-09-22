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
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

/* ============================ Performance reporting ============================
 * Shared helpers for formatting numbers and rendering the "Performance" summary
 * tables printed by the example applications. Consolidated here so the table
 * layout and number formatting stay consistent across apps.
 */

/** A single metric/value pair for the two-column performance table. */
struct PerfRow {
  std::string metric;  ///< Metric name shown in the left column.
  std::string value;   ///< Formatted metric value shown in the right column.
};

/** One benchmark entry for the labelled three-column performance table. */
struct PerfEntry {
  std::string label;           ///< Row label (e.g. pass or model name).
  std::string inference_time;  ///< Formatted average inference time.
  std::string throughput;      ///< Formatted average throughput.
};

/** Formats @p v with two decimal places and no unit (e.g. "12.34"). */
inline std::string fmt2(double v) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << v;
  return oss.str();
}

/**
 * @brief Formats @p ms as a per-inference latency string (e.g. "12.34 ms/inference").
 *
 * Use this overload when the value being reported does not correspond to a single,
 * well-defined Data Parallelism size (dp_size) - i.e. the number of HW instances the
 * compiled model is replicated across and executed on in parallel (e.g. it aggregates
 * concurrently-running models with different dp_size, or the underlying runtime may
 * internally split one call into more than one dp_size-wide parallel execution) -
 * annotating it with a dp_size would misrepresent what was actually measured.
 */
inline std::string fmt_ms_per_inference(double ms) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << ms << " ms/inference";
  return oss.str();
}

/**
 * @brief Formats @p ms as a per-inference latency string annotated with the Data
 * Parallelism size (dp_size) that one inference call processes (e.g.
 * "12.34 ms/inference (dp_size=4)").
 *
 * dp_size is the number of HW instances the compiled model is replicated across; one
 * inference call runs the model in parallel on all dp_size instances simultaneously.
 * Use this overload only when one measured inference call is known to correspond to
 * exactly @p batch_size frames executed this way (e.g. one vart::Runner::execute()
 * call, whose batch size always equals the compiled model's dp_size) - not when a
 * runtime may internally loop over multiple dp_size-wide executions per call.
 */
inline std::string fmt_ms_per_inference(double ms, size_t batch_size) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << ms << " ms/inference (dp_size=" << batch_size << ")";
  return oss.str();
}

/** Formats @p fps as a throughput string (e.g. "12.34 FPS"). */
inline std::string fmt_fps(double fps) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << fps << " FPS";
  return oss.str();
}

/**
 * @brief Renders a two-column ("Metric" | "Value") performance table.
 *
 * Column widths auto-size to the widest header/cell, a centered "Performance"
 * title spans both columns, and a dashed separator is drawn between rows.
 *
 * Example output:
 * @verbatim
 * +------------------------+--------------------------------+
 * |                       Performance                       |
 * +------------------------+--------------------------------+
 * | Metric                 | Value                          |
 * +------------------------+--------------------------------+
 * | Average Inference Time | x.xx ms/inference (dp_size=N)  |
 * |------------------------|--------------------------------|
 * | Average Throughput     | xxx.xx FPS                     |
 * +------------------------+--------------------------------+
 * @endverbatim
 *
 * @param rows Metric/value pairs to display, one per table row.
 */
inline void print_perf_table(const std::vector<PerfRow>& rows) {
  /* h_* = column header text; w_* = column display widths (grown to the widest
   * header/cell so the columns line up); r = one metric/value row from rows. */
  const std::string h_metric = "Metric";
  const std::string h_value = "Value";
  size_t w_metric = h_metric.size();
  size_t w_value = h_value.size();
  for (const auto& r : rows) {
    if (r.metric.size() > w_metric) w_metric = r.metric.size();
    if (r.value.size() > w_value) w_value = r.value.size();
  }
  const std::string sep_plus = "+-" + std::string(w_metric, '-') + "-+-" + std::string(w_value, '-') + "-+";
  const std::string sep_pipe = "|-" + std::string(w_metric, '-') + "-|-" + std::string(w_value, '-') + "-|";
  const std::string title = "Performance";
  const size_t title_width = w_metric + w_value + 3;
  const size_t left_pad = (title_width > title.size()) ? (title_width - title.size()) / 2 : 0;
  const size_t right_pad = (title_width > title.size()) ? (title_width - title.size() - left_pad) : 0;
  std::cout << sep_plus << "\n";
  std::cout << "| " << std::string(left_pad, ' ') << title << std::string(right_pad, ' ') << " |\n";
  std::cout << sep_plus << "\n";
  std::cout << "| " << std::left << std::setw(w_metric) << h_metric << " | " << std::setw(w_value) << h_value
            << " |\n";
  std::cout << sep_plus << "\n";
  for (size_t i = 0; i < rows.size(); ++i) {
    std::cout << "| " << std::left << std::setw(w_metric) << rows[i].metric << " | " << std::setw(w_value)
              << rows[i].value << " |\n";
    if (i + 1 < rows.size()) {
      std::cout << sep_pipe << "\n";
    }
  }
  std::cout << sep_plus << std::endl;
}

/**
 * @brief Renders an N-column performance table with a centered "Performance" title.
 *
 * Column widths auto-size to the widest header/cell.
 *
 * Example output (group_separators = true; a divider precedes each new group,
 * i.e. each row whose first cell is non-empty). With group_separators = false the
 * inter-group divider between the two model groups is omitted:
 * @verbatim
 * +---------+------------+---------------------------------+------------------+
 * |                             Performance                                    |
 * +---------+------------+---------------------------------+------------------+
 * | Models  | Category   | Time                             | Throughput (FPS) |
 * +---------+------------+---------------------------------+------------------+
 * | Model x | PreProcess | x.xx ms/frame                    | -                |
 * |         | Inference  | x.xx ms/inference (dp_size=N)    | -                |
 * |         | Pipeline   | x.xx ms/frame                    | xxx.xx           |
 * +---------+------------+---------------------------------+------------------+
 * | Model x | PreProcess | x.xx ms/frame                    | -                |
 * |         | Inference  | x.xx ms/inference (dp_size=N)    | -                |
 * |         | Pipeline   | x.xx ms/frame                    | xxx.xx           |
 * +---------+------------+---------------------------------+------------------+
 * @endverbatim
 *
 * @note Pipeline throughput (FPS) = 1000 / pipeline_ms, i.e. one second
 * (1000 ms) divided by the average time to process one frame, where
 * pipeline_ms = PreProcess + Inference-per-frame + PostProcess (the enabled
 * per-frame stage latencies added together). All apps use this sequential
 * per-frame formula so the reported FPS is comparable across them.
 *
 * @param headers           Column headers; their count defines the column count.
 * @param rows              Table rows; cells beyond the header count are ignored and
 *                          missing trailing cells render as empty.
 * @param group_separators  When true, a divider is drawn before each new group (a row
 *                          whose first cell is non-empty, except the first row); when
 *                          false, no inter-row dividers are drawn.
 */
inline void print_perf_table(const std::vector<std::string>& headers,
                             const std::vector<std::vector<std::string>>& rows,
                             bool group_separators = true) {
  const size_t ncols = headers.size();
  /* w[c] = display width of column c (grown to the widest header/cell in that column);
   * c = column index; r = one table row (a vector of cell strings). */
  std::vector<size_t> w(ncols, 0);
  for (size_t c = 0; c < ncols; ++c) {
    w[c] = headers[c].size();
  }
  for (const auto& r : rows) {
    for (size_t c = 0; c < ncols && c < r.size(); ++c) {
      if (r[c].size() > w[c]) w[c] = r[c].size();
    }
  }

  std::string sep = "+";
  for (size_t c = 0; c < ncols; ++c) {
    sep += "-" + std::string(w[c], '-') + "-+";
  }

  size_t inner = (ncols > 0) ? (ncols - 1) * 3 : 0;  // " | " between columns
  for (size_t c = 0; c < ncols; ++c) {
    inner += w[c];
  }
  const std::string title = "Performance";
  const size_t left_pad = (inner > title.size()) ? (inner - title.size()) / 2 : 0;
  const size_t right_pad = (inner > title.size()) ? (inner - title.size() - left_pad) : 0;

  auto print_row = [&](const std::vector<std::string>& cells) {
    std::cout << "|";
    for (size_t c = 0; c < ncols; ++c) {
      std::cout << " " << std::left << std::setw(w[c]) << (c < cells.size() ? cells[c] : std::string()) << " |";
    }
    std::cout << "\n";
  };

  std::cout << sep << "\n";
  std::cout << "| " << std::string(left_pad, ' ') << title << std::string(right_pad, ' ') << " |\n";
  std::cout << sep << "\n";
  print_row(headers);
  std::cout << sep << "\n";
  for (size_t r = 0; r < rows.size(); ++r) {
    /* Draw a divider before each new group (a row whose first cell is non-empty), except the first. */
    if (group_separators && r > 0 && !rows[r].empty() && !rows[r][0].empty()) {
      std::cout << sep << "\n";
    }
    print_row(rows[r]);
  }
  std::cout << sep << std::endl;
}

/**
 * @brief Renders a labelled three-column benchmark performance table.
 *
 * Columns are @p label_col, "Average Inference Time" and "Average Throughput",
 * under a centered "Performance" title.
 *
 * Example output (label_col = "Model"):
 * @verbatim
 * +---------+---------------------------------+--------------------+
 * |                       Performance                              |
 * +---------+---------------------------------+--------------------+
 * | Model   | Average Inference Time          | Average Throughput |
 * +---------+---------------------------------+--------------------+
 * | Model_x | x.xx ms/inference (dp_size=N)   | xxx.xx FPS         |
 * | Model_x | x.xx ms/inference (dp_size=N)   | xxx.xx FPS         |
 * +---------+---------------------------------+--------------------+
 * @endverbatim
 *
 * @param label_col Header for the first (label) column.
 * @param entries   Benchmark entries, one per table row.
 */
inline void print_perf_table(const std::string& label_col, const std::vector<PerfEntry>& entries) {
  /* h_* = column header text; w_* = column display widths (grown to the widest
   * header/cell so the columns line up); e = one benchmark entry from entries. */
  const std::string h_inf = "Average Inference Time";
  const std::string h_fps = "Average Throughput";
  size_t w_label = label_col.size();
  size_t w_inf = h_inf.size();
  size_t w_fps = h_fps.size();
  for (const auto& e : entries) {
    if (e.label.size() > w_label) w_label = e.label.size();
    if (e.inference_time.size() > w_inf) w_inf = e.inference_time.size();
    if (e.throughput.size() > w_fps) w_fps = e.throughput.size();
  }
  const std::string sep = "+-" + std::string(w_label, '-') + "-+-" + std::string(w_inf, '-') + "-+-" +
                          std::string(w_fps, '-') + "-+";
  const std::string title = "Performance";
  const size_t title_width = w_label + w_inf + w_fps + 6;
  const size_t left_pad = (title_width > title.size()) ? (title_width - title.size()) / 2 : 0;
  const size_t right_pad = (title_width > title.size()) ? (title_width - title.size() - left_pad) : 0;
  std::cout << sep << "\n";
  std::cout << "| " << std::string(left_pad, ' ') << title << std::string(right_pad, ' ') << " |\n";
  std::cout << sep << "\n";
  std::cout << "| " << std::left << std::setw(w_label) << label_col << " | " << std::setw(w_inf) << h_inf << " | "
            << std::setw(w_fps) << h_fps << " |\n";
  std::cout << sep << "\n";
  for (const auto& e : entries) {
    std::cout << "| " << std::left << std::setw(w_label) << e.label << " | " << std::setw(w_inf) << e.inference_time
              << " | " << std::setw(w_fps) << e.throughput << " |\n";
  }
  std::cout << sep << std::endl;
}

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
 * @brief Sanitizes a tensor name so it can be used as a single filename
 * component.
 *
 * Tensor names (e.g. for CPU-subgraph models) may contain path separators such
 * as "/model/head/Conv_output_0". When appended to a std::filesystem::path
 * these are interpreted as directory separators, causing file creation to fail
 * because the intermediate directories do not exist. Other characters ('\\',
 * '*', '?', '"', '<', '>', '|') and control characters are illegal or
 * reserved on common filesystems and would also cause the file open to fail.
 * Every such character is replaced with '-' and leading '-' characters are
 * dropped so the result is a valid, flat filename fragment.
 *
 * @param tensor_name The raw tensor name.
 * @return A filesystem-safe version of the tensor name.
 */
inline std::string sanitize_tensor_name(const std::string& tensor_name) {
  std::string safe_name = tensor_name;
  for (char& c : safe_name) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (c == '/' || c == '\\' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
        uc < 0x20 /* control chars */) {
      c = '-';
    }
  }
  // Drop leading '-' (find_first_not_of returns npos when the name is empty or
  // becomes all dashes, in which case nothing remains to keep).
  const size_t first_keep = safe_name.find_first_not_of('-');
  safe_name = (first_keep == std::string::npos) ? std::string() : safe_name.substr(first_keep);
  return safe_name;
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
