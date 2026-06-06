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
 * VART Multi-Tenancy Application – block diagram
 *
 *   +--------------+     +------------+     +-----------+
 *   | Parse Config |---->| Initialize |---->| Validate  |
 *   | (JSON + CLI) |     | Runner(s)  |     | IFM Names |
 *   +--------------+     +------------+     | & Sizes   |
 *                                           +-----+-----+
 *                                                 |
 *                                                 v
 *                               +-----------------+-----------------+
 *                               |  Phase 2: Allocate & Load IFMs   |
 *                               |  (sequential, per model)         |
 *                               +-----------------+-----------------+
 *                                                 |
 *                                                 v
 *                               +-----------------+-----------------+
 *                               |  Phase 3: spawn one std::thread   |
 *                               |  per model – inference only       |
 *                               +---+-----------+-----------+-------+
 *                                   |           |           |
 *                                   v           v           v
 *                             +-----------+ +-----------+ +-----------+
 *                             | Thread 0  | | Thread 1  | | Thread N  |
 *                             | Model_0   | | Model_1   | | Model_N   |
 *                             |-----------| |-----------| |-----------|
 *                             | Infer x I | | Infer x I | | Infer x I |
 *                             +-----+-----+ +-----+-----+ +-----+-----+
 *                                   |             |             |
 *                                   +-----+-------+------+------+
 *                                         v
 *                                    +---------+
 *                                    |  join   |
 *                                    +---------+
 *                                         |
 *                                         v
 *                               +-----------------+-----------------+
 *                               |  Phase 4: Save OFMs              |
 *                               |  (sequential, per model)         |
 *                               +-----------------+-----------------+
 *                                                 |
 *                                                 v
 *                                            +---------+
 *                                            |  exit   |
 *                                            +---------+
 *
 *   Phases 2 and 4 run sequentially in the main thread. Phase 3 spawns
 *   one std::thread per model for inference only; the main thread joins
 *   all threads before proceeding to Phase 4.
 */

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

#include "vart_multi_tenancy.hpp"

// ---------------------------------------------------------------------------
// Model execution phase (runs in worker threads after validation passes)
// ---------------------------------------------------------------------------

/**
 * @brief Allocate tensors and load IFM data for a single model
 * @details Allocates tensors via the runner and loads IFMs from files or
 *          fills them with random bytes (--dry-run).
 * @param index Zero-based model index
 * @param model Already-initialised VartMultiTenancy instance
 * @param cfg   Model configuration
 * @param random_io  True when --dry-run mode is active
 * @return 0 on success, 1 on failure
 */
static int prepare_model(size_t index, VartMultiTenancy& model, const utils::ModelConfig& cfg, bool random_io) {
  std::string model_name = "Model_" + std::to_string(index + 1);

  try {
    // ---- Allocate tensors via runner ----
    if (!model.allocate_tensors()) {
      log_error("[ERROR] ", model_name, " [Step 1/4 Allocate]: failed to allocate tensors.");
      return 1;
    }

    // ---- Load IFM data: from files (default) or random bytes (--dry-run) ----
    if (random_io) {
      if (!model.fill_random_ifms()) {
        log_error("[ERROR] ", model_name, " [Step 2/4 Load IFMs]: failed to fill random IFMs.");
        return 1;
      }
    } else if (!cfg.ifm_node_file_map.empty()) {
      if (!model.load_ifms(cfg)) {
        log_error("[ERROR] ", model_name, " [Step 2/4 Load IFMs]: failed to load IFMs.");
        return 1;
      }
    } else {
      log_error("[ERROR] ", model_name,
                " [Step 2/4 Load IFMs]: no IFM files specified in ifm_node_file_map."
                " Provide IFM files in the JSON config or use --dry-run.");
      return 1;
    }

    return 0;
  } catch (const std::exception& e) {
    log_error("[ERROR] ", model_name, ": ", e.what());
    return 1;
  }
}

/**
 * @brief Execute inference for a single model (runs in a worker thread)
 * @details Runs inference for the given number of iterations.
 *          Allocate/load and save are handled outside this function so that
 *          the wall-clock timer covers only inference.
 * @param index Zero-based model index
 * @param model Already-initialised VartMultiTenancy instance with tensors loaded
 * @param iterations Number of inference iterations
 * @return 0 on success, 1 on failure
 */
static int run_model_execution(size_t index, VartMultiTenancy& model, uint32_t iterations) {
  std::string model_name = "Model_" + std::to_string(index + 1);

  try {
    for (uint32_t iter = 0; iter < iterations; ++iter) {
      auto status = model.infer_execute();
      if (status != vart::StatusCode::SUCCESS) {
        log_error("[ERROR] ", model_name, " [Step 3/4 Infer]: inference failed at iteration ", iter, " (status ",
                  static_cast<int>(status), ").");
        return 1;
      }
    }

    return 0;
  } catch (const std::exception& e) {
    log_error("[ERROR] ", model_name, ": ", e.what());
    return 1;
  }
}

/**
 * @brief Save OFM data for a single model
 * @param index Zero-based model index
 * @param model VartMultiTenancy instance with completed inference
 * @param cfg   Model configuration
 * @param random_io  True when --dry-run mode is active
 * @return 0 on success, 1 on failure
 */
static int save_model_ofms(size_t index, VartMultiTenancy& model, const utils::ModelConfig& cfg, bool random_io) {
  std::string model_name = "Model_" + std::to_string(index + 1);

  if (!random_io && !cfg.ofm_dir.empty()) {
    if (!model.save_ofms(cfg, index)) {
      log_error("[ERROR] ", model_name, " [Step 4/4 Save OFMs]: failed to save OFMs.");
      return 1;
    }
  } else if (random_io) {
    log_info("[INFO] ", model_name, " [Step 4/4 Save OFMs]: dry-run mode \u2013 OFMs not written to disk.");
  }

  return 0;
}

/**
 * @brief Print IFM node metadata for all models in table format
 * @details Displays configured IFM nodes and expected model input nodes
 *          in a tabular format with their data types and shapes. Called when
 *          validation finds name mismatches to help users identify and correct
 *          the mapping. Output is saved to a file and also printed to console.
 * @param opt    Parsed options containing all model configs
 * @param models Initialised model instances
 */
static void print_ifm_node_maps_with_metadata(const utils::Options& opt,
                                              const std::vector<std::unique_ptr<VartMultiTenancy>>& models) {
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
    auto tensor_type = model->get_ifm_alloc_tensor_type();
    const auto& input_tensors = model->get_input_tensors_info(tensor_type);
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
 * @brief Phase 1: Initialise all models and validate IFM mappings
 * @details Creates a VartMultiTenancy for each model entry, runs
 *          initialisation, and validates IFM node names and file sizes.
 *          If any node-name mismatch is found, prints the full IFM node
 *          metadata (name, dtype, shape) for all models before reporting
 *          the mismatch error.
 * @param opt    Parsed options containing all model configs
 * @param models Output vector of initialised model instances
 * @return 0 on success, 1 on any initialisation or validation error
 */
static int init_and_validate_models(const utils::Options& opt,
                                    std::vector<std::unique_ptr<VartMultiTenancy>>& models,
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

    auto model = std::make_unique<VartMultiTenancy>(model_name, cfg.model_cache_path);

    if (!model->initialize(cfg)) {
      log_error("[ERROR] ", model_name, " initialization failed.");
      return 1;
    }

    // In --dry-run dry-run mode the IFM file map is not consulted, so
    // skip the file-existence and size validation entirely.
    if (!random_io) {
      utils::validate_model_ifms(i, *model, cfg, validation);
    }

    models.push_back(std::move(model));
  }

  // If any IFM node-name mismatches were found across any model,
  // print full node metadata for all models as diagnostic aid.
  if (!validation.name_mismatches.empty()) {
    print_ifm_node_maps_with_metadata(opt, models);
  }

  // If any IFM mismatches were found across any model, print the
  // tables and exit.
  if (validation.has_errors()) {
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

/**
 * @brief Application entry point
 * @details Phase 1: Parses CLI, loads JSON config, validates column sharing,
 *          initialises all models, and validates all IFM node names and file
 *          sizes. Prints a summary table and exits on any mismatch.
 *          Phase 2: Launches each model's execution (allocate, load, infer,
 *          save) in its own thread.
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 when all models succeed, 1 if any model fails
 */
int main(int argc, char* argv[]) {
  try {
    // ---- Parse command-line arguments ----
    utils::Options opt;
    po::options_description desc("\n**** Multi-Tenancy App Options ****");
    utils::add_options(desc);

    auto vm = utils::parse_arguments(argc, argv, desc);
    g_verbose = std::clamp(vm["log-level"].as<int>(), 0, 2);
    uint32_t iterations = vm["runs"].as<uint32_t>();
    bool benchmark = vm["benchmark"].as<bool>();
    if (iterations < 1) {
      log_error("[ERROR] --runs must be >= 1.");
      return 1;
    }
    bool random_io = vm["dry-run"].as<bool>();
    opt = utils::load_config(vm["config"].as<std::string>());

    log_info("Loaded ", opt.models.size(), " model(s) from config.");
    log_info("Iterations per model: ", iterations);
    if (random_io) {
      log_info("Mode               : dry-run (dry run \u2013 no file I/O)");
    }
    log_info("");

    // ================================================================
    // Phase 1: Initialise all models and validate IFM mappings
    // ================================================================
    log_info("========== Phase 1: Initialise & Validate ==========");
    std::vector<std::unique_ptr<VartMultiTenancy>> models;
    int rc = init_and_validate_models(opt, models, random_io);
    if (rc != 0)
      return rc;

    // ================================================================
    // Phase 2: Allocate tensors and load IFMs (sequential)
    // ================================================================
    log_info("\n========== Phase 2: Allocate & Load ==========");
    for (size_t i = 0; i < opt.models.size(); ++i) {
      int prc = prepare_model(i, *models[i], opt.models[i], random_io);
      if (prc != 0) {
        log_error("[ERROR] Model_", (i + 1), " preparation failed.");
        return 1;
      }
    }

    // ================================================================
    // Phase 3: Inference only (parallel threads, timed)
    // ================================================================
    log_info("\n========== Phase 3: Inference (parallel threads) ==========");
    auto infer_start = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> threads;
    std::vector<int> results(opt.models.size(), 0);

    for (size_t i = 0; i < opt.models.size(); ++i) {
      threads.emplace_back(
          [i, &model = *models[i], &results, iterations]() { results[i] = run_model_execution(i, model, iterations); });
    }

    // ---- Wait for all threads to finish ----
    for (auto& t : threads) {
      t.join();
    }
    auto infer_end = std::chrono::high_resolution_clock::now();
    double infer_ms = std::chrono::duration<double, std::milli>(infer_end - infer_start).count();

    // ---- Check results ----
    for (size_t i = 0; i < results.size(); ++i) {
      if (results[i] != 0) {
        log_error("[ERROR] Model_", (i + 1), " failed.");
        return 1;
      }
      log_info("[INFO] Model_", i + 1, " [Step 3/4 Infer]: ", iterations, " iteration(s) completed.");
    }

    // ================================================================
    // Phase 4: Save OFMs (sequential)
    // ================================================================
    log_info("\n========== Phase 4: Save OFMs ==========");
    for (size_t i = 0; i < opt.models.size(); ++i) {
      int src = save_model_ofms(i, *models[i], opt.models[i], random_io);
      if (src != 0)
        return src;
    }

    log_info("\n[INFO] All models completed successfully.");

    // ---- Final summary table (AIE Columns | Models executed | OFMs saved) ----
    utils::print_execution_summary(opt, models, random_io);

    if (benchmark) {
      std::cout << "\n========== Performance ==========\n";
      double avg_infer_ms = infer_ms / static_cast<double>(iterations);
      std::ostringstream total_oss;
      total_oss << std::fixed << std::setprecision(2) << infer_ms;
      std::ostringstream avg_oss;
      avg_oss << std::fixed << std::setprecision(2) << avg_infer_ms;

      // Prefix convention used below:
      // h_ = table header label, v_ = table value text, w_ = computed column width.
      const std::string h_models = "Models per Run";
      const std::string h_runs = "Total Runs";
      const std::string h_total = "Total inference time (ms)";
      const std::string h_avg = "Avg inference for one Run (ms)";

      const std::string v_models = std::to_string(opt.models.size());
      const std::string v_runs = std::to_string(iterations);
      const std::string v_total = total_oss.str();
      const std::string v_avg = avg_oss.str();

      const size_t w_models = std::max(h_models.size(), v_models.size());
      const size_t w_runs = std::max(h_runs.size(), v_runs.size());
      const size_t w_total = std::max(h_total.size(), v_total.size());
      const size_t w_avg = std::max(h_avg.size(), v_avg.size());

      const std::string sep = "+-" + std::string(w_models, '-') + "-+-" + std::string(w_runs, '-') + "-+-" +
                              std::string(w_total, '-') + "-+-" + std::string(w_avg, '-') + "-+";

      std::cout << sep << "\n";
      std::cout << "| " << std::left << std::setw(w_models) << h_models << " | " << std::left << std::setw(w_runs)
                << h_runs << " | " << std::left << std::setw(w_total) << h_total << " | " << std::left
                << std::setw(w_avg) << h_avg << " |\n";
      std::cout << sep << "\n";
      std::cout << "| " << std::left << std::setw(w_models) << v_models << " | " << std::left << std::setw(w_runs)
                << v_runs << " | " << std::left << std::setw(w_total) << v_total << " | " << std::left
                << std::setw(w_avg) << v_avg << " |\n";
      std::cout << sep << "\n";

      std::cout << "Models per Run : Number of models configured for execution in a single run.\n";
      std::cout << "Total Runs     : Number of times configured full model set inference cycle is repeated.\n";
      std::cout << "Avg inference  : Total inference time divided by Total Runs.\n\n";
    }

  } catch (const std::exception& e) {
    log_error("Error: ", e.what());
    return 1;
  }

  return 0;
}
