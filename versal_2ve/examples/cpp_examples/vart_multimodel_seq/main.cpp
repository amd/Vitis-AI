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
 */

/*
 * VART Multi-model Sequential Application â€“ block diagram
 *
 *   +--------------+     +------------+     +-----------+     +-----------+     +----------+
 *   | Parse Config |---->| Initialize |---->| Validate  |---->| Allocate  |---->|  Load    |
 *   | (JSON + CLI) |     | Runners    |     | IFM Names |     | Tensors   |     |  IFMs    |
 *   +--------------+     +------------+     | & Sizes   |     | (per-mdl) |     | (per-mdl)|
 *                                           +-----------+     +-----------+     +----------+
 *                                                                                    |
 *                                                                                    v
 *                          +-----------+     +-----------+     +----------+     +----------+
 *                          |   exit    |<----| Save OFMs |<----| Iterate  |<----|  Infer   |
 *                          +-----------+     | (per-mdl) |     | M0..Mn   |     | (single  |
 *                                            +-----------+     | xN times |     |  thread) |
 *                                                              +----------+     +----------+
 *
 *   Models execute sequentially in a single thread. Each iteration cycles through
 *   every model once, exercising the AIE fast partition-swap path when models
 *   share columns and were compiled against the unified overlay.
 */

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

#include "vart_multimodel_seq.hpp"

// ---------------------------------------------------------------------------
// Phase 1: Initialise all models and validate IFM mappings
// ---------------------------------------------------------------------------

/**
 * @brief Print IFM node metadata for all models
 * @details Displays configured IFM nodes and expected model input nodes
 *          in a tabular format with their data types and shapes. Called when
 *          validation finds name mismatches to help users identify and correct
 *          the mapping. Output is saved to a file and also printed to console.
 * @param opt    Parsed options containing all model configs
 * @param models Initialised model instances
 */
static void print_ifm_node_maps_with_metadata(const utils::Options& opt,
                                              const std::vector<std::unique_ptr<VartMultimodelSeq>>& models) {
  // Open file for output
  std::string filename = "ifm_node_data.txt";
  std::ofstream outfile(filename);

  // Lambda to write to both cout and file
  auto write_line = [&](const std::string& line) {
    std::cout << line;
    outfile << line;
  };

  write_line("\nIFM Node Metadata for All Models\n");
  write_line("Use this table to update ifm_node_file_map in your JSON config.\n");
  write_line("Ensure IFM node names in the config match the model input node names.\n");

  for (size_t i = 0; i < models.size(); ++i) {
    const auto& cfg = opt.models[i];
    const auto& model = models[i];
    std::string model_name = "Model_" + std::to_string(i + 1);

    // Collect configured IFM nodes (sorted)
    std::vector<std::string> configured_nodes;
    for (const auto& [key, _] : cfg.ifm_node_file_map) {
      configured_nodes.push_back(key);
    }
    std::sort(configured_nodes.begin(), configured_nodes.end());

    // Collect expected input nodes (sorted)
    const auto& input_tensors = model->get_input_tensors_info();
    std::vector<std::string> expected_nodes;
    for (const auto& tensor : input_tensors) {
      expected_nodes.push_back(tensor.name);
    }
    std::sort(expected_nodes.begin(), expected_nodes.end());

    // Determine column widths
    size_t w_configured = 20;
    size_t w_expected = 20;
    size_t w_dtype = 12;
    size_t w_shape = 20;

    for (const auto& node : configured_nodes) {
      w_configured = std::max(w_configured, node.size());
    }
    for (const auto& node : expected_nodes) {
      w_expected = std::max(w_expected, node.size());
    }
    for (const auto& tensor : input_tensors) {
      std::string dtype_str = data_type_to_string(tensor.data_type);
      w_dtype = std::max(w_dtype, dtype_str.size());
      std::string shape_str = shape_to_string(tensor.shape);
      w_shape = std::max(w_shape, shape_str.size());
    }

    // Total width calculation
    size_t total_width = w_configured + w_expected + w_dtype + w_shape + 11;  // 11 for separators

    // Print model name
    write_line("\n" + model_name + ":\n");
    write_line(std::string(total_width + 2, '-') + "\n");

    // Print table header
    {
      std::ostringstream oss;
      oss << "| " << std::left << std::setw(w_configured) << "Configured IFM Nodes"
          << " | " << std::setw(w_expected) << "Expected Input Node"
          << " | " << std::setw(w_dtype) << "Data Type"
          << " | " << std::setw(w_shape) << "Shape"
          << " |\n";
      write_line(oss.str());
    }

    // Print header separator
    {
      std::ostringstream oss;
      oss << "| " << std::string(w_configured, '-') << " | " << std::string(w_expected, '-') << " | "
          << std::string(w_dtype, '-') << " | " << std::string(w_shape, '-') << " |\n";
      write_line(oss.str());
    }

    // Print rows
    size_t max_rows = std::max(configured_nodes.size(), expected_nodes.size());
    if (max_rows == 0) {
      std::ostringstream oss;
      oss << "| " << std::string(total_width, ' ') << " |\n";
      write_line(oss.str());
    } else {
      for (size_t row = 0; row < max_rows; ++row) {
        std::ostringstream oss;
        oss << "| ";

        // Configured column
        if (row < configured_nodes.size()) {
          oss << std::left << std::setw(w_configured) << configured_nodes[row];
        } else {
          oss << std::left << std::setw(w_configured) << "";
        }
        oss << " | ";

        // Expected node column
        if (row < expected_nodes.size()) {
          oss << std::left << std::setw(w_expected) << expected_nodes[row];
        } else {
          oss << std::left << std::setw(w_expected) << "";
        }
        oss << " | ";

        // Data type and shape columns
        if (row < expected_nodes.size()) {
          const auto& tensor = *std::find_if(input_tensors.begin(), input_tensors.end(),
                                             [&](const auto& t) { return t.name == expected_nodes[row]; });
          std::string dtype_str = data_type_to_string(tensor.data_type);
          std::string shape_str = shape_to_string(tensor.shape);
          oss << std::left << std::setw(w_dtype) << dtype_str << " | " << std::left << std::setw(w_shape) << shape_str;
        } else {
          oss << std::left << std::setw(w_dtype) << ""
              << " | " << std::left << std::setw(w_shape) << "";
        }
        oss << " |\n";
        write_line(oss.str());
      }
    }

    // Print bottom border
    write_line(std::string(total_width + 2, '-') + "\n");
  }

  // Close file
  outfile.close();

  // Print file save confirmation
  std::cout << "\n[INFO] IFM node validation report saved to: " << filename << "\n";
}

/**
 * @brief Initialise all models and validate IFM mappings
 * @details Creates a VartMultimodelSeq for each model entry, runs
 *          initialisation, and (unless @p random_io is set) validates IFM
 *          node names and file sizes.
 * @param opt        Parsed options containing all model configs
 * @param models     Output vector of initialised model instances
 * @param random_io  When true, IFM file validation is skipped
 * @return 0 on success, 1 on any initialisation or validation error
 */
static int init_and_validate_models(const utils::Options& opt,
                                    std::vector<std::unique_ptr<VartMultimodelSeq>>& models,
                                    bool random_io) {
  utils::ValidationResult validation;

  for (size_t i = 0; i < opt.models.size(); ++i) {
    const auto& cfg = opt.models[i];
    std::string model_name = "Model_" + std::to_string(i + 1);

    log_info("\n========== ", model_name, " ==========");
    log_info("  model_cache_path      : ", cfg.model_cache_path);
    if (cfg.is_start_column_provided)
      log_info("  start_column          : ", cfg.start_column);
    if (cfg.is_columns_sharing_provided)
      log_info("  aie_columns_sharing   : ", (cfg.aie_columns_sharing ? "shared" : "exclusive"));

    auto model = std::make_unique<VartMultimodelSeq>(model_name, cfg.model_cache_path);

    if (!model->initialize(cfg)) {
      std::cerr << "[ERROR] " << model_name << " initialization failed.\n";
      return 1;
    }

    // In --dry-run mode the IFM file map is not consulted, so
    // skip the file-existence and size validation entirely.
    if (!random_io) {
      utils::validate_model_ifms(i, *model, cfg, validation);
    }

    models.push_back(std::move(model));
  }

  // If any IFM mismatches were found across any model, print the
  // diagnostic metadata for all models first, then print the error
  // tables and exit.
  if (validation.has_errors()) {
    if (!validation.name_mismatches.empty()) {
      print_ifm_node_maps_with_metadata(opt, models);
    }
    if (!validation.name_mismatches.empty()) {
      utils::print_name_mismatch_table(validation.name_mismatches);
    }
    if (!validation.size_mismatches.empty()) {
      utils::print_size_mismatch_table(validation.size_mismatches);
    }
    return 1;
  }

  if (random_io) {
    log_info("\n[INFO] dry-run mode \u2013 skipping IFM validation.");
  } else {
    log_info("\n[INFO] IFM validation passed for all models.");
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Phase 2: Per-model setup (allocate tensors + load IFMs once)
// ---------------------------------------------------------------------------

/**
 * @brief Allocate tensors and load IFMs for a single model
 * @details Tensors are allocated once and reused for every inference iteration.
 *          The same IFM is used across all iterations. When @p random_io is
 *          set, IFM tensors are filled with random bytes instead of reading
 *          files.
 * @param index     Zero-based model index
 * @param model     Already-initialised VartMultimodelSeq instance
 * @param cfg       Model configuration
 * @param random_io When true, fill IFMs with random bytes (skip file I/O)
 * @return 0 on success, 1 on failure
 */
static int prepare_model(size_t index, VartMultimodelSeq& model, const utils::ModelConfig& cfg, bool random_io) {
  std::string model_name = "Model_" + std::to_string(index + 1);

  if (!model.allocate_tensors()) {
    log_error("[ERROR] ", model_name, " [Allocate]: failed to allocate tensors.");
    return 1;
  }

  if (random_io) {
    if (!model.fill_random_ifms()) {
      log_error("[ERROR] ", model_name, " [Load IFMs]: failed to fill random IFMs.");
      return 1;
    }
  } else if (!cfg.ifm_node_file_map.empty()) {
    if (!model.load_ifms(cfg)) {
      log_error("[ERROR] ", model_name, " [Load IFMs]: failed to load IFMs.");
      return 1;
    }
  }

  return 0;
}

// ---------------------------------------------------------------------------
// Application entry point
// ---------------------------------------------------------------------------

/**
 * @brief Application entry point
 * @details Phase 1: Parses CLI, loads JSON config, validates column sharing,
 *          initialises all models, and validates IFM node names and file sizes.
 *          Phase 2: Allocates tensors and loads IFMs once per model.
 *          Phase 3: Runs the inference loop sequentially in a single thread.
 *                   The outer loop iterates `iterations` times; the inner loop
 *                   cycles through every model. This pattern exercises the
 *                   AIE fast partition-swap path on each iteration when models
 *                   share columns and use a unified overlay.
 *          Phase 4: Saves OFMs once per model after all iterations complete
 *                   (skipped under --dry-run), and prints a unified
 *                   execution summary table with per-model average latency.
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 when all models succeed, 1 if any model fails
 */
int main(int argc, char* argv[]) {
  try {
    // ---- Parse command-line arguments ----
    utils::Options opt;
    po::options_description desc("\n**** Multi-model Sequential App Options ****");
    utils::add_options(desc);

    auto vm = utils::parse_arguments(argc, argv, desc);
    g_verbose = std::clamp(vm["log-level"].as<int>(), 0, 2);
    uint32_t iterations = vm["runs"].as<uint32_t>();
    bool benchmark = vm["benchmark"].as<bool>();
    if (iterations < 1) {
      std::cerr << "[ERROR] --runs must be >= 1.\n";
      return 1;
    }
    bool random_io = vm["dry-run"].as<bool>();
    opt = utils::load_config(vm["config"].as<std::string>());

    log_info("Loaded ", opt.models.size(), " model(s) from config.");
    log_info("Iterations over the model sequence: ", iterations);
    if (benchmark) {
      log_info("Benchmark mode     : enabled");
    }
    if (random_io) {
      log_info("Mode               : dry-run (dry run \u2013 no file I/O)");
    }
    log_info("");

    // ================================================================
    // Phase 1: Initialise all models and validate IFM mappings
    // ================================================================
    log_info("========== Phase 1: Initialise & Validate ==========");
    std::vector<std::unique_ptr<VartMultimodelSeq>> models;
    int rc = init_and_validate_models(opt, models, random_io);
    if (rc != 0)
      return rc;

    // ================================================================
    // Phase 2: Allocate tensors + load IFMs (once per model)
    // ================================================================
    log_info("\n========== Phase 2: Allocate & Load IFMs ==========");
    for (size_t i = 0; i < models.size(); ++i) {
      if (prepare_model(i, *models[i], opt.models[i], random_io) != 0) {
        return 1;
      }
    }

    // ================================================================
    // Phase 3: Sequential inference loop (single thread)
    //          Outer: iterations; Inner: models. This ordering forces
    //          a model swap on every iteration boundary, exercising the
    //          fast partition-swap path.
    // ================================================================
    log_info("\n========== Phase 3: Sequential Inference ==========");

    // Per-model accumulated inference time (nanoseconds)
    std::vector<uint64_t> total_ns(models.size(), 0);

    for (uint32_t iter = 0; iter < iterations; ++iter) {
      for (size_t i = 0; i < models.size(); ++i) {
        std::string model_name = "Model_" + std::to_string(i + 1);

        auto t0 = std::chrono::high_resolution_clock::now();
        auto status = models[i]->infer_execute();
        auto t1 = std::chrono::high_resolution_clock::now();

        if (status != vart::StatusCode::SUCCESS) {
          log_error("[ERROR] ", model_name, " [Infer] iteration ", iter + 1, "/", iterations, " failed (status ",
                    static_cast<int>(status), ").");
          return 1;
        }

        auto dur_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        total_ns[i] += static_cast<uint64_t>(dur_ns);
      }
    }

    // ================================================================
    // Phase 4: Save OFMs once per model + report unified summary
    // ================================================================
    log_info("\n========== Phase 4: Save OFMs ==========");
    if (random_io) {
      log_info("[INFO] dry-run mode \u2013 OFMs not written to disk.");
    } else {
      for (size_t i = 0; i < models.size(); ++i) {
        const auto& cfg = opt.models[i];
        if (!cfg.ofm_dir.empty()) {
          std::string model_name = "Model_" + std::to_string(i + 1);
          if (!models[i]->save_ofms(cfg, i)) {
            log_error("[ERROR] ", model_name, " [Save OFMs]: failed to save OFMs.");
            return 1;
          }
        }
      }
    }

    // ---- Per-model average inference latency (ms) ----
    std::vector<double> avg_ms(models.size(), 0.0);
    for (size_t i = 0; i < models.size(); ++i) {
      double total_ms = total_ns[i] / 1.0e6;
      avg_ms[i] = total_ms / static_cast<double>(iterations);
    }

    log_info("\n[INFO] All models completed successfully.");

    // ---- Final output tables ----
    utils::print_execution_summary(opt, models, random_io);
    if (benchmark) {
      utils::print_performance_summary(opt, iterations, avg_ms);
    }

  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
