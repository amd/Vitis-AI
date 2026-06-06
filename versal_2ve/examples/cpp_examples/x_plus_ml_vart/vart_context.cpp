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
 * EVENT SHALL AMD BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 * OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. Except as contained in this notice, the name of the AMD shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from AMD.
 */

/**
 * @file vart_context.cpp
 * @brief This file has the methods for creating VART modules context required
 *        for the application.
 */

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>
#include "common/app_utils.hpp"
#include "x_plus_ml_app.hpp"

using namespace vart;

/* Initialize the parameters and handles in the AppContext structure
 */
void init_app_context(AppContext* ctx) {
  /* Initialize handle parameters */
  ctx->log_level = AppLogLevel::WARNING;

  ctx->dump_all_inputs = false;
  ctx->input_file_path.clear();
  ctx->output_dir_path.clear();
  ctx->xclbin_location.clear();
  ctx->app_config.clear();
  ctx->model_json_path.clear();
  ctx->model_snap_path.clear();
  ctx->preproc_json_str.clear();
  ctx->model_ifms_configs.clear();

  ctx->postproc_cfg.clear();
  ctx->postprocess_enable.clear();
  ctx->metaconvert_enable.clear();
  ctx->postprocess.clear();
  ctx->postproc_json_str.clear();
  ctx->any_postprocess_enabled = false;

  ctx->input_height = 0;
  ctx->input_width = 0;
  ctx->iteration_counter = 0;
  ctx->num_model_instances = 0;
  ctx->batch_size_per_model.clear();
  ctx->max_iterations = 1;
  ctx->device_idx = PL_DEVICE_INDEX;
  ctx->device = nullptr;

  ctx->num_frame_to_process = APP_PROCESS_ALL_FRAMES;
  ctx->preprocess_enable = false;
  ctx->output_dir_path = "output";
  ctx->is_benchmark_enabled = false;
  ctx->pipeline_timeout_seconds = 0;  // Will be set after benchmark flag is known
  ctx->ppe_mbank_in = 0;

  // Initialize pointers and containers
  ctx->preprocess.clear();
  ctx->inference.clear();
  ctx->model_info.clear();

  // Note: Individual queues for preprocessing and inference instances are
  // created dynamically during context creation
}

/* Helper function to create multiple component instances based on processing mode */

/**
 * @brief Parse inference input configuration from ifms-config
 * Mode-specific parsing: preprocessing (single entry) vs inference-only (multiple entries)
 */
static bool parse_inference_ifms_config(AppContext* ctx,
                                        const pt::ptree& config,
                                        size_t model_idx,
                                        std::vector<AppContext::IfmsConfig>& ifms_configs) {
  AppLogLevel log_level = ctx->log_level;

  auto ifms_config_opt = config.get_child_optional("ifms-config");
  if (!ifms_config_opt) {
    return true;  // ifms-config is optional (preprocess broadcast mode)
  }

  if (ctx->preprocess_enable) {
    // PREPROCESSING MODE
    // Skip parsing if CLI --input-file provided (overrides ifms-config)
    if (!ctx->input_file_path.empty()) {
      APP_LOG(AppLogLevel::DEBUG, log_level, "\tModel %zu: Skipping ifms-config (CLI --input-file overrides)",
              model_idx);
      return true;  // Empty ifms_configs vector
    }

    // Parse only first entry (single image file)
    auto it = ifms_config_opt.get().begin();
    if (it != ifms_config_opt.get().end()) {
      AppContext::IfmsConfig ifms;
      ifms.tensor_name = it->second.get<std::string>("name", "");
      ifms.file_path = it->second.get<std::string>("file", "");
      ifms.width = 0;
      ifms.height = 0;

      std::string ext = get_file_extension_lowercase(ifms.file_path);

      // Parse dim ONLY for NV12/BGR (mandatory for these formats)
      if (ext == "nv12" || ext == "bgr") {
        auto dim_opt = it->second.get_optional<std::string>("dim");
        if (dim_opt) {
          if (sscanf(dim_opt.get().c_str(), "%ux%u", &ifms.width, &ifms.height) != 2) {
            APP_LOG(AppLogLevel::ERROR, log_level, "Model %zu: Invalid dim format in ifms-config: %s", model_idx,
                    dim_opt.get().c_str());
            return false;
          }
        } else {
          APP_LOG(AppLogLevel::ERROR, log_level, "Model %zu: NV12/BGR file requires 'dim' field in ifms-config: %s",
                  model_idx, ifms.file_path.c_str());
          return false;
        }
      }
      // JPEG: dim not required (auto-detected from file)

      /* Preprocessing mode: the file path is the only mandatory field at
       * parse time. 'name' (if provided) is cross-checked against the
       * runner's single input tensor in create_file_reader_context();
       * the CLI broadcast path overrides ifms-config entirely and never
       * reaches that check. */
      if (ifms.file_path.empty()) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Model %zu: ifms-config[0]: 'file' is missing or empty.", model_idx);
        return false;
      }
      ifms_configs.push_back(ifms);
      std::string dim_str =
          (ifms.width > 0) ? ", dim=" + std::to_string(ifms.width) + "x" + std::to_string(ifms.height) : "";
      APP_LOG(AppLogLevel::DEBUG, log_level, "\tParsed ifms-config[0]: tensor=%s, file=%s%s", ifms.tensor_name.c_str(),
              ifms.file_path.c_str(), dim_str.c_str());

      // Warn if multiple entries present (only first used)
      if (ifms_config_opt.get().size() > 1) {
        APP_LOG(AppLogLevel::WARNING, log_level,
                "Model %zu: Preprocessing uses only first ifms-config entry (found %zu)", model_idx,
                ifms_config_opt.get().size());
      }
    }
  } else {
    // INFERENCE-ONLY MODE: Parse all entries (one per input tensor).
    // 'name' must match the runner-reported input tensor name; binding is
    // resolved later in create_file_reader_context() once the runner is up.
    std::unordered_map<std::string, std::string> seen;  // name -> first-bound file (for dedup error msg)
    size_t idx = 0;
    for (const auto& ifm : ifms_config_opt.get()) {
      AppContext::IfmsConfig ifms;
      ifms.tensor_name = ifm.second.get<std::string>("name", "");
      ifms.file_path = ifm.second.get<std::string>("file", "");
      ifms.width = 0;  // Not used in inference-only mode
      ifms.height = 0;

      if (ifms.tensor_name.empty()) {
        APP_LOG(AppLogLevel::ERROR, log_level,
                "Model %zu: ifms-config[%zu]: 'name' is missing or empty; it must match the runner-reported input "
                "tensor name.",
                model_idx, idx);
        return false;
      }
      if (ifms.file_path.empty()) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Model %zu: ifms-config[%zu] (name='%s'): 'file' is missing or empty.",
                model_idx, idx, ifms.tensor_name.c_str());
        return false;
      }
      if (!std::filesystem::exists(ifms.file_path)) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Model %zu: ifms-config[%zu] (name='%s'): file not found: %s", model_idx,
                idx, ifms.tensor_name.c_str(), ifms.file_path.c_str());
        return false;
      }
      if (std::filesystem::path(ifms.file_path).extension() != ".bin") {
        APP_LOG(AppLogLevel::ERROR, log_level,
                "Model %zu: ifms-config[%zu] (name='%s'): inference-only mode requires .bin extension: %s", model_idx,
                idx, ifms.tensor_name.c_str(), ifms.file_path.c_str());
        return false;
      }
      auto seen_it = seen.find(ifms.tensor_name);
      if (seen_it != seen.end()) {
        APP_LOG(AppLogLevel::ERROR, log_level,
                "Model %zu: ifms-config[%zu]: duplicate 'name'='%s' (previously bound to file '%s').", model_idx, idx,
                ifms.tensor_name.c_str(), seen_it->second.c_str());
        return false;
      }
      seen.emplace(ifms.tensor_name, ifms.file_path);
      ifms_configs.push_back(ifms);
      APP_LOG(AppLogLevel::DEBUG, log_level, "\tParsed ifms-config[%zu]: tensor=%s, file=%s", idx,
              ifms.tensor_name.c_str(), ifms.file_path.c_str());
      ++idx;
    }
  }

  return true;
}

/* Utility functions for accessing model info by instance */
const InferenceConfig& get_model_info_for_instance(const AppContext* ctx, size_t instance_idx) {
  if (ctx->model_info.empty()) {
    throw std::runtime_error("No model_infos available");
  }

  if (instance_idx >= ctx->model_info.size()) {
    // Return first instance if index is out of bounds
    return ctx->model_info[0];
  }

  return ctx->model_info[instance_idx];
}

static bool create_queue_binding_context(AppContext* ctx) {
  AppLogLevel log_level = ctx->log_level;

  APP_LOG(AppLogLevel::INFO, log_level, "\tCreating individual queues for %u instances (preprocess_enable=%d)",
          ctx->num_model_instances, ctx->preprocess_enable);

  // Always create inference queues
  ctx->inf_inqs_vec.clear();
  ctx->inf_outqs_vec.clear();
  ctx->inf_inqs_vec.reserve(ctx->num_model_instances);
  ctx->inf_outqs_vec.reserve(ctx->num_model_instances);

  for (uint32_t i = 0; i < ctx->num_model_instances; i++) {
    // Create dedicated input/output queues for each inference instance
    auto inf_input_queue = std::make_unique<AppQueue<PreprocessedFrame>>(INFERENCE_QUEUE_DEPTH);
    auto inf_output_queue = std::make_unique<AppQueue<InferenceResult>>(INFERENCE_QUEUE_DEPTH);

    ctx->inf_inqs_vec.push_back(std::move(inf_input_queue));
    ctx->inf_outqs_vec.push_back(std::move(inf_output_queue));

    APP_LOG(AppLogLevel::DEBUG, log_level, "\t - Created queues for inference instance %u", i);
  }

  // Compute if ANY model has postprocessing enabled (independent of preprocessing mode)
  // This must be done BEFORE the preprocessing check to support Mode 3 (Inference + PostProcessing without
  // Preprocessing)
  ctx->any_postprocess_enabled = false;
  for (const auto& enabled : ctx->postprocess_enable) {
    if (enabled) {
      ctx->any_postprocess_enabled = true;
      break;
    }
  }

  // Create completion notification queues for postprocess instances (independent of preprocessing mode)
  // This supports Mode 3 (Inference + PostProcessing without Preprocessing)
  ctx->postproc_outqs_vec.clear();
  ctx->postproc_outqs_vec.resize(ctx->num_model_instances);

  if (ctx->any_postprocess_enabled) {
    for (uint32_t i = 0; i < ctx->num_model_instances; i++) {
      if (i < ctx->postprocess_enable.size() && ctx->postprocess_enable[i]) {
        // Create completion queue for this postprocess instance
        ctx->postproc_outqs_vec[i] = std::make_unique<AppQueue<ProcessingComplete>>(INFERENCE_QUEUE_DEPTH);
        APP_LOG(AppLogLevel::DEBUG, log_level,
                "\t - Created completion (out) queue for postprocess instance %u (depth=%u)", i, INFERENCE_QUEUE_DEPTH);
      }
    }
    APP_LOG(AppLogLevel::DEBUG, log_level, "\t - postproc_outqs_vec initialized (%zu instances)",
            ctx->postproc_outqs_vec.size());
  }

  if (ctx->preprocess_enable) {
    // Create preprocessing queues and bindings
    APP_LOG(AppLogLevel::INFO, log_level, "\t - Creating preprocessing queues and bindings");

    // Compute if ANY model has metaconvert enabled (requires preprocessing + postprocessing + metaconvert-config)
    bool any_metaconvert_enabled = false;
    for (const auto& enabled : ctx->metaconvert_enable) {
      if (enabled) {
        any_metaconvert_enabled = true;
        break;
      }
    }

    // Always initialize orig_frame_qs_vec with nullptrs to prevent undefined behavior
    ctx->orig_frame_qs_vec.clear();
    ctx->orig_frame_qs_vec.resize(ctx->num_model_instances);

    // Create queues only for models with metaconvert enabled (not just postprocessing)
    if (any_metaconvert_enabled) {
      for (uint32_t i = 0; i < ctx->num_model_instances; i++) {
        if (i < ctx->metaconvert_enable.size() && ctx->metaconvert_enable[i]) {
          // Replace nullptr with actual queue at this index
          ctx->orig_frame_qs_vec[i] = std::make_unique<AppQueue<InputFrame>>(POSTPROCESS_QUEUE_DEPTH);
          APP_LOG(AppLogLevel::INFO, log_level,
                  "\t - Created original frame queue for metaconvert instance %u (depth=%u)", i,
                  POSTPROCESS_QUEUE_DEPTH);
        }
      }
    }
    APP_LOG(AppLogLevel::DEBUG, log_level, "\t - orig_frame_qs_vec initialized (%zu instances, %d with queues)",
            ctx->orig_frame_qs_vec.size(), any_metaconvert_enabled ? 1 : 0);

    ctx->preproc_inqs_vec.clear();
    ctx->preproc_outqs_vec.clear();
    ctx->preproc_inqs_vec.reserve(ctx->num_model_instances);
    ctx->preproc_outqs_vec.reserve(ctx->num_model_instances);

    for (uint32_t i = 0; i < ctx->num_model_instances; i++) {
      // Create dedicated input/output queues for each preprocessing instance
      auto preproc_input_queue = std::make_unique<AppQueue<InputFrame>>(PREPROCESS_QUEUE_DEPTH);
      auto preproc_output_queue = std::make_unique<AppQueue<PreprocessedFrame>>(PREPROCESS_QUEUE_DEPTH);

      ctx->preproc_inqs_vec.push_back(std::move(preproc_input_queue));
      ctx->preproc_outqs_vec.push_back(std::move(preproc_output_queue));

      APP_LOG(AppLogLevel::DEBUG, log_level, "\t - Created queues for preprocessing instance %u", i);
    }

    // Initialize binding map for tight coupling (preprocessing → inference)
    APP_LOG(AppLogLevel::INFO, log_level, "\t - Initializing preprocessing-inference binding map for %u instances",
            ctx->num_model_instances);

    ctx->instance_bindings.clear();
    ctx->instance_bindings.reserve(ctx->num_model_instances);

    for (uint32_t i = 0; i < ctx->num_model_instances; i++) {
      // Create 1:1 binding between preprocessing instance(i) and inference instance(i)
      PreprocessInferenceBinding binding(i, i);

      // Map to individual dedicated queues for complete isolation
      binding.preproc_inq = ctx->preproc_inqs_vec[i].get();
      binding.preproc_outq = ctx->preproc_outqs_vec[i].get();
      binding.inf_inq = ctx->inf_inqs_vec[i].get();
      binding.inf_outq = ctx->inf_outqs_vec[i].get();

      ctx->instance_bindings.push_back(binding);

      APP_LOG(AppLogLevel::DEBUG, log_level, "\t - Created binding: preprocess[%u] ↔ inference[%u]",
              binding.preproc_inst_id, binding.inf_inst_id);
    }

    APP_LOG(AppLogLevel::INFO, log_level, "\t Successfully initialized %zu preprocessing-inference bindings",
            ctx->instance_bindings.size());
  } else {
    // No preprocessing queues needed, but create minimal inference-only bindings
    // File readers will connect directly to inference input queues
    APP_LOG(AppLogLevel::INFO, log_level, "\t - Skipping preprocessing queues (file readers → inference direct)");

    ctx->preproc_inqs_vec.clear();
    ctx->preproc_outqs_vec.clear();

    // Create minimal bindings for inference-only mode
    APP_LOG(AppLogLevel::INFO, log_level, "\t - Creating inference-only bindings for %u instances",
            ctx->num_model_instances);

    ctx->instance_bindings.clear();
    ctx->instance_bindings.reserve(ctx->num_model_instances);

    for (uint32_t i = 0; i < ctx->num_model_instances; i++) {
      // Create binding with only inference queues (preprocessing queues are nullptr)
      PreprocessInferenceBinding binding(i, i);

      // No preprocessing queues
      binding.preproc_inq = nullptr;
      binding.preproc_outq = nullptr;

      // Only inference queues are set
      binding.inf_inq = ctx->inf_inqs_vec[i].get();
      binding.inf_outq = ctx->inf_outqs_vec[i].get();

      ctx->instance_bindings.push_back(binding);

      APP_LOG(AppLogLevel::DEBUG, log_level, "\t - Created inference-only binding for instance %u", i);
    }
    APP_LOG(AppLogLevel::INFO, log_level, "\t Successfully initialized %zu inference-only bindings",
            ctx->instance_bindings.size());
  }

  return true;
}

/**
 * @brief Log both name lists (ifms-config 'name' values and runner-reported
 *        input tensor names) at ERROR level to aid the user in fixing a
 *        name mismatch. Called from the two binding-failure sites in
 *        create_file_reader_context().
 */
static void log_ifms_name_mismatch(AppLogLevel app_log_level,
                                   const std::vector<AppContext::IfmsConfig>& ifms_vec,
                                   const std::vector<InferTensorInfo>& runner_inputs) {
  std::string json_names;
  for (const auto& e : ifms_vec) {
    if (!json_names.empty())
      json_names += ", ";
    json_names += "'" + e.tensor_name + "'";
  }
  std::string runner_names;
  for (const auto& t : runner_inputs) {
    if (!runner_names.empty())
      runner_names += ", ";
    runner_names += "'" + t.meta.name + "'";
  }
  APP_LOG(AppLogLevel::ERROR, app_log_level, "  ifms-config 'name' values   : [%s]", json_names.c_str());
  APP_LOG(AppLogLevel::ERROR, app_log_level, "  Runner-expected tensor names: [%s]", runner_names.c_str());
}

/**
 * @brief Create file reader context - creates N file readers (one per model) for both modes
 */
static bool create_file_reader_context(AppContext* ctx) {
  AppLogLevel log_level = ctx->log_level;

  APP_LOG(AppLogLevel::INFO, log_level, "\t - Creating %u file readers (preprocess_enable=%d)",
          ctx->num_model_instances, ctx->preprocess_enable);

  ctx->file_readers.reserve(ctx->num_model_instances);

  for (size_t i = 0; i < ctx->num_model_instances; i++) {
    const InferenceConfig& model_info = get_model_info_for_instance(ctx, i);

    FileReaderConfig config;

    // Get dimensions from inference config
    config.model_width = model_info.model_width;
    config.model_height = model_info.model_height;
    config.model_batch_size = model_info.batch_size;
    config.num_input_tensors = model_info.num_in_tensors;
    config.input_tensor_info = model_info.in_tensors_info;
    config.mem_bank = ctx->ppe_mbank_in;
    config.device = ctx->device.get();
    config.max_iterations = ctx->max_iterations;
    config.log_level = ctx->log_level;
    config.instance_id = i;
    config.num_model_instances = ctx->num_model_instances;
    config.num_frames_to_process = ctx->num_frame_to_process;
    config.frames_submitted_ptr = &ctx->frames_submitted;
    config.critical_error_ptr = &ctx->critical_error;

    // Set mode based on global flag
    config.bypass_preprocessing = !ctx->preprocess_enable;

    // Configure file paths based on mode
    if (ctx->preprocess_enable) {
      // PREPROCESSING MODE: Use single image file
      /* The HLS image_processing kernel produces a single preprocessed
       * tensor per inference call, so preprocessing mode is restricted to
       * single-input models. Multi-input models must be run with
       * preprocess-en:false (inference-only mode) where each input tensor
       * gets its own .bin via ifms-config. */
      if (model_info.in_tensors_info.size() != 1) {
        APP_LOG(AppLogLevel::ERROR, log_level,
                "Model %zu: preprocess-en=true is only supported for single-input models, but this model has %zu "
                "input tensor(s). Set preprocess-en:false and provide a .bin per input tensor in ifms-config.",
                i, model_info.in_tensors_info.size());
        return false;
      }

      // CLI override takes precedence
      if (!ctx->input_file_path.empty()) {
        config.input_image_file_path = ctx->input_file_path;  // All readers use CLI file (pseudo-broadcast)
        // Use CLI dimensions if provided
        config.bin_input_width = ctx->input_width;
        config.bin_input_height = ctx->input_height;
        APP_LOG(AppLogLevel::DEBUG, log_level, "\t - File reader %zu: Using CLI file: %s", i,
                config.input_image_file_path.c_str());
      } else {
        // Per-model mode: use ifms-config file and dimensions
        if (i >= ctx->model_ifms_configs.size() || ctx->model_ifms_configs[i].empty()) {
          APP_LOG(AppLogLevel::ERROR, log_level, "No ifms-config found for preprocessing model %zu", i);
          return false;
        }

        /* Validate the ifms-config 'name' against the runner's single
         * input tensor name, same contract as inference-only mode. The
         * CLI broadcast path above skips this because the CLI file
         * overrides ifms-config entirely. */
        const auto& ifms_vec = ctx->model_ifms_configs[i];
        const std::string& json_name = ifms_vec[0].tensor_name;
        const std::string& runner_name = model_info.in_tensors_info[0].meta.name;
        if (json_name != runner_name) {
          APP_LOG(AppLogLevel::ERROR, log_level,
                  "Model %zu: ifms-config 'name'='%s' does not match the model's input tensor name '%s'.", i,
                  json_name.c_str(), runner_name.c_str());
          log_ifms_name_mismatch(log_level, ifms_vec, model_info.in_tensors_info);
          return false;
        }

        config.input_image_file_path = ctx->model_ifms_configs[i][0].file_path;

        // Use dimensions from ifms-config (CLI --dim not applicable in per-model mode)
        config.bin_input_width = ctx->model_ifms_configs[i][0].width;
        config.bin_input_height = ctx->model_ifms_configs[i][0].height;

        std::string dim_info = (config.bin_input_width > 0) ? " (" + std::to_string(config.bin_input_width) + "x" +
                                                                  std::to_string(config.bin_input_height) + ")"
                                                            : "";
        APP_LOG(AppLogLevel::DEBUG, log_level, "\t - File reader %zu: Using ifms-config file: %s%s", i,
                config.input_image_file_path.c_str(), dim_info.c_str());
      }

      // Set VideoInfo provider lambda
      config.get_vinfo = [ctx, i](uint32_t h, uint32_t w, vart::VideoFormat fmt) {
        return ctx->preprocess[i]->get_input_vinfo(h, w, fmt);
      };

      config.preprocess_queue_depth = PREPROCESS_QUEUE_DEPTH;
      config.inference_queue_depth = INFERENCE_QUEUE_DEPTH;

    } else {
      // INFERENCE-ONLY MODE: Use multiple binary files
      if (i >= ctx->model_ifms_configs.size() || ctx->model_ifms_configs[i].empty()) {
        APP_LOG(AppLogLevel::ERROR, log_level, "No ifms-config found for inference-only model %zu", i);
        return false;
      }

      /* Bind each ifms-config entry to a runner-reported input tensor by
       * name. The runner's tensor order (model_info.in_tensors_info) drives
       * the resulting file vector, so downstream code can keep indexing
       * positionally regardless of JSON authoring order. */
      const auto& runner_inputs = model_info.in_tensors_info;
      const auto& ifms_vec = ctx->model_ifms_configs[i];

      if (ifms_vec.size() != runner_inputs.size()) {
        APP_LOG(AppLogLevel::ERROR, log_level,
                "Model %zu: ifms-config provides %zu entry/entries, but the model expects %zu input tensor(s).", i,
                ifms_vec.size(), runner_inputs.size());
        log_ifms_name_mismatch(log_level, ifms_vec, runner_inputs);
        return false;
      }

      std::unordered_map<std::string, std::string> file_by_name;
      file_by_name.reserve(ifms_vec.size());
      for (const auto& e : ifms_vec) {
        file_by_name.emplace(e.tensor_name, e.file_path);
      }

      config.input_tensors_file_path.reserve(runner_inputs.size());
      for (size_t t = 0; t < runner_inputs.size(); ++t) {
        const std::string& tname = runner_inputs[t].meta.name;
        auto it = file_by_name.find(tname);
        if (it == file_by_name.end()) {
          APP_LOG(AppLogLevel::ERROR, log_level,
                  "Model %zu: ifms-config is missing the input for runner-expected tensor '%s'.", i, tname.c_str());
          log_ifms_name_mismatch(log_level, ifms_vec, runner_inputs);
          return false;
        }
        config.input_tensors_file_path.push_back(it->second);
        APP_LOG(AppLogLevel::INFO, log_level, "\t - Model %zu: Bound ifms-config name='%s' -> file='%s'", i,
                tname.c_str(), it->second.c_str());
      }

      config.preprocess_queue_depth = 0;
      config.inference_queue_depth = INFERENCE_QUEUE_DEPTH;

      APP_LOG(AppLogLevel::DEBUG, log_level, "\t - File reader %zu: Using %zu binary files from ifms-config", i,
              config.input_tensors_file_path.size());
    }

    // Connect to SINGLE output queue (1:1 mapping)
    void* output_queue;
    if (ctx->preprocess_enable) {
      output_queue = static_cast<void*>(ctx->preproc_inqs_vec[i].get());
    } else {
      output_queue = static_cast<void*>(ctx->inf_inqs_vec[i].get());
    }

    // Connect to SINGLE postprocess queue (1:1, nullable)
    void* postproc_queue = nullptr;
    if (ctx->preprocess_enable && i < ctx->metaconvert_enable.size() && ctx->metaconvert_enable[i] &&
        i < ctx->orig_frame_qs_vec.size() && ctx->orig_frame_qs_vec[i]) {
      postproc_queue = static_cast<void*>(ctx->orig_frame_qs_vec[i].get());
      config.postprocess_queue_depth = POSTPROCESS_QUEUE_DEPTH;
      APP_LOG(AppLogLevel::DEBUG, log_level, "\t - File reader %zu: Connected to postprocess queue for metaconvert", i);
    } else {
      config.postprocess_queue_depth = 0;  // No postprocess overlay in inference-only mode
    }

    // Create file reader with single queues (1:1 mapping)
    auto reader = std::make_unique<AppFileReader>(config, output_queue, postproc_queue);
    if (!reader) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Failed to create file reader %zu", i);
      return false;
    }

    if (!reader->initialize()) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Failed to initialize file reader %zu", i);
      return false;
    }

    ctx->file_readers.push_back(std::move(reader));
    APP_LOG(AppLogLevel::INFO, log_level, "\t - File reader %zu created and initialized successfully", i);
  }

  APP_LOG(AppLogLevel::INFO, log_level, "\t - Created %zu file readers successfully", ctx->file_readers.size());
  return true;
}

static bool create_preprocess_context(AppContext* ctx) {
  AppLogLevel log_level = ctx->log_level;

  try {
    // create preprocessing instances (using model info from inference)
    APP_LOG(AppLogLevel::INFO, log_level, "\tCreating %u instances of preprocessing instances",
            ctx->num_model_instances);

    // Create individual preprocessing instances (each with its own config)
    for (size_t i = 0; i < ctx->num_model_instances; i++) {
      // Prepare per-instance preprocessing configuration
      PreProcessConfig config = ctx->preproc_cfg[i];  // Copy config

      // Use model info from corresponding inference instance
      const InferenceConfig& instance_model_info = get_model_info_for_instance(ctx, i);

      /* Resolve VideoFormat: try direct map from user string, then derive from tensor metadata */
      const auto& first_tensor = instance_model_info.in_tensors_info[0].meta;
      vart::MemoryLayout effective_layout = first_tensor.memory_layout;
      uint32_t inferred_width = 0;
      uint32_t inferred_height = 0;
      if (first_tensor.memory_layout == vart::MemoryLayout::GENERIC) {
        // Preprocess and colour-format validation require a concrete layout,
        // so infer one from tensor shape when metadata reports GENERIC.

        string generic_reason;
        if (!infer_generic_preprocess_layout(first_tensor, effective_layout, inferred_width, inferred_height,
                                             generic_reason)) {
          APP_LOG(AppLogLevel::ERROR, log_level,
                  "Model instance %zu uses GENERIC memory_layout that cannot be inferred as NHW, NCHW, or NHWC: %s. "
                  "This GENERIC layout is not compatible with preprocessing.",
                  i, generic_reason.c_str());
          return false;
        }

        APP_LOG(AppLogLevel::INFO, log_level,
                "Model instance %zu uses GENERIC memory_layout; inferred preprocess layout=%s with output=%ux%u", i,
                to_string(effective_layout), inferred_width, inferred_height);
      }

      if (!config.colour_format_str.empty()) {
        config.preprocess_info.colour_format = get_vart_video_format(config.colour_format_str);
        if (config.preprocess_info.colour_format == vart::VideoFormat::UNKNOWN) {
          APP_LOG(AppLogLevel::ERROR, log_level, "Unrecognised colour-format \"%s\" for instance %zu",
                  config.colour_format_str.c_str(), i);
          return false;
        }

        string colour_space = get_colour_space(config.preprocess_info.colour_format);
        if (colour_space.empty()) {
          APP_LOG(AppLogLevel::ERROR, log_level,
                  "Cannot determine colour space for colour-format \"%s\" (instance %zu)",
                  config.colour_format_str.c_str(), i);
          return false;
        }
        vart::VideoFormat expected_colour_format =
            derive_vart_video_format(colour_space, effective_layout, first_tensor.data_type);
        if (expected_colour_format != config.preprocess_info.colour_format) {
          APP_LOG(AppLogLevel::ERROR, log_level,
                  "colour-format mismatch for instance %zu: user specified \"%s\" but "
                  "tensor metadata (layout=%s, dtype=%s) expects %s",
                  i, config.colour_format_str.c_str(), to_string(effective_layout), to_string(first_tensor.data_type),
                  to_string(expected_colour_format));
          return false;
        }
        APP_LOG(AppLogLevel::INFO, log_level,
                "\t - Validated colour-format \"%s\" for instance %zu "
                "(colour_space=%s, layout=%s, dtype=%s)",
                config.colour_format_str.c_str(), i, colour_space.c_str(), to_string(effective_layout),
                to_string(first_tensor.data_type));
      } else {
        const string default_colour_space = "RGB";
        APP_LOG(AppLogLevel::INFO, log_level,
                "colour-format not specified for instance %zu; auto-detecting VideoFormat "
                "using default colour_space=%s with tensor layout=%s, dtype=%s",
                i, default_colour_space.c_str(), to_string(effective_layout), to_string(first_tensor.data_type));
        config.preprocess_info.colour_format =
            derive_vart_video_format(default_colour_space, effective_layout, first_tensor.data_type);
        if (config.preprocess_info.colour_format == vart::VideoFormat::UNKNOWN) {
          APP_LOG(AppLogLevel::ERROR, log_level,
                  "Cannot derive VideoFormat for instance %zu: colour_space=%s, layout=%s, dtype=%s", i,
                  default_colour_space.c_str(), to_string(effective_layout), to_string(first_tensor.data_type));
          return false;
        }
        APP_LOG(AppLogLevel::INFO, log_level,
                "\t - Derived VideoFormat %s for instance %zu from colour_space=%s, layout=%s, dtype=%s",
                to_string(config.preprocess_info.colour_format), i, default_colour_space.c_str(),
                to_string(effective_layout), to_string(first_tensor.data_type));
      }

      /* Obtain the quantization factor from first tensor info*/
      config.input_tensor_quantization_factor = instance_model_info.in_tensors_info[0].quantization_factor;
      config.batch_size = instance_model_info.batch_size;
      config.frames_per_batch = instance_model_info.num_in_tensors;
      config.output_width = instance_model_info.model_width;
      config.output_height = instance_model_info.model_height;

      if (first_tensor.memory_layout == vart::MemoryLayout::GENERIC) {
        // Use inferred dimensions for preprocessing when layout is GENERIC
        config.output_width = inferred_width;
        config.output_height = inferred_height;
        APP_LOG(AppLogLevel::INFO, log_level, "\t - Using inferred dimensions for preprocessing output: %ux%u",
                config.output_width, config.output_height);
      }

      // Transfer app context details to preprocessor config
      config.input_height = ctx->input_height;
      config.input_width = ctx->input_width;
      config.log_level = ctx->log_level;
      config.json_str = ctx->preproc_json_str[i];
      config.device = ctx->device.get();  // Convert shared_ptr to raw pointer
      config.instance_id = i;
      config.dump_all_inputs = ctx->dump_all_inputs;
      config.output_dir_path = ctx->output_dir_path;
      config.max_iterations = ctx->max_iterations;
      config.critical_error_ptr = &ctx->critical_error;  // Set error signaling pointer

      config.num_model_instances = ctx->num_model_instances;

      APP_LOG(AppLogLevel::INFO, log_level,
              "\t - Creating preprocessing instance %zu with config %ux%u -> "
              "%ux%u",
              i, config.input_width, config.input_height, config.output_width, config.output_height);

      // Each preprocessing instance gets its own dedicated input and output queue
      AppQueue<InputFrame>* input_queue = ctx->preproc_inqs_vec[i].get();
      AppQueue<PreprocessedFrame>* output_queue = ctx->preproc_outqs_vec[i].get();

      APP_LOG(AppLogLevel::DEBUG, log_level,
              "\t - Preprocessing instance %zu will be coupled to inference "
              "instance %zu",
              i, i);

      // Create preprocessing instance
      auto preproc_inst = std::make_unique<AppPreProcess>(config, *input_queue, *output_queue);
      if (!preproc_inst) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Failed to create preprocessing instance %zu", i);
        return false;
      }

      if (!preproc_inst->initialize()) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Failed to initialize preprocessing instance %zu", i);
        return false;
      }
      ctx->preprocess.push_back(std::move(preproc_inst));
      APP_LOG(AppLogLevel::DEBUG, log_level, "\t - Successfully created preprocessing instance %zu", i);
    }

    APP_LOG(AppLogLevel::INFO, log_level, "\t Successfully created %zu preprocessing instances",
            ctx->preprocess.size());
    return true;

  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Exception creating multiple instances: %s", e.what());
    return false;
  }
}

static bool create_infer_context(AppContext* ctx) {
  AppLogLevel log_level = ctx->log_level;

  try {
    APP_LOG(AppLogLevel::INFO, log_level, "\tCreating %u instances of inference component", ctx->num_model_instances);

    // STEP 1: Create inference instances with per-instance configurations
    for (size_t i = 0; i < ctx->num_model_instances; i++) {
      pt::ptree config;

      APP_LOG(AppLogLevel::INFO, log_level, "\tCreating instance %zu ", i);
      try {
        pt::read_json(ctx->model_json_path[i], config);
      } catch (const std::exception& e) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Failed to read JSON config file %s: %s",
                ctx->model_json_path[i].c_str(), e.what());
        return false;
      }

      // Parse ifms-config using mode-specific parsing function
      std::vector<AppContext::IfmsConfig> ifms_configs;
      if (!parse_inference_ifms_config(ctx, config, i, ifms_configs)) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Failed to parse ifms-config for model %zu", i);
        return false;
      }

      // Store ifms-config for this model
      ctx->model_ifms_configs.push_back(std::move(ifms_configs));

      // Runner options
      std::unordered_map<std::string, std::any> runner_options = {};

      /* Extract runner options from config */
      auto config_file = config.get<std::string>("inference-config.runner-options.config-file", "");
      if (!config_file.empty()) {
        runner_options["config_json"] = config_file;
      }

      auto log_level_str = config.get<std::string>("inference-config.runner-options.log-level", "ERROR");
      auto ai_analyzer_profile_opt = config.get_optional<bool>("inference-config.runner-options.ai-analyzer-profiling");
      auto aie_columns_sharing_opt = config.get_optional<bool>("inference-config.runner-options.aie-columns-sharing");
      auto start_column_opt = config.get_optional<uint32_t>("inference-config.runner-options.start-column");
      auto input_tensor_type_str = config.get<std::string>("inference-config.runner-options.input-tensor-type", "HW");
      auto output_tensor_type_str = config.get<std::string>("inference-config.runner-options.output-tensor-type", "HW");

      if (input_tensor_type_str != "CPU" && input_tensor_type_str != "HW") {
        APP_LOG(AppLogLevel::WARNING, log_level,
                "Invalid input-tensor-type '%s'. Supported values are CPU and HW. Falling back to HW.",
                input_tensor_type_str.c_str());
        input_tensor_type_str = "HW";
      }

      if (output_tensor_type_str != "CPU" && output_tensor_type_str != "HW") {
        APP_LOG(AppLogLevel::WARNING, log_level,
                "Invalid output-tensor-type '%s'. Supported values are CPU and HW. Falling back to HW.",
                output_tensor_type_str.c_str());
        output_tensor_type_str = "HW";
      }

      runner_options["input_tensor_type"] = input_tensor_type_str;
      runner_options["output_tensor_type"] = output_tensor_type_str;
      runner_options["log_level"] = log_level_str;

      APP_LOG(AppLogLevel::DEBUG, log_level, "\tRunner options:");
      APP_LOG(AppLogLevel::DEBUG, log_level, "\tconfig-file:           %s", config_file.c_str());
      APP_LOG(AppLogLevel::DEBUG, log_level, "\tlog-level:             %s", log_level_str.c_str());
      APP_LOG(AppLogLevel::DEBUG, log_level, "\tinput-tensor-type:     %s", input_tensor_type_str.c_str());
      APP_LOG(AppLogLevel::DEBUG, log_level, "\toutput-tensor-type:    %s", output_tensor_type_str.c_str());

      if (ai_analyzer_profile_opt) {
        runner_options["ai_analyzer_profiling"] = ai_analyzer_profile_opt.get();
        APP_LOG(AppLogLevel::DEBUG, log_level, "\tai-analyzer-profiling: %d", ai_analyzer_profile_opt.get());
      }
      if (aie_columns_sharing_opt) {
        runner_options["aie_columns_sharing"] = aie_columns_sharing_opt.get();
        APP_LOG(AppLogLevel::DEBUG, log_level, "\taie-columns-sharing:   %d", aie_columns_sharing_opt.get());
      }
      if (start_column_opt) {
        runner_options["start_column"] = start_column_opt.get();
        APP_LOG(AppLogLevel::DEBUG, log_level, "\tstart-column:          %d", start_column_opt.get());
      }

      // Create per-instance configuration
      InferenceConfig inference_config;

      inference_config.runner_type = vart::RunnerType::VAIML;
      inference_config.log_level = ctx->log_level;
      inference_config.device = ctx->device.get();  // Convert shared_ptr to raw pointer
      inference_config.device_index = ctx->device_idx;
      inference_config.mbank_idx = DEFAULT_FRAME_MEMBANK;
      inference_config.dump_all_inputs = ctx->dump_all_inputs;
      inference_config.is_benchmark_enabled = ctx->is_benchmark_enabled;
      inference_config.max_iterations = ctx->max_iterations;
      inference_config.output_dir_path = ctx->output_dir_path;
      inference_config.runner_options = std::move(runner_options);
      inference_config.input_tensor_type =
          (input_tensor_type_str == "CPU") ? vart::TensorType::CPU : vart::TensorType::HW;
      inference_config.output_tensor_type =
          (output_tensor_type_str == "CPU") ? vart::TensorType::CPU : vart::TensorType::HW;
      inference_config.instance_id = i;
      inference_config.critical_error_ptr = &ctx->critical_error;  // Set error signaling pointer
      inference_config.num_model_instances = ctx->num_model_instances;

      // Per-instance snap path
      inference_config.model_path = ctx->model_snap_path[i];

      APP_LOG(AppLogLevel::INFO, log_level, "\t - Creating inference instance %zu with model snapshot: %s", i,
              inference_config.model_path.c_str());

      auto inference_inst =
          std::make_unique<Inference>(inference_config, *ctx->inf_inqs_vec[i], *ctx->inf_outqs_vec[i]);
      if (!inference_inst) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Failed to create inference instance %zu", i);
        return false;
      }

      if (!inference_inst->initialize()) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Failed to initialize inference instance %zu", i);
        return false;
      }

      ctx->inference.push_back(std::move(inference_inst));
      APP_LOG(AppLogLevel::DEBUG, log_level, "\t - Successfully created inference instance %zu", i);
    }  // end-for

    // STEP 2: Update AppContext model_infos with extracted parameters from each component
    ctx->model_info.clear();
    ctx->model_info.reserve(ctx->inference.size());

    for (size_t i = 0; i < ctx->inference.size(); i++) {
      // extract & store model info for each inference instance in AppContext
      const InferenceConfig& inst_config = ctx->inference[i]->get_config();

      // save model info in AppContext
      ctx->model_info.push_back(inst_config);
      // save batch information per model
      ctx->batch_size_per_model.push_back(inst_config.batch_size);
    }

    APP_LOG(AppLogLevel::INFO, log_level, "\t Successfully created %zu inference instances", ctx->inference.size());
    return true;
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Exception creating multiple instances: %s", e.what());
    return false;
  }
}

static bool create_postprocess_context(AppContext* ctx) {
  AppLogLevel log_level = ctx->log_level;

  try {
    // Use the precomputed any_postprocess_enabled flag from AppContext
    if (!ctx->any_postprocess_enabled) {
      APP_LOG(AppLogLevel::INFO, log_level,
              "\tNo models have postprocess configuration enabled, skipping postprocess creation");
      return true;
    }

    APP_LOG(AppLogLevel::INFO, log_level, "\tCreating postprocess instances for enabled models");

    // Create postprocess instances selectively based on postprocess_enable flags
    for (size_t i = 0; i < ctx->num_model_instances; i++) {
      // Check if postprocessing is enabled for this model
      if (i >= ctx->postprocess_enable.size() || !ctx->postprocess_enable[i]) {
        APP_LOG(AppLogLevel::INFO, log_level, "\t - Skipping postprocess instance %zu (not enabled in config)", i);
        ctx->postprocess.push_back(nullptr);  // Placeholder for consistent indexing
        continue;
      }
      // Prepare per-instance postprocess configuration
      PostProcessConfig config = ctx->postproc_cfg[i];  // Copy config

      // Use model info from corresponding inference instance
      const InferenceConfig& instance_model_info = get_model_info_for_instance(ctx, i);

      config.model_batch_size = instance_model_info.batch_size;
      config.model_input_width = instance_model_info.model_width;
      config.model_input_height = instance_model_info.model_height;
      config.model_num_input_tensors = instance_model_info.num_in_tensors;
      config.model_input_tensors_info = instance_model_info.in_tensors_info;
      config.model_num_out_tensors = instance_model_info.num_out_tensors;
      config.model_out_tensors_info = instance_model_info.out_tensors_info;

      // Transfer app context details to postprocess config
      config.log_level = ctx->log_level;
      config.postprocess_json = ctx->postproc_json_str[i];
      config.metaconvert_json = ctx->postproc_json_str[i];  // Same JSON file
      config.output_dir_path = ctx->output_dir_path;
      config.device = ctx->device.get();  // Convert shared_ptr to raw pointer
      config.instance_id = i;
      config.is_metaconvert_enabled = ctx->metaconvert_enable[i];
      config.is_benchmark_enabled = ctx->is_benchmark_enabled;
      config.max_iterations = ctx->max_iterations;       // Set for iteration-aware file naming
      config.critical_error_ptr = &ctx->critical_error;  // Set error signaling pointer
      config.is_jpg_input = false;                       // Default value

      // Check if input is JPEG format from corresponding file reader
      // This is needed for dumping the overlaid output in correct format
      if (!ctx->file_readers.empty() && i < ctx->file_readers.size() && ctx->file_readers[i]) {
        if (ctx->file_readers[i]->get_input_file_type() == AppVideoInputFormat::APP_VIDEO_INPUT_FORMAT_JPEG) {
          config.is_jpg_input = true;
        }
      }

      APP_LOG(AppLogLevel::INFO, log_level, "\t - Creating postprocess instance %zu", i);

      // Each postprocess instance gets inference output queue
      AppQueue<InferenceResult>* inference_output_queue = ctx->inf_outqs_vec[i].get();

      // Original frame queue (only if preprocessing enabled and this instance has postprocessing)
      AppQueue<InputFrame>* original_frame_queue = nullptr;
      if (ctx->preprocess_enable && !ctx->orig_frame_qs_vec.empty() && i < ctx->orig_frame_qs_vec.size() &&
          ctx->orig_frame_qs_vec[i]) {
        original_frame_queue = ctx->orig_frame_qs_vec[i].get();
        APP_LOG(AppLogLevel::DEBUG, log_level, "\t - Postprocess instance %zu will receive orig frames for overlay", i);
      }

      // Completion queue for this postprocess instance
      AppQueue<ProcessingComplete>* completion_queue = nullptr;
      if (!ctx->postproc_outqs_vec.empty() && (i < ctx->postproc_outqs_vec.size()) && ctx->postproc_outqs_vec[i]) {
        completion_queue = ctx->postproc_outqs_vec[i].get();
        APP_LOG(AppLogLevel::DEBUG, log_level, "\t - Postprocess instance %zu will send completion notifications", i);
      }

      // Create postprocess instance
      auto postproc_inst =
          std::make_unique<AppPostProcess>(config, *inference_output_queue, original_frame_queue, completion_queue);
      if (!postproc_inst) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Failed to create postprocess instance %zu", i);
        return false;
      }

      if (!postproc_inst->initialize()) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Failed to initialize postprocess instance %zu", i);
        return false;
      }

      ctx->postprocess.push_back(std::move(postproc_inst));
      APP_LOG(AppLogLevel::DEBUG, log_level, "\t - Successfully created postprocess instance %zu", i);
    }

    APP_LOG(AppLogLevel::INFO, log_level, "\t Successfully created %zu postprocess instances", ctx->postprocess.size());
    return true;

  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Exception creating postprocess instances: %s", e.what());
    return false;
  }
}

static bool start_all_contexts(AppContext* ctx) {
  AppLogLevel log_level = ctx->log_level;

  APP_LOG(AppLogLevel::INFO, log_level, "Starting pipeline threads (back-to-front: consumers before producers)");

  // Start postprocess instances FIRST (sink/consumer - ready to receive)
  if (!ctx->postprocess.empty()) {
    APP_LOG(AppLogLevel::INFO, log_level, "Starting postprocess instance(s)");
    size_t started_count = 0;
    for (size_t i = 0; i < ctx->postprocess.size(); i++) {
      if (ctx->postprocess[i]) {  // Check for nullptr (disabled instances)
        if (!ctx->postprocess[i]->start()) {
          APP_LOG(AppLogLevel::ERROR, log_level, "Failed to start postprocess instance %zu", i);
          return false;
        }
        APP_LOG(AppLogLevel::DEBUG, log_level, "Postprocess instance %zu started successfully", i);
        started_count++;
      }
    }
    if (started_count > 0) {
      APP_LOG(AppLogLevel::INFO, log_level, "Started %zu postprocess instance(s) successfully", started_count);
    }
  }

  // Start inference instances (middle consumer/producer)
  APP_LOG(AppLogLevel::INFO, log_level, "Starting %u inference instance(s)", ctx->num_model_instances);
  for (size_t i = 0; i < ctx->num_model_instances; i++) {
    if (!ctx->inference[i]->start()) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Failed to start inference instance %zu", i);
      return false;
    }
  }
  APP_LOG(AppLogLevel::INFO, log_level, "All inference instances started successfully");

  // Start preprocessing instances (middle consumer/producer)
  if (ctx->preprocess_enable) {
    APP_LOG(AppLogLevel::INFO, log_level, "Starting %u preprocessing instance(s)", ctx->num_model_instances);
    for (size_t i = 0; i < ctx->num_model_instances; i++) {
      if (!ctx->preprocess[i]->start()) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Failed to start preprocess instance %zu", i);
        return false;
      }
    }
    APP_LOG(AppLogLevel::INFO, log_level, "All preprocessing instances started successfully");
  }

  // Start file reader threads LAST (source/producer - starts sending data)
  if (!ctx->file_readers.empty()) {
    APP_LOG(AppLogLevel::INFO, log_level, "Starting %zu file reader thread(s)", ctx->file_readers.size());

    for (size_t i = 0; i < ctx->file_readers.size(); i++) {
      if (!ctx->file_readers[i]->start()) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Failed to start file reader %zu", i);
        return false;
      }
      APP_LOG(AppLogLevel::DEBUG, log_level, "File reader %zu started successfully", i);
    }

    APP_LOG(AppLogLevel::INFO, log_level, "All file reader threads started successfully");
  }

  APP_LOG(AppLogLevel::INFO, log_level, "Pipeline startup complete (all threads running)");
  return true;
}

/* Create all instances in the AppContext structure */
bool create_all_context(AppContext* ctx) {
  AppLogLevel log_level = ctx->log_level;

  try {
    APP_LOG(AppLogLevel::DEBUG, log_level, "create_all_context()::");
    /* Device is required for all Vart APIs, this load xclbin of device only if
     * not already loaded */
    ctx->device = vart::Device::get_device_hdl(ctx->device_idx, ctx->xclbin_location);
    if (ctx->device == nullptr) {
      throw std::runtime_error("Failed to get device handle");
    }

    APP_LOG(AppLogLevel::DEBUG, log_level, " - create_queue_binding_context()::");
    if (!create_queue_binding_context(ctx)) {
      throw std::runtime_error("Failed to create queue and instance bindings");
    }

    APP_LOG(AppLogLevel::DEBUG, log_level, " - create_infer_context()::");
    /* Create multiple component instances based on num_instances parameter */
    if (!create_infer_context(ctx)) {
      throw std::runtime_error("Failed to create multiple component instances");
    }

    if (ctx->preprocess_enable) {
      APP_LOG(AppLogLevel::DEBUG, log_level, " - create_preprocess_context()::");
      if (!create_preprocess_context(ctx)) {
        throw std::runtime_error("Failed to create multiple component instances");
      }
    } else {
      APP_LOG(AppLogLevel::INFO, log_level, "Preprocessing disabled, skip preprocess instance creation");
    }

    // Create file reader context after preprocessing context
    APP_LOG(AppLogLevel::DEBUG, log_level, " - create_file_reader_context()::");
    if (!create_file_reader_context(ctx)) {
      throw std::runtime_error("Failed to create file reader context");
    }

    // Create postprocess context after inference context
    APP_LOG(AppLogLevel::DEBUG, log_level, " - create_postprocess_context()::");
    if (!create_postprocess_context(ctx)) {
      throw std::runtime_error("Failed to create postprocess context");
    }

    /* Validate pipeline mode requirements */
    if (ctx->inference.size() == 0) {
      APP_LOG(AppLogLevel::ERROR, log_level, "No inference instances created");
      destroy_all_context(ctx);
      return false;
    }

    if (ctx->preprocess_enable && ctx->preprocess.size() == 0) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Preprocessing enabled but no preprocessing instances created");
      destroy_all_context(ctx);
      return false;
    }

    if (ctx->file_readers.empty()) {
      APP_LOG(AppLogLevel::ERROR, log_level, "No file readers created");
      destroy_all_context(ctx);
      return false;
    }

    APP_LOG(AppLogLevel::INFO, log_level,
            "Pipeline validation passed: %zu file readers, %zu preprocessing, %zu inference", ctx->file_readers.size(),
            ctx->preprocess.size(), ctx->inference.size());

    APP_LOG(AppLogLevel::DEBUG, log_level, " - start_all_contexts()::");
    start_all_contexts(ctx);

    APP_LOG(AppLogLevel::INFO, log_level, "create_all_context() completed successfully");

    return true;
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Exception caught: %s", e.what());
    destroy_all_context(ctx);
    return false;
  }
}

/**
 * @brief Adaptively drain the pipeline by monitoring processing state
 * @param ctx Application context
 * @return true if successfully drained, false if timeout/error
 */
static bool drain_pipeline(AppContext* ctx) {
  AppLogLevel log_level = ctx->log_level;

  const char* mode_str = ctx->any_postprocess_enabled ? "with-postprocessing" : "inference-only";
  APP_LOG(AppLogLevel::INFO, log_level, "Draining pipeline (mode: %s, timeout: %ds)...", mode_str,
          ctx->pipeline_timeout_seconds);

  const auto start_time = std::chrono::steady_clock::now();
  const auto max_wait = std::chrono::seconds(ctx->pipeline_timeout_seconds);
  const auto check_interval = std::chrono::milliseconds(10);

  int frames_drained = 0;

  while (true) {
    // Check timeout
    auto elapsed = std::chrono::steady_clock::now() - start_time;
    if (elapsed > max_wait) {
      APP_LOG(AppLogLevel::WARNING, log_level, "Pipeline drain timeout after %ld ms - forcing shutdown",
              std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
      return false;
    }

    // PER-INSTANCE DRAINING: Handle mixed postprocessing configurations
    // Some models may have postprocessing while others don't

    // Drain inference output queues for models WITHOUT postprocessing
    for (size_t i = 0; i < ctx->inf_outqs_vec.size(); i++) {
      if (!ctx->inf_outqs_vec[i])
        continue;

      // Check if THIS SPECIFIC instance has postprocessing
      bool has_postprocess = (i < ctx->postprocess.size() && ctx->postprocess[i] != nullptr);

      if (!has_postprocess) {
        // No postprocess consumer - we must drain to prevent blocking
        InferenceResult result;
        while (ctx->inf_outqs_vec[i]->try_pop(result)) {
          // Update per-model counter for drained frames
          if (i < ctx->frames_processed_per_model.size()) {
            ctx->frames_processed_per_model[i] += ctx->batch_size_per_model[i];
          }
          frames_drained++;
          APP_LOG(AppLogLevel::DEBUG, log_level, "Drained result from inference[%zu]: frame %d", i, result.frame_index);
          // Note: Results are already written to disk by inference thread before push
          // We're just consuming to unblock the queue
        }
      }
    }

    // Monitor postprocess completion queues for models WITH postprocessing
    for (size_t i = 0; i < ctx->postproc_outqs_vec.size(); i++) {
      if (!ctx->postproc_outqs_vec[i])
        continue;

      ProcessingComplete complete;
      while (ctx->postproc_outqs_vec[i]->try_pop(complete)) {
        // Update per-model counter for drained frames (use actual batch_size from completion)
        if (i < ctx->frames_processed_per_model.size()) {
          ctx->frames_processed_per_model[i] += complete.batch_size;
        }
        frames_drained += complete.batch_size;
        APP_LOG(AppLogLevel::DEBUG, log_level, "Drained %u frames from postprocess[%zu]: frame %d", complete.batch_size,
                i, complete.frame_index);
      }
    }

    // Check if ANY component is still processing
    bool any_processing = false;
    int inf_processing = 0;
    int post_processing = 0;

    // Check inference instances
    for (size_t i = 0; i < ctx->inference.size(); i++) {
      if (ctx->inference[i] && ctx->inference[i]->is_processing()) {
        any_processing = true;
        inf_processing++;
      }
    }

    // Check postprocessing instances (if enabled)
    if (ctx->any_postprocess_enabled) {
      for (size_t i = 0; i < ctx->postprocess.size(); i++) {
        if (ctx->postprocess[i] && ctx->postprocess[i]->is_processing()) {
          any_processing = true;
          post_processing++;
        }
      }
    }

    // Log progress periodically
    if (any_processing) {
      APP_LOG(AppLogLevel::DEBUG, log_level, "Still processing: %d inference, %d postprocess", inf_processing,
              post_processing);
    }

    // Pipeline is drained when nothing is processing
    if (!any_processing) {
      auto drain_time =
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();
      APP_LOG(AppLogLevel::INFO, log_level, "Pipeline drained in %ld ms (%d items tracked)", drain_time,
              frames_drained);
      return true;
    }

    std::this_thread::sleep_for(check_interval);
  }
}

/* Flush the pipeline by stopping all components and draining remaining frames */
void flush_pipeline(AppContext* ctx) {
  AppLogLevel log_level = ctx->log_level;

  APP_LOG(AppLogLevel::INFO, log_level, "Starting graceful pipeline shutdown...");

  // STEP 1: Stop producers (no new frames)
  if (!ctx->file_readers.empty()) {
    APP_LOG(AppLogLevel::INFO, log_level, "Stopping %zu file reader(s)...", ctx->file_readers.size());
    for (size_t i = 0; i < ctx->file_readers.size(); i++) {
      if (ctx->file_readers[i]) {
        APP_LOG(AppLogLevel::DEBUG, log_level, "Stopping file reader %zu...", i);
        ctx->file_readers[i]->stop();
        APP_LOG(AppLogLevel::DEBUG, log_level, "File reader %zu stopped", i);
      }
    }
    APP_LOG(AppLogLevel::INFO, log_level, "All file readers stopped");
  }

  // Allow time for pending pipeline transactions to clear
  APP_LOG(AppLogLevel::DEBUG, log_level, "Allowing time for pending pipeline transactions to clear...");
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // STEP 2: Stop preprocessing (no new preprocessed frames)
  if (!ctx->preprocess.empty()) {
    APP_LOG(AppLogLevel::INFO, log_level, "Stopping %zu preprocessing instance(s)...", ctx->preprocess.size());
    for (size_t i = 0; i < ctx->preprocess.size(); i++) {
      if (ctx->preprocess[i]) {
        APP_LOG(AppLogLevel::DEBUG, log_level, "Stopping preprocessing instance %zu...", i);
        ctx->preprocess[i]->stop();
        APP_LOG(AppLogLevel::DEBUG, log_level, "Preprocessing instance %zu stopped", i);
      }
    }
    APP_LOG(AppLogLevel::INFO, log_level, "Preprocessing instances stopped");
  }

  // STEP 3: Mark inference input queues as finished (allows clean exit)
  APP_LOG(AppLogLevel::DEBUG, log_level, "Finishing inference input queues to signal completion...");
  for (auto& queue : ctx->inf_inqs_vec) {
    if (queue)
      queue->finish();
  }
  APP_LOG(AppLogLevel::DEBUG, log_level, "Inference input queues finished");

  // STEP 4: Adaptive pipeline drain (processes remaining frames, updates counters)
  bool drain_success = drain_pipeline(ctx);
  if (!drain_success) {
    APP_LOG(AppLogLevel::WARNING, log_level, "Pipeline drain incomplete - proceeding with forced shutdown");
  }

  // STEP 5: Stop consumers (now safe - work complete or timeout)
  if (!ctx->inference.empty()) {
    APP_LOG(AppLogLevel::INFO, log_level, "Stopping %zu inference instance(s)...", ctx->inference.size());
    for (size_t i = 0; i < ctx->inference.size(); i++) {
      if (ctx->inference[i]) {
        APP_LOG(AppLogLevel::DEBUG, log_level, "Stopping inference instance %zu...", i);
        ctx->inference[i]->stop();
        APP_LOG(AppLogLevel::DEBUG, log_level, "Inference instance %zu stopped", i);
      }
    }
    APP_LOG(AppLogLevel::INFO, log_level, "Inference instances stopped");
  }

  // STEP 6: Finish remaining queues and stop postprocess
  APP_LOG(AppLogLevel::DEBUG, log_level, "Finishing inference output queues...");
  for (auto& queue : ctx->inf_outqs_vec) {
    if (queue)
      queue->finish();
  }
  APP_LOG(AppLogLevel::DEBUG, log_level, "Inference output queues finished");

  // Also finish original frame queues if they exist
  if (!ctx->orig_frame_qs_vec.empty()) {
    APP_LOG(AppLogLevel::DEBUG, log_level, "Finishing original frame queues...");
    for (auto& queue : ctx->orig_frame_qs_vec) {
      if (queue)
        queue->finish();
    }
    APP_LOG(AppLogLevel::DEBUG, log_level, "Original frame queues finished");
  }

  // NOW safe to stop postprocess instances (threads can exit cleanly)
  if (!ctx->postprocess.empty()) {
    APP_LOG(AppLogLevel::INFO, log_level, "Stopping %zu postprocess instance(s)...", ctx->postprocess.size());
    for (size_t i = 0; i < ctx->postprocess.size(); i++) {
      if (ctx->postprocess[i]) {
        APP_LOG(AppLogLevel::DEBUG, log_level, "Stopping postprocess instance %zu...", i);
        ctx->postprocess[i]->stop();
        APP_LOG(AppLogLevel::DEBUG, log_level, "Postprocess instance %zu stopped", i);
      }
    }
    APP_LOG(AppLogLevel::INFO, log_level, "Postprocess instances stopped");
  }

  APP_LOG(AppLogLevel::INFO, log_level, "Pipeline stopped and drained successfully");
}

/* Clean up all resources after pipeline is stopped */
void destroy_all_context(AppContext* ctx) {
  AppLogLevel log_level = ctx->log_level;

  APP_LOG(AppLogLevel::INFO, log_level, "Cleaning up pipeline resources...");

  // Clear file readers
  if (!ctx->file_readers.empty()) {
    ctx->file_readers.clear();
    APP_LOG(AppLogLevel::DEBUG, log_level, "File readers destroyed");
  }

  // Clear preprocessing instances
  if (!ctx->preprocess.empty()) {
    ctx->preprocess.clear();
    APP_LOG(AppLogLevel::DEBUG, log_level, "Preprocessing instances destroyed");
  }

  // Clear inference instances
  if (!ctx->inference.empty()) {
    ctx->inference.clear();
    APP_LOG(AppLogLevel::DEBUG, log_level, "Inference instances destroyed");
  }

  // Clear postprocessing instances
  if (!ctx->postprocess.empty()) {
    ctx->postprocess.clear();
    APP_LOG(AppLogLevel::DEBUG, log_level, "Postprocess instances destroyed");
  }

  // Clear all queue vectors
  ctx->preproc_inqs_vec.clear();
  ctx->preproc_outqs_vec.clear();
  ctx->inf_inqs_vec.clear();
  ctx->inf_outqs_vec.clear();
  ctx->orig_frame_qs_vec.clear();
  ctx->postproc_outqs_vec.clear();
  APP_LOG(AppLogLevel::DEBUG, log_level, "Queue vectors cleared");

  // Clear model information
  ctx->model_info.clear();

  // Reset device handle
  if (ctx->device) {
    ctx->device.reset();
    APP_LOG(AppLogLevel::DEBUG, log_level, "Device handle released");
  }

  APP_LOG(AppLogLevel::INFO, log_level, "Resource cleanup completed successfully");
}
