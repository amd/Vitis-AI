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
 * @file file_reader.hpp
 * @brief File Reader component for dedicated file I/O thread
 *
 * This module provides a dedicated thread for reading input files and
 * submitting frames to the pipeline. It manages its own buffer pool and
 * handles file operations independently from the main thread.
 */

#pragma once

#include <atomic>
#include <fstream>
#include <functional>
#include <memory>
#include <opencv2/opencv.hpp>
#include <string>
#include <thread>

#include "app_queue.hpp"
#include "common/app_logger.hpp"
#include "frame_types.hpp"
#include "pool_timeouts.hpp"

#include <vart/vart_device.hpp>
#include <vart/vart_npu_tensor.hpp>
#include <vart/vart_videoframe.hpp>
#include <vart/vart_videoframe_types.hpp>

// Need full definition for InferTensorInfo since we use std::vector<InferTensorInfo>
#include "inference.hpp"

// Forward declaration
namespace vart {
class Device;
class VideoFrame;
}  // namespace vart

typedef enum {
  /* Input file read operation was successful */
  APP_READ_SUCCESS = 0,
  /* End-of-file (EOF) reached */
  APP_EOF,
  /* Input file read operation encountered an error */
  APP_READ_FAILED
} AppReadStatus;

typedef enum {
  APP_VIDEO_INPUT_FORMAT_UNKNOWN = 0,
  APP_VIDEO_INPUT_FORMAT_JPEG,
  APP_VIDEO_INPUT_FORMAT_NV12,
  APP_VIDEO_INPUT_FORMAT_BGR,
  APP_VIDEO_INPUT_FORMAT_BINARY
} AppVideoInputFormat;

/**
 * @brief Configuration for AppFileReader
 */

struct FileReaderConfig {
  // File configuration
  std::string input_image_file_path;  ///< Path to input image file (JPEG/NV12/BGR for preprocessing)
  int32_t num_frames_to_process;      ///< num frames to process in a sequence

  // Frame dimensions
  uint32_t bin_input_width;   ///< raw input width
  uint32_t bin_input_height;  ///< raw input height
  uint32_t model_width;       ///< model input width
  uint32_t model_height;      ///< model input height
  uint32_t model_batch_size;  ///< Batch size of model

  // Memory configuration
  uint8_t mem_bank;      ///< Memory bank for buffer allocation
  vart::Device* device;  ///< VART device context

  // Control parameters
  uint32_t max_iterations;                 ///< Maximum number of iterations to run
  AppLogLevel log_level;                   ///< Component log level
  std::atomic<int>* frames_submitted_ptr;  ///< Pointer to shared frames submitted counter
  uint32_t instance_id;                    ///< Instance identifier for logging
  uint32_t num_model_instances;            ///< Total parallel pipelines (for VideoFramePool acquire timeout)

  // Direct-to-inference mode
  bool bypass_preprocessing;  ///< true = direct to inference, false = to preprocessing

  // Pipeline queue depths (for pool sizing to prevent deadlock)
  uint32_t preprocess_queue_depth;   ///< Preprocessing queue depth (0 if bypassed)
  uint32_t inference_queue_depth;    ///< Inference queue depth
  uint32_t postprocess_queue_depth;  ///< Postprocess queue depth (0 if not connected)

  // Multiple input tensor files from ifms-config (inference-only mode)
  std::vector<std::string>
      input_tensors_file_path;  ///< Paths to binary tensor files, in runner-reported input-tensor order
  size_t num_input_tensors;     ///< Number of input tensors for this model
  std::vector<InferTensorInfo> input_tensor_info;  ///< Tensor metadata from model

  // VideoInfo provider for preprocessing mode
  std::function<vart::VideoInfo(uint32_t height,
                                uint32_t width,
                                vart::VideoFormat fmt)>
      get_vinfo;  ///< Lambda to get VideoInfo from preprocess

  // Error signaling (shared atomic pointer)
  std::atomic<bool>* critical_error_ptr;  ///< Pointer to shared critical error flag

  /**
   * @brief Default constructor with sensible defaults
   */
  FileReaderConfig()
      : num_frames_to_process(1),
        bin_input_width(0),
        bin_input_height(0),
        model_width(0),
        model_height(0),
        model_batch_size(1),
        mem_bank(0),
        device(nullptr),
        max_iterations(1),
        log_level(AppLogLevel::WARNING),
        frames_submitted_ptr(nullptr),
        instance_id(0),
        num_model_instances(1),
        bypass_preprocessing(false),
        preprocess_queue_depth(0),
        inference_queue_depth(3),
        postprocess_queue_depth(0),
        num_input_tensors(0),
        critical_error_ptr(nullptr) {}
};

/**
 * @brief Application File Reader - Dedicated thread for file I/O
 *
 * This component runs in its own thread and is responsible for:
 * - Opening and managing input files
 * - Creating and managing its own output buffer pool
 * - Reading frames from files in configurable batches
 * - Submitting frames to output queue
 * - Handling file rewinding for multiple iterations
 * - Error reporting that causes application exit on read failures
 */
class AppFileReader {
 public:
  /**
   * @brief Constructor
   * @param config File reader configuration
   * @param output_queue Single output queue (1:1 mapping)
   *                     preprocess_en:  AppQueue<InputFrame>*
   *                     !preprocess_en: AppQueue<PreprocessedFrame>* (cast to void*)
   * @param postprocess_queue Optional single postprocess queue for original frames (1:1 mapping)
   *                          AppQueue<InputFrame>* (cast to void*), nullptr if not needed
   */
  AppFileReader(const FileReaderConfig& config, void* output_queue, void* postprocess_queue = nullptr);

  /**
   * @brief Destructor - ensures cleanup
   */
  ~AppFileReader();

  // Delete copy/move operations - this class manages unique resources
  AppFileReader(const AppFileReader&) = delete;
  AppFileReader& operator=(const AppFileReader&) = delete;
  AppFileReader(AppFileReader&&) = delete;
  AppFileReader& operator=(AppFileReader&&) = delete;

  /**
   * @brief Initialize file reader (open file, create pool)
   * @return true on success, false on failure
   */
  bool initialize();

  /**
   * @brief Start the file reader thread
   * @return true on success, false on failure
   */
  bool start();

  /**
   * @brief Stop the file reader thread gracefully
   */
  void stop();

  /**
   * @brief Check if reader is running
   * @return true if running, false otherwise
   */
  bool is_running() const { return state_.load() == ThreadState::RUNNING; }

  /**
   * @brief Get total frames read
   * @return Total number of frames read
   */
  uint64_t get_frames_read() const { return frames_read_.load(); }

  /**
   * @brief Get current iteration
   * @return Current iteration number
   */
  uint32_t get_current_iteration() const { return current_iteration_.load(); }

  /**
   * @brief Get total frames read
   * @return Total number of frames read
   */
  vart::VideoFormat get_input_vart_format(void) const;

  /**
   * @brief Get extracted file type
   * @return input frame width
   */
  AppVideoInputFormat get_input_file_type(void) const { return input_fmt_; }

  /**
   * @brief Get extracted input width
   * @return input frame width
   */
  uint32_t get_input_width(void) const { return dectected_input_width_; }

  /**
   * @brief Get extracted input height
   * @return input frame height
   */
  uint32_t get_input_height(void) const { return dectected_input_height_; }

  /**
   * @brief Check if error occurred
   * @return true if error occurred, false otherwise
   */
  bool has_error() const { return error_occurred_.load(); }

 private:
  // Configuration
  FileReaderConfig config_;  ///< Component configuration

  // File handling
  std::ifstream input_file_;             ///< For NV12/binary files
  AppVideoInputFormat input_fmt_;        ///< Type of input file (JPEG, NV12, etc.)
  uint32_t dectected_input_width_;       ///< width extracted from input file
  uint32_t dectected_input_height_;      ///< height extracted from input file
  bool jpeg_read_in_current_iteration_;  ///< Track if JPEG was read in current iteration

  // Memory management
  VideoFramePoolPtr output_pool_;                ///< Output buffer pool (preprocessing mode)
  std::vector<VideoFramePoolPtr> tensor_pools_;  ///< Per-tensor buffer pools (inference-only mode)

  // Queues for output (void* to support both InputFrame and PreprocessedFrame)
  void* output_queue_;       ///< Single output queue (1:1 mapping)
  void* postprocess_queue_;  ///< Optional single postprocess queue for original frames (1:1 mapping, nullable)

  // Thread management
  std::unique_ptr<std::thread> worker_thread_;  ///< Worker thread
  std::atomic<ThreadState> state_;

  // Statistics and state
  std::atomic<uint64_t> frames_read_;        ///< Total frames read
  std::atomic<uint32_t> current_iteration_;  ///< Current iteration number
  std::atomic<bool> error_occurred_;         ///< Error flag
  int frame_index_;                          ///< Current frame index
  int32_t frames_read_in_iteration_;         ///< Frames read in current iteration
  std::string inst_name_;

  /**
   * @brief Worker thread main function
   * Continuously reads frames and submits to output queue
   */
  void worker_thread_function();

  /**
   * @brief worker thread - reads JPEG/NV12 and creates InputFrame and broadcast to preprocess instance
   */
  void worker_thread_preprocess_input();

  /**
   * @brief worker thread - reads binary files and creates PreprocessedFrame and send to inference
   */
  void worker_thread_inference_input();

  /**
   * @brief Create output VideoFrame pool for reader
   */
  bool create_output_pool(vart::VideoInfo& in_vinfo);

  /**
   * @brief Read frames from file into batch
   * @param batch Output batch to fill
   * @return APP_READ_SUCCESS, APP_EOF, or APP_READ_FAILED
   */
  AppReadStatus read_frame_batch(InputFrame& batch);

  /**
   * @brief Read single frame from file
   * @param frame Frame buffer to fill
   * @return Read status
   */
  AppReadStatus read_single_frame(vart::VideoFrame* frame);

  /**
   * @brief Submit batch to single output queue (1:1 mapping)
   * @param batch Batch to submit
   * @param is_partial True if this is a partial batch (for logging)
   * @return true if successfully submitted, false otherwise
   */
  bool submit_batch_to_queue(const InputFrame& batch, bool is_partial = false);

  /**
   * @brief Open input file based on configuration
   * @return true on success, false on failure
   */
  bool open_input_file();

  /**
   * @brief Close input file
   */
  void close_input_file();

  /**
   * @brief Rewind file for next iteration
   * @return true on success, false on failure
   */
  bool rewind_input_file();

  /**
   * @brief Extract input resolution from file
   * @return true on success, false on failure
   */
  bool extract_input_resolution();

  /**
   * @brief Check if should continue processing (max iterations check only)
   * @return true if more iterations remain, false if max iterations reached
   */
  bool should_continue_processing() const;

  /**
   * @brief Handle iteration transition (increment, reset counters, rewind)
   * @return true on success, false on failure
   */
  bool handle_iteration_transition();

  /**
   * @brief Update read counters after successful batch read
   * @param batch_size Number of frames in the batch
   * @param num_submissions Number of successful queue submissions
   */
  void update_read_counters(size_t batch_size, uint32_t num_submissions);

  /**
   * @brief Handle queue submission failure (set error flags, signal app)
   */
  void handle_queue_submission_failure();
};
