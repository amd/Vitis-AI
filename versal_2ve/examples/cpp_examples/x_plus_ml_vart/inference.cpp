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

/**
 * @file inference.cpp
 * @brief Implementation of black box inference component
 *
 * Simple black box that pops PreprocessedFrame from input queue,
 * runs inference, and pushes results to output queue.
 * Each instance works completely independently.
 */

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
#include "inference.hpp"
#include "x_plus_ml_app.hpp"  // For frame types and AppQueue

#include <vart/vart_memory.hpp>
#include <vart/vart_videoframe.hpp>

using namespace std;
using namespace vart;

#define DEFAULT_INFERENCE_POOL_TYPE vart::MemoryImplType::XRT

/* -----------------------Helper Functions------------------------------------------- */
static string vector_to_string(const std::vector<uint32_t>& vec) {
  ostringstream oss;
  for (size_t i = 0; i < vec.size(); ++i) {
    oss << vec[i];
    if (i != vec.size() - 1) {
      oss << "x";
    }
  }
  return oss.str();
}

/*
 * Convert vart::DataType to string
 */
static string get_data_type_string(const vart::DataType& data_type) {
  switch (data_type) {
    case vart::DataType::BOOLEAN:
      return "boolean";
    case vart::DataType::INT8:
      return "int8";
    case vart::DataType::UINT8:
      return "uint8";
    case vart::DataType::INT16:
      return "int16";
    case vart::DataType::UINT16:
      return "uint16";
    case vart::DataType::BF16:
      return "bf16";
    case vart::DataType::FP16:
      return "fp16";
    case vart::DataType::INT32:
      return "int32";
    case vart::DataType::UINT32:
      return "uint32";
    case vart::DataType::FLOAT32:
      return "fp32";
    case vart::DataType::INT64:
      return "int64";
    case vart::DataType::UINT64:
      return "uint64";
    default:
      return "UNKNOWN";
  }
}

/*
 * Convert vart::MemoryLayout to string
 */
static string get_memory_layout_string(const vart::MemoryLayout& layout) {
  switch (layout) {
    case vart::MemoryLayout::NC:
      return "NC";
    case vart::MemoryLayout::NCH:
      return "NCH";
    case vart::MemoryLayout::NHC:
      return "NHC";
    case vart::MemoryLayout::NHW:
      return "NHW";
    case vart::MemoryLayout::NHWC:
      return "NHWC";
    case vart::MemoryLayout::NCHW:
      return "NCHW";
    case vart::MemoryLayout::NHWC4:
      return "NHWC4";
    case vart::MemoryLayout::NHWC8:
      return "NHWC8";
    case vart::MemoryLayout::NC4HW4:
      return "NC4HW4";
    case vart::MemoryLayout::NC8HW8:
      return "NC8HW8";
    case vart::MemoryLayout::HCWNC4:
      return "HCWNC4";
    case vart::MemoryLayout::HCWNC8:
      return "HCWNC8";
    case vart::MemoryLayout::HCWNC16:
      return "HCWNC16";
    case vart::MemoryLayout::NHW16C4WC:
      return "NHW16C4WC";
    case vart::MemoryLayout::GENERIC:
      return "GENERIC";
    default:
      return "UNKNOWN";
  }
}

/*
 * Print tensor information
 */
static void print_tensor_info(const std::vector<InferTensorInfo>& infos, AppLogLevel log_level) {
  for (size_t i = 0; i < infos.size(); ++i) {
    string shape_str = vector_to_string(infos[i].meta.shape);
    APP_LOG(AppLogLevel::DEBUG, log_level, "Tensor[%ld] name: %s", i, infos[i].meta.name.c_str());
    APP_LOG(AppLogLevel::DEBUG, log_level, "Tensor[%ld] shape: %s", i, shape_str.c_str());
    APP_LOG(AppLogLevel::DEBUG, log_level, "Tensor[%ld] size: %ld", i, infos[i].meta.size_in_bytes);
    APP_LOG(AppLogLevel::DEBUG, log_level, "Tensor[%ld] memory_layout: %s", i,
            get_memory_layout_string(infos[i].meta.memory_layout).c_str());
    APP_LOG(AppLogLevel::DEBUG, log_level, "Tensor[%ld] data_type: %s", i,
            get_data_type_string(infos[i].meta.data_type).c_str());
    APP_LOG(AppLogLevel::DEBUG, log_level, "Tensor[%ld] quantization_factor: %f", i, infos[i].quantization_factor);
  }
}

/**
 * @brief Dump inference input data to a binary file for debugging purposes.
 *        All frames are concatenated to a single file.
 * @param ctx  Pointer to the application context containing configuration and
 *             state information.
 * @param map_info Pointer to the video frame map information.
 * @param name Name to be used in the output file name.
 * @param frame_index Index of the current frame (0 = first frame).
 * @param iteration_number Current iteration number for multi-iteration runs.
 */
void dump_infer_input_to_file(const InferenceConfig* ctx,
                              const vart::VideoFrameMapInfo* map_info,
                              const string& name,
                              int frame_index,
                              int64_t iteration_number) {
  AppLogLevel log_level = ctx->log_level;
  string file_name;
  string safe_name = name;

  // Replace '/' with '-'
  replace(safe_name.begin(), safe_name.end(), '/', '-');

  // Remove leading '-' if present
  if (!safe_name.empty() && safe_name.front() == '-') {
    safe_name.erase(0, 1);
  }

  // Conditionally include iteration number based on max_iterations
  if (ctx->max_iterations > 1) {
    // Multiple iterations - include iteration number in filename
    file_name = ctx->output_dir_path + "/iter" + to_string(iteration_number) + "_infer" + to_string(ctx->instance_id) +
                "_input-" + safe_name + ".bin";
  } else {
    // Single iteration - use original filename format
    file_name = ctx->output_dir_path + "/infer" + to_string(ctx->instance_id) + "_input-" + safe_name + ".bin";
  }

  // Create/truncate file on first frame, append on subsequent frames
  ios_base::openmode mode = ((frame_index == 0) ? (ios::binary | ios::trunc) : (ios::binary | ios::app));
  ofstream file(file_name, mode);

  // Check file open once at the start
  if (!file.is_open()) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Error opening file: %s", file_name.c_str());
    return;
  }

  switch (map_info->fmt) {
    case VideoFormat::RGB:
    case VideoFormat::BGR:
      // 3 bytes per pixel (packed RGB/BGR)
      for (int h = 0; h < map_info->height; h++) {
        uint8_t* src = map_info->planes[0].data + (h * map_info->planes[0].stride);
        file.write(reinterpret_cast<const char*>(src), (map_info->width * 3));
      }
      break;

    case VideoFormat::RGBx:
    case VideoFormat::BGRx:
      // 4 bytes per pixel (packed RGBx/BGRx)
      for (int h = 0; h < map_info->height; ++h) {
        uint8_t* row_ptr = map_info->planes[0].data + (h * map_info->planes[0].stride);
        file.write(reinterpret_cast<const char*>(row_ptr), map_info->width * 4);
      }
      break;

    case VideoFormat::RGBx_BF16:
    case VideoFormat::BGRx_BF16:
    case VideoFormat::RGBx_FP16:
    case VideoFormat::BGRx_FP16:
      // 8 bytes per pixel (packed RGBx/BGRx with BF16 or FP16)
      for (int h = 0; h < map_info->height; ++h) {
        uint8_t* row_ptr = map_info->planes[0].data + (h * map_info->planes[0].stride);
        file.write(reinterpret_cast<const char*>(row_ptr), map_info->width * 8);
      }
      break;

    case VideoFormat::RGBP:
      // Planar RGB: 1 byte per pixel per plane, dump line by line to skip padding
      for (uint8_t plane = 0; plane < map_info->nplanes; ++plane) {
        for (int h = 0; h < map_info->height; ++h) {
          uint8_t* row_ptr = map_info->planes[plane].data + (h * map_info->planes[plane].stride);
          file.write(reinterpret_cast<const char*>(row_ptr), map_info->width);
        }
      }
      break;

    case VideoFormat::RGBP_FLOAT:
      // Planar RGB float: 4 bytes per pixel per plane, dump line by line to skip padding
      for (uint8_t plane = 0; plane < map_info->nplanes; ++plane) {
        for (int h = 0; h < map_info->height; ++h) {
          uint8_t* row_ptr = map_info->planes[plane].data + (h * map_info->planes[plane].stride);
          file.write(reinterpret_cast<const char*>(row_ptr), map_info->width * sizeof(float));
        }
      }
      break;

    case VideoFormat::RGBP_BF16:
    case VideoFormat::RGBP_FP16:
      // Planar RGB half: 2 bytes per pixel per plane, dump line by line to skip padding
      for (uint8_t plane = 0; plane < map_info->nplanes; ++plane) {
        for (int h = 0; h < map_info->height; ++h) {
          uint8_t* row_ptr = map_info->planes[plane].data + (h * map_info->planes[plane].stride);
          file.write(reinterpret_cast<const char*>(row_ptr), map_info->width * sizeof(uint16_t));
        }
      }
      break;

    case VideoFormat::RGB_BF16:
    case VideoFormat::RGB_FP16:
      // 6 bytes per pixel (packed RGB with BF16 or FP16)
      for (int h = 0; h < map_info->height; ++h) {
        uint8_t* row_ptr = map_info->planes[0].data + (h * map_info->planes[0].stride);
        file.write(reinterpret_cast<const char*>(row_ptr), map_info->width * 3 * sizeof(uint16_t));
      }
      break;

    case vart::VideoFormat::GRAY8:
      // Gray8 format: 1 byte per pixel, no padding
      file.write(reinterpret_cast<const char*>(map_info->planes[0].data), map_info->size);
      break;

    default:
      APP_LOG(AppLogLevel::ERROR, log_level, "Unsupported video format: %d", static_cast<int>(map_info->fmt));
      break;
  }

  // Check for write errors and close
  if (!file.good()) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Error writing to file: %s", file_name.c_str());
  }
  file.close();
  APP_LOG(AppLogLevel::DEBUG, log_level, "Dumped infer input frame %d to %s", frame_index, file_name.c_str());
}

/**
 * @brief Dump output tensors to binary files
 * Single file per tensor with all batch elements concatenated (works for any batch_size)
 * All frames are concatenated to a single file per tensor.
 * @param ctx Pointer to the application context containing configuration and
 * state information.
 * @param npu_out_tensors_memory Vector of vectors containing shared pointers to
 * vart::Memory objects representing the output tensors for each batch.
 * @param frame_index Index of the current frame (0 = first frame).
 * @param iteration_number Current iteration number for multi-iteration runs.
 */
void dump_tensors_to_files(const InferenceConfig* ctx,
                           BatchedTensors& npu_out_tensors_memory,
                           int frame_index,
                           int64_t iteration_number) {
  AppLogLevel log_level = ctx->log_level;

  // Iterate over output tensors (not batch elements)
  for (size_t i = 0; i < ctx->out_tensors_info.size(); ++i) {
    string append_str = get_data_type_string(ctx->out_tensors_info[i].meta.data_type) + "_" +
                        vector_to_string(ctx->out_tensors_info[i].meta.shape) + "_";

    /* Create a file name for each tensor */
    string file_name;
    string safe_name = ctx->out_tensors_info[i].meta.name;

    // Replace '/' with '-'
    replace(safe_name.begin(), safe_name.end(), '/', '-');

    // Remove leading '-' if present
    if (!safe_name.empty() && safe_name.front() == '-') {
      safe_name.erase(0, 1);
    }

    // Conditionally include iteration number based on max_iterations
    if (ctx->max_iterations > 1) {
      // Multiple iterations - include iteration number in filename
      file_name = ctx->output_dir_path + "/iter" + to_string(iteration_number) + "_infer" +
                  to_string(ctx->instance_id) + "_out" + to_string(i) + "-" + append_str + safe_name + ".bin";
    } else {
      // Single iteration - use original filename format
      file_name = ctx->output_dir_path + "/infer" + to_string(ctx->instance_id) + "_out" + to_string(i) + "-" +
                  append_str + safe_name + ".bin";
    }

    // Create/truncate file on first frame, append on subsequent frames
    ios_base::openmode mode = ((frame_index == 0) ? (ios::binary | ios::trunc) : (ios::binary | ios::app));
    ofstream file(file_name, mode);

    if (!file) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Error opening file: %s", file_name.c_str());
      continue;
    }

    // Write all batch elements sequentially to the same file
    for (size_t b = 0; b < npu_out_tensors_memory.size(); ++b) {
      const unsigned char* mapped_memory = npu_out_tensors_memory[b][i]->map(vart::DataMapFlags::READ);
      file.write(reinterpret_cast<const char*>(mapped_memory), ctx->out_tensors_info[i].meta.size_in_bytes);
      npu_out_tensors_memory[b][i]->unmap();
    }

    file.close();
    APP_LOG(AppLogLevel::DEBUG, log_level, "Dumped tensor %ld (batch_size=%zu) frame %d to %s", i,
            npu_out_tensors_memory.size(), frame_index, file_name.c_str());
  }
}

// Constructor
Inference::Inference(const InferenceConfig& config,
                     AppQueue<PreprocessedFrame>& input_queue,
                     AppQueue<InferenceResult>& output_queue)
    : config_(config),
      input_queue_(input_queue),
      output_queue_(output_queue),
      state_(ThreadState::IDLE),
      total_time_(0),
      inst_name_("Inference" + std::to_string(config.instance_id)) {}

// Destructor
Inference::~Inference() {
  if (is_running()) {
    stop();
  }

  // Log cache statistics before cleanup and clear cache
  if (config_.log_level >= AppLogLevel::INFO) {
    log_cache_stats();
  }
  clear_tensor_cache();

  // Clean up memory pools (smart pointers handle automatic cleanup)
  output_pool_.clear();
}

// Initialize method
bool Inference::initialize() {
  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] Initializing...", inst_name_.c_str());

  try {
    if (!create_inference_runner()) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to create VART Runner");
      return false;
    }

    if (!create_output_tensor_pools()) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to create output tensor pools");
      return false;
    }

    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] initialized successfully", inst_name_.c_str());
    return true;

  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Exception during initialization: %s", e.what());
    return false;
  }
}

// Start method
bool Inference::start() {
  if (is_running()) {
    APP_LOG(AppLogLevel::WARNING, config_.log_level, "[%s] already running", inst_name_.c_str());
    return true;
  }

  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] Starting...", inst_name_.c_str());

  try {
    state_ = ThreadState::RUNNING;
    worker_thread_ = std::make_unique<std::thread>(&Inference::worker_thread_function, this);
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] started successfully", inst_name_.c_str());
    return true;

  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Failed to start worker thread: %s", inst_name_.c_str(),
            e.what());
    state_ = ThreadState::IDLE;
    return false;
  }
}

// Stop method
void Inference::stop() {
  if (!is_running()) {
    APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] already stopped", inst_name_.c_str());
    return;
  }

  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] Stopping...", inst_name_.c_str());

  state_ = ThreadState::SHUTTING_DOWN;
  if (worker_thread_ && worker_thread_->joinable()) {
    worker_thread_->join();
  }

  state_ = ThreadState::IDLE;
  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] stopped", inst_name_.c_str());
}

// Get queue depths method - updated for AppQueue
void Inference::get_queue_depths(uint32_t& input_depth, uint32_t& output_depth) const {
  input_depth = static_cast<uint32_t>(input_queue_.size());
  output_depth = static_cast<uint32_t>(output_queue_.size());
}

// Worker thread function - simple black box implementation
void Inference::worker_thread_function() {
  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] worker thread started", inst_name_.c_str());

  while (state_.load() == ThreadState::RUNNING) {
    try {
      PreprocessedFrame preprocessed_input;
      // Pop PreprocessedFrame from queue (blocking) - now includes frame_index!
      if (!input_queue_.pop(preprocessed_input)) {
        // Check if queue is finished (normal completion - all frames processed)
        if (input_queue_.is_finished()) {
          APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Input queue finished, exiting worker thread",
                  inst_name_.c_str());
          break;  // Clean exit - all frames processed
        }

        // Check for shutdown request
        if (state_.load() != ThreadState::RUNNING) {
          break;  // Shutdown requested
        }

        // Queue temporarily empty (inference faster than preprocessing) - wait briefly to avoid busy loop
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }

      // Set processing flag and process the video frames (black box operation)
      // Move frames in - they are released when run_inference_on_frame returns (before dump)
      int frame_index = preprocessed_input.frame_index;
      int64_t iteration_number = preprocessed_input.iteration_number;
      processing_frame_ = true;
      bool success =
          process_video_frame(std::move(preprocessed_input.preprocessed_frame), frame_index, iteration_number);
      preprocessed_input = {};  // Release any remaining references
      processing_frame_ = false;

      if (!success) {
        APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] CRITICAL: Failed to process frame %d. Shutting down.",
                inst_name_.c_str(), frame_index);

        // Signal critical error to application
        if (config_.critical_error_ptr) {
          *config_.critical_error_ptr = true;
        }
        // Trigger graceful shutdown
        state_ = ThreadState::SHUTTING_DOWN;
        break;
      }
    } catch (const std::exception& e) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level,
              "[%s] CRITICAL: Unexpected exception in worker thread: %s. Shutting down.", inst_name_.c_str(), e.what());
      // Signal critical error to application
      if (config_.critical_error_ptr) {
        *config_.critical_error_ptr = true;
      }
      // Exit thread gracefully
      state_ = ThreadState::SHUTTING_DOWN;
      break;
    }
  }
  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] worker thread exiting", inst_name_.c_str());
}

// Process video frame method - black box implementation
bool Inference::process_video_frame(BatchedFrames&& input_frames, int frame_index, int64_t iteration_number) {
  if (input_frames.empty()) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Empty input frames batch");
    return false;
  }

  size_t batch_size = input_frames.size();
  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Processing frame batch %d (batch_size=%zu)", inst_name_.c_str(),
          frame_index, batch_size);

  try {
    // Run inference - input_frames moved in; released when run_inference_on_frame returns
    // (before dump_tensors_to_files) so preprocess pool buffers are not held during disk I/O
    BatchedTensors inference_results = run_inference_on_frame(std::move(input_frames), frame_index, iteration_number);
    if (inference_results.empty()) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] inference failed", inst_name_.c_str());
      return false;
    }

    if (!config_.is_benchmark_enabled) {
      dump_tensors_to_files(&config_, inference_results, frame_index, iteration_number);
    }

    // Create output frame with ALL batch results
    InferenceResult output_frame;
    output_frame.frame_index = frame_index;
    output_frame.iteration_number = iteration_number;
    output_frame.inference_output = std::move(inference_results);

    // Push results to output queue
    if (!output_queue_.push(output_frame)) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to push results to output queue");
      return false;
    }

    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Successfully processed frame batch %d (size=%zu)",
            inst_name_.c_str(), frame_index, batch_size);

    return true;

  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Exception during frame processing: %s", e.what());
    return false;
  }
}

// Helper method to run inference on preprocessed video frames
// Returns vector of vectors: [batch_idx][tensor_idx]
BatchedTensors Inference::run_inference_on_frame(BatchedFrames input_frames,
                                                 int frame_index,
                                                 int64_t iteration_number) {
  AppLogLevel log_level = config_.log_level;
  const InferenceConfig* model_info = &config_;

  // Use actual input batch size (supports partial batches)
  uint32_t actual_batch_size = static_cast<uint32_t>(input_frames.size());
  APP_LOG(AppLogLevel::INFO, log_level, "Processing batch: actual_size=%u, config_size=%u", actual_batch_size,
          model_info->batch_size);

  // Build input batch from cached NpuTensors (one per pool VideoFrame, created on first use)
  std::vector<std::vector<vart::NpuTensor>> batch_in_nputensors;
  batch_in_nputensors.reserve(actual_batch_size);

  // Process each batch element
  for (uint32_t b = 0; b < actual_batch_size; b++) {
    if (input_frames[b].empty()) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Batch element %u is empty", b);
      return {};
    }

    std::vector<vart::NpuTensor> input_tensors;
    input_tensors.reserve(model_info->num_in_tensors);

    // Process each frame/tensor in this batch element
    for (uint32_t t = 0; t < model_info->num_in_tensors; t++) {
      if (t >= input_frames[b].size()) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Batch[%u] has %zu frames but model expects %zu", b,
                input_frames[b].size(), model_info->num_in_tensors);
        return {};
      }

      /* Look up or create a cached NpuTensor for this VideoFrame.
       * Pool VideoFrames have stable identities — the same pointer always maps
       * to the same dma-buf fd, so we export the fd only on the first encounter
       * and reuse the cached NpuTensor on subsequent frames.*/
      auto* vf = input_frames[b][t].get();
      auto it = input_tensor_cache_[t].find(vf);
      if (it == input_tensor_cache_[t].end()) {
        int fd = input_frames[b][t]->export_buffer();
        if (fd < 0) {
          APP_LOG(AppLogLevel::ERROR, log_level, "Failed to export fd from input videoframe[%u][%u]", b, t);
          return {};
        }
        it = input_tensor_cache_[t]
                 .emplace(vf, vart::NpuTensor(config_.in_tensors_info[t].meta, &fd, vart::MemoryType::DMA_FD))
                 .first;
        APP_LOG(AppLogLevel::DEBUG, log_level, "[%s] Created input tensor for VideoFrame=%p, fd=%d, batch[%u][%u]",
                inst_name_.c_str(), (void*)vf, fd, b, t);
      }
      input_tensors.push_back(it->second);

      if (model_info->dump_all_inputs) {
        const vart::VideoFrameMapInfo* map_info = nullptr;
        try {
          map_info = &input_frames[b][t]->map(vart::DataMapFlags::READ);
        } catch (const exception& e) {
          APP_LOG(AppLogLevel::ERROR, log_level, "Failed to map memory : %s", e.what());
          return {};
        }

        // Use frame_index + b so each batch element appends correctly
        dump_infer_input_to_file(model_info, map_info, model_info->in_tensors_info[t].meta.name, frame_index + b,
                                 iteration_number);
      }
    }

    batch_in_nputensors.push_back(std::move(input_tensors));
  }

  std::vector<std::vector<vart::NpuTensor>> batch_out_nputensors;
  BatchedTensors all_output_tensors;  //[batch][tensors]

  batch_out_nputensors.reserve(actual_batch_size);
  all_output_tensors.reserve(actual_batch_size);

  // Process each batch element
  for (unsigned int b = 0; b < actual_batch_size; ++b) {
    // Acquire output tensors (same pool used for all batches)
    auto tensors = acquire_output_tensors();
    if (tensors.empty()) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Failed to acquire output tensors for batch %u", b);
      return {};
    }
    // Store tensors in function scope for return after execution
    all_output_tensors.push_back(tensors);

    std::vector<vart::NpuTensor> output_tensors;
    output_tensors.reserve(config_.num_out_tensors);

    for (uint32_t t = 0; t < config_.num_out_tensors; t++) {
      /* Look up or create a cached NpuTensor for this Memory buffer.
       * Pool Memory objects have stable identities — the same pointer always maps
       * to the same dma-buf fd, so we export the fd only on the first encounter
       * and reuse the cached NpuTensor on subsequent frames. */
      auto* mem = tensors[t].get();
      auto it = output_tensor_cache_[t].find(mem);
      if (it == output_tensor_cache_[t].end()) {
        int fd = tensors[t]->export_buffer();
        if (fd < 0) {
          APP_LOG(AppLogLevel::ERROR, log_level, "Failed to export fd from output memory[%u][%u]", b, t);
          return {};
        }
        it = output_tensor_cache_[t]
                 .emplace(mem, vart::NpuTensor(config_.out_tensors_info[t].meta, &fd, vart::MemoryType::DMA_FD))
                 .first;
        APP_LOG(AppLogLevel::DEBUG, log_level, "[%s] Created output tensor for Memory=%p, fd=%d, batch[%u][%u]",
                inst_name_.c_str(), (void*)mem, fd, b, t);
      }
      output_tensors.push_back(it->second);
    }
    batch_out_nputensors.push_back(std::move(output_tensors));
  }

  // Execute inference and track result
  vart::StatusCode ret = vart::StatusCode::RUNTIME_ERROR;
  int64_t duration = 0;
  bool execute_success = false;

  try {
    auto start = chrono::high_resolution_clock::now();
    ret = runner_->execute(batch_in_nputensors, batch_out_nputensors);
    auto end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::microseconds>(end - start).count();
    total_time_ += duration;
    execute_success = (vart::StatusCode::SUCCESS == ret);
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "[%s] Inference execution exception: %s", inst_name_.c_str(), e.what());
    execute_success = false;
  }

  // Check execution result
  if (!execute_success) {
    APP_LOG(AppLogLevel::ERROR, log_level, "[%s] Inference execution failed with status=%d", inst_name_.c_str(),
            static_cast<int>(ret));
    return {};
  }

  APP_LOG(AppLogLevel::INFO, log_level, "[%s] Inference execution time: %.4f ms", inst_name_.c_str(),
          static_cast<float>(duration) / 1000);  // in ms

  // unmap frames if debug was enabled
  if (model_info->dump_all_inputs) {
    for (uint32_t b = 0; b < actual_batch_size; b++) {
      for (uint32_t t = 0; t < model_info->num_in_tensors; t++) {
        input_frames[b][t]->unmap();
      }
    }
  }

  // Return all batch output tensors (supports batch processing)
  return all_output_tensors;
}

// Create inference runner method
bool Inference::create_inference_runner() {
  try {
    auto start = chrono::high_resolution_clock::now();
    // Create VART Runner from snapshot
    runner_ = vart::RunnerFactory::create_runner(config_.runner_type, config_.model_path, config_.runner_options);
    if (!runner_) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to create VART Runner from snapshot: %s",
              config_.model_path.c_str());
      return false;
    }
    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::microseconds>(end - start).count();
    APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] Created VART Runner from model-path: %s in %.4f ms",
            inst_name_.c_str(), config_.model_path.c_str(), static_cast<float>(duration) / 1000);
  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Exception creating VART Runner: %s", e.what());
    return false;
  }

  /* Get input and output tensors */
  auto input_tensors_info = runner_->get_tensors_info(vart::TensorDirection::INPUT, config_.input_tensor_type);
  auto output_tensors_info = runner_->get_tensors_info(vart::TensorDirection::OUTPUT, config_.output_tensor_type);

  if (!input_tensors_info.size() && !output_tensors_info.size()) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Couldn't get input and output tensors");
    return false;
  }

  /* Extract information about the first input tensor for model dimensions */
  const uint32_t* in_shape = input_tensors_info[0].shape.data();

  /* Set model's information based on the tensors */
  /* Assumption we always has one input tensor and of size H*W*C */
  config_.batch_size = runner_->get_batch_size();
  config_.num_in_tensors = runner_->get_num_input_tensors();
  config_.num_out_tensors = runner_->get_num_output_tensors();

  // Resize tensor caches based on actual model requirements
  input_tensor_cache_.resize(config_.num_in_tensors);
  output_tensor_cache_.resize(config_.num_out_tensors);

  // Clear tensor cache when creating new runner
  clear_tensor_cache();

  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Input Tensor Memory layout: %s", inst_name_.c_str(),
          get_memory_layout_string(input_tensors_info[0].memory_layout).c_str());

  switch (input_tensors_info[0].memory_layout) {
    case vart::MemoryLayout::NHW:
    case vart::MemoryLayout::NHWC:
    case vart::MemoryLayout::NHWC4:
      config_.model_width = in_shape[2];
      config_.model_height = in_shape[1];
      break;

    case vart::MemoryLayout::NCHW:
    case vart::MemoryLayout::NC4HW4:
    case vart::MemoryLayout::NC8HW8:
      config_.model_width = in_shape[3];
      config_.model_height = in_shape[2];
      break;

    case vart::MemoryLayout::HCWNC4:
    case vart::MemoryLayout::HCWNC8:
    case vart::MemoryLayout::HCWNC16:
      config_.model_width = in_shape[2];
      config_.model_height = in_shape[0];
      break;

    case vart::MemoryLayout::GENERIC:
      /*model width and height is inferred by preprocessor if it is GENERIC*/
      config_.model_width = 0;   // not applicable for GENERIC
      config_.model_height = 0;  // not applicable for GENERIC
      break;

    default:
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Unsupported memory layout");
      return false;
  }

  /* Resize in_tensors_info to accommodate num_in_tensors elements */
  config_.in_tensors_info.resize(config_.num_in_tensors);

  /* Initialize GENERIC layout flag */
  config_.has_generic_memory_layout = false;

  /* Set input tensor information */
  for (size_t j = 0; j < config_.num_in_tensors; ++j) {
    config_.in_tensors_info[j].meta = input_tensors_info[j];
    auto quant_factor = runner_->get_quant_parameters(input_tensors_info[j].name).scale;
    config_.in_tensors_info[j].quantization_factor = (quant_factor < 0.0f) ? DEFAULT_QUANT_FACTOR : quant_factor;

    // Check if this tensor uses GENERIC memory layout
    if (input_tensors_info[j].memory_layout == vart::MemoryLayout::GENERIC) {
      config_.has_generic_memory_layout = true;
    }
  }

  /* Resize out_tensors_info to accommodate num_out_tensors elements */
  config_.out_tensors_info.resize(config_.num_out_tensors);

  /* Set output tensor information */
  for (size_t j = 0; j < config_.num_out_tensors; ++j) {
    config_.out_tensors_info[j].meta = output_tensors_info[j];
    auto quant_factor = runner_->get_quant_parameters(output_tensors_info[j].name).scale;
    config_.out_tensors_info[j].quantization_factor = (quant_factor < 0.0f) ? DEFAULT_QUANT_FACTOR : quant_factor;
  }

  /* Log model and tensor information for debugging */
  {
    APP_LOG(AppLogLevel::DEBUG, config_.log_level,
            "[%s] Batch size: %d, Num Input Tensors: %ld, "
            "Num Output Tensors: %ld",
            inst_name_.c_str(), config_.batch_size, config_.num_in_tensors, config_.num_out_tensors);
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Model resolution: width x height: %dx%d", inst_name_.c_str(),
            config_.model_width, config_.model_height);

    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Input Tensors Info:", inst_name_.c_str());
    print_tensor_info(config_.in_tensors_info, config_.log_level);
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Output Tensors Info:", inst_name_.c_str());
    print_tensor_info(config_.out_tensors_info, config_.log_level);
  }

  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] VART Runner created successfully", inst_name_.c_str());
  return true;
}

// Create output tensor pools method
bool Inference::create_output_tensor_pools() {
  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Creating output tensor memory pools...", inst_name_.c_str());

  try {
    /* Create one memory pool per output tensor
     * - Each output tensor can have different size/shape
     */
    uint8_t mem_index = config_.mbank_idx;
    DevicePtr device_ptr(config_.device, [](vart::Device*) {});

    // Resize to number of output tensors
    output_pool_.resize(config_.num_out_tensors);

    uint32_t pool_depth = config_.batch_size * INFERENCE_QUEUE_DEPTH;

    for (unsigned int i = 0u; i < config_.num_out_tensors; ++i) {
      uint32_t buf_size = config_.out_tensors_info[i].meta.size_in_bytes;

      if (!buf_size) {
        APP_LOG(AppLogLevel::ERROR, config_.log_level, "Invalid output tensor size: %d", buf_size);
        throw std::runtime_error("Invalid output tensor size");
      }

      output_pool_[i] =
          std::make_unique<MemoryBufferPool>(pool_depth, DEFAULT_INFERENCE_POOL_TYPE, buf_size, mem_index, device_ptr,
                                             MEMORY_BUFFER_TIMEOUT_DURATION(config_.num_model_instances));
      APP_LOG(AppLogLevel::DEBUG, config_.log_level,
              "[%s] Created output tensor pool[%u]: depth=%u, size=%u bytes, shape=%s, name=%s", inst_name_.c_str(), i,
              pool_depth, buf_size, vector_to_string(config_.out_tensors_info[i].meta.shape).c_str(),
              config_.out_tensors_info[i].meta.name.c_str());
    }

    APP_LOG(AppLogLevel::INFO, config_.log_level,
            "[%s] Created %zu output tensor pools (depth=%u per pool, total_buffers=%zu)", inst_name_.c_str(),
            config_.num_out_tensors, pool_depth, config_.num_out_tensors * pool_depth);
    return true;
  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Exception creating output tensor pools: %s", e.what());
    return false;
  }
}

// Acquire output tensors method:
TensorList Inference::acquire_output_tensors() {
  TensorList tensors;
  tensors.reserve(config_.num_out_tensors);

  try {
    // Acquire one buffer from each tensor's pool
    for (uint32_t i = 0; i < config_.num_out_tensors; i++) {
      auto memory = output_pool_[i]->acquire_buffer();
      tensors.push_back(std::move(memory));
    }

    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Acquired %zu output tensors from pools", inst_name_.c_str(),
            config_.num_out_tensors);
    return tensors;

  } catch (const std::runtime_error& e) {
    /* The pool throws on acquire timeout or on pool shutdown; either way
     * the partial `tensors` vector is destroyed on return and the
     * shared_ptr deleters recycle whatever was already acquired. */
    const bool shutting_down = std::string(e.what()).find("shutting down") != std::string::npos;
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] %s while acquiring output tensors: %s", inst_name_.c_str(),
            shutting_down ? "Pool shutdown" : "Timeout", e.what());
    return {};
  }
}

float Inference::get_total_time_us() const {
  return total_time_;
}

// Clear all cached tensors
void Inference::clear_tensor_cache() {
  size_t total_cleared = 0;

  // Clear input tensor cache
  for (size_t i = 0; i < input_tensor_cache_.size(); i++) {
    total_cleared += input_tensor_cache_[i].size();
    input_tensor_cache_[i].clear();
  }

  // Clear output tensor cache
  for (size_t i = 0; i < output_tensor_cache_.size(); i++) {
    total_cleared += output_tensor_cache_[i].size();
    output_tensor_cache_[i].clear();
  }

  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] Tensor cache cleared: %zu tensors freed", inst_name_.c_str(),
          total_cleared);
}

// Log cache statistics
void Inference::log_cache_stats() const {
  size_t total_input = 0, total_output = 0;

  // Log input tensor cache
  for (size_t i = 0; i < input_tensor_cache_.size(); i++) {
    size_t in_size = input_tensor_cache_[i].size();
    total_input += in_size;
    if (in_size > 0) {
      APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Input Cache[%ld]: %zu tensors", inst_name_.c_str(), i,
              in_size);
    }
  }

  // Log output tensor cache
  for (size_t i = 0; i < output_tensor_cache_.size(); i++) {
    size_t out_size = output_tensor_cache_[i].size();
    total_output += out_size;
    if (out_size > 0) {
      APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Output Cache[%ld]: %zu tensors", inst_name_.c_str(), i,
              out_size);
    }
  }

  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] Total cache: %zu input tensors, %zu output tensors (total=%zu)",
          inst_name_.c_str(), total_input, total_output, total_input + total_output);
}

// Get total number of cached tensors
size_t Inference::get_total_cache_size() const {
  size_t total = 0;

  // Count input tensor cache
  for (size_t i = 0; i < input_tensor_cache_.size(); i++) {
    total += input_tensor_cache_[i].size();
  }

  // Count output tensor cache
  for (size_t i = 0; i < output_tensor_cache_.size(); i++) {
    total += output_tensor_cache_[i].size();
  }

  return total;
}
