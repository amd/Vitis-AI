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
 * @file postprocess.cpp
 * @brief Implementation of PostProcess component
 */

#include "postprocess.hpp"

#include <chrono>
#include <iomanip>
#include <numeric>
#include <opencv2/opencv.hpp>
#include <sstream>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <vart/vart_memory.hpp>
#include <vart/vart_videoframe.hpp>

using namespace std;
using namespace vart;
namespace pt = boost::property_tree;

// Default overlay type for vart::Overlay
static constexpr vart::OverlayImplType DEFAULT_OVERLAY_TYPE = vart::OverlayImplType::OPENCV;

static string build_shape_string(const std::vector<uint32_t>& shape) {
  string shape_str;
  size_t len = 0;
  for (size_t i = 0; i < shape.size(); ++i) {
    len += to_string(shape[i]).size();
    if (i + 1 < shape.size())
      len += 1;
  }
  shape_str.reserve(len);
  for (size_t i = 0; i < shape.size(); ++i) {
    shape_str += to_string(shape[i]);
    if (i + 1 < shape.size())
      shape_str += "x";
  }
  return shape_str;
}

/**
 * @brief Constructor
 */
AppPostProcess::AppPostProcess(const PostProcessConfig& config,
                               AppQueue<InferenceResult>& inference_queue,
                               AppQueue<InputFrame>* original_frame_queue,
                               AppQueue<ProcessingComplete>* completion_queue)
    : config_(config),
      vart_postprocess_(nullptr),
      vart_metaconvert_(nullptr),
      vart_overlay_(nullptr),
      inference_queue_(inference_queue),
      original_frame_queue_(original_frame_queue),
      completion_queue_(completion_queue),
      worker_thread_(nullptr),
      state_(ThreadState::IDLE),
      processing_frame_(false),
      frames_processed_(0),
      total_time_(0.0f),
      current_iteration_(-1) {
  // Create instance name for logging
  inst_name_ = "PostProcess" + to_string(config_.instance_id);
}

/**
 * @brief Destructor
 */
AppPostProcess::~AppPostProcess() {
  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] Destructor called", inst_name_.c_str());

  // Stop thread if running
  if (is_running()) {
    stop();
  }

  // Close output files
  close_output_files();

  // Clean up vart-x components
  vart_postprocess_.reset();
  vart_metaconvert_.reset();
  vart_overlay_.reset();
}

/**
 * @brief Initialize postprocess components
 */
bool AppPostProcess::initialize() {
  APP_LOG(AppLogLevel::INFO, config_.log_level, "Initializing %s...", inst_name_.c_str());

  try {
    // Skip file operations in benchmark mode
    if (!config_.is_benchmark_enabled) {
      // Validate output directory path
      if (config_.output_dir_path.empty()) {
        APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Output directory path is empty", inst_name_.c_str());
        return false;
      }

      // Open output files
      if (!open_output_files()) {
        APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Failed to open output files", inst_name_.c_str());
        return false;
      }
    } else {
      APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Benchmark mode enabled - skipping file I/O operations",
              inst_name_.c_str());
    }

    // Create vart::PostProcess
    if (!create_vart_postprocess()) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Failed to create vart::PostProcess", inst_name_.c_str());
      return false;
    }

    // Conditionally create MetaConvert and Overlay
    if (config_.is_metaconvert_enabled) {
      if (!create_vart_metaconvert()) {
        APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Failed to create vart::MetaConvert", inst_name_.c_str());
        return false;
      }

      if (!create_vart_overlay()) {
        APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Failed to create vart::Overlay", inst_name_.c_str());
        return false;
      }
    } else {
      APP_LOG(AppLogLevel::DEBUG, config_.log_level,
              "[%s] preprocess disabled (no original frames available), skipping MetaConvert and Overlay",
              inst_name_.c_str());
    }

    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Initialization complete", inst_name_.c_str());
    return true;

  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Exception during initialization: %s", inst_name_.c_str(),
            e.what());
    return false;
  }
}

/**
 * @brief Start the postprocess thread
 */
bool AppPostProcess::start() {
  if (is_running()) {
    APP_LOG(AppLogLevel::WARNING, config_.log_level, "%s already running", inst_name_.c_str());
    return false;
  }

  APP_LOG(AppLogLevel::INFO, config_.log_level, "Starting %s...", inst_name_.c_str());

  state_ = ThreadState::RUNNING;
  worker_thread_ = make_unique<thread>(&AppPostProcess::worker_thread_function, this);

  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "%s started successfully", inst_name_.c_str());
  return true;
}

/**
 * @brief Stop the postprocess thread gracefully
 */
void AppPostProcess::stop() {
  if (!is_running()) {
    APP_LOG(AppLogLevel::WARNING, config_.log_level, "%s already stopped", inst_name_.c_str());
    return;
  }

  APP_LOG(AppLogLevel::INFO, config_.log_level, "Stopping %s...", inst_name_.c_str());

  state_ = ThreadState::SHUTTING_DOWN;
  if (worker_thread_ && worker_thread_->joinable()) {
    worker_thread_->join();
  }

  state_ = ThreadState::IDLE;
  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "%s stopped: Processed %lu frames, total time: %.2f ms",
          inst_name_.c_str(), frames_processed_.load(), total_time_ / 1000.0f);
}

/**
 * @brief Get total processing time
 */
float AppPostProcess::get_total_time_us() const {
  return total_time_;
}

/**
 * @brief Worker thread main function
 */
void AppPostProcess::worker_thread_function() {
  APP_LOG(AppLogLevel::INFO, config_.log_level, "%s worker thread started", inst_name_.c_str());

  while (state_.load() == ThreadState::RUNNING) {
    try {
      // Handle original frames (non-blocking) if queue exists
      if (original_frame_queue_) {
        InputFrame orig_frame;
        // Try to pop - will return false if queue is empty
        if (original_frame_queue_->pop(orig_frame)) {
          store_original_frame(orig_frame);
        }
      }

      // Handle inference results (blocking)
      InferenceResult result;
      if (!inference_queue_.pop(result)) {
        // Queue finished or empty, continue
        continue;
      }

      // Process the inference result
      if (!process_inference_result(result)) {
        APP_LOG(AppLogLevel::ERROR, config_.log_level,
                "[%s] CRITICAL: Failed to process inference result for frame %d. Shutting down.", inst_name_.c_str(),
                result.frame_index);

        // Signal critical error to application
        if (config_.critical_error_ptr) {
          *config_.critical_error_ptr = true;
        }

        // Trigger graceful shutdown
        state_ = ThreadState::SHUTTING_DOWN;
        break;  // Exit the while loop
      }

      // Only increment counter on successful processing
      frames_processed_++;

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

  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] Worker thread exiting", inst_name_.c_str());
}

/**
 * @brief Process single inference result (with full batch support)
 */
bool AppPostProcess::process_inference_result(const InferenceResult& result) {
  // Mark as processing
  processing_frame_ = true;

  // Get batch size from inference_output structure
  uint32_t batch_size = static_cast<uint32_t>(result.inference_output.size());

  APP_LOG(AppLogLevel::DEBUG, config_.log_level,
          "[%s] Processing batch starting at frame %d (iteration %ld, batch_size=%u)", inst_name_.c_str(),
          result.frame_index, result.iteration_number, batch_size);

  auto start_time = chrono::high_resolution_clock::now();

  // Step 1: Handle frame retrieval based on configuration
  std::vector<InputFrame> original_frames;
  bool preprocessing_enabled = (original_frame_queue_ != nullptr);

  if (preprocessing_enabled) {
    // Frames MUST be available when preprocessing is enabled
    bool frames_found =
        get_matching_frames_batch(result.iteration_number, result.frame_index, batch_size, original_frames);
    if (!frames_found) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level,
              "[%s] Pipeline sync error: Missing original frames for batch starting at %d", inst_name_.c_str(),
              result.frame_index);
      processing_frame_ = false;
      return false;  // Error - no cleanup needed, tensors not mapped yet
    }
  }

  // Process FULL BATCH through vart::PostProcess
  BatchedInferResults infer_results;
  try {
    infer_results = vart_postprocess_->process(result.inference_output, batch_size);
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] PostProcess returned results for %zu frames",
            inst_name_.c_str(), infer_results.size());
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Exception in vart::PostProcess: %s", inst_name_.c_str(),
            e.what());

    // Unmap tensors before returning
    for (uint32_t batch_idx = 0; batch_idx < batch_size; ++batch_idx) {
      for (size_t tensor_idx = 0; tensor_idx < config_.model_num_out_tensors; ++tensor_idx) {
        result.inference_output[batch_idx][tensor_idx]->unmap();
      }
    }
    processing_frame_ = false;
    return false;
  }

  // Step 2: Process each frame based on configuration
  if (preprocessing_enabled) {
    // Full pipeline: PostProcess → Transform → MetaConvert → Overlay
    for (uint32_t batch_idx = 0; batch_idx < batch_size; ++batch_idx) {
      if (batch_idx < infer_results.size()) {
        run_metaconvert_overlay(infer_results[batch_idx], original_frames[batch_idx], (result.frame_index + batch_idx),
                                result.iteration_number);
      } else {
        APP_LOG(AppLogLevel::WARNING, config_.log_level, "[%s] No inference results for batch index %u",
                inst_name_.c_str(), batch_idx);
      }
    }
  } else {
    // PostProcess only path - save results to text file
    for (uint32_t batch_idx = 0; batch_idx < batch_size; ++batch_idx) {
      if (batch_idx < infer_results.size()) {
        save_text_results({infer_results[batch_idx]}, (result.frame_index + batch_idx));
      } else {
        APP_LOG(AppLogLevel::WARNING, config_.log_level, "[%s] No inference results for batch index %u",
                inst_name_.c_str(), batch_idx);
      }
    }
  }

  auto end_time = chrono::high_resolution_clock::now();
  auto duration = chrono::duration_cast<chrono::microseconds>(end_time - start_time).count();
  total_time_ += duration;

  // Send single completion notification for the batch (with actual batch size for partial batch support)
  if (completion_queue_) {
    ProcessingComplete completion(result.iteration_number, result.frame_index, config_.instance_id, batch_size);
    if (!completion_queue_->push(completion)) {
      APP_LOG(AppLogLevel::WARNING, config_.log_level,
              "[%s] Failed to push completion notification for batch at frame %d", inst_name_.c_str(),
              result.frame_index);
    } else {
      APP_LOG(AppLogLevel::DEBUG, config_.log_level,
              "[%s] Sent completion notification: iter=%ld, batch_start_frame=%d (batch_size=%u)", inst_name_.c_str(),
              result.iteration_number, result.frame_index, batch_size);
    }
  }

  // Mark as done processing
  processing_frame_ = false;

  return true;
}

/**
 * @brief Get matching original frames for batch processing
 */
bool AppPostProcess::get_matching_frames_batch(int64_t iteration_number,
                                               int start_frame_index,
                                               uint32_t batch_size,
                                               std::vector<InputFrame>& output_frames) {
  std::lock_guard<std::mutex> lock(buffer_mutex_);

  output_frames.clear();
  output_frames.reserve(batch_size);

  // Try to retrieve all frames in the batch
  for (uint32_t i = 0; i < batch_size; ++i) {
    auto key = std::make_pair(iteration_number, start_frame_index + i);
    auto it = frame_buffer_.find(key);

    if (it != frame_buffer_.end()) {
      output_frames.push_back(it->second);
      frame_buffer_.erase(it);
    } else {
      APP_LOG(AppLogLevel::WARNING, config_.log_level,
              "[%s] Missing original frame for batch idx %u: iter=%ld, frame=%d", inst_name_.c_str(), i,
              iteration_number, start_frame_index + i);
      // Clear partial results and return false
      output_frames.clear();
      return false;
    }
  }

  APP_LOG(AppLogLevel::DEBUG, config_.log_level,
          "[%s] Retrieved %u original frames for batch: iter=%ld, start_frame=%d, buffer_size=%zu", inst_name_.c_str(),
          batch_size, iteration_number, start_frame_index, frame_buffer_.size());

  return true;
}

/**
 * @brief Run MetaConvert and Overlay pipeline on single frame
 */
void AppPostProcess::run_metaconvert_overlay(const InferResultList& frame_results,
                                             const InputFrame& original_frame,
                                             int frame_index,
                                             int64_t iteration_number) {
  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Running MetaConvert/Overlay for frame %d (iteration %ld)",
          inst_name_.c_str(), frame_index, iteration_number);

  // Get the first VideoFrame from the InputFrame structure to extract dimensions
  // InputFrame structure: video_frame[batch_idx][frame_idx]
  if (original_frame.video_frame.empty() || original_frame.video_frame[0].empty()) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Original frame has no video data", inst_name_.c_str());
    return;
  }

  const auto& first_vframe = original_frame.video_frame[0][0];
  if (!first_vframe) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Original frame VideoFrame is null", inst_name_.c_str());
    return;
  }

  // Get VideoInfo to extract dimensions
  const vart::VideoInfo& vinfo = first_vframe->get_video_info();

  // Create root result to hold all detections/classifications for this frame
  auto root_result = make_shared<vart::InferResult>(vart::InferResultType::ROOT);
  root_result->add_children(frame_results);

  // Calculate scale factors for transforming results to original resolution
  InferResScaleInfo scale_info;
  scale_info.input_frame_width = vinfo.width;
  scale_info.input_frame_height = vinfo.height;
  scale_info.model_input_width = config_.model_input_width;
  scale_info.model_input_height = config_.model_input_height;

  // Transform each result to original resolution
  try {
    for (auto& result : frame_results) {
      result->transform(scale_info);
    }
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Exception in transform: %s", inst_name_.c_str(), e.what());
    return;
  }

  // Generate overlay metadata and draw
  try {
    // Generate overlay metadata
    auto overlay_shape = vart_metaconvert_->prepare_overlay_meta(std::move(root_result));

    // Use the existing VideoFrame from InputFrame (no need to create new one)
    // Draw overlay directly on the original frame
    vart_overlay_->draw_overlay(*first_vframe, *overlay_shape);

    // Save overlaid frame
    if (!config_.is_benchmark_enabled) {
      save_overlay_frame(first_vframe, frame_index, iteration_number);
    }
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Exception in overlay generation/drawing: %s",
            inst_name_.c_str(), e.what());
  }

  // Save results to text file
  save_text_results({frame_results}, frame_index);
}

/**
 * @brief Store original frame in buffer for later matching
 */
void AppPostProcess::store_original_frame(const InputFrame& frame) {
  std::unique_lock<std::mutex> lock(buffer_mutex_);

  // Split batch into individual frames and store each with its unique index
  uint32_t batch_size = static_cast<uint32_t>(frame.video_frame.size());

  for (uint32_t batch_idx = 0; batch_idx < batch_size; ++batch_idx) {
    // Throttling: Block if buffer is full (backpressure)
    while (frame_buffer_.size() >= MAX_FRAME_BUFFER_STORAGE_SIZE) {
      APP_LOG(AppLogLevel::WARNING, config_.log_level, "[%s] Frame buffer full (%zu frames), applying backpressure",
              inst_name_.c_str(), frame_buffer_.size());

      // This will block the queue pop, propagating backpressure upstream
      lock.unlock();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      lock.lock();
    }

    // Create individual frame entry
    InputFrame single_frame;
    single_frame.iteration_number = frame.iteration_number;
    single_frame.frame_index = frame.frame_index + batch_idx;          // Unique index for each frame
    single_frame.video_frame.push_back(frame.video_frame[batch_idx]);  // Extract single frame from batch

    // Store with unique key
    auto key = std::make_pair(single_frame.iteration_number, single_frame.frame_index);
    frame_buffer_[key] = single_frame;

    APP_LOG(AppLogLevel::DEBUG, config_.log_level,
            "[%s] Stored original frame[%u/%u]: iter=%ld, frame=%d, buffer_size=%zu", inst_name_.c_str(), batch_idx + 1,
            batch_size, single_frame.iteration_number, single_frame.frame_index, frame_buffer_.size());
  }

  // Monitor buffer usage
  if (frame_buffer_.size() > MAX_FRAME_BUFFER_STORAGE_SIZE * 0.8) {
    APP_LOG(AppLogLevel::WARNING, config_.log_level, "[%s] Frame buffer approaching limit: %zu/%zu frames",
            inst_name_.c_str(), frame_buffer_.size(), MAX_FRAME_BUFFER_STORAGE_SIZE);
  }
}

/**
 * @brief Convert VideoFrame to cv::Mat for saving
 */
cv::Mat AppPostProcess::video_frame_to_mat(shared_ptr<vart::VideoFrame> video_frame) {
  cv::Mat mat;

  // Get VideoInfo to extract format and dimensions
  const vart::VideoInfo& vinfo = video_frame->get_video_info();

  // Map the video frame to access data
  const vart::VideoFrameMapInfo* map_info = nullptr;
  try {
    map_info = &video_frame->map(vart::DataMapFlags::READ);
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Failed to map VideoFrame: %s", inst_name_.c_str(), e.what());
    return mat;
  }

  // Handle different video formats
  switch (vinfo.fmt) {
    case vart::VideoFormat::BGR: {
      mat = cv::Mat(vinfo.height, vinfo.width, CV_8UC3, map_info->planes[0].data, map_info->planes[0].stride).clone();
    } break;
    case vart::VideoFormat::RGB: {
      cv::Mat rgb_mat(vinfo.height, vinfo.width, CV_8UC3, map_info->planes[0].data, map_info->planes[0].stride);
      cv::cvtColor(rgb_mat, mat, cv::COLOR_RGB2BGR);
    } break;
    case vart::VideoFormat::Y_UV8_420:  // NV12
    {
      // For NV12, we need to handle Y and UV planes separately
      cv::Mat y_plane(vinfo.height, vinfo.width, CV_8UC1, map_info->planes[0].data, map_info->planes[0].stride);
      cv::Mat uv_plane(vinfo.height / 2, vinfo.width, CV_8UC1, map_info->planes[1].data, map_info->planes[1].stride);

      // Combine planes and convert to BGR
      cv::Mat yuv;
      cv::vconcat(y_plane, uv_plane, yuv);
      cv::cvtColor(yuv, mat, cv::COLOR_YUV2BGR_NV12);
    } break;
    default:
      APP_LOG(AppLogLevel::WARNING, config_.log_level, "[%s] Unsupported video format for conversion: %d",
              inst_name_.c_str(), static_cast<int>(vinfo.fmt));
      break;
  }

  // Unmap the video frame
  video_frame->unmap();

  return mat;
}

/**
 * @brief Save text results to file
 */
bool AppPostProcess::save_text_results(const BatchedInferResults& infer_results, int frame_index) {
  // Skip file I/O in benchmark mode
  if (config_.is_benchmark_enabled) {
    return true;
  }

  if (!result_file_.is_open()) {
    APP_LOG(AppLogLevel::WARNING, config_.log_level, "[%s] Result file not open, skipping save", inst_name_.c_str());
    return false;
  }

  try {
    result_file_ << "Frame " << frame_index << ":\n";
    APP_LOG(AppLogLevel::RESULT, config_.log_level, "Model %u - Frame %d:", config_.instance_id, frame_index);
    for (const auto& frame_results : infer_results) {
      for (const auto& result : frame_results) {
        InferResultData* base_result = result->get_infer_result();

        switch (base_result->result_type) {
          case vart::InferResultType::CLASSIFICATION: {
            auto* class_result = static_cast<ClassificationResData*>(base_result);
            for (size_t i = 0; i < class_result->label.size(); i++) {
              result_file_ << "  Classification: " << class_result->label[i] << " (confidence: " << fixed
                           << setprecision(4) << class_result->confidence[i] << ")\n";
              APP_LOG(AppLogLevel::RESULT, config_.log_level, "  Classification Label : %s (confidence %lf)",
                      class_result->label[i].c_str(), class_result->confidence[i]);
            }
            break;
          }
          case vart::InferResultType::DETECTION: {
            auto* det_result = static_cast<DetectionResData*>(base_result);
            result_file_ << "  Detection: " << det_result->label << " bbox[" << det_result->x << "," << det_result->y
                         << "," << det_result->width << "," << det_result->height << "] (confidence: " << fixed
                         << setprecision(4) << det_result->confidence << ")\n";
            APP_LOG(AppLogLevel::RESULT, config_.log_level,
                    "  Detection bbox  x : %u y : %u width  : %u height : %u and "
                    "label : %s (confidence %lf)",
                    det_result->x, det_result->y, det_result->width, det_result->height, det_result->label.c_str(),
                    det_result->confidence);
            break;
          }
          case vart::InferResultType::SEGMENTATION: {
            auto* seg_result = static_cast<SegmentationResData*>(base_result);
            for (size_t i = 0; i < seg_result->numOutputs; i++) {
              result_file_ << "  Segmentation: " << seg_result->width[i] << "x" << seg_result->height[i] << "\n";
              APP_LOG(AppLogLevel::RESULT, config_.log_level, "Segmentation output width : %u height : %u",
                      seg_result->width[i], seg_result->height[i]);
            }
            break;
          }
          default:
            // Handle unknown or unsupported result types
            APP_LOG(AppLogLevel::WARNING, config_.log_level, "[%s] Unsupported inference result type: %d",
                    inst_name_.c_str(), static_cast<int>(base_result->result_type));
            result_file_ << "  Unsupported result type\n";
            break;
        }
      }
    }
    result_file_ << "---\n" << flush;

    return true;
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Exception saving text results: %s", inst_name_.c_str(),
            e.what());
    return false;
  }
}

/**
 * @brief Save overlaid frame to file - format follows input format
 * JPG input -> JPG output (single frame)
 * NV12 input -> NV12 output (multiple frames)
 * BGR input -> BGR output (multiple frames)
 */
bool AppPostProcess::save_overlay_frame(const shared_ptr<vart::VideoFrame>& video_frame,
                                        int frame_index,
                                        int64_t iteration_number) {
  // Skip file I/O in benchmark mode
  if (config_.is_benchmark_enabled) {
    return true;
  }

  try {
    // Get VideoInfo to extract format and dimensions
    const vart::VideoInfo& vinfo = video_frame->get_video_info();

    // Determine output format based on input format
    // is_jpg_input_ indicates JPG input (single frame processing)
    // Y_UV8_420 (NV12) format indicates NV12 input (multiple frames processing)
    // BGR format indicates BGR input (multiple frames processing)

    if (config_.is_jpg_input) {
      // JPG input -> save as JPG
      return save_overlay_frame_as_jpg(video_frame, iteration_number);
    } else if (vinfo.fmt == vart::VideoFormat::Y_UV8_420 || vinfo.fmt == vart::VideoFormat::BGR) {
      // NV12 input -> save as NV12
      // BGR input -> save as BGR
      return save_overlay_frame_as_raw(video_frame, frame_index, iteration_number);
    } else {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Unsupported video format for saving: %d", inst_name_.c_str(),
              static_cast<int>(vinfo.fmt));
      return false;
    }

  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Exception saving overlay frame: %s", inst_name_.c_str(),
            e.what());
    return false;
  }
}

/**
 * @brief Save overlaid frame as JPG file (single frame processing)
 */
bool AppPostProcess::save_overlay_frame_as_jpg(const shared_ptr<vart::VideoFrame>& video_frame,
                                               int64_t iteration_number) {
  try {
    // Convert VideoFrame to BGR Mat using consolidated conversion function
    cv::Mat bgr_frame = video_frame_to_mat(video_frame);
    if (bgr_frame.empty()) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Failed to convert VideoFrame to Mat", inst_name_.c_str());
      return false;
    }

    // Generate JPG filename
    string jpg_filename;
    if (config_.max_iterations > 1) {
      jpg_filename = config_.output_dir_path + "/iter" + to_string(iteration_number) + "_postproc" +
                     to_string(config_.instance_id) + "_overlay.jpg";
    } else {
      jpg_filename = config_.output_dir_path + "/postproc" + to_string(config_.instance_id) + "_overlay.jpg";
    }

    // Save as JPG using OpenCV
    if (!cv::imwrite(jpg_filename, bgr_frame)) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Failed to write JPG file: %s", inst_name_.c_str(),
              jpg_filename.c_str());
      return false;
    }

    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Saved frame as JPG: %s", inst_name_.c_str(),
            jpg_filename.c_str());

    return true;

  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Exception saving overlay frame as JPG: %s", inst_name_.c_str(),
            e.what());
    return false;
  }
}

/**
 * @brief Open raw video output file (on-demand, handles iteration switching)
 * @param iteration_number Current iteration number
 * @param format Video format for output
 */
bool AppPostProcess::open_raw_output_file(int64_t iteration_number, vart::VideoFormat format) {
  // Check if file is already open for the current iteration
  if (raw_output_file_.is_open() && (config_.max_iterations == 1 || iteration_number == current_iteration_)) {
    // File is already open for this iteration, nothing to do
    return true;
  }

  std::string format_extension;

  // Determine file extension based on format
  if (format == vart::VideoFormat::Y_UV8_420) {
    format_extension = "nv12";
  } else if (format == vart::VideoFormat::BGR) {
    format_extension = "bgr";
  } else {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Unsupported format for raw output file: %d",
            inst_name_.c_str(), static_cast<int>(format));
    return false;
  }

  // Close current file if switching iterations
  if (raw_output_file_.is_open()) {
    raw_output_file_.close();
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Closed %s file for iteration %ld", inst_name_.c_str(),
            format_extension.c_str(), current_iteration_);
  }

  // Generate filename based on iteration mode
  string raw_filename;
  if (config_.max_iterations > 1) {
    raw_filename = config_.output_dir_path + "/iter" + to_string(iteration_number) + "_postproc" +
                   to_string(config_.instance_id) + "_overlay." + format_extension;
  } else {
    raw_filename =
        config_.output_dir_path + "/postproc" + to_string(config_.instance_id) + "_overlay." + format_extension;
  }

  // Open the file
  raw_output_file_.open(raw_filename, ios::out | ios::binary | ios::trunc);
  if (!raw_output_file_.is_open()) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Failed to open %s output file: %s", inst_name_.c_str(),
            format_extension.c_str(), raw_filename.c_str());
    return false;
  }

  current_iteration_ = iteration_number;
  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] Opened %s output file: %s", inst_name_.c_str(),
          format_extension.c_str(), raw_filename.c_str());

  return true;
}

/**
 * @brief Save overlaid frame as raw video (multiple frames processing)
 */
bool AppPostProcess::save_overlay_frame_as_raw(const shared_ptr<vart::VideoFrame>& video_frame,
                                               int frame_index,
                                               int64_t iteration_number) {
  if (!video_frame) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Null VideoFrame provided for raw save", inst_name_.c_str());
    return false;
  }

  vart::VideoInfo vinfo = video_frame->get_video_info();
  vart::VideoFormat fmt = vinfo.fmt;

  // Ensure raw output file is open for this iteration
  if (!open_raw_output_file(iteration_number, fmt)) {
    return false;
  }

  try {
    // Get VideoInfo to extract format and dimensions
    const vart::VideoInfo& vinfo = video_frame->get_video_info();

    // Map the video frame to access data
    const vart::VideoFrameMapInfo* map_info = nullptr;
    try {
      map_info = &video_frame->map(vart::DataMapFlags::READ);
    } catch (const exception& e) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Failed to map VideoFrame: %s", inst_name_.c_str(), e.what());
      return false;
    }

    if (vinfo.fmt == vart::VideoFormat::Y_UV8_420) {
      // NV12 format - write Y plane followed by UV plane
      // Must write row-by-row to handle images where stride differs from width
      const uint8_t* y_data = map_info->planes[0].data;
      const uint8_t* uv_data = map_info->planes[1].data;
      size_t y_stride = map_info->planes[0].stride;
      size_t uv_stride = map_info->planes[1].stride;
      int32_t height = vinfo.height;
      int32_t width = vinfo.width;

      // Write Y plane row-by-row
      for (int32_t row = 0; row < height; ++row) {
        raw_output_file_.write(reinterpret_cast<const char*>(y_data + row * y_stride), width);
      }

      // Write UV plane row-by-row (height/2 rows)
      for (int32_t row = 0; row < height / 2; ++row) {
        raw_output_file_.write(reinterpret_cast<const char*>(uv_data + row * uv_stride), width);
      }

      APP_LOG(AppLogLevel::DEBUG, config_.log_level,
              "[%s] Wrote NV12 frame %d (Y stride: %zu, UV stride: %zu, width: %d, height: %d)", inst_name_.c_str(),
              frame_index, y_stride, uv_stride, width, height);

      // Unmap the video frame
      video_frame->unmap();

      // Flush to ensure data is written
      raw_output_file_.flush();

      return true;
    } else if (vinfo.fmt == vart::VideoFormat::BGR) {
      // BGR format

      size_t width = vinfo.width;
      size_t height = vinfo.height;

      for (size_t row = 0; row < height; ++row) {
        const uint8_t* row_ptr = map_info->planes[0].data + row * map_info->planes[0].stride;
        raw_output_file_.write(reinterpret_cast<const char*>(row_ptr), width * 3);  // 3 bytes per pixel for BGR
      }
      APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Wrote BGR frame %d directly (size: %zu bytes)",
              inst_name_.c_str(), frame_index, width * height * 3);

      // Unmap the video frame
      video_frame->unmap();

      // Flush to ensure data is written
      raw_output_file_.flush();

      return true;
    } else {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Unexpected format for raw video save: %d",
              inst_name_.c_str(), static_cast<int>(vinfo.fmt));
      video_frame->unmap();
      return false;
    }

  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Exception saving overlay frame as raw video: %s",
            inst_name_.c_str(), e.what());
    return false;
  }
}

/**
 * @brief Open output files for results
 */
bool AppPostProcess::open_output_files() {
  // Generate filename following inference naming convention
  string result_filename;
  if (config_.max_iterations > 1) {
    // Multiple iterations - results will be appended across iterations
    // Use iteration-agnostic name since we append all iterations to one file
    result_filename = config_.output_dir_path + "/postproc" + to_string(config_.instance_id) + "_results.txt";
  } else {
    // Single iteration - use simpler format
    result_filename = config_.output_dir_path + "/postproc" + to_string(config_.instance_id) + "_results.txt";
  }

  result_file_.open(result_filename, ios::out | ios::trunc);
  if (!result_file_.is_open()) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Failed to open result file: %s", inst_name_.c_str(),
            result_filename.c_str());
    return false;
  }

  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] Opened result file: %s", inst_name_.c_str(),
          result_filename.c_str());

  // Note: Overlayed output files are not opened here, they are opened on-demand during saving

  return true;
}

/**
 * @brief Close output files
 */
void AppPostProcess::close_output_files() {
  if (result_file_.is_open()) {
    result_file_.close();
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Closed result file", inst_name_.c_str());
  }

  if (raw_output_file_.is_open()) {
    raw_output_file_.close();
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Closed raw video output file", inst_name_.c_str());
  }
}

/* Extract component-specific JSON configuration */
static string extract_json_component(const string& json_string, const string& component) {
  try {
    pt::ptree config;
    istringstream iss(json_string);
    pt::read_json(iss, config);
    pt::ptree specificConfig = config.get_child(component);
    ostringstream oss;
    pt::write_json(oss, specificConfig);
    return oss.str();
  } catch (const exception& e) {
    cerr << "Error parsing " << component << " config: " << e.what() << endl;
    return "{}";
  }
}

/**
 * @brief Create vart::PostProcess instance
 */
bool AppPostProcess::create_vart_postprocess() {
  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] Creating vart::PostProcess", inst_name_.c_str());

  // Extract JSON configuration
  string postproc_json_config = extract_json_component(config_.postprocess_json, "postprocess-config");
  if (postproc_json_config.empty()) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to parse postprocess-config");
    return false;
  }

  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "PostProcess Config: %s", postproc_json_config.c_str());

  // Create shared_ptr for device once and store as member variable
  device_ptr_ = DevicePtr(config_.device, [](vart::Device*) {});

  // Parse user-provided quantization scale factors (optional)
  std::vector<float> quant_scale_factors = {};
  bool use_user_provided_scale_factors = false;

  try {
    pt::ptree config;
    istringstream iss(postproc_json_config);
    pt::read_json(iss, config);

    if (config.get_child_optional("quant-scale-factors")) {
      for (const auto& value : config.get_child("quant-scale-factors")) {
        quant_scale_factors.push_back(value.second.get_value<float>());
      }

      if (quant_scale_factors.empty()) {
        APP_LOG(AppLogLevel::INFO, config_.log_level,
                "User provided empty postprocess scale factors, using inference scale factors");
      } else if (quant_scale_factors.size() != config_.model_num_out_tensors) {
        APP_LOG(AppLogLevel::ERROR, config_.log_level,
                "User provided scale factors size (%zu) does not match output tensors (%zu)",
                quant_scale_factors.size(), config_.model_num_out_tensors);
        return false;
      } else {
        use_user_provided_scale_factors = true;
        APP_LOG(AppLogLevel::INFO, config_.log_level, "Using user-provided quantization scale factors");
      }
    } else {
      APP_LOG(AppLogLevel::INFO, config_.log_level, "No user-provided scale factors, using inference scale factors");
    }
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::WARNING, config_.log_level, "Failed to parse quant-scale-factors: %s", e.what());
  }

  // Create vart::PostProcess instance using modern C++ best practices
  try {
    vart_postprocess_ =
        std::make_unique<vart::PostProcess>(config_.postprocess_type, postproc_json_config, device_ptr_);
    if (!vart_postprocess_) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to create vart::PostProcess instance");
      return false;
    }
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Exception creating vart::PostProcess: %s", e.what());
    return false;
  }

  // Prepare TensorInfo for PostProcess configuration
  std::vector<TensorInfo> tensor_info;
  for (size_t i = 0; i < config_.model_num_input_tensors; ++i) {
    TensorInfo tinfo = {};
    tinfo.direction = TensorDataDirection::INPUT;
    tinfo.name = config_.model_input_tensors_info[i].meta.name;
    tinfo.size = config_.model_input_tensors_info[i].meta.size_in_bytes;
    tinfo.shape = config_.model_input_tensors_info[i].meta.shape;
    tinfo.scale_coeff = config_.model_input_tensors_info[i].quantization_factor;

    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "Input Tensor%ld name: %s", i, tinfo.name.c_str());
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "Input Tensor%ld size: %u", i, tinfo.size);
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "Input Tensor%ld scale coeff: %f", i, tinfo.scale_coeff);
    string tensor_shape = build_shape_string(config_.model_input_tensors_info[i].meta.shape);
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "Input Tensor%ld shape: %s", i, tensor_shape.c_str());

    // Map vart::DataType to TensorDataType
    switch (config_.model_input_tensors_info[i].meta.data_type) {
      case vart::DataType::INT8:
        tinfo.data_type = TensorDataType::INT8;
        break;
      case vart::DataType::BF16:
        tinfo.data_type = TensorDataType::BF16;
        break;
      case vart::DataType::FP16:
        tinfo.data_type = TensorDataType::FP16;
        break;
      case vart::DataType::FLOAT32:
        tinfo.data_type = TensorDataType::FLOAT32;
        break;
      default:
        APP_LOG(AppLogLevel::ERROR, config_.log_level, "Unsupported input tensor data type for PostProcess");
        return false;
    }

    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "Tensor[%zu]: name=%s, size=%u, scale=%f", i, tinfo.name.c_str(),
            tinfo.size, tinfo.scale_coeff);

    tensor_info.emplace_back(std::move(tinfo));
  }

  for (size_t i = 0; i < config_.model_num_out_tensors; ++i) {
    TensorInfo tinfo = {};
    tinfo.direction = TensorDataDirection::OUTPUT;
    tinfo.name = config_.model_out_tensors_info[i].meta.name;
    tinfo.size = config_.model_out_tensors_info[i].meta.size_in_bytes;
    tinfo.shape = config_.model_out_tensors_info[i].meta.shape;
    tinfo.scale_coeff = use_user_provided_scale_factors ? quant_scale_factors[i]
                                                        : config_.model_out_tensors_info[i].quantization_factor;

    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "Output Tensor%ld name: %s", i, tinfo.name.c_str());
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "Output Tensor%ld size: %u", i, tinfo.size);
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "Output Tensor%ld scale coeff: %f", i, tinfo.scale_coeff);
    string tensor_shape = build_shape_string(config_.model_out_tensors_info[i].meta.shape);
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "Output Tensor%ld shape: %s", i, tensor_shape.c_str());

    // Map vart::DataType to TensorDataType
    switch (config_.model_out_tensors_info[i].meta.data_type) {
      case vart::DataType::INT8:
        tinfo.data_type = TensorDataType::INT8;
        break;
      case vart::DataType::BF16:
        tinfo.data_type = TensorDataType::BF16;
        break;
      case vart::DataType::FP16:
        tinfo.data_type = TensorDataType::FP16;
        break;
      case vart::DataType::FLOAT32:
        tinfo.data_type = TensorDataType::FLOAT32;
        break;
      default:
        APP_LOG(AppLogLevel::ERROR, config_.log_level, "Unsupported output tensor data type for PostProcess");
        return false;
    }

    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "Tensor[%zu]: name=%s, size=%u, scale=%f", i, tinfo.name.c_str(),
            tinfo.size, tinfo.scale_coeff);

    tensor_info.emplace_back(std::move(tinfo));
  }

  // Configure PostProcess with tensor info
  try {
    vart_postprocess_->set_config(tensor_info, config_.model_batch_size);
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "PostProcess configured with %zu tensors, batch_size=%u",
            tensor_info.size(), config_.model_batch_size);
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Exception configuring vart::PostProcess: %s", e.what());
    return false;
  }

  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] vart::PostProcess created successfully", inst_name_.c_str());
  return true;
}

const char* ppNameToString(vart::PostProcessType type) {
  switch (type) {
    case vart::PostProcessType::RESNET50:
      return "RESNET50";
    case vart::PostProcessType::YOLOV2:
      return "YOLOV2";
    case vart::PostProcessType::SSDRESNET34:
      return "SSDRESNET34";
    case vart::PostProcessType::SOFTMAX:
      return "SOFTMAX";
    case vart::PostProcessType::TOPK:
      return "TOPK";
    case vart::PostProcessType::ARGMAX:
      return "ARGMAX";
    case vart::PostProcessType::THRESHOLD:
      return "THRESHOLD";
    case vart::PostProcessType::LABEL_MAPPING:
      return "LABEL_MAPPING";
    case vart::PostProcessType::NORMALIZATION:
      return "NORMALIZATION";
    case vart::PostProcessType::CALIBRATION_TEMPERATURE:
      return "CALIBRATION_TEMPERATURE";
    case vart::PostProcessType::CALIBRATION_PLATT:
      return "CALIBRATION_PLATT";
    case vart::PostProcessType::BIAS_CORRECTION:
      return "BIAS_CORRECTION";
    case vart::PostProcessType::OUTLIER_DETECTION:
      return "OUTLIER_DETECTION";
    case vart::PostProcessType::UNCERTAINTY_ESTIMATION:
      return "UNCERTAINTY_ESTIMATION";
    case vart::PostProcessType::NMS:
      return "NMS";
    case vart::PostProcessType::CLASSWISE_NMS:
      return "CLASSWISE_NMS";
    case vart::PostProcessType::SOFT_NMS:
      return "SOFT_NMS";
    case vart::PostProcessType::DISTANCE_IOU_NMS:
      return "DISTANCE_IOU_NMS";
    case vart::PostProcessType::ANCHOR_ADJUSTMENT:
      return "ANCHOR_ADJUSTMENT";
    case vart::PostProcessType::OBJECT_COUNT:
      return "OBJECT_COUNT";
    case vart::PostProcessType::SOFTMAXSEG:
      return "SOFTMAXSEG";
    case vart::PostProcessType::SIGMOIDSEG:
      return "SIGMOIDSEG";
    case vart::PostProcessType::ARGMAXSEG:
      return "ARGMAXSEG";
    default:
      return "UNKNOWN";
  }
}

/**
 * @brief Create vart::MetaConvert instance
 */
bool AppPostProcess::create_vart_metaconvert() {
  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] Creating vart::MetaConvert", inst_name_.c_str());

  // Extract JSON configuration
  string metaconvert_json_config = extract_json_component(config_.metaconvert_json, "metaconvert-config");
  if (metaconvert_json_config.empty()) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to parse metaconvert-config");
    return false;
  }

  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "MetaConvert Config: %s", metaconvert_json_config.c_str());

  // Determine InferResultType based on PostProcessType
  vart::InferResultType infer_result_type = {};
  switch (config_.postprocess_type) {
    case vart::PostProcessType::RESNET50:
    case vart::PostProcessType::ARGMAX:
    case vart::PostProcessType::BIAS_CORRECTION:
    case vart::PostProcessType::CALIBRATION_PLATT:
    case vart::PostProcessType::CALIBRATION_TEMPERATURE:
    case vart::PostProcessType::LABEL_MAPPING:
    case vart::PostProcessType::NORMALIZATION:
    case vart::PostProcessType::OUTLIER_DETECTION:
    case vart::PostProcessType::SOFTMAX:
    case vart::PostProcessType::THRESHOLD:
    case vart::PostProcessType::TOPK:
    case vart::PostProcessType::UNCERTAINTY_ESTIMATION:
      infer_result_type = vart::InferResultType::CLASSIFICATION;
      APP_LOG(AppLogLevel::RESULT, config_.log_level, "Model %u - Post Process : Classification (%s)",
              config_.instance_id, ppNameToString(config_.postprocess_type));
      break;
    case vart::PostProcessType::YOLOV2:
    case vart::PostProcessType::SSDRESNET34:
    case vart::PostProcessType::NMS:
    case vart::PostProcessType::CLASSWISE_NMS:
    case vart::PostProcessType::SOFT_NMS:
    case vart::PostProcessType::DISTANCE_IOU_NMS:
    case vart::PostProcessType::ANCHOR_ADJUSTMENT:
    case vart::PostProcessType::OBJECT_COUNT:
      infer_result_type = vart::InferResultType::DETECTION;
      APP_LOG(AppLogLevel::RESULT, config_.log_level, "Model %u - Post Process : Detection (%s)", config_.instance_id,
              ppNameToString(config_.postprocess_type));
      break;
    case vart::PostProcessType::SOFTMAXSEG:
    case vart::PostProcessType::SIGMOIDSEG:
    case vart::PostProcessType::ARGMAXSEG:
      infer_result_type = vart::InferResultType::SEGMENTATION;
      APP_LOG(AppLogLevel::RESULT, config_.log_level, "Model %u - Post Process : Segmentation (%s)",
              config_.instance_id, ppNameToString(config_.postprocess_type));
      break;
    default:
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Model %u - Unsupported PostProcessType for MetaConvert",
              config_.instance_id);
      return false;
  }

  // Create vart::MetaConvert instance using modern C++ best practices
  try {
    vart_metaconvert_ = std::make_unique<vart::MetaConvert>(infer_result_type, metaconvert_json_config, device_ptr_);
    if (!vart_metaconvert_) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to create vart::MetaConvert instance");
      return false;
    }
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Exception creating vart::MetaConvert: %s", e.what());
    return false;
  }

  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] vart::MetaConvert created successfully", inst_name_.c_str());
  return true;
}

/**
 * @brief Create vart::Overlay instance
 */
bool AppPostProcess::create_vart_overlay() {
  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] Creating vart::Overlay", inst_name_.c_str());

  // Create vart::Overlay instance with default type using modern C++ best practices
  try {
    vart_overlay_ = std::make_unique<vart::Overlay>(DEFAULT_OVERLAY_TYPE, device_ptr_);
    if (!vart_overlay_) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to create vart::Overlay instance");
      return false;
    }
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Exception creating vart::Overlay: %s", e.what());
    return false;
  }

  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] vart::Overlay created successfully", inst_name_.c_str());
  return true;
}
