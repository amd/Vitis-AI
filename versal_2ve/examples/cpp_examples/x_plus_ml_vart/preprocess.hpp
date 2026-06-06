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
 * OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR other DEALINGS IN THE
 * SOFTWARE. Except as contained in this notice, the name of the AMD shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from AMD.
 */

/**
 * @file preprocess.hpp
 * @brief Preprocessing component
 *
 * This module interfaces with VART PreProcess library to handle
 * hardware-accelerated preprocessing.This module is a simple black box that:
 * 1. Pops 1 input video frame from its input queue
 * 2. Runs preprocessing function on the frame
 * 3. Pushes results to its output queue
 *
 * Each preprocess instance works completely independently with its own
 * configuration and its won input/output queues.No frame selection or
 * synchronization logic needed.
 */

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include <vart/vart_device.hpp>
#include <vart/vart_preprocess.hpp>
#include <vart/vart_preprocess_types.hpp>
#include <vart/vart_videoframe.hpp>

#include "common/app_logger.hpp"
#include "pool_timeouts.hpp"

#include "app_queue.hpp"
#include "frame_types.hpp"

// Forward declarations to minimize header dependencies
namespace vart {
class PreProcess;
class VideoFrame;
class Device;
}  // namespace vart

/* HLS HW accelerated image pre-processing IP */
#define DEFAULT_PREPROCESS_TYPE vart::PreProcessImplType::IMAGE_PROCESSING_HLS
#define DEFAULT_PREPROCESS_POOL_DEPTH 4

/**
 * @brief Configuration for preprocessing component
 * Filled in by application and used for this module
 * Application extracts this from user JSON one instance at a time
 */
struct PreProcessConfig {
  // Model dimensions (extracted from InferModelConf)
  uint32_t batch_size;                      ///< Model batch size
  uint32_t frames_per_batch;                ///< Model tensors per batch
  double input_tensor_quantization_factor;  ///< Quantization factor from first
                                            ///< input tensor
  uint32_t output_width;                    ///< Output frame width
  uint32_t output_height;                   ///< Output frame height

  // Device configuration
  vart::Device* device;  ///< VART device context

  // Quantization params
  bool quant_scale_factor_conf_set;  ///< Whether quant scale factor is user-provided
  float quant_scale_factor;          ///< User-provided quantization scale factor

  // Input/Output dimensions
  uint32_t input_width;   ///< Input frame width
  uint32_t input_height;  ///< Input frame height

  // Memory bank configuration
  uint32_t in_mem_bank;                ///< Input memory bank
  uint32_t out_mem_bank;               ///< Output memory bank
  vart::VideoFrameImplType pool_type;  ///< Pool implementation type
  uint32_t instance_id;                ///< Instance identifier for logging

  /** Parallel model instances (pipelines); scales VideoFramePool acquire timeout */
  uint32_t num_model_instances;

  // Logging and processing options
  AppLogLevel log_level;  ///< Component log level
  bool do_pan_scan;       ///< Enable pan-scan cropping

  // JSON configuration for VART PreProcess initialization
  std::string json_str;                  ///< JSON configuration string for VART PreProcess
  std::string colour_format_str;         ///< Colour-format from JSON (optional; e.g. "RGBX", "RGB", "BGR_FLOAT", ...)
  vart::PreProcessInfo preprocess_info;  ///< VART preprocess information

  // Input dumping configuration
  bool dump_all_inputs;         ///< Enable dumping of input frames for debugging
  std::string output_dir_path;  ///< Output directory path for dumped files
  uint32_t max_iterations;      ///< Maximum iterations for iteration-aware file naming

  // Error signaling (shared atomic pointer)
  std::atomic<bool>* critical_error_ptr;  ///< Pointer to shared critical error flag

  /**
   * @brief Default constructor with sensible defaults
   */
  PreProcessConfig()
      : batch_size(1),
        frames_per_batch(1),
        input_tensor_quantization_factor(1.0),
        output_width(224),
        output_height(224),
        device(nullptr),
        quant_scale_factor_conf_set(false),
        quant_scale_factor(1.0f),
        input_width(1920),
        input_height(1080),
        in_mem_bank(0),
        out_mem_bank(0),
        pool_type(vart::VideoFrameImplType::XRT),
        instance_id(0),
        num_model_instances(1),
        log_level(AppLogLevel::WARNING),
        do_pan_scan(false),
        json_str(""),
        colour_format_str(""),
        preprocess_info(),
        dump_all_inputs(false),
        output_dir_path(""),
        max_iterations(1),
        critical_error_ptr(nullptr) {}
};

/**
 * @brief Black box preprocess component class
 *
 * Simple preprocess engine that:
 * 1. Pops InputFrame from input queue
 * 2. Send frames for processing to PL IP
 * 3. Pushes results to output queue
 *
 * Each instance works independently with its own queues.
 */
class AppPreProcess {
 public:
  /**
   * @brief Constructor
   * @param config Dual preprocessing configuration (application creates 2
   * configs at application level)
   * @param input_queue Pointer to input queue (InputFrame from main thread)
   * @param output_queue Pointer to output queue (PreprocessedFrame to inference
   * threads)
   */
  explicit AppPreProcess(const PreProcessConfig& config,
                         AppQueue<InputFrame>& input_queue,
                         AppQueue<PreprocessedFrame>& output_queue);

  /**
   * @brief Destructor - ensures proper cleanup of VART contexts
   */
  ~AppPreProcess();

  // Delete copy and move operations - this class manages unique resources
  AppPreProcess(const AppPreProcess&) = delete;
  AppPreProcess& operator=(const AppPreProcess&) = delete;
  AppPreProcess(AppPreProcess&&) = delete;
  AppPreProcess& operator=(AppPreProcess&&) = delete;

  /**
   * @brief Initialize both VART preprocessing engines internally
   * @return true on success, false on failure
   */
  bool initialize();

  /**
   * @brief Start the preprocessing thread
   * @return true on success, false on failure
   */
  bool start();

  /**
   * @brief Stop the preprocessing thread gracefully
   */
  void stop();

  /**
   * @brief Check if preprocessing thread is running
   * @return true if running, false otherwise
   */
  bool is_running() const { return state_.load() == ThreadState::RUNNING; }

  /**
   * @brief Get component configuration
   * @return Reference to dual configuration
   */
  const PreProcessConfig& get_config() const { return config_; }

  /**
   * @brief Get current queue depths for monitoring
   * @param input_depth Reference to store input queue depth
   * @param output_depth Reference to store output queue depth
   */
  void get_queue_depths(uint32_t& input_depth, uint32_t& output_depth) const;

  /**
   * @brief Get input video info for frame creation
   * @param height Input frame height
   * @param width Input frame width
   * @param fmt Video format
   * @return VideoInfo structure with input requirements
   */
  vart::VideoInfo get_input_vinfo(uint32_t height, uint32_t width, vart::VideoFormat fmt) const;

  /**
   * @brief Get instance processing time
   */
  float get_total_time_us() const;

 private:
  PreProcessConfig config_;                        ///< component configuration
  std::unique_ptr<vart::PreProcess> pre_process_;  ///< VART preprocess context (smart pointer)

  // Output memory pools for dual preprocessing
  VideoFramePoolPtr output_pool_;  ///< Output memory pool preprocessing

  // Queue pointers for pipeline communication
  AppQueue<InputFrame>* input_queue_;          ///< Input queue from main thread
  AppQueue<PreprocessedFrame>* output_queue_;  ///< Output queue to inference threads

  // Threading and lifecycle
  std::unique_ptr<std::thread> worker_thread_;  ///< Preprocessing worker thread
  std::atomic<ThreadState> state_;
  float total_time_;  ///< accumulate processing time
  std::string inst_name_;

  /**
   * @brief Worker thread function
   * Continuously processes frames from input queue and outputs to output queue
   */
  void worker_thread_function();

  /**
   * @brief Process single input frame through entire preprocessing pipeline
   * @param input_frame Input frame from main thread
   * @return true on success, false on failure
   */
  bool process_input_frame(const InputFrame& input_frame);

  /**
   * @brief Create and initialize both VART PreProcess engines
   * Initialize VART preprocess at init
   * @return true on success, false on failure
   */
  bool create_preprocess_engine();

  /**
   * @brief Create output memory pools for both preprocessing engines
   * @return true on success, false on failure
   */
  bool create_output_pool();

  /**
   * @brief Acquire output frame from first memory pool
   * @return Shared pointer to acquired frame, nullptr on failure
   */
  std::shared_ptr<vart::VideoFrame> acquire_output_frame();

  /**
   * @brief Process frame using VART engine
   * Actual function to trigger PL processing (called 2 times internally)
   * @param pre_process VART preprocess instance to use
   * @param input_frame Input video frame
   * @param output_frame Output video frame
   * @param frame_index Index of the current frame (0 = first frame)
   * @param iteration_number Current iteration number for multi-iteration runs
   * @return true on success, false on failure
   */
  bool process_frame_with_vart(std::shared_ptr<vart::VideoFrame> input_frame,
                               std::shared_ptr<vart::VideoFrame> output_frame,
                               int frame_index,
                               int64_t iteration_number);

  /**
   * @brief Set ROI for PanScan cropping
   * @param preprocess_op PreProcess operation to modify
   * @param config Configuration containing PanScan settings and log level
   */
  void set_roi_pan_scan(vart::PreProcessOp& preprocess_op);
};
