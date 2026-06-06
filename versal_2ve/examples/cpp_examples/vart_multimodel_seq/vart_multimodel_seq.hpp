/*
 * Copyright (C) 2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software
 * is furnished to do so, subject to the following conditions:
 *
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
 *
 * Multi-model sequential-swap application header.
 * Includes command-line option parsing, JSON config loading, and a generic
 * VartMultimodelSeq class that can run any VAIML model given a
 * model cache path. All models execute sequentially in a single thread,
 * exercising the AIE fast partition-swap path when models share columns and
 * are compiled against the unified overlay.
 */

#pragma once

#include <algorithm>
#include <any>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <vart/vart_npu_tensor.hpp>
#include <vart/vart_runner_factory.hpp>

namespace po = boost::program_options;
using boost::property_tree::ptree;

// ---------------------------------------------------------------------------
// Logging – single-threaded application, no mutex required
// ---------------------------------------------------------------------------

/**
 * @brief Global verbosity level
 * @details 0=errors only, 1=+warnings, 2=+info (default).
 */
inline int g_verbose = 2;

/**
 * @brief Info log
 * @details Fold expression, appends newline. Suppressed when g_verbose < 2.
 */
template <typename... Args>
inline void log_info(Args&&... args) {
  if (g_verbose < 2)
    return;
  (std::cout << ... << std::forward<Args>(args)) << '\n';
}

/**
 * @brief Warning log
 * @details Fold expression, appends newline. Suppressed when g_verbose < 1.
 */
template <typename... Args>
inline void log_warn(Args&&... args) {
  if (g_verbose < 1)
    return;
  (std::cerr << ... << std::forward<Args>(args)) << '\n';
}

/**
 * @brief Error log
 * @details Fold expression, appends newline. Always prints regardless of g_verbose.
 */
template <typename... Args>
inline void log_error(Args&&... args) {
  (std::cerr << ... << std::forward<Args>(args)) << '\n';
}

// Forward declaration – needed by utils::validate_model_ifms().
class VartMultimodelSeq;

/**
 * @brief Utility class containing configuration types and helper methods
 *        for command-line parsing, JSON config loading, and column-sharing
 *        validation.
 */
class utils {
 public:
  /**
   * @brief Per-model configuration parsed from each entry in the JSON array.
   *
   * The fields below are forwarded to the VART runner via the
   * std::unordered_map<std::string, std::any> options bag passed to
   * vart::RunnerFactory::create_runner(...). See VartMultimodelSeq::initialize()
   * for the exact mapping. In particular:
   *   - "start_column"          (uint32_t) ← start_column           [policy: where this model starts]
   *   - "aie_columns_sharing"   (bool)     ← aie_columns_sharing    [policy: temporal vs spatial]
   *   - "input_tensor_type"     (string)   ← "HW" or "CPU"
   *   - "output_tensor_type"    (string)   ← "HW" or "CPU"
   * Both start_column and aie_columns_sharing are only forwarded when the
   * corresponding "is_*_provided" flag is true; otherwise the runner uses its
   * own defaults.
   */
  struct ModelConfig {
    std::string model_cache_path;  // "model_cache_path"
    uint32_t start_column = 0;     // "start_column" – starting NPU column
    bool is_start_column_provided = false;
    bool aie_columns_sharing = true;  // "aie_columns_sharing" – true=shared/temporal, false=exclusive/spatial
    bool is_columns_sharing_provided = false;
    std::unordered_map<std::string, std::string> ifm_node_file_map;  // tensor name → full IFM file path
    std::string ofm_dir;                                             // "ofm_dir"  – directory to save OFM files
  };

  /**
   * @brief Top-level options: CLI path + all model configs.
   */
  struct Options {
    std::string config_path;          // path supplied via -c / --config
    std::vector<ModelConfig> models;  // one entry per model in the JSON array
  };

  /**
   * @brief Register the accepted command-line options on @p desc.
   * @details Adds "--help" / "-h", "--config" / "-c" (required),
   *          "--runs" / "-r", "--benchmark" / "-b", "--dry-run" / "-d",
   *          and "--log-level" / "-l" to the given
   *          Boost.ProgramOptions description.
   */
  static void add_options(po::options_description& desc) {
    // clang-format off
    desc.add_options()
        ("help,h",                                          "Print this help message")
        ("config,c", po::value<std::string>()->required(),  "Path to JSON configuration file")
        ("runs,r", po::value<uint32_t>()->default_value(1), "Number of iterations over the full model sequence (>=1)")
      ("benchmark,b", po::bool_switch()->default_value(false), "Enable benchmark mode for repeat-run latency/performance measurements")
        ("dry-run,d", po::bool_switch()->default_value(false), "Dry-run: fill IFMs with random bytes; skip reading IFM files and writing OFM files")
        ("log-level,l", po::value<int>()->default_value(2),   "Log level (0=errors only, 1=+warnings, 2=+info)");
    // clang-format on
  }

  /**
   * @brief Parse @p argc / @p argv according to @p desc and return a variables_map.
   * @details Prints the help message and exits if "--help" is passed.
   *          Calls po::notify() to enforce required options; throws on missing args.
   */
  static po::variables_map parse_arguments(int argc, char* argv[], const po::options_description& desc) {
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);

    if (vm.count("help")) {
      std::cout << desc << "\n";
      std::exit(0);
    }

    po::notify(vm);  // enforces required options
    return vm;
  }

  /**
   * @brief Parse a single model ptree node into a ModelConfig.
   * @details Extracts model_cache_path, start_column, aie_columns_sharing,
   *          ofm_dir, and ifm_node_file_map from the given JSON node. Throws
   *          std::runtime_error if the required field model_cache_path is
   *          missing.
   * @param node   Boost.PropertyTree node representing one model entry.
   * @param index  Zero-based index of this model in the config array.
   * @return Populated ModelConfig struct.
   */
  static ModelConfig parse_model_entry(const ptree& node, size_t index) {
    ModelConfig cfg;

    cfg.model_cache_path = node.get<std::string>("model_cache_path", "");
    if (cfg.model_cache_path.empty()) {
      throw std::runtime_error("Model entry " + std::to_string(index) + " missing required field: model_cache_path");
    }

    // start_column / aie_columns_sharing are forwarded to the VART runner
    // via the options bag in initialize() – see VartMultimodelSeq::initialize().
    auto start_col_node = node.get_optional<uint32_t>("start_column");
    if (start_col_node) {
      cfg.start_column = *start_col_node;
      cfg.is_start_column_provided = true;

      if (cfg.start_column > 32) {
        std::cerr << "[ERROR] Model entry " << index << ": invalid start_column=" << cfg.start_column
                  << ". Must be in the range 0-32.\n";
        std::exit(1);
      }
    }

    auto sharing_node = node.get_optional<std::string>("aie_columns_sharing");
    if (sharing_node) {
      const std::string& val = *sharing_node;
      cfg.aie_columns_sharing = (val == "true" || val == "1");
      cfg.is_columns_sharing_provided = true;
    }
    cfg.ofm_dir = node.get<std::string>("ofm_dir", ".");

    // Parse ifm_node_file_map object (optional). Values must be full file paths.
    auto ifm_map_node = node.get_child_optional("ifm_node_file_map");
    if (ifm_map_node) {
      for (const auto& item : *ifm_map_node) {
        const std::string key = item.first;
        const std::string value = item.second.get_value<std::string>("");
        if (!key.empty() && !value.empty()) {
          cfg.ifm_node_file_map[key] = value;
        }
      }
    }

    return cfg;
  }

  /**
   * @brief Read and parse the JSON config file.
   * @details The file must contain a JSON array of model objects. After
   *          parsing, validates that no two models have overlapping NPU
   *          columns when either model sets aie_columns_sharing to false
   *          (exclusive). On conflict the application prints a diagnostic
   *          and exits. Finally prints a summary of overlay column
   *          assignments.
   * @param config_path  Filesystem path to the JSON configuration file.
   * @return Populated Options struct containing all model configs.
   */
  static Options load_config(const std::string& config_path) {
    if (!std::filesystem::exists(config_path)) {
      throw std::runtime_error("Config file not found: " + config_path);
    }

    ptree root;
    boost::property_tree::read_json(config_path, root);

    Options opt;
    opt.config_path = config_path;

    // boost::property_tree represents JSON arrays as ptree nodes with empty keys
    size_t index = 0;
    for (const auto& item : root) {
      if (!item.first.empty()) {
        throw std::runtime_error("Config file must contain a JSON array of model objects");
      }
      opt.models.push_back(parse_model_entry(item.second, index));
      ++index;
    }

    if (opt.models.empty()) {
      throw std::runtime_error("Config file must contain a non-empty JSON array of model objects");
    }

    // Validate column sharing conflicts
    {
      for (size_t i = 0; i < opt.models.size(); ++i) {
        uint32_t sc_i = opt.models[i].start_column;
        bool sharing_i = opt.models[i].aie_columns_sharing;

        for (size_t k = i + 1; k < opt.models.size(); ++k) {
          uint32_t sc_k = opt.models[k].start_column;
          bool sharing_k = opt.models[k].aie_columns_sharing;

          // Check if start columns match
          if (sc_i != sc_k)
            continue;

          // Conflict: same start column but at least one is exclusive
          if (!sharing_i || !sharing_k) {
            std::cerr << "[ERROR] Column sharing conflict between models:\n"
                      << "  Model_" << (i + 1) << " uses start_column " << sc_i
                      << " with aie_columns_sharing=" << (sharing_i ? "true (shared)" : "false (exclusive)") << "\n"
                      << "  Model_" << (k + 1) << " uses start_column " << sc_k
                      << " with aie_columns_sharing=" << (sharing_k ? "true (shared)" : "false (exclusive)") << "\n"
                      << "Same start_column cannot be used when any model "
                         "sets aie_columns_sharing to false (exclusive).\n";
            std::exit(1);
          }
        }
      }
    }

    // Print overlay column assignments
    {
      std::map<uint32_t, std::vector<std::string>> overlay_map;
      for (size_t i = 0; i < opt.models.size(); ++i) {
        uint32_t sc = opt.models[i].start_column;
        overlay_map[sc].push_back("Model_" + std::to_string(i + 1));
      }

      std::cout << "========== Overlay Column Assignments ==========\n";
      for (const auto& [start_col, models] : overlay_map) {
        std::cout << "  start_column " << start_col << " : ";
        for (size_t i = 0; i < models.size(); ++i) {
          if (i > 0)
            std::cout << ", ";
          std::cout << models[i];
        }
        std::cout << "\n";
      }
      std::cout << "================================================\n";
    }

    return opt;
  }

  // -- IFM validation types and methods -----------------------------------

  /**
   * @brief Node-name mismatch record (expected tensor name vs provided name).
   */
  struct NameMismatchEntry {
    std::string model_label;    // e.g. "Model_1"
    std::string model_path;     // model_cache_path
    std::string expected_name;  // tensor name from model metadata
    std::string provided_name;  // key from ifm_node_file_map or "(not provided)"
  };

  /**
   * @brief File-size mismatch record (expected tensor size vs actual file size).
   */
  struct SizeMismatchEntry {
    std::string model_label;         // e.g. "Model_1"
    std::string model_path;          // model_cache_path
    std::string tensor_name;         // tensor name
    std::string expected_size;       // expected size in bytes
    std::string provided_file_size;  // actual file size or "(file not found)"
  };

  /**
   * @brief Collected validation results for all models.
   */
  struct ValidationResult {
    std::vector<NameMismatchEntry> name_mismatches;
    std::vector<SizeMismatchEntry> size_mismatches;
    bool has_errors() const { return !name_mismatches.empty() || !size_mismatches.empty(); }
  };

  /**
   * @brief Validate IFM node names and file sizes for a single initialised model.
   * @details Defined out-of-line after VartMultimodelSeq is fully declared.
   */
  static void validate_model_ifms(size_t index,
                                  const VartMultimodelSeq& model,
                                  const ModelConfig& cfg,
                                  ValidationResult& result);

  /**
   * @brief Print the node-name mismatch table to stderr.
   */
  static void print_name_mismatch_table(const std::vector<NameMismatchEntry>& entries) {
    const std::string h_model = "Model (model_cache_path)";
    const std::string h_ename = "Expected Node Name";
    const std::string h_pname = "Provided Node Name";

    size_t w_model = h_model.size();
    size_t w_ename = h_ename.size();
    size_t w_pname = h_pname.size();

    for (const auto& e : entries) {
      std::string mc = e.model_label + " (" + e.model_path + ")";
      w_model = std::max(w_model, mc.size());
      w_ename = std::max(w_ename, e.expected_name.size());
      w_pname = std::max(w_pname, e.provided_name.size());
    }

    std::cerr << "\n[ERROR] IFM node-name mismatches:\n\n";

    std::cerr << std::left << std::setw(w_model) << h_model << " | " << std::setw(w_ename) << h_ename << " | "
              << h_pname << "\n";

    std::cerr << std::string(w_model, '-') << "-+-" << std::string(w_ename, '-') << "-+-" << std::string(w_pname, '-')
              << "\n";

    for (const auto& e : entries) {
      std::string mc = e.model_label + " (" + e.model_path + ")";
      std::cerr << std::left << std::setw(w_model) << mc << " | " << std::setw(w_ename) << e.expected_name << " | "
                << e.provided_name << "\n";
    }
    std::cerr << "\n";
  }

  /**
   * @brief Print the file-size mismatch table to stderr.
   */
  static void print_size_mismatch_table(const std::vector<SizeMismatchEntry>& entries) {
    const std::string h_model = "Model (model_cache_path)";
    const std::string h_tname = "Tensor Name";
    const std::string h_esize = "Expected Size";
    const std::string h_psize = "Provided File Size";

    size_t w_model = h_model.size();
    size_t w_tname = h_tname.size();
    size_t w_esize = h_esize.size();
    size_t w_psize = h_psize.size();

    for (const auto& e : entries) {
      std::string mc = e.model_label + " (" + e.model_path + ")";
      w_model = std::max(w_model, mc.size());
      w_tname = std::max(w_tname, e.tensor_name.size());
      w_esize = std::max(w_esize, e.expected_size.size());
      w_psize = std::max(w_psize, e.provided_file_size.size());
    }

    std::cerr << "\n[ERROR] IFM file-size mismatches:\n\n";

    std::cerr << std::left << std::setw(w_model) << h_model << " | " << std::setw(w_tname) << h_tname << " | "
              << std::setw(w_esize) << h_esize << " | " << h_psize << "\n";

    std::cerr << std::string(w_model, '-') << "-+-" << std::string(w_tname, '-') << "-+-" << std::string(w_esize, '-')
              << "-+-" << std::string(w_psize, '-') << "\n";

    for (const auto& e : entries) {
      std::string mc = e.model_label + " (" + e.model_path + ")";
      std::cerr << std::left << std::setw(w_model) << mc << " | " << std::setw(w_tname) << e.tensor_name << " | "
                << std::setw(w_esize) << e.expected_size << " | " << e.provided_file_size << "\n";
    }
    std::cerr << "\n";
  }

  /**
   * @brief Print the post-execution summary table
   * @details Prints one row per model with columns:
   *            | AIE Columns | Model | OFMs file saved |
   *          When @p random_io is true the OFM column reports
   *          "(skipped – dry-run)" since save_ofms() is not called in
   *          dry-run mode. Defined out-of-line after VartMultimodelSeq is
   *          fully declared.
   * @param opt           Parsed options (for start_column / sharing / ofm_dir)
   * @param models        Initialised model instances (for OFM tensor metadata)
   * @param random_io     True when --dry-run / -d dry-run mode is active
   */
  static void print_execution_summary(const Options& opt,
                                      const std::vector<std::unique_ptr<VartMultimodelSeq>>& models,
                                      bool random_io = false);

  /**
   * @brief Print the post-execution performance table
   * @details Prints one row per model with columns:
   *            | Model | Iterations | Avg (ms) |
   * @param opt           Parsed options containing all model configs
   * @param iterations    Number of iterations executed for each model
   * @param avg_ms        Per-model average inference latency in milliseconds
   */
  static void print_performance_summary(const Options& opt, uint32_t iterations, const std::vector<double>& avg_ms);

 private:
  utils() = delete;
};  // class utils

// ---------------------------------------------------------------------------
// OFM filename helpers — shared by save_ofms() and print_execution_summary()
// ---------------------------------------------------------------------------

/**
 * @brief Convert a vart::DataType enum to a short string (e.g. "int8").
 */
inline std::string data_type_to_string(vart::DataType dt) {
  switch (dt) {
    case vart::DataType::BOOLEAN:
      return "bool";
    case vart::DataType::INT8:
      return "int8";
    case vart::DataType::UINT8:
      return "uint8";
    case vart::DataType::INT16:
      return "int16";
    case vart::DataType::UINT16:
      return "uint16";
    case vart::DataType::BF16:
      return "bfloat16";
    case vart::DataType::FP16:
      return "float16";
    case vart::DataType::INT32:
      return "int32";
    case vart::DataType::UINT32:
      return "uint32";
    case vart::DataType::FLOAT32:
      return "float32";
    case vart::DataType::INT64:
      return "int64";
    case vart::DataType::UINT64:
      return "uint64";
    default:
      return "unknown";
  }
}

/**
 * @brief Join a shape vector with 'x' separators (e.g. {1,224,224,3} → "1x224x224x3").
 */
inline std::string shape_to_string(const std::vector<uint32_t>& shape) {
  std::string s;
  for (size_t i = 0; i < shape.size(); ++i) {
    if (i > 0)
      s += 'x';
    s += std::to_string(shape[i]);
  }
  return s;
}

// ---------------------------------------------------------------------------
// Generic VART model runner class (single-threaded sequential-swap variant)
// ---------------------------------------------------------------------------

/**
 * @class VartMultimodelSeq
 * @brief Generic model runner that works with any VAIML model cache.
 *
 * Counterpart to VartSpatialTemporalApp but designed for single-threaded
 * sequential execution. When all models share the same NPU columns and were
 * compiled against the unified overlay, swapping between them on the AIE
 * columns uses the fast partition-swap path.
 *
 * Uses runner->allocate_npu_tensor / deallocate_npu_tensor for buffer
 * management (no direct XRT device or BO usage).
 */
class VartMultimodelSeq {
 public:
  /**
   * @brief Construct a VartMultimodelSeq.
   * @param model_name       Human-readable name used in log messages.
   * @param model_cache_dir  Path to the compiled model cache directory.
   * @param input_tensor_type  HW or CPU.
   * @param output_tensor_type HW or CPU.
   */
  VartMultimodelSeq(const std::string& model_name,
                    const std::string& model_cache_dir,
                    vart::TensorType input_tensor_type = vart::TensorType::HW,
                    vart::TensorType output_tensor_type = vart::TensorType::HW);

  ~VartMultimodelSeq();

  /**
   * @brief Create the VART runner and retrieve tensor metadata.
   * @param cfg Model configuration (includes vitisai config, start_column, etc.).
   * @return true on success.
   */
  bool initialize(const utils::ModelConfig& cfg);

  // -- Tensor metadata accessors ------------------------------------------

  /**
   * @brief Return the number of input tensors reported by the runner.
   */
  size_t get_num_input_tensors() const;

  /**
   * @brief Return the number of output tensors reported by the runner.
   */
  size_t get_num_output_tensors() const;

  /**
   * @brief Return tensor metadata (name, shape, size) for input tensors.
   * @param type  HW or CPU tensor view.
   */
  const std::vector<vart::NpuTensorInfo>& get_input_tensors_info(vart::TensorType type = vart::TensorType::HW) const;

  /**
   * @brief Return tensor metadata (name, shape, size) for output tensors.
   * @param type  HW or CPU tensor view.
   */
  const std::vector<vart::NpuTensorInfo>& get_output_tensors_info(vart::TensorType type = vart::TensorType::HW) const;

  /**
   * @brief Return the TensorType used when allocating input (IFM) buffers.
   */
  vart::TensorType get_ifm_alloc_tensor_type() const;

  /**
   * @brief Return the TensorType used when allocating output (OFM) buffers.
   */
  vart::TensorType get_ofm_alloc_tensor_type() const;

  // -- Buffer management (uses runner allocator) --------------------------

  /**
   * @brief Allocate IFM and OFM tensors via the runner's allocate_npu_tensor().
   * @details Must be called after initialize().
   * @return true on success.
   */
  bool allocate_tensors();

  /**
   * @brief Deallocate all previously allocated IFM and OFM tensors.
   * @details Called automatically by the destructor.
   */
  void deallocate_tensors();

  // -- I/O ----------------------------------------------------------------

  /**
   * @brief Load input feature maps (IFMs) from binary files into allocated tensors.
   * @details Matches tensor names against cfg.ifm_node_file_map (values are full
   *          file paths). The IFM binary may contain a single frame or multiple
   *          frames concatenated end-to-end. Partial-frame counts (fewer than
   *          batch_size) are allowed: only the first frames are loaded and a
   *          WARN is logged – VART ML handles partial-batch execution.
   * @param cfg  Model configuration containing ifm_node_file_map (full paths).
   * @return true on success.
   */
  bool load_ifms(const utils::ModelConfig& cfg);

  /**
   * @brief Fill all allocated IFM tensors with random uint8_t bytes.
   * @details Used by --dry-run / -d dry-run mode; requires no IFM files.
   *          Tensors must have been allocated via allocate_tensors() first.
   * @return true on success, false if tensors are not yet allocated.
   */
  bool fill_random_ifms();

  /**
   * @brief Save output feature maps (OFMs) from allocated tensors to binary files.
   * @details One file per OFM node (<node_name>_<shape>_<dtype>.bin) placed in a
   *          per-model subdirectory (ofm_model_1, ofm_model_2, ...) within ofm_dir.
   *          Only m_actual_batch_size frames are written — if the user provided
   *          fewer frames than the model's batch size, only those frames appear
   *          in the output files. Creates the output directory if it does not
   *          already exist.
   * @param cfg          Model configuration containing ofm_dir.
   * @param model_index  Zero-based model index (directory is named model_index + 1).
   * @return true on success.
   */
  bool save_ofms(const utils::ModelConfig& cfg, size_t model_index);

  // -- Inference ----------------------------------------------------------

  /**
   * @brief Execute inference on the loaded IFMs and store results in OFM tensors.
   * @details Only the actual number of loaded frames (m_actual_batch_size) is
   *          forwarded to the runner. When the user provides fewer frames than
   *          the model's batch size, inference is executed with the partial
   *          batch only — no dummy/stale data is sent to the runner.
   * @return vart::StatusCode::SUCCESS on success, FAILURE otherwise.
   */
  vart::StatusCode infer_execute();

  /**
   * @brief Return the human-readable model name.
   */
  const std::string& name() const { return m_model_name; }

 private:
  /**
   * @brief Query the runner for input/output tensor counts and metadata.
   * @details Populates the m_input/output_hw/cpu_tensors_info vectors.
   * @return true on success.
   */
  bool get_tensor_metadata();

  std::string m_model_name;
  std::string m_model_cache_dir;
  vart::TensorType m_input_tensor_type;
  vart::TensorType m_output_tensor_type;

  std::shared_ptr<vart::Runner> m_runner;
  size_t m_num_input_tensors = 0;
  size_t m_num_output_tensors = 0;

  std::vector<vart::NpuTensorInfo> m_input_hw_tensors_info;
  std::vector<vart::NpuTensorInfo> m_input_cpu_tensors_info;
  std::vector<vart::NpuTensorInfo> m_output_hw_tensors_info;
  std::vector<vart::NpuTensorInfo> m_output_cpu_tensors_info;

  // Runner-allocated tensors: [batch][tensor_idx]
  std::vector<std::vector<vart::NpuTensor>> m_ifm_tensors;
  std::vector<std::vector<vart::NpuTensor>> m_ofm_tensors;
  size_t m_batch_size = 1;         // populated by allocate_tensors() from runner->get_batch_size()
  size_t m_actual_batch_size = 0;  // actual number of frames loaded by load_ifms() or fill_random_ifms();
                                   // may be less than m_batch_size for partial batches
  bool m_tensors_allocated = false;
};

// ---------------------------------------------------------------------------
// Out-of-line definition – requires complete VartMultimodelSeq type.
// ---------------------------------------------------------------------------

/**
 * @brief Validate IFM node names and file sizes for a single initialised model
 * @details Checks each input tensor name against cfg.ifm_node_file_map and,
 *          when the name matches, verifies that the binary file size equals the
 *          expected tensor size. Also reports extra provided names that do not
 *          correspond to any expected tensor.
 * @param index  Zero-based model index
 * @param model  Initialised VartMultimodelSeq (tensor metadata ready)
 * @param cfg    Model configuration with ifm_node_file_map
 * @param result Validation result to append mismatches to
 */
inline void utils::validate_model_ifms(size_t index,
                                       const VartMultimodelSeq& model,
                                       const ModelConfig& cfg,
                                       ValidationResult& result) {
  if (cfg.ifm_node_file_map.empty())
    return;

  std::string label = "Model_" + std::to_string(index + 1);
  auto tensor_type = model.get_ifm_alloc_tensor_type();
  const auto& info = model.get_input_tensors_info(tensor_type);
  size_t num = model.get_num_input_tensors();

  // Collect unmatched expected tensor names and unmatched provided names
  std::vector<std::string> unmatched_expected;
  for (size_t j = 0; j < num; ++j) {
    const std::string& tensor_name = info[j].name;
    auto it = cfg.ifm_node_file_map.find(tensor_name);

    if (it == cfg.ifm_node_file_map.end()) {
      // Expected name not found in provided map
      unmatched_expected.push_back(tensor_name);
    } else {
      // Name matches – check file existence and size
      std::filesystem::path file_path = it->second;

      if (!std::filesystem::exists(file_path)) {
        result.size_mismatches.push_back(
            {label, cfg.model_cache_path, tensor_name, std::to_string(info[j].size_in_bytes), "(file not found)"});
      } else {
        auto actual = std::filesystem::file_size(file_path);
        // The IFM binary may pack multiple frames concatenated; the runner's
        // batch_size determines how many frames are expected. Validate that
        // file size is a positive multiple of the per-frame tensor size.
        if (info[j].size_in_bytes == 0 || actual == 0 || actual % info[j].size_in_bytes != 0) {
          result.size_mismatches.push_back({label, cfg.model_cache_path, tensor_name,
                                            "N * " + std::to_string(info[j].size_in_bytes), std::to_string(actual)});
        }
      }
    }
  }

  // Collect extra provided names that don't match any expected tensor
  std::vector<std::string> unmatched_provided;
  for (const auto& [key, val] : cfg.ifm_node_file_map) {
    bool found = false;
    for (size_t j = 0; j < num; ++j) {
      if (info[j].name == key) {
        found = true;
        break;
      }
    }
    if (!found) {
      unmatched_provided.push_back(key);
    }
  }

  // Pair unmatched expected names with unmatched provided names
  size_t max_rows = std::max(unmatched_expected.size(), unmatched_provided.size());
  for (size_t r = 0; r < max_rows; ++r) {
    std::string exp_name = (r < unmatched_expected.size()) ? unmatched_expected[r] : "";
    std::string prov_name = (r < unmatched_provided.size()) ? unmatched_provided[r] : "";
    result.name_mismatches.push_back({label, cfg.model_cache_path, exp_name, prov_name});
  }
}

/**
 * @brief Print the post-execution summary table
 * @details For each model prints the AIE column range, the model label, and
 *          the OFM file paths written by save_ofms(). When @p random_io is true
 *          the OFM column reports
 *          "(skipped \xe2\x80\x93 dry-run)". For brevity the listed path uses
 *          the base tensor name (<tensor_name>.bin).
 * @param opt         Parsed options containing all model configs
 * @param models      Vector of initialised VartMultimodelSeq instances
 * @param random_io   True when --dry-run / -d dry-run mode is active
 */
inline void utils::print_execution_summary(const Options& opt,
                                           const std::vector<std::unique_ptr<VartMultimodelSeq>>& models,
                                           bool random_io) {
  struct Row {
    std::string aie_columns;
    std::string model_label;
    std::string ofm_files;
  };
  std::vector<Row> rows;

  const std::string h_cols = "Start Column";
  const std::string h_model = "Model";
  const std::string h_ofm = "OFMs file saved";

  size_t w_cols = h_cols.size();
  size_t w_model = h_model.size();
  size_t w_ofm = h_ofm.size();

  for (size_t i = 0; i < opt.models.size(); ++i) {
    const auto& cfg = opt.models[i];
    Row r;

    // Start column for this model (plus sharing mode when set)
    r.aie_columns = std::to_string(cfg.start_column) +
                    (cfg.is_columns_sharing_provided ? (cfg.aie_columns_sharing ? " (shared)" : " (exclusive)") : "");

    r.model_label = "Model_" + std::to_string(i + 1);

    std::string joined;
    if (random_io) {
      joined = "(skipped \u2013 random-io)";
    } else if (i < models.size() && models[i]) {
      auto tensor_type = models[i]->get_ofm_alloc_tensor_type();
      const auto& oi = models[i]->get_output_tensors_info(tensor_type);
      for (size_t j = 0; j < oi.size(); ++j) {
        if (!joined.empty())
          joined += ", ";
        // Build the same filename that save_ofms() produces:
        // <ofm_dir>/ofm_model_<N>/<name>_<shape>_<datatype>.bin
        std::string fname =
            oi[j].name + "_" + shape_to_string(oi[j].shape) + "_" + data_type_to_string(oi[j].data_type) + ".bin";
        std::filesystem::path p = std::filesystem::path(cfg.ofm_dir) / ("ofm_model_" + std::to_string(i + 1)) / fname;
        joined += p.string();
      }
    }
    if (joined.empty())
      joined = "(none)";
    r.ofm_files = joined;

    w_cols = std::max(w_cols, r.aie_columns.size());
    w_model = std::max(w_model, r.model_label.size());
    w_ofm = std::max(w_ofm, r.ofm_files.size());

    rows.push_back(std::move(r));
  }

  std::cout << "\n========== Execution Summary ==========\n";
  const std::string sep =
      std::string(w_cols, '-') + "-+-" + std::string(w_model, '-') + "-+-" + std::string(w_ofm, '-');
  std::cout << sep << "\n";
  std::cout << std::left << std::setw(w_cols) << h_cols << " | " << std::setw(w_model) << h_model << " | " << h_ofm
            << "\n";
  std::cout << sep << "\n";
  for (const auto& r : rows) {
    std::cout << std::left << std::setw(w_cols) << r.aie_columns << " | " << std::setw(w_model) << r.model_label
              << " | " << r.ofm_files << "\n";
  }
  std::cout << sep << "\n";
}

/**
 * @brief Print the post-execution performance table
 * @details For each model prints the number of inference iterations executed
 *          and the average per-iteration latency in milliseconds.
 * @param opt         Parsed options containing all model configs
 * @param iterations  Number of inference iterations per model
 * @param avg_ms      Per-model average inference latency in milliseconds
 */
inline void utils::print_performance_summary(const Options& opt,
                                             uint32_t iterations,
                                             const std::vector<double>& avg_ms) {
  struct Row {
    std::string model_label;
    std::string iters;
    std::string avg_ms_str;
  };
  std::vector<Row> rows;

  // Prefix convention for table formatting:
  // h_ = header text, w_ = computed column width, v_/row fields = row values.
  const std::string h_model = "Model";
  const std::string h_iters = "Iterations";
  const std::string h_avg = "Avg (ms)";

  size_t w_model = h_model.size();
  size_t w_iters = h_iters.size();
  size_t w_avg = h_avg.size();

  for (size_t i = 0; i < opt.models.size(); ++i) {
    Row r;
    r.model_label = "Model_" + std::to_string(i + 1);
    r.iters = std::to_string(iterations);

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3) << (i < avg_ms.size() ? avg_ms[i] : 0.0);
    r.avg_ms_str = oss.str();

    w_model = std::max(w_model, r.model_label.size());
    w_iters = std::max(w_iters, r.iters.size());
    w_avg = std::max(w_avg, r.avg_ms_str.size());

    rows.push_back(std::move(r));
  }

  std::cout << "\n========== Performance ==========\n";
  const std::string sep =
      std::string(w_model, '-') + "-+-" + std::string(w_iters, '-') + "-+-" + std::string(w_avg, '-');
  std::cout << sep << "\n";
  std::cout << std::left << std::setw(w_model) << h_model << " | " << std::setw(w_iters) << h_iters << " | " << h_avg
            << "\n";
  std::cout << sep << "\n";
  for (const auto& r : rows) {
    std::cout << std::left << std::setw(w_model) << r.model_label << " | " << std::setw(w_iters) << r.iters << " | "
              << r.avg_ms_str << "\n";
  }
  std::cout << sep << "\n";
}
