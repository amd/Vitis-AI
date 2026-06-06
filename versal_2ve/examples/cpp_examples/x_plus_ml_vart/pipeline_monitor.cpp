/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
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
 * @file pipeline_monitor.cpp
 * @brief This file has the methods for monitoring the pipeline processing.
 */

#include <chrono>
#include "x_plus_ml_app.hpp"

using namespace vart;

/**
 * @brief Handle preprocessing completion and implement tight coupling bridge
 */
uint32_t handle_preprocess_completion(AppContext* ctx) {
  AppLogLevel log_level = ctx->log_level;

  try {
    // Each preprocessing instance produces output for its coupled inference
    // instance preprocess_output[i] → inference[i]
    //
    // Multi-instance behavior: Process all available results in this cycle,
    // route each to its coupled inference, and return count of results
    // processed
    uint32_t results_processed = 0;

    for (const auto& binding : ctx->instance_bindings) {
      PreprocessedFrame current_preprocess_output;

      // Check for preprocessing completion from the bound output queue
      // (non-blocking)
      if (binding.preproc_outq->try_pop(current_preprocess_output)) {
        APP_LOG(AppLogLevel::DEBUG, log_level, "Collected preprocessing result for frame %d from preprocess[%u]",
                current_preprocess_output.frame_index, binding.preproc_inst_id);

        // Route to the coupled inference instance using binding access
        if (binding.inf_inq->push(current_preprocess_output, 0)) {
          APP_LOG(AppLogLevel::DEBUG, log_level,
                  "Successfully routed frame %d from preprocess[%u] to "
                  "inference[%u] via binding",
                  current_preprocess_output.frame_index, binding.preproc_inst_id, binding.inf_inst_id);
          results_processed++;
        } else {
          APP_LOG(AppLogLevel::DEBUG, log_level, "Inference %u input queue full for frame %d", binding.inf_inst_id,
                  current_preprocess_output.frame_index);
          APP_LOG(AppLogLevel::WARNING, log_level, "Dropping frame %d due to full inference queue %u",
                  current_preprocess_output.frame_index, binding.inf_inst_id);
        }
      }
    }

    if (results_processed > 0) {
      APP_LOG(AppLogLevel::DEBUG, log_level,
              "Successfully processed %u preprocessing results with 1:1 tight "
              "coupling",
              results_processed);
    }

    return results_processed;
  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Exception in 1:1 tight coupling routing: %s", e.what());
    return 0;
  }
}

/**
 * @brief Collect inference results asynchronously (with timeout)
 *
 * This function abstracts the completion source based on postprocessing configuration:
 * - WITHOUT postprocessing: Pops from inference output queue
 * - WITH postprocessing: Pops from postprocess completion queue
 */
static bool handle_inference_completion(AppContext* ctx,
                                        uint32_t inst_index,
                                        InferenceResult& npu_output,
                                        uint32_t timeout_ms,
                                        bool has_postproc,
                                        uint32_t& actual_batch_size) {
  AppLogLevel log_level = ctx->log_level;

  try {
    if (has_postproc) {
      // PATH A: Get completion notification from postprocess completion queue
      if (inst_index >= ctx->postproc_outqs_vec.size() || !ctx->postproc_outqs_vec[inst_index]) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Completion queue not found for postprocess instance %u", inst_index);
        return false;
      }

      ProcessingComplete completion;
      bool got_completion = false;

      if (timeout_ms == 0) {
        // Non-blocking check
        got_completion = ctx->postproc_outqs_vec[inst_index]->try_pop(completion);
      } else {
        // Blocking pop
        got_completion = ctx->postproc_outqs_vec[inst_index]->pop(completion);
      }

      if (got_completion) {
        // Convert to InferenceResult for uniform interface
        // Note: inference_output vector remains empty for postproc path
        npu_output.iteration_number = completion.iteration_number;
        npu_output.frame_index = completion.frame_index;
        npu_output.inference_output.clear();  // No tensor data for postproc path

        // Extract actual batch size from completion (supports partial batches)
        actual_batch_size = completion.batch_size;

        APP_LOG(AppLogLevel::DEBUG, log_level,
                "Got postprocess completion for instance %u: iter=%ld, frame=%d, batch_size=%u", inst_index,
                completion.iteration_number, completion.frame_index, actual_batch_size);
        return true;
      }
      return false;

    } else {
      // PATH B: Get result from inference output queue (existing logic)
      if (inst_index >= ctx->instance_bindings.size()) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Binding not found for inference instance %u", inst_index);
        return false;
      }

      const auto& binding = ctx->instance_bindings[inst_index];
      AppQueue<InferenceResult>* output_queue = binding.inf_outq;

      // Check for completed inference results
      if (timeout_ms == 0) {
        // Non-blocking check - return immediately if no results available
        if (!output_queue->try_pop(npu_output)) {
          return false;  // No results available
        }
      } else {
        // Blocking pop with timeout handling
        if (!output_queue->pop(npu_output)) {
          return false;  // Queue finished and empty
        }
      }

      // Extract actual batch size from inference output
      actual_batch_size = static_cast<uint32_t>(npu_output.inference_output.size());

      APP_LOG(AppLogLevel::DEBUG, log_level, "Got inference result from instance %u: iter=%ld, frame=%d, batch_size=%u",
              inst_index, npu_output.iteration_number, npu_output.frame_index, actual_batch_size);
      return true;
    }

  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Exception in completion handling: %s", e.what());
    return false;
  }
}

/**
 * @brief Trigger graceful pipeline shutdown on critical error
 * Stops all components in the correct order to ensure clean shutdown
 */
void trigger_pipeline_shutdown(AppContext* ctx, const std::string& reason) {
  AppLogLevel log_level = ctx->log_level;

  APP_LOG(AppLogLevel::ERROR, log_level, "CRITICAL: Initiating emergency pipeline shutdown - %s", reason.c_str());

  // Phase 1: Stop FileReaders first (prevent new data from entering pipeline)
  APP_LOG(AppLogLevel::INFO, log_level, "Shutdown Phase 1: Stopping FileReaders...");
  for (auto& reader : ctx->file_readers) {
    if (reader && reader->is_running()) {
      reader->stop();
    }
  }

  // Phase 2: Brief pause to allow in-flight frames to drain
  APP_LOG(AppLogLevel::DEBUG, log_level, "Shutdown Phase 2: Draining in-flight frames...");
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Phase 3: Stop preprocessing
  APP_LOG(AppLogLevel::INFO, log_level, "Shutdown Phase 3: Stopping Preprocessing...");
  for (auto& preproc : ctx->preprocess) {
    if (preproc && preproc->is_running()) {
      preproc->stop();
    }
  }

  // Phase 4: Stop inference
  APP_LOG(AppLogLevel::INFO, log_level, "Shutdown Phase 4: Stopping Inference...");
  for (auto& inf : ctx->inference) {
    if (inf && inf->is_running()) {
      inf->stop();
    }
  }

  // Phase 5: Stop postprocess
  APP_LOG(AppLogLevel::INFO, log_level, "Shutdown Phase 5: Stopping PostProcess...");
  for (auto& post : ctx->postprocess) {
    if (post && post->is_running()) {
      post->stop();
    }
  }

  APP_LOG(AppLogLevel::INFO, log_level, "Pipeline shutdown complete - all components stopped");
}

/**
 * @brief Process available inference results from any instance that completes first
 *
 * Unified completion handling that abstracts the source:
 * - WITHOUT postprocessing: Gets results from inference output queue, processes tensors
 * - WITH postprocessing: Gets completion notifications from postprocess completion queue
 *
 * @param ctx Application context
 * @return count of results processed (0 if no results available)
 */
uint32_t handle_all_inference_completions(AppContext* ctx) {
  AppLogLevel log_level = ctx->log_level;
  uint32_t results_processed = 0;

  // Unified result processing - handle_inference_completion abstracts the source
  for (const auto& binding : ctx->instance_bindings) {
    uint32_t inst_id = binding.inf_inst_id;

    // Compute postprocessing flag once for this instance
    bool has_postproc = (inst_id < ctx->postprocess_enable.size() && ctx->postprocess_enable[inst_id] &&
                         inst_id < ctx->postprocess.size() && ctx->postprocess[inst_id] != nullptr);

    // Unified handling - abstracted by handle_inference_completion
    InferenceResult result_frame;
    uint32_t actual_batch_size = 0;
    if (handle_inference_completion(ctx, inst_id, result_frame, 0, has_postproc, actual_batch_size)) {
      // Update per-model frame count (actual_batch_size now correctly includes partial batch support)
      if (inst_id < ctx->frames_processed_per_model.size()) {
        ctx->frames_processed_per_model[inst_id] += actual_batch_size;
      }

      results_processed += actual_batch_size;

      const char* mode_str = has_postproc ? "postprocessed" : "inference-only";
      APP_LOG(AppLogLevel::DEBUG, log_level, "Completed %u frames from instance %u (%s): iteration %ld, frame %d",
              actual_batch_size, inst_id, mode_str, result_frame.iteration_number, result_frame.frame_index);
    }
  }

  if (results_processed > 0) {
    APP_LOG(AppLogLevel::DEBUG, log_level, "Processed %u completions in this cycle (inference + postprocess)",
            results_processed);
  }

  return results_processed;
}
