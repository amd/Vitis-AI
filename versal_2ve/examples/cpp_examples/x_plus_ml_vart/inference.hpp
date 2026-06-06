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
 * @file inference.hpp
 * @brief Black box inference component for 4-thread pipeline design
 *
 * This module is a simple black box that:
 * 1. Pops 1 PreprocessedFrame from its input queue
 * 2. Runs inference on the frame
 * 3. Pushes results to its output queue
 *
 * Each inference instance works completely independently with its own
 * input/output queues. No frame selection or synchronization logic needed.
 */

#pragma once

#include <any>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vart/vart_device.hpp>
#include <vart/vart_npu_tensor.hpp>
#include <vector>

#include "app_queue.hpp"
#include "common/app_logger.hpp"
#include "frame_types.hpp"
#include "pool_timeouts.hpp"

#include <vart/vart_runner_factory.hpp>

// Per-tensor cache: maps pool object pointer to its NpuTensor (created once, reused via copy)
using InputTensorCache = std::unordered_map<vart::VideoFrame*, vart::NpuTensor>;
using OutputTensorCache = std::unordered_map<vart::Memory*, vart::NpuTensor>;

// Forward declarations to minimize header dependencies
namespace vart {
class Runner;
class VideoFrame;
class Memory;
}  // namespace vart

typedef struct {
  /* Tensor metadata */
  vart::NpuTensorInfo meta;
  /* Infer generate Quantized data, quantization_factor is used to quantized the
   * output of infer
   */
  double quantization_factor;
} InferTensorInfo;

// Configuration for inference component
struct InferenceConfig {
  uint32_t device_index;  ///< Device index for processing
  vart::Device* device;   ///< VART device context
  AppLogLevel log_level;
  vart::RunnerType runner_type;
  std::string model_path;
  std::unordered_map<std::string, std::any> runner_options;
  vart::TensorType input_tensor_type;
  vart::TensorType output_tensor_type;

  /* Output directory path to dump output files */
  std::string output_dir_path;
  bool dump_all_inputs;
  bool is_benchmark_enabled;
  int64_t max_iterations;  ///< Maximum number of iterations (for conditional filename generation)

  // Memory bank configuration
  uint32_t mbank_idx;  ///< required for output tensor allocation

  // Inference instance configuration
  uint32_t instance_id;  ///< Instance identifier for logging
  /** Parallel model instances; scales MemoryBufferPool acquire timeout */
  uint32_t num_model_instances;

  /* model metadata extracted from snapshot */
  uint32_t model_width;
  uint32_t model_height;
  uint32_t batch_size;
  size_t num_in_tensors;
  size_t num_out_tensors;
  std::vector<InferTensorInfo> in_tensors_info;
  std::vector<InferTensorInfo> out_tensors_info;

  bool has_generic_memory_layout;  ///< True if any input tensor uses GENERIC layout

  // Error signaling (shared atomic pointer)
  std::atomic<bool>* critical_error_ptr;  ///< Pointer to shared critical error flag

  /**
   * @brief Default constructor with sensible defaults
   */
  InferenceConfig()
      : device_index(0),
        device(nullptr),
        log_level(AppLogLevel::WARNING),
        runner_type(vart::RunnerType::VAIML),
        model_path(""),
        runner_options(),
        input_tensor_type(vart::TensorType::HW),
        output_tensor_type(vart::TensorType::HW),
        output_dir_path(""),
        dump_all_inputs(false),
        is_benchmark_enabled(false),
        max_iterations(1),
        mbank_idx(0),
        instance_id(0),
        num_model_instances(1),
        model_width(224),
        model_height(224),
        batch_size(1),
        num_in_tensors(0),
        num_out_tensors(0),
        in_tensors_info(),
        out_tensors_info(),
        has_generic_memory_layout(false),
        critical_error_ptr(nullptr) {}
};

// Forward declarations for frame types
struct PreprocessedFrame;
struct InferenceResult;
template <typename T>
class AppQueue;

/**
 * @brief Black box inference component class
 *
 * Simple inference engine that:
 * 1. Pops PreprocessedFrame from input queue
 * 2. Runs inference on the frame
 * 3. Pushes results to output queue
 *
 * Each instance works independently with its own queues.
 */
class Inference {
 public:
  static constexpr uint32_t DEFAULT_INPUT_QUEUE_DEPTH = 8;
  static constexpr uint32_t DEFAULT_OUTPUT_QUEUE_DEPTH = 8;

  explicit Inference(const InferenceConfig& config,
                     AppQueue<PreprocessedFrame>& input_queue,
                     AppQueue<InferenceResult>& output_queue);
  ~Inference();

  // Delete copy and move operations - this class manages unique resources
  Inference(const Inference&) = delete;
  Inference& operator=(const Inference&) = delete;
  Inference(Inference&&) = delete;
  Inference& operator=(Inference&&) = delete;

  bool initialize();
  bool start();
  void stop();
  bool is_running() const { return state_.load() == ThreadState::RUNNING; }
  bool is_processing() const { return processing_frame_.load(); }

  const InferenceConfig& get_config() const { return config_; }

  void get_queue_depths(uint32_t& input_depth, uint32_t& output_depth) const;
  /**
   * @brief Get instance processing time
   */
  float get_total_time_us() const;

 private:
  InferenceConfig config_;
  std::vector<MemoryBufferPoolPtr> output_pool_;  //< memory pool per output tensor

  // Threading and lifecycle
  std::unique_ptr<std::thread> worker_thread_;

  // Pipeline queue references (passed from AppContext)
  AppQueue<PreprocessedFrame>& input_queue_;  ///< Input queue with PreprocessedFrame from preprocessing
  AppQueue<InferenceResult>& output_queue_;   ///< Output queue for inference results

  // Unified state management
  std::atomic<ThreadState> state_;
  std::atomic<bool> processing_frame_{false};  ///< Track if currently processing a frame
  float total_time_;                           ///< accumulate processing time
  std::string inst_name_;

  // VART inference
  std::shared_ptr<vart::Runner> runner_;

  // Tensor caching: maps pool object pointer to NpuTensor (created once per pool slot)
  std::vector<InputTensorCache> input_tensor_cache_;    ///< Per input tensor index
  std::vector<OutputTensorCache> output_tensor_cache_;  ///< Per output tensor index

  void worker_thread_function();
  bool process_video_frame(BatchedFrames&& input_frames, int frame_index, int64_t iteration_number);
  BatchedTensors run_inference_on_frame(BatchedFrames input_frames, int frame_index, int64_t iteration_number);
  bool create_inference_runner();
  bool create_output_tensor_pools();
  TensorList acquire_output_tensors();

  // Cache management methods
  void clear_tensor_cache();            ///< Clear all cached tensors
  void log_cache_stats() const;         ///< Log cache statistics
  size_t get_total_cache_size() const;  ///< Get total number of cached tensors
};
