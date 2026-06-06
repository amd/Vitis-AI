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
 * @file postprocess.hpp
 * @brief PostProcess component for pipeline sink processing
 *
 * This module is a sink component that:
 * 1. Pops InferenceResult from inference output queue
 * 2. Optionally pops original InputFrame (when preprocessing enabled)
 * 3. Runs three-stage processing: PostProcess → MetaConvert → Overlay
 * 4. Outputs results to files (no output queue - pipeline sink)
 *
 * Each postprocess instance is 1:1 bound to an inference instance.
 * Processing stages:
 * - PostProcess: Converts output tensors to InferResult (classification/detection)
 * - MetaConvert: Converts InferResult to overlay metadata (conditional)
 * - Overlay: Draws results on original frames (conditional)
 */

#pragma once

#include <atomic>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <opencv2/opencv.hpp>

#include <vart/vart_device.hpp>
#include <vart/vart_inferresult.hpp>
#include <vart/vart_metaconvert.hpp>
#include <vart/vart_overlay.hpp>
#include <vart/vart_overlay_types.hpp>
#include <vart/vart_postprocess.hpp>
#include <vart/vart_postprocess_types.hpp>

#include "app_queue.hpp"
#include "common/app_logger.hpp"
#include "frame_types.hpp"
#include "inference.hpp"

// Postprocess-specific type aliases
using PostProcessPtr = std::unique_ptr<vart::PostProcess>;
using MetaConvertPtr = std::unique_ptr<vart::MetaConvert>;
using OverlayPtr = std::unique_ptr<vart::Overlay>;

// Forward declarations to minimize header dependencies
namespace vart {
class PostProcess;
class MetaConvert;
class Overlay;
class InferResult;
}  // namespace vart

/**
 * @brief Configuration for postprocess component
 * Filled in by application and used for this module
 */
struct PostProcessConfig {
  // Basic configuration
  uint32_t instance_id;   ///< Instance identifier for logging
  AppLogLevel log_level;  ///< Component log level

  // Model information (from inference config)
  uint32_t model_batch_size;                              ///< Model batch size
  uint32_t model_input_width;                             ///< Model input width for coordinate transformation
  uint32_t model_input_height;                            ///< Model input height for coordinate transformation
  uint32_t model_num_input_tensors;                       ///< Number of input tensors
  std::vector<InferTensorInfo> model_input_tensors_info;  ///< Input tensor metadata
  size_t model_num_out_tensors;                           ///< Number of output tensors
  std::vector<InferTensorInfo> model_out_tensors_info;    ///< Output tensor metadata

  // PostProcess specific configuration
  /**
   * @brief Type of postprocessing.
   * @see vart::PostProcessType for full list of supported legacy and modular types.
   */
  vart::PostProcessType postprocess_type;
  std::string postprocess_json;  ///< JSON configuration for vart::PostProcess
  std::string metaconvert_json;  ///< JSON configuration for vart::MetaConvert

  // Output configuration
  std::string output_dir_path;  ///< Directory for output files
  bool is_metaconvert_enabled;  ///< create metaconvert and overlay if enabled
  bool is_benchmark_enabled;    ///< Disable output writing in benchmark mode
  uint32_t max_iterations;      ///< Maximum number of iterations for file naming
  bool is_jpg_input;  ///< True if input format is JPG (single frame), false if NV12/BGR (multiple frames). This is
                      ///< needed for output format decision.

  // Device reference (raw pointer - non-owning)
  vart::Device* device;  ///< VART device context

  // Error signaling (shared atomic pointer)
  std::atomic<bool>* critical_error_ptr;  ///< Pointer to shared critical error flag

  /**
   * @brief Default constructor with sensible defaults
   */
  PostProcessConfig()
      : instance_id(0),
        log_level(AppLogLevel::WARNING),
        model_batch_size(1),
        model_input_width(224),
        model_input_height(224),
        model_num_input_tensors(0),
        model_input_tensors_info(),
        model_num_out_tensors(0),
        model_out_tensors_info(),
        postprocess_type(vart::PostProcessType::RESNET50),
        postprocess_json(""),
        metaconvert_json(""),
        output_dir_path("output"),
        is_metaconvert_enabled(false),
        is_benchmark_enabled(false),
        max_iterations(1),
        is_jpg_input(false),
        device(nullptr),
        critical_error_ptr(nullptr) {}
};

/**
 * @brief PostProcess component class - Pipeline sink
 *
 * Sink component that:
 * 1. Receives inference results from bound inference instance
 * 2. Optionally receives original frames for overlay
 * 3. Processes through vart-x pipeline
 * 4. Outputs to files (no downstream queues)
 *
 * Each instance is 1:1 bound to an inference instance.
 */
class AppPostProcess {
 public:
  static constexpr uint32_t DEFAULT_QUEUE_TIMEOUT_MS = 100;
  static constexpr size_t MAX_FRAME_BUFFER_STORAGE_SIZE = 20;

  /**
   * @brief Constructor
   * @param config PostProcess configuration
   * @param inference_queue Reference to inference output queue (1:1 binding)
   * @param original_frame_queue Pointer to original frame queue (nullptr if
   * preprocessing disabled)
   * @param completion_queue Pointer to completion notification queue (nullptr if not needed)
   */
  explicit AppPostProcess(const PostProcessConfig& config,
                          AppQueue<InferenceResult>& inference_queue,
                          AppQueue<InputFrame>* original_frame_queue,
                          AppQueue<ProcessingComplete>* completion_queue);

  /**
   * @brief Destructor - ensures proper cleanup
   */
  ~AppPostProcess();

  // Delete copy and move operations - this class manages unique resources
  AppPostProcess(const AppPostProcess&) = delete;
  AppPostProcess& operator=(const AppPostProcess&) = delete;
  AppPostProcess(AppPostProcess&&) = delete;
  AppPostProcess& operator=(AppPostProcess&&) = delete;

  /**
   * @brief Initialize postprocess components
   * Creates vart::PostProcess and conditionally MetaConvert/Overlay
   * @return true on success, false on failure
   */
  bool initialize();

  /**
   * @brief Start the postprocess thread
   * @return true on success, false on failure
   */
  bool start();

  /**
   * @brief Stop the postprocess thread gracefully
   */
  void stop();

  /**
   * @brief Check if postprocess thread is running
   * @return true if running, false otherwise
   */
  bool is_running() const { return state_.load() == ThreadState::RUNNING; }

  /**
   * @brief Check if currently processing a frame
   * @return true if actively processing, false otherwise
   */
  bool is_processing() const { return processing_frame_.load(); }

  /**
   * @brief Get component configuration
   * @return Reference to configuration
   */
  const PostProcessConfig& get_config() const { return config_; }

  /**
   * @brief Get total processing time
   * @return Total time in microseconds
   */
  float get_total_time_us() const;

  /**
   * @brief Get number of frames processed
   * @return Total frames processed
   */
  uint64_t get_frames_processed() const { return frames_processed_.load(); }

 private:
  PostProcessConfig config_;  ///< Component configuration

  // vart-x components
  PostProcessPtr vart_postprocess_;  ///< PostProcess instance
  MetaConvertPtr vart_metaconvert_;  ///< MetaConvert instance (conditional)
  OverlayPtr vart_overlay_;          ///< Overlay instance (conditional)
  DevicePtr device_ptr_;             ///< Shared device pointer for vart modules

  // Queue connections (1:1 binding with inference)
  AppQueue<InferenceResult>& inference_queue_;      ///< Input queue from inference
  AppQueue<InputFrame>* original_frame_queue_;      ///< Optional queue for original frames
  AppQueue<ProcessingComplete>* completion_queue_;  ///< Optional queue for completion notifications

  // Frame synchronization (when preprocessing enabled)
  std::map<std::pair<int64_t, int>, InputFrame> frame_buffer_;  ///< Buffer for original frames
  std::mutex buffer_mutex_;                                     ///< Mutex for frame buffer access

  // Threading and lifecycle
  std::unique_ptr<std::thread> worker_thread_;  ///< Worker thread
  std::atomic<ThreadState> state_;              ///< Thread state
  std::atomic<bool> processing_frame_;          ///< Currently processing a frame

  // Statistics
  std::atomic<uint64_t> frames_processed_;  ///< Total frames processed
  float total_time_;                        ///< Accumulated processing time (us)

  // Output file handles
  std::ofstream result_file_;      ///< Text output file
  std::ofstream raw_output_file_;  ///< Raw video output file for overlay frames
  std::string inst_name_;          ///< Instance name for logging
  int64_t current_iteration_;      ///< Track current iteration for raw video file management

  /**
   * @brief Worker thread main function
   * Continuously processes inference results and outputs to files
   */
  void worker_thread_function();

  /**
   * @brief Process single inference result
   * @param result Inference result to process
   * @return true on success, false on failure
   */
  bool process_inference_result(const InferenceResult& result);

  /**
   * @brief Get matching original frames for batch processing
   * @param iteration_number Iteration number to match
   * @param start_frame_index Starting frame index
   * @param batch_size Number of frames in batch
   * @param output_frames Vector to store retrieved frames
   * @return true if all frames found, false otherwise
   */
  bool get_matching_frames_batch(int64_t iteration_number,
                                 int start_frame_index,
                                 uint32_t batch_size,
                                 std::vector<InputFrame>& output_frames);

  /**
   * @brief Run MetaConvert and Overlay pipeline on single frame
   * @param frame_results Inference results for this frame
   * @param original_frame Original frame for overlay
   * @param frame_index Frame index for output naming
   * @param iteration_number Iteration number for output naming
   */
  void run_metaconvert_overlay(const InferResultList& frame_results,
                               const InputFrame& original_frame,
                               int frame_index,
                               int64_t iteration_number);

  /**
   * @brief Convert VideoFrame to cv::Mat for saving
   * @param video_frame VideoFrame to convert
   * @return OpenCV Mat
   */
  cv::Mat video_frame_to_mat(std::shared_ptr<vart::VideoFrame> video_frame);

  /**
   * @brief Store original frame in buffer for later matching
   * @param frame Frame to store
   */
  void store_original_frame(const InputFrame& frame);

  /**
   * @brief Save text results to file
   * @param infer_results Vector of inference results
   * @param frame_index Starting frame index
   * @return true on success, false on failure
   */
  bool save_text_results(const BatchedInferResults& infer_results, int frame_index);

  /**
   * @brief Save overlaid frame to file - format follows input format
   * JPG input -> JPG output (single frame)
   * NV12 input -> NV12 output (multiple frames)
   * @param video_frame Frame with overlay drawn
   * @param frame_index Frame index for filename
   * @param iteration_number Iteration number for filename
   * @return true on success, false on failure
   */
  bool save_overlay_frame(const std::shared_ptr<vart::VideoFrame>& video_frame,
                          int frame_index,
                          int64_t iteration_number);

  /**
   * @brief Save overlaid frame as JPG file (single frame processing)
   * @param video_frame Frame with overlay drawn
   * @param frame_index Frame index for filename
   * @param iteration_number Iteration number for filename
   * @return true on success, false on failure
   */
  bool save_overlay_frame_as_jpg(const std::shared_ptr<vart::VideoFrame>& video_frame, int64_t iteration_number);

  /**
   * @brief Save overlaid raw frame as NV12/BGR file (multiple frames processing)
   * @param video_frame Frame with overlay drawn
   * @param frame_index Frame index for filename
   * @param iteration_number Iteration number for filename
   * @return true on success, false on failure
   */
  bool save_overlay_frame_as_raw(const std::shared_ptr<vart::VideoFrame>& video_frame,
                                 int frame_index,
                                 int64_t iteration_number);

  /**
   * @brief Open raw output file like NV12/BGR (on-demand, handles iteration switching)
   * @param iteration_number Iteration number for filename
   * @param format Video format for output
   * @return true on success, false on failure
   */
  bool open_raw_output_file(int64_t iteration_number, vart::VideoFormat format);

  /**
   * @brief Open output files for results
   * @return true on success, false on failure
   */
  bool open_output_files();

  /**
   * @brief Close output files
   */
  void close_output_files();

  /**
   * @brief Create vart::PostProcess instance
   * @return true on success, false on failure
   */
  bool create_vart_postprocess();

  /**
   * @brief Create vart::MetaConvert instance (conditional)
   * Only created when original frames are available
   * @return true on success, false on failure
   */
  bool create_vart_metaconvert();

  /**
   * @brief Create vart::Overlay instance (conditional)
   * Only created when original frames are available
   * @return true on success, false on failure
   */
  bool create_vart_overlay();
};
