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
 * @file file_reader.cpp
 * @brief Implementation of AppFileReader component
 */

#include <algorithm>
#include <filesystem>
#include "common/app_utils.hpp"
#include "file_reader.hpp"

using namespace vart;

namespace fs = std::filesystem;
/* XRT based buffer allocation */
#define DEFAULT_FILE_READER_POOL_TYPE vart::VideoFrameImplType::XRT

/* Return the size of frame required for VideoFormat */
static size_t get_video_frame_size(vart::VideoFormat fmt, size_t width, size_t height) {
  size_t size;

  switch (fmt) {
    case vart::VideoFormat::BGR:
    case vart::VideoFormat::RGB:
      size = (width * height) * 3;
      break;
    case vart::VideoFormat::BGR_FLOAT:
      size = (width * height) * 3 * 4;
      break;
    case vart::VideoFormat::Y_UV8_420:
      size = static_cast<size_t>(static_cast<double>(width * height) * 1.5);
      break;
    case vart::VideoFormat::RGBx_BF16:
    case vart::VideoFormat::BGRx_BF16:
    case vart::VideoFormat::RGBx_FP16:
    case vart::VideoFormat::BGRx_FP16:
      size = (width * height) * 4 * 2;
      break;
    case vart::VideoFormat::RGBP_BF16:
    case vart::VideoFormat::RGBP_FP16:
    case vart::VideoFormat::RGB_BF16:
    case vart::VideoFormat::RGB_FP16:
      size = (width * height) * 3 * 2;
      break;
    case vart::VideoFormat::RGBx:
    case vart::VideoFormat::BGRx:
      size = (width * height) * 4;
      break;
    default:
      size = 0;
      break;
  }
  return size;
}

/* Map AppVideoInputFormat to Vart VideoFormat
 * As JPEG return BGR output, the function return accordingly
 */
static vart::VideoFormat get_video_frame_format(AppVideoInputFormat app_fmt) {
  vart::VideoFormat fmt;
  switch (app_fmt) {
    /* For mp4 and jpeg, decoder is returning bgr for now */
    case APP_VIDEO_INPUT_FORMAT_JPEG:
    case APP_VIDEO_INPUT_FORMAT_BGR:
      fmt = vart::VideoFormat::BGR;
      break;
    case APP_VIDEO_INPUT_FORMAT_NV12:
      fmt = vart::VideoFormat::Y_UV8_420;
      break;
    case APP_VIDEO_INPUT_FORMAT_BINARY:
      fmt = vart::VideoFormat::RGBx_BF16;  // binary files allocated as max size for RGBx_BF16
      break;
    default:
      fmt = vart::VideoFormat::UNKNOWN;
      break;
  }
  return fmt;
}

AppFileReader::AppFileReader(const FileReaderConfig& config, void* output_queue, void* postprocess_queue)
    : config_(config),
      input_fmt_(APP_VIDEO_INPUT_FORMAT_UNKNOWN),
      dectected_input_width_(0),
      dectected_input_height_(0),
      jpeg_read_in_current_iteration_(false),
      output_pool_(nullptr),
      output_queue_(output_queue),
      postprocess_queue_(postprocess_queue),
      state_(ThreadState::IDLE),
      frames_read_(0),
      current_iteration_(0),
      error_occurred_(false),
      frame_index_(0),
      frames_read_in_iteration_(0),
      inst_name_("AppFileReader" + std::to_string(config.instance_id)) {
  if (config_.bypass_preprocessing) {
    APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] created for (inference-only mode): %zu input files",
            inst_name_.c_str(), config_.input_tensors_file_path.size());
  } else {
    APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] created for (preprocessing mode): file=%s", inst_name_.c_str(),
            config_.input_image_file_path.c_str());
  }
}

AppFileReader::~AppFileReader() {
  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "AppFileReader destructor called");

  // Stop thread
  stop();

  // Close file if open
  close_input_file();
}

bool AppFileReader::initialize() {
  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] Initializing...", inst_name_.c_str());

  // Validate configuration
  if (!config_.device) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Device pointer is null");
    return false;
  }

  if (!output_queue_) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Output queue is null");
    return false;
  }

  if (config_.bypass_preprocessing) {
    // DIRECT-TO-INFERENCE MODE
    // Validate binary file paths
    if (config_.input_tensors_file_path.empty()) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "No input file paths provided for inference-only mode");
      return false;
    }

    if (config_.num_input_tensors == 0 || config_.input_tensor_info.empty()) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "No tensor info provided for inference-only mode");
      return false;
    }

    // Verify all binary files exist
    for (const auto& file_path : config_.input_tensors_file_path) {
      if (!fs::exists(file_path)) {
        APP_LOG(AppLogLevel::ERROR, config_.log_level, "Binary input file does not exist: %s", file_path.c_str());
        return false;
      }
    }

    // Set default format to binary
    input_fmt_ = APP_VIDEO_INPUT_FORMAT_BINARY;

    // Set dimensions for logging
    dectected_input_width_ = config_.model_width;
    dectected_input_height_ = config_.model_height;

    // Create tensor pools with dummy VideoInfo
    vart::VideoInfo dummy_vinfo;
    memset(&dummy_vinfo, 0, sizeof(dummy_vinfo));

    if (!create_output_pool(dummy_vinfo)) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to create tensor pools for inference-only mode");
      return false;
    }

    APP_LOG(AppLogLevel::DEBUG, config_.log_level,
            "Initialized for inference-only mode: %zu binary files, %zu tensor pools created",
            config_.input_tensors_file_path.size(), tensor_pools_.size());

  } else {
    // PREPROCESSING MODE
    // Validate single image file path
    if (config_.input_image_file_path.empty()) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "File path is empty");
      return false;
    }

    // Extract input resolution from file
    if (!extract_input_resolution()) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to extract input resolution");
      return false;
    }

    // Check if VideoInfo provider lambda is available
    if (!config_.get_vinfo) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "No VideoInfo provider lambda for preprocessing mode");
      return false;
    }

    // Get VideoInfo from preprocess via lambda
    vart::VideoFormat fmt = get_input_vart_format();
    vart::VideoInfo vinfo = config_.get_vinfo(dectected_input_height_, dectected_input_width_, fmt);

    // Create pool with VideoInfo from preprocess
    if (!create_output_pool(vinfo)) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to create output pool for preprocessing mode");
      return false;
    }

    // Open input file
    if (!open_input_file()) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to open input file");
      return false;
    }

    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "Initialized for preprocessing mode with VideoInfo from lambda");
  }

  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "AppFileReader initialized successfully");
  return true;
}

bool AppFileReader::start() {
  if (is_running()) {
    APP_LOG(AppLogLevel::WARNING, config_.log_level, "[%s] already running", inst_name_.c_str());
    return true;
  }

  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] Starting...", inst_name_.c_str());

  // Check that appropriate pools are created based on mode
  if (config_.bypass_preprocessing) {
    if (tensor_pools_.empty()) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "AppFileReader tensor pools not created for inference-only mode");
      return false;
    }
  } else {
    if (!output_pool_) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "AppFileReader output pool not created for preprocessing mode");
      return false;
    }
  }

  try {
    state_ = ThreadState::RUNNING;
    worker_thread_ = std::make_unique<std::thread>(&AppFileReader::worker_thread_function, this);
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] started successfully", inst_name_.c_str());
    return true;
  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Failed to start worker thread: %s", inst_name_.c_str(),
            e.what());
    state_ = ThreadState::IDLE;
    return false;
  }
}

void AppFileReader::stop() {
  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] Stopping...", inst_name_.c_str());

  // Signal shutdown if thread is RUNNING or has finished (IDLE)
  ThreadState current_state = state_.load();
  if (current_state == ThreadState::RUNNING || current_state == ThreadState::IDLE) {
    // Signal shutdown (only meaningful if RUNNING)
    state_ = ThreadState::SHUTTING_DOWN;

    // Notify output queue to wake up any waiting threads
    if (output_queue_) {
      if (config_.bypass_preprocessing) {
        // Cast to PreprocessedFrame queue
        auto* queue = static_cast<AppQueue<PreprocessedFrame>*>(output_queue_);
        queue->finish();
      } else {
        // Cast to InputFrame queue
        auto* queue = static_cast<AppQueue<InputFrame>*>(output_queue_);
        queue->finish();
      }
    }
  }

  // Wait for thread to finish
  if (worker_thread_ && worker_thread_->joinable()) {
    worker_thread_->join();
  }
  state_ = ThreadState::IDLE;
  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] stopped", inst_name_.c_str());
}

bool AppFileReader::should_continue_processing() const {
  // Only check terminal condition: max iterations limit
  // Frame limits are iteration boundaries, not processing limits
  return current_iteration_ < config_.max_iterations;
}

bool AppFileReader::handle_iteration_transition() {
  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Iteration %u complete, transitioning to next iteration",
          inst_name_.c_str(), current_iteration_.load());

  current_iteration_++;           // move to next iteration
  frames_read_in_iteration_ = 0;  // Reset counter for new iteration

  // Check if we've completed all iterations
  if (current_iteration_ >= config_.max_iterations) {
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Completed all iterations, stopping file reader",
            inst_name_.c_str());
    return false;  // No more iterations
  }

  // Reset frame index for next iteration
  frame_index_ = 0;
  std::cout << inst_name_ << " ----Starting iteration " << current_iteration_.load() + 1 << "/"
            << config_.max_iterations << "----" << std::endl;
  return true;
}

void AppFileReader::update_read_counters(size_t batch_size, uint32_t num_submissions) {
  frames_read_ += batch_size;
  frame_index_ += batch_size;
  frames_read_in_iteration_ += batch_size;

  // Update shared frames submitted counter
  // Increment by batch_size to track actual frames
  if (config_.frames_submitted_ptr && num_submissions > 0) {
    config_.frames_submitted_ptr->fetch_add(batch_size);
    APP_LOG(AppLogLevel::DEBUG, config_.log_level,
            "[%s] Incremented frames submitted counter by %zu (now %d total submitted)", inst_name_.c_str(), batch_size,
            config_.frames_submitted_ptr->load());
  }
}

void AppFileReader::handle_queue_submission_failure() {
  APP_LOG(AppLogLevel::ERROR, config_.log_level,
          "[%s] Queue finished unexpectedly - unable to submit batch! Frame will be lost!", inst_name_.c_str());

  error_occurred_ = true;
  // Signal critical error to application
  if (config_.critical_error_ptr) {
    config_.critical_error_ptr->store(true);
  }
  // Stop processing immediately
  state_ = ThreadState::SHUTTING_DOWN;
}

void AppFileReader::worker_thread_function() {
  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] worker thread started (mode: %s)", inst_name_.c_str(),
          config_.bypass_preprocessing ? "inference-only" : "preprocess");

  if (config_.bypass_preprocessing) {
    // Read binary files and create PreprocessedFrame
    worker_thread_inference_input();
  } else {
    // Read JPEG/NV12/BGR and create InputFrame
    worker_thread_preprocess_input();
  }

  // Set state to indicate thread is no longer running
  state_ = ThreadState::IDLE;
  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] thread exiting, read %lu total frames", inst_name_.c_str(),
          frames_read_.load());
}

/**
 * @brief worker thread
 *          - reads JPEG/NV12/BGR and creates InputFrame
 */
void AppFileReader::worker_thread_preprocess_input() {
  while (state_.load() == ThreadState::RUNNING) {
    // Check if max iterations reached (terminal condition)
    if (!should_continue_processing()) {
      APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Max iterations reached, stopping", inst_name_.c_str());
      break;
    }

    // Check if frame limit reached for this iteration (iteration boundary)
    if (config_.num_frames_to_process > 0 && frames_read_in_iteration_ >= config_.num_frames_to_process) {
      if (!handle_iteration_transition()) {
        break;  // All iterations complete or error
      }
      // Rewind file for next iteration
      if (!rewind_input_file()) {
        APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to rewind file");
        error_occurred_ = true;
        break;
      }
      continue;  // Start next iteration
    }

    // Prepare batch with proper structure: batch_size elements, 1 frame per element
    InputFrame batch(config_.model_batch_size, 1, frame_index_);

    // Read frames into batch
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Preparing to read batch of %u frame(s) starting at frame %d",
            inst_name_.c_str(), config_.model_batch_size, frame_index_);
    AppReadStatus status = read_frame_batch(batch);
    switch (status) {
      case APP_READ_SUCCESS: {
        if (!submit_batch_to_queue(batch, false)) {
          // Queue rejected - this might indicate shutdown
          APP_LOG(AppLogLevel::WARNING, config_.log_level, "Failed to submit batch to queue, possible shutdown");
          continue;  // Don't increment counters, retry with same batch
        }
      } break;

      case APP_EOF:
        // Submit the partial batch if it has frames
        if (batch.size() > 0) {
          submit_batch_to_queue(batch, true);
        }

        APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] EOF reached", inst_name_.c_str());
        if (!handle_iteration_transition()) {
          // All iterations complete or error occurred
          break;  // Exit switch, while loop will terminate naturally
        }
        // Rewind file for next iteration
        if (!rewind_input_file()) {
          APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to rewind file");
          error_occurred_ = true;
          break;  // Exit switch, while loop will terminate naturally
        }
        break;

      case APP_READ_FAILED:
        APP_LOG(AppLogLevel::ERROR, config_.log_level, "File read error, signaling application exit");
        handle_queue_submission_failure();
        break;
    }  // switch
  }    // while

  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] preprocess worker thread completed, read %lu frames",
          inst_name_.c_str(), frames_read_.load());
}

/**
 * @brief worker thread
 *          - reads binary files and creates PreprocessedFrame
 *          - send to connected inference model input queue
 */
void AppFileReader::worker_thread_inference_input() {
  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s ]worker: Reading %zu binary input files", inst_name_.c_str(),
          config_.input_tensors_file_path.size());

  // Open all binary input files
  std::vector<std::ifstream> binary_files;
  binary_files.reserve(config_.input_tensors_file_path.size());

  for (size_t i = 0; i < config_.input_tensors_file_path.size(); i++) {
    std::ifstream file(config_.input_tensors_file_path[i], std::ios::binary);
    if (!file.is_open()) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to open binary file: %s",
              config_.input_tensors_file_path[i].c_str());
      error_occurred_ = true;
      return;
    }
    binary_files.push_back(std::move(file));
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Opened binary file %zu: %s", inst_name_.c_str(), i,
            config_.input_tensors_file_path[i].c_str());
  }

  while (state_.load() == ThreadState::RUNNING) {
    // Check if max iterations reached (terminal condition)
    if (!should_continue_processing()) {
      APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Max iterations reached, stopping", inst_name_.c_str());
      break;
    }

    // Check if frame limit reached for this iteration (iteration boundary)
    if (config_.num_frames_to_process > 0 && frames_read_in_iteration_ >= config_.num_frames_to_process) {
      if (!handle_iteration_transition()) {
        break;  // All iterations complete or error
      }
      // Need to rewind binary files for next iteration
      for (size_t i = 0; i < binary_files.size(); i++) {
        binary_files[i].clear();
        binary_files[i].seekg(0, std::ios::beg);
        if (!binary_files[i].good()) {
          APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to rewind binary file %zu", i);
          error_occurred_ = true;
          return;
        }
      }
      continue;  // Start next iteration
    }

    // Create PreprocessedFrame
    PreprocessedFrame preprocessed_frame;
    preprocessed_frame.preprocessed_frame.resize(config_.model_batch_size);

    bool eof_reached = false;

    APP_LOG(AppLogLevel::DEBUG, config_.log_level,
            "[%s] Preparing to read batch of %u frame(s) from binary files starting at frame %d", inst_name_.c_str(),
            config_.model_batch_size, frame_index_);
    // Read data for each batch element
    for (uint32_t b = 0; b < config_.model_batch_size; b++) {
      preprocessed_frame.preprocessed_frame[b].resize(config_.num_input_tensors);

      // Read data for each input tensor
      for (size_t t = 0; t < config_.num_input_tensors; t++) {
        if (t >= binary_files.size()) {
          APP_LOG(AppLogLevel::ERROR, config_.log_level, "Tensor index %zu exceeds number of binary files %zu", t,
                  binary_files.size());
          error_occurred_ = true;
          return;
        }

        if (t >= tensor_pools_.size()) {
          APP_LOG(AppLogLevel::ERROR, config_.log_level, "Tensor index %zu exceeds number of tensor pools %zu", t,
                  tensor_pools_.size());
          error_occurred_ = true;
          return;
        }

        // Get tensor size from config
        size_t tensor_size = config_.input_tensor_info[t].meta.size_in_bytes;
        // Check file state BEFORE acquiring buffer and mapping
        if (binary_files[t].eof()) {
          APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] EOF already reached on binary file %zu before read",
                  inst_name_.c_str(), t);
          eof_reached = true;
          break;
        }

        // Acquire VideoFrame from the TENSOR-SPECIFIC pool
        std::shared_ptr<vart::VideoFrame> vframe;
        try {
          vframe = tensor_pools_[t]->acquire_frame();
        } catch (const std::runtime_error& e) {
          /* The pool throws on acquire timeout or on pool shutdown; both
           * are fatal for this reader. */
          const bool shutting_down = std::string(e.what()).find("shutting down") != std::string::npos;
          APP_LOG(AppLogLevel::ERROR, config_.log_level,
                  "[%s] %s while acquiring buffer from tensor pool %zu for batch %u: %s", inst_name_.c_str(),
                  shutting_down ? "Pool shutdown" : "Timeout", t, b, e.what());
          error_occurred_ = true;
          return;
        }

        // Map memory for writing
        const vart::VideoFrameMapInfo* map_info = nullptr;
        try {
          map_info = &vframe->map(vart::DataMapFlags::WRITE);
        } catch (const std::exception& e) {
          APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to map memory: %s", e.what());
          error_occurred_ = true;
          return;
        }

        // Read binary data from file
        binary_files[t].read(reinterpret_cast<char*>(map_info->planes[0].data), tensor_size);
        size_t bytes_read = binary_files[t].gcount();

        vframe->unmap();

        // check read status
        if (bytes_read != tensor_size) {
          if (bytes_read == 0 && binary_files[t].eof()) {
            APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] EOF reached on binary file %zu at frame %d",
                    inst_name_.c_str(), t, frame_index_);
            eof_reached = true;
            break;
          } else {
            APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to read complete tensor data: expected %zu, got %zu",
                    tensor_size, bytes_read);
            error_occurred_ = true;
            return;
          }
        }
        preprocessed_frame.preprocessed_frame[b][t] = std::move(vframe);

        APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s ]Read %zu bytes for batch %u, tensor %zu from %s",
                inst_name_.c_str(), bytes_read, b, t, config_.input_tensors_file_path[t].c_str());
      }

      if (eof_reached) {
        // Check if we read a partial batch
        if (b > 0) {
          APP_LOG(AppLogLevel::INFO, config_.log_level,
                  "[%s] Partial batch read (%u/%u frames) at EOF, resizing and submitting for processing",
                  inst_name_.c_str(), b, config_.model_batch_size);
          preprocessed_frame.preprocessed_frame.resize(b);
        } else {
          // No frames read at all - resize to 0 to prevent submission
          APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] EOF reached with no frames in batch",
                  inst_name_.c_str());
          preprocessed_frame.preprocessed_frame.resize(0);
        }
        break;
      }
    }

    // Only submit if we actually read frames
    if (preprocessed_frame.preprocessed_frame.size() > 0) {
      // Set frame metadata
      preprocessed_frame.frame_index = frame_index_;
      preprocessed_frame.iteration_number = current_iteration_.load();

      // Push to single output queue (1:1 mapping)
      auto* queue = static_cast<AppQueue<PreprocessedFrame>*>(output_queue_);
      if (queue->push(preprocessed_frame, 0)) {
        // Update counters with actual batch size
        size_t actual_batch_size = preprocessed_frame.preprocessed_frame.size();
        update_read_counters(actual_batch_size, 1);  // actual frames in batch, 1 submission
        APP_LOG(AppLogLevel::DEBUG, config_.log_level,
                "[%s] Submitted PreprocessedFrame %d (batch_size=%zu) to inference queue, total read: %lu",
                inst_name_.c_str(), preprocessed_frame.frame_index, actual_batch_size, frames_read_.load());
      } else {
        handle_queue_submission_failure();
        break;
      }
    }

    if (eof_reached) {
      APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] EOF reached, iteration %u complete", inst_name_.c_str(),
              current_iteration_.load());

      if (!handle_iteration_transition()) {
        // All iterations complete or error occurred
        break;  // Exit while loop, function completes normally
      }
      // Rewind binary files for next iteration
      for (size_t i = 0; i < binary_files.size(); i++) {
        binary_files[i].clear();
        binary_files[i].seekg(0, std::ios::beg);
        if (!binary_files[i].good()) {
          APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to rewind binary file %zu", i);
          error_occurred_ = true;
          break;  // Exit for loop, then break from while loop below
        }
      }
      // Check if error occurred during rewind
      if (error_occurred_) {
        break;  // Exit while loop
      }
      continue;
    }
  }

  // Close all binary files
  for (auto& file : binary_files) {
    if (file.is_open()) {
      file.close();
    }
  }

  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] inference-only worker thread completed, read %lu frames",
          inst_name_.c_str(), frames_read_.load());
}

bool AppFileReader::open_input_file() {
  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] Opening input file: %s", inst_name_.c_str(),
          config_.input_image_file_path.c_str());

  // Check if file exists
  if (!fs::exists(config_.input_image_file_path)) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Input file does not exist: %s",
            config_.input_image_file_path.c_str());
    return false;
  }

  if (input_fmt_ == APP_VIDEO_INPUT_FORMAT_NV12 || input_fmt_ == APP_VIDEO_INPUT_FORMAT_BGR) {
    // Open binary file for NV12/BGR reading
    input_file_.open(config_.input_image_file_path, std::ios::binary | std::ios::in);
    if (!input_file_.is_open()) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to open file: %s", config_.input_image_file_path.c_str());
      return false;
    }
    APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s ]Opened %s file successfully", inst_name_.c_str(),
            input_fmt_ == APP_VIDEO_INPUT_FORMAT_NV12 ? "NV12" : "BGR");
  } else if (input_fmt_ == APP_VIDEO_INPUT_FORMAT_JPEG) {
    // For JPEG, we use cv::imread which doesn't need file handle
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] JPEG file will be read using OpenCV", inst_name_.c_str());
  }

  return true;
}

void AppFileReader::close_input_file() {
  if (input_file_.is_open()) {
    input_file_.close();
    APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] Input file closed", inst_name_.c_str());
  }
}

bool AppFileReader::rewind_input_file() {
  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Rewinding input file for next iteration", inst_name_.c_str());

  if (input_fmt_ == APP_VIDEO_INPUT_FORMAT_NV12 || input_fmt_ == APP_VIDEO_INPUT_FORMAT_BGR) {
    if (!input_file_.is_open()) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "File not open, cannot rewind");
      return false;
    }

    input_file_.clear();
    input_file_.seekg(0, input_file_.beg);

    if (!input_file_.good()) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to rewind file");
      return false;
    }

    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s ] %s file rewound successfully", inst_name_.c_str(),
            input_fmt_ == APP_VIDEO_INPUT_FORMAT_NV12 ? "NV12" : "BGR");
  } else if (input_fmt_ == APP_VIDEO_INPUT_FORMAT_JPEG) {
    // For JPEG, reset the read flag to allow reading in next iteration
    jpeg_read_in_current_iteration_ = false;
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] JPEG read flag reset for next iteration", inst_name_.c_str());
  }

  return true;
}

bool AppFileReader::create_output_pool(vart::VideoInfo& in_vinfo) {
  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Creating output buffer pool(s)", inst_name_.c_str());

  // Create device shared_ptr
  DevicePtr device_ptr(config_.device, [](vart::Device*) {});

  if (config_.bypass_preprocessing) {
    // INFERENCE-ONLY MODE: Create per-tensor pools
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "Creating %zu tensor-specific buffer pools",
            config_.num_input_tensors);

    tensor_pools_.clear();
    tensor_pools_.reserve(config_.num_input_tensors);

    // Calculate pool depth based on pipeline configuration
    uint32_t pipeline_queue_depth = config_.inference_queue_depth;
    uint32_t pool_depth = (pipeline_queue_depth + 2) * config_.model_batch_size;

    for (size_t i = 0; i < config_.num_input_tensors; i++) {
      const auto& tensor_info = config_.input_tensor_info[i];
      size_t tensor_size = tensor_info.meta.size_in_bytes;

      // Create VideoInfo for this tensor with actual model dimensions and correct format
      vart::VideoInfo tensor_vinfo;
      memset(&tensor_vinfo, 0, sizeof(tensor_vinfo));

      /**
       * For inference-only mode with binary files, we treat each tensor as a GRAY frame
       * with width equal to tensor size and height of 1.
       * This simplifies Video buffer management while ensuring correct size allocation.
       */
      tensor_vinfo.height = 1;
      tensor_vinfo.width = tensor_size;  // Width set to tensor size in bytes

      /* For GRAY format, number of planes is 1 */
      tensor_vinfo.fmt = vart::VideoFormat::GRAY8;  // As width is set to tensor size in bytes
      tensor_vinfo.n_planes = 1;
      tensor_vinfo.alignment.stride_align[0] = 1;

      try {
        auto pool = std::make_unique<VideoFramePool>(pool_depth, DEFAULT_FILE_READER_POOL_TYPE, tensor_size,
                                                     config_.mem_bank, tensor_vinfo, device_ptr,
                                                     VIDEO_FRAME_TIMEOUT_DURATION(config_.num_model_instances));
        tensor_pools_.push_back(std::move(pool));

        APP_LOG(AppLogLevel::DEBUG, config_.log_level, "Created pool[%zu] for tensor '%s': size=%zu bytes, depth=%u", i,
                tensor_info.meta.name.c_str(), tensor_size, pool_depth);
      } catch (const std::exception& e) {
        APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to create pool for tensor %zu: %s", i, e.what());
        return false;
      }
    }

    APP_LOG(AppLogLevel::INFO, config_.log_level, "Created %zu tensor-specific pools for inference-only mode",
            tensor_pools_.size());

  } else {
    // PREPROCESSING MODE: Create single output pool
    vart::VideoFormat fmt = get_video_frame_format(input_fmt_);
    size_t buf_size = get_video_frame_size(fmt, dectected_input_width_, dectected_input_height_);

    if (!buf_size) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Invalid buffer pool size: %zu", buf_size);
      return false;
    }

    // Calculate pool depth based on pipeline configuration (includes postprocess if connected)
    uint32_t pipeline_queue_depth =
        config_.preprocess_queue_depth + config_.inference_queue_depth + config_.postprocess_queue_depth;

    uint32_t pool_depth = (pipeline_queue_depth + 2) * config_.model_batch_size * config_.num_input_tensors;
    try {
      output_pool_ = std::make_unique<VideoFramePool>(pool_depth, DEFAULT_FILE_READER_POOL_TYPE, buf_size,
                                                      config_.mem_bank, in_vinfo, device_ptr,
                                                      VIDEO_FRAME_TIMEOUT_DURATION(config_.num_model_instances));
      APP_LOG(AppLogLevel::DEBUG, config_.log_level,
              "[%s] pool created: %ux%u, format=%d, depth=%u (pipeline_depth=%u), buf_size=%zu", inst_name_.c_str(),
              dectected_input_width_, dectected_input_height_, static_cast<int>(fmt), pool_depth, pipeline_queue_depth,
              buf_size);
    } catch (const std::exception& e) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to create output pool: %s", e.what());
      return false;
    }
  }

  return true;
}

bool AppFileReader::extract_input_resolution() {
  APP_LOG(AppLogLevel::INFO, config_.log_level, "Extracting input resolution");
  size_t in_frame_size = 0;

  // Get file extension
  std::string fileExtension = get_file_extension_lowercase(config_.input_image_file_path);

  // Process based on file extension
  if (fileExtension == "jpg" || fileExtension == "jpeg") {
    // Read image file for capturing properties
    cv::Mat frame = cv::imread(config_.input_image_file_path);

    if (frame.empty()) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Unable to open image file for resolution extraction");
      return false;
    }

    // Update configuration with actual dimensions
    dectected_input_width_ = static_cast<uint32_t>(frame.cols);
    dectected_input_height_ = static_cast<uint32_t>(frame.rows);
    in_frame_size =
        static_cast<size_t>(dectected_input_width_ * dectected_input_height_ * static_cast<uint32_t>(frame.channels()));
    input_fmt_ = APP_VIDEO_INPUT_FORMAT_JPEG;
  } else if (fileExtension == "nv12") {
    if (!config_.bin_input_width || !config_.bin_input_height) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "For NV12 format, width and height must be provided using --dim");
      return false;
    }
    input_fmt_ = APP_VIDEO_INPUT_FORMAT_NV12;
    in_frame_size = config_.bin_input_width * config_.bin_input_height * 1.5;
    dectected_input_width_ = config_.bin_input_width;
    dectected_input_height_ = config_.bin_input_height;
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] NV12 resolution: %ux%u", inst_name_.c_str(),
            config_.bin_input_width, config_.bin_input_height);

  } else if (fileExtension == "bgr") {
    if (!config_.bin_input_width || !config_.bin_input_height) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "For BGR format, width and height must be provided using --dim");
      return false;
    }
    input_fmt_ = APP_VIDEO_INPUT_FORMAT_BGR;
    in_frame_size = config_.bin_input_width * config_.bin_input_height * 3;
    dectected_input_width_ = config_.bin_input_width;
    dectected_input_height_ = config_.bin_input_height;
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] BGR resolution: %ux%u", inst_name_.c_str(),
            config_.bin_input_width, config_.bin_input_height);

  } else {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Unsupported file format: %s (Supported: .jpg, .jpeg, .nv12, .bgr)",
            fileExtension.c_str());
    return false;
  }
  /* Log and update the input resolution in AppContext */
  APP_LOG(AppLogLevel::INFO, config_.log_level, "in_frame_width = %d , in_frame_height = %d , in_frame_size = %ld",
          dectected_input_width_, dectected_input_height_, in_frame_size);
  return true;
}

vart::VideoFormat AppFileReader::get_input_vart_format(void) const {
  return get_video_frame_format(input_fmt_);
}

AppReadStatus AppFileReader::read_single_frame(vart::VideoFrame* frame) {
  size_t width, height;
  size_t bytes = 0;
  size_t bytes_to_read = 0;

  // Map the video frame memory for writing
  const vart::VideoFrameMapInfo* map_info = nullptr;

  try {
    map_info = &frame->map(vart::DataMapFlags::WRITE);
  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to map memory: %s", e.what());
    return APP_READ_FAILED;
  }

  if (input_fmt_ == APP_VIDEO_INPUT_FORMAT_JPEG) {
    // For JPEG: Only read once per iteration (single frame input)
    // Check if we already read the JPEG in this iteration
    if (jpeg_read_in_current_iteration_) {
      frame->unmap();
      APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] JPEG already read in current iteration, returning EOF",
              inst_name_.c_str());
      return APP_EOF;
    }

    auto read_frame = cv::imread(config_.input_image_file_path);
    if (read_frame.empty()) {
      frame->unmap();
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Unable to open image file");
      return APP_READ_FAILED;
    }

    // Copy image data into buffer
    for (int h = 0; h < read_frame.rows; h++) {
      uint8_t* dst = map_info->planes[0].data + (h * map_info->planes[0].stride);
      uint8_t* src = read_frame.data + (h * read_frame.cols * 3);
      memcpy(dst, src, (read_frame.cols * 3));
    }

    frame->unmap();

    // Mark that we've read the JPEG in this iteration
    jpeg_read_in_current_iteration_ = true;

    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Successfully read JPEG frame (will return EOF on next read)",
            inst_name_.c_str());
    return APP_READ_SUCCESS;

  } else if (input_fmt_ == APP_VIDEO_INPUT_FORMAT_NV12) {
    bytes = 0;
    width = config_.bin_input_width;
    height = config_.bin_input_height;
    bytes_to_read = width * height;

    // Read luminance (Y) plane data
    for (size_t h = 0; h < height; h++) {
      uint8_t* dst = map_info->planes[0].data + (h * map_info->planes[0].stride);
      bytes += input_file_.read(reinterpret_cast<char*>(dst), map_info->width).gcount();
    }

    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "Read %lu bytes for plane 0 for NV12", bytes);

    if (bytes != bytes_to_read) {
      if (bytes != 0 && !input_file_.eof()) {
        APP_LOG(AppLogLevel::WARNING, config_.log_level, "Read less data than expected");
      }
      frame->unmap();

      // Check if the end of the file is reached
      if (input_file_.eof()) {
        return APP_EOF;
      } else {
        return APP_READ_FAILED;
      }
    }

    bytes_to_read = width * height * 0.5;
    bytes = 0;

    // Read chrominance (U and V plane interleaved) plane data
    for (size_t h = 0; h < height / 2; h++) {
      uint8_t* dst = map_info->planes[1].data + (h * map_info->planes[1].stride);
      bytes += input_file_.read(reinterpret_cast<char*>(dst), map_info->width).gcount();
    }

    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Read %lu bytes for plane 1 for NV12", inst_name_.c_str(),
            bytes);

    if (bytes != bytes_to_read) {
      if (bytes != 0 && !input_file_.eof()) {
        APP_LOG(AppLogLevel::WARNING, config_.log_level, "Read less data than expected");
      }
      frame->unmap();

      // Check if the end of the file is reached
      if (input_file_.eof()) {
        return APP_EOF;
      } else {
        return APP_READ_FAILED;
      }
    }
  } else if (input_fmt_ == APP_VIDEO_INPUT_FORMAT_BGR) {
    bytes = 0;
    width = config_.bin_input_width;
    height = config_.bin_input_height;
    bytes_to_read = width * height * 3;

    // Read BGR data
    for (size_t h = 0; h < height; h++) {
      uint8_t* dst = map_info->planes[0].data + (h * map_info->planes[0].stride);
      bytes += input_file_.read(reinterpret_cast<char*>(dst), width * 3).gcount();
    }

    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Read %lu bytes for BGR", inst_name_.c_str(), bytes);

    if (bytes != bytes_to_read) {
      if (bytes != 0 && !input_file_.eof()) {
        APP_LOG(AppLogLevel::WARNING, config_.log_level, "Read less data than expected");
      }
      frame->unmap();

      // Check if the end of the file is reached
      if (input_file_.eof()) {
        return APP_EOF;
      } else {
        return APP_READ_FAILED;
      }
    }
  } else {
    frame->unmap();
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Unsupported input format for reading");
    return APP_READ_FAILED;
  }

  // Unmap the video frame memory
  frame->unmap();
  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Read data into buffer successfully", inst_name_.c_str());
  return APP_READ_SUCCESS;
}

/**
 * @brief Submit batch to single output queue (1:1 mapping)
 */
bool AppFileReader::submit_batch_to_queue(const InputFrame& batch, bool is_partial) {
  const char* batch_type = is_partial ? "partial batch" : "batch";

  // Submit to single preprocessing queue
  auto* queue = static_cast<AppQueue<InputFrame>*>(output_queue_);
  if (!queue->push(batch, 0)) {
    handle_queue_submission_failure();
    return false;
  }

  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Successfully submitted %s (%zu frames) to preprocessing queue",
          inst_name_.c_str(), batch_type, batch.size());

  // Submit to single postprocess queue if available (for overlay, 1:1 mapping)
  if (postprocess_queue_) {
    auto* pp_queue = static_cast<AppQueue<InputFrame>*>(postprocess_queue_);
    if (pp_queue->push(batch, 0)) {
      APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Successfully sent %s to postprocess queue",
              inst_name_.c_str(), batch_type);
    } else {
      APP_LOG(AppLogLevel::WARNING, config_.log_level, "Failed to send %s to postprocess queue", batch_type);
    }
  }

  // Update counters
  update_read_counters(batch.size(), 1);
  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Submitted %s with %zu frames, total read: %lu",
          inst_name_.c_str(), batch_type, batch.size(), frames_read_.load());

  return true;
}

AppReadStatus AppFileReader::read_frame_batch(InputFrame& batch) {
  AppReadStatus overall_status = APP_READ_SUCCESS;

  // Read frames for each batch element
  // batch structure: batch[batch_idx][frame_idx]
  // For image processing: batch_idx goes from 0 to batch_size-1, frame_idx is always 0
  for (size_t batch_idx = 0; batch_idx < batch.size(); batch_idx++) {
    // Each batch element has 1 frame for image processing
    for (size_t frame_idx = 0; frame_idx < batch[batch_idx].size(); frame_idx++) {
      // Calculate actual frame index for this batch position
      int actual_frame_index = frame_index_ + batch_idx;

      APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Reading frame index %d into batch position [%zu]",
              inst_name_.c_str(), actual_frame_index, batch_idx);

      // Acquire buffer from pool
      try {
        batch[batch_idx][frame_idx] = output_pool_->acquire_frame();
      } catch (const std::runtime_error& e) {
        const bool shutting_down = std::string(e.what()).find("shutting down") != std::string::npos;
        APP_LOG(AppLogLevel::ERROR, config_.log_level,
                "[%s] %s while acquiring buffer from file reader pool (batch %zu, frame %zu): %s", inst_name_.c_str(),
                shutting_down ? "Pool shutdown" : "Timeout", batch_idx, frame_idx, e.what());
        return APP_READ_FAILED;
      }

      // Read frame data
      AppReadStatus status = read_single_frame(batch[batch_idx][frame_idx].get());
      if (status != APP_READ_SUCCESS) {
        overall_status = status;
        // If we hit EOF or error, we're done reading
        if (status == APP_EOF || status == APP_READ_FAILED) {
          // Check if this is the first frame of the batch
          if (batch_idx == 0 && frame_idx == 0) {
            // No frames read at all - resize to remove the acquired but unused buffer
            batch.video_frame.resize(0);
            return status;
          } else {
            // Partial batch read - resize and process what we have
            APP_LOG(AppLogLevel::INFO, config_.log_level,
                    "[%s] Partial batch read (%zu/%zu frames), resizing batch for processing", inst_name_.c_str(),
                    batch_idx, batch.size());

            // Resize the batch to actual frames read
            batch.video_frame.resize(batch_idx);
            // Set iteration number for partial batch
            batch.set_batch_start_index(frame_index_);
            batch.iteration_number = current_iteration_.load();

            // Return EOF to signal last batch, but batch is valid and will be processed
            return APP_EOF;
          }
        }
      }
    }
  }

  // Set frame index and iteration number for the batch
  batch.set_batch_start_index(frame_index_);
  batch.iteration_number = current_iteration_.load();

  return overall_status;
}
