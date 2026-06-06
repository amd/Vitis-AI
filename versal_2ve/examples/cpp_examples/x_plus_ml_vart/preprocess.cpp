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
 * @file preprocess.cpp
 * @brief Implementation of preprocessing component
 */

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <chrono>
#include <iostream>
#include <sstream>
#include "preprocess.hpp"

/* XRT based buffer allocation */
#define DEFAULT_PREPROCESS_POOL_TYPE vart::VideoFrameImplType::XRT

using namespace std;
using namespace vart;
namespace pt = boost::property_tree;

/* Return the size of frame required for VideoFormat */
static size_t get_video_frame_size(vart::VideoFormat fmt, size_t width, size_t height) {
  size_t size;
  switch (fmt) {
    case vart::VideoFormat::BGR:
    case vart::VideoFormat::RGB:
    case vart::VideoFormat::RGBP:
      size = (width * height) * 3;
      break;
    case vart::VideoFormat::RGB_FLOAT:
    case vart::VideoFormat::BGR_FLOAT:
    case vart::VideoFormat::RGBP_FLOAT:
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

/* Extract component-specific JSON configuration */
static string extract_preproc_json(const string& json_string, const string& component) {
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
 * @brief Dump preprocessing input data to a binary file for debugging purposes.
 *        All frames are concatenated to a single file.
 * @param ctx  Pointer to the preprocessing configuration containing configuration and
 *             state information.
 * @param map_info Pointer to the video frame map information.
 * @param name Name to be used in the output file name.
 * @param frame_index Index of the current frame (0 = first frame).
 * @param iteration_number Current iteration number for multi-iteration runs.
 */
static void dump_preproc_input_to_file(const PreProcessConfig* ctx,
                                       const vart::VideoFrameMapInfo* map_info,
                                       const string& name,
                                       int frame_index,
                                       int64_t iteration_number) {
  AppLogLevel log_level = ctx->log_level;
  string file_name;

  // Conditionally include iteration number based on max_iterations
  if (ctx->max_iterations > 1) {
    // Multiple iterations - include iteration number in filename
    file_name = ctx->output_dir_path + "/iter" + to_string(iteration_number) + "_preproc" +
                to_string(ctx->instance_id) + name + ".bin";
  } else {
    // Single iteration - use original filename format
    file_name = ctx->output_dir_path + "/preproc" + to_string(ctx->instance_id) + name + ".bin";
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
      // 8 bytes per pixel (packed RGBx/BGRx with BF16 or  FP16)
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

    case VideoFormat::Y_UV8_420:
      // NV12 format: Y plane followed by interleaved UV plane
      // Y plane (full resolution)
      for (int h = 0; h < map_info->height; ++h) {
        uint8_t* row_ptr = map_info->planes[0].data + (h * map_info->planes[0].stride);
        file.write(reinterpret_cast<const char*>(row_ptr), map_info->width);
      }
      // UV plane (half resolution)
      for (int h = 0; h < map_info->height / 2; ++h) {
        uint8_t* row_ptr = map_info->planes[1].data + (h * map_info->planes[1].stride);
        file.write(reinterpret_cast<const char*>(row_ptr), map_info->width);
      }
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
  APP_LOG(AppLogLevel::DEBUG, log_level, "Dumped preproc input frame %d to %s", frame_index, file_name.c_str());
}

AppPreProcess::AppPreProcess(const PreProcessConfig& config,
                             AppQueue<InputFrame>& input_queue,
                             AppQueue<PreprocessedFrame>& output_queue)
    : config_(config),
      pre_process_(nullptr),
      input_queue_(&input_queue),
      output_queue_(&output_queue),
      worker_thread_(nullptr),
      state_(ThreadState::IDLE),
      total_time_(0),
      inst_name_("Preprocess" + std::to_string(config.instance_id)) {}

AppPreProcess::~AppPreProcess() {
  if (is_running()) {
    stop();
  }

  // Clean up VART PreProcess contexts (smart pointer handles automatic cleanup)
  pre_process_.reset();

  // Clean up output pools (smart pointer handles automatic cleanup)
  output_pool_.reset();
}

bool AppPreProcess::initialize() {
  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] Initializing ...", inst_name_.c_str());

  try {
    // Create VART PreProcess engine
    if (!create_preprocess_engine()) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to create VART PreProcess engine");
      return false;
    }

    // Create output memory pools for both preprocessing outputs
    if (!create_output_pool()) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to create output memory pool");
      return false;
    }

    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] initialized successfully", inst_name_.c_str());
    return true;

  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Exception during PreProcess initialization: %s", e.what());
    return false;
  }
}

bool AppPreProcess::start() {
  if (is_running()) {
    APP_LOG(AppLogLevel::WARNING, config_.log_level, "[%s] already running", inst_name_.c_str());
    return true;
  }

  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] Starting...", inst_name_.c_str());
  try {
    // Start worker thread
    state_ = ThreadState::RUNNING;
    worker_thread_ = std::make_unique<std::thread>(&AppPreProcess::worker_thread_function, this);
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] started successfully", inst_name_.c_str());
    return true;

  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] Failed to start worker thread: %s", inst_name_.c_str(),
            e.what());
    state_ = ThreadState::IDLE;
    return false;
  }
}

void AppPreProcess::stop() {
  if (!is_running()) {
    APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] already stopped", inst_name_.c_str());
    return;
  }

  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] Stopping...", inst_name_.c_str());

  // Signal shutdown
  state_ = ThreadState::SHUTTING_DOWN;

  // Notify queues to wake up any waiting threads
  input_queue_->finish();
  output_queue_->finish();

  // Wait for worker thread to finish
  if (worker_thread_ && worker_thread_->joinable()) {
    worker_thread_->join();
  }

  state_ = ThreadState::IDLE;
  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] stopped", inst_name_.c_str());
}

void AppPreProcess::worker_thread_function() {
  APP_LOG(AppLogLevel::INFO, config_.log_level, "[%s] worker thread started", inst_name_.c_str());

  // Track backpressure state for logging
  bool backpressure_logged = false;
  const size_t MIN_AVAILABLE_BUFFERS = 2;  // Threshold for backpressure

  while (state_.load() == ThreadState::RUNNING) {
    try {
      // BACKPRESSURE HANDLING: Check output pool availability before processing
      size_t available_buffers = output_pool_->get_available_count();

      if (available_buffers < MIN_AVAILABLE_BUFFERS) {
        // Output pool is nearly exhausted - inference is backed up
        if (!backpressure_logged) {
          APP_LOG(AppLogLevel::WARNING, config_.log_level,
                  "[%s]: Backpressure detected - output pool low (%zu buffers available), "
                  "waiting for inference to release buffers",
                  inst_name_.c_str(), available_buffers);
          backpressure_logged = true;
        }

        // Wait briefly before checking again
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        continue;
      }

      // Reset backpressure logging when pool recovers
      if (backpressure_logged && available_buffers >= MIN_AVAILABLE_BUFFERS) {
        APP_LOG(AppLogLevel::INFO, config_.log_level,
                "[%s]: Backpressure relieved - output pool recovered to %zu buffers", inst_name_.c_str(),
                available_buffers);
        backpressure_logged = false;
      }

      InputFrame input_frame;

      // Wait for input frame from main thread
      if (input_queue_->pop(input_frame)) {
        // Process the frame through vart preprocessing
        if (!process_input_frame(input_frame)) {
          APP_LOG(AppLogLevel::ERROR, config_.log_level, "[%s] CRITICAL: Failed to process frame %d. Shutting down.",
                  inst_name_.c_str(), input_frame.frame_index);

          // Signal critical error to application
          if (config_.critical_error_ptr) {
            *config_.critical_error_ptr = true;
          }

          // Trigger graceful shutdown
          state_ = ThreadState::SHUTTING_DOWN;
          break;
        }
      } else {
        // Queue finished or error - check if we should continue
        if (state_.load() != ThreadState::RUNNING) {
          break;
        }
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

bool AppPreProcess::process_input_frame(const InputFrame& input_frame) {
  auto batch_size = input_frame.size();
  try {
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s]: got new frame batch to process (batch_size=%zu)",
            inst_name_.c_str(), batch_size);

    // Validate input
    if (input_frame.video_frame.empty()) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "No video frames in InputFrame");
      return false;
    }

    // Create output batch with same structure as input
    PreprocessedFrame result(batch_size, input_frame.frames_per_batch(), input_frame.frame_index);
    result.iteration_number = input_frame.iteration_number;

    // Process each batch element
    for (size_t batch_idx = 0; batch_idx < batch_size; batch_idx++) {
      // Validate batch element
      if (input_frame.video_frame[batch_idx].empty()) {
        APP_LOG(AppLogLevel::ERROR, config_.log_level, "Batch element %zu is empty", batch_idx);
        return false;
      }

      // Process each frame within the batch element
      for (size_t frame_idx = 0; frame_idx < input_frame[batch_idx].size(); frame_idx++) {
        // Acquire output frame from pool for this specific frame
        auto output_frame = acquire_output_frame();
        if (!output_frame) {
          APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to acquire output frame for batch[%zu][%zu]",
                  batch_idx, frame_idx);
          return false;
        }

        // Process this specific frame
        APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] processing batch[%zu][%zu]", inst_name_.c_str(), batch_idx,
                frame_idx);

        if (!process_frame_with_vart(input_frame.video_frame[batch_idx][frame_idx], output_frame,
                                     input_frame.frame_index, input_frame.iteration_number)) {
          APP_LOG(AppLogLevel::ERROR, config_.log_level, "VART preprocess engine failed for batch[%zu][%zu]", batch_idx,
                  frame_idx);
          return false;
        }

        // Store the preprocessed frame in the result batch
        result[batch_idx][frame_idx] = std::move(output_frame);

        APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] completed batch[%zu][%zu]", inst_name_.c_str(), batch_idx,
                frame_idx);
      }
    }

    // Send entire batch to output queue for inference
    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] push batch to output q", inst_name_.c_str());

    if (!output_queue_->push(result)) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to push result batch to output queue");
      return false;
    }

    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Successfully processed batch (size=%zu, frame_index=%d)",
            inst_name_.c_str(), batch_size, input_frame.frame_index);
    return true;

  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Exception during frame processing: %s", e.what());
    return false;
  }
}

bool AppPreProcess::create_preprocess_engine() {
  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Creating VART PreProcess engine", inst_name_.c_str());

  try {
    // Extract preprocessing JSON configuration
    string preprocess_json_config = extract_preproc_json(config_.json_str, "preprocess-config");
    if (preprocess_json_config.empty()) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to parse preprocess config");
      return false;
    }

    APP_LOG(AppLogLevel::DEBUG, config_.log_level, "Config: %s", preprocess_json_config.c_str());

    // Create shared_ptr for device as required by VART API
    DevicePtr device_ptr(config_.device, [](vart::Device*) {});

    // Create VART PreProcess context (smart pointer for automatic resource
    // management)
    pre_process_ = std::make_unique<vart::PreProcess>(DEFAULT_PREPROCESS_TYPE, preprocess_json_config, device_ptr);
    if (!pre_process_) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Unable to create VART pre-process context");
      return false;
    }

    // Set preprocessing information
    vart::PreProcessInfo preprocess_info = config_.preprocess_info;
    preprocess_info.height = config_.output_height;
    preprocess_info.width = config_.output_width;

    /* The image processing IP requires the scale factor to be the reciprocal of the model's original scale factor due
     * to internal optimization*/
    preprocess_info.qt_fctr = config_.quant_scale_factor_conf_set ? config_.quant_scale_factor
                                                                  : (1.0 / config_.input_tensor_quantization_factor);

    pre_process_->set_preprocess_info(preprocess_info);

    APP_LOG(AppLogLevel::DEBUG, config_.log_level,
            "[%s] VART PreProcess engine created successfully (output: %dx%d, qt_fctr: %f)", inst_name_.c_str(),
            preprocess_info.width, preprocess_info.height, preprocess_info.qt_fctr);
    return true;

  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Exception creating VART PreProcess engine: %s", e.what());
    // Smart pointer automatically handles cleanup on exception
    pre_process_.reset();
    return false;
  }
}

bool AppPreProcess::process_frame_with_vart(std::shared_ptr<vart::VideoFrame> input_frame,
                                            std::shared_ptr<vart::VideoFrame> output_frame,
                                            int frame_index,
                                            int64_t iteration_number) {
  vector<vart::PreProcessOp> preprocess_ops;
  vart::PreProcessOp preprocess_op;

  // Set input ROI
  preprocess_op.in_roi.x = 0;
  preprocess_op.in_roi.y = 0;
  preprocess_op.in_roi.height = input_frame->get_video_info().height;
  preprocess_op.in_roi.width = input_frame->get_video_info().width;
  preprocess_op.in_frame = input_frame.get();

  // Set output ROI
  preprocess_op.out_roi.x = 0;
  preprocess_op.out_roi.y = 0;
  preprocess_op.out_roi.height = output_frame->get_video_info().height;
  preprocess_op.out_roi.width = output_frame->get_video_info().width;
  preprocess_op.out_frame = output_frame.get();

  // Apply PanScan if enabled for this specific preprocessing config
  if (config_.do_pan_scan) {
    set_roi_pan_scan(preprocess_op);
  }

  preprocess_ops.push_back(preprocess_op);

  // Dump input frame if enabled
  if (config_.dump_all_inputs) {
    const vart::VideoFrameMapInfo* map_info = nullptr;
    try {
      map_info = &input_frame->map(vart::DataMapFlags::READ);
    } catch (const exception& e) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to map input frame memory: %s", e.what());
      return false;
    }

    // Create name from video info
    string name = "_input_" + to_string(input_frame->get_video_info().width) + "x" +
                  to_string(input_frame->get_video_info().height);
    dump_preproc_input_to_file(&config_, map_info, name, frame_index, iteration_number);

    input_frame->unmap();
  }

  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Running VART PreProcess: %dx%d -> %dx%d", inst_name_.c_str(),
          preprocess_op.in_roi.width, preprocess_op.in_roi.height, preprocess_op.out_roi.width,
          preprocess_op.out_roi.height);
  try {
    // Execute VART preprocessing
    auto start = std::chrono::high_resolution_clock::now();
    pre_process_->process(preprocess_ops);
    auto end = std::chrono::high_resolution_clock::now();
    total_time_ += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    return true;
  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to process frame with VART: %s", e.what());
    return false;
  }
}

void AppPreProcess::set_roi_pan_scan(vart::PreProcessOp& preprocess_op) {
  float current_aspect_ratio =
      static_cast<float>(preprocess_op.in_roi.width) / static_cast<float>(preprocess_op.in_roi.height);
  float target_aspect_ratio =
      static_cast<float>(preprocess_op.out_roi.width) / static_cast<float>(preprocess_op.out_roi.height);

  int x, y, width, height;
  x = preprocess_op.in_roi.x;
  y = preprocess_op.in_roi.y;
  width = preprocess_op.in_roi.width;
  height = preprocess_op.in_roi.height;

  // Target aspect ratio is greater so crop from top and bottom
  if (current_aspect_ratio < target_aspect_ratio) {
    width = preprocess_op.in_roi.width;
    height = static_cast<int>(static_cast<float>(preprocess_op.in_roi.width) / target_aspect_ratio);
    x = 0;
    y = (preprocess_op.in_roi.height - height) / 2;
  }
  // Target aspect ratio is smaller so crop from left and right
  else {
    width = static_cast<int>(static_cast<float>(preprocess_op.in_roi.height) * target_aspect_ratio);
    height = preprocess_op.in_roi.height;
    x = (preprocess_op.in_roi.width - width) / 2;
    y = 0;
  }

  preprocess_op.in_roi.x = x;
  preprocess_op.in_roi.y = y;
  preprocess_op.in_roi.width = width;
  preprocess_op.in_roi.height = height;

  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] PanScan ROI adjusted: (%d,%d) %dx%d", inst_name_.c_str(), x, y,
          width, height);
}

bool AppPreProcess::create_output_pool() {
  APP_LOG(AppLogLevel::DEBUG, config_.log_level, "[%s] Creating output memory pool", inst_name_.c_str());

  try {
    // Get output video info from VART PreProcess
    vart::VideoInfo out_vinfo = pre_process_->get_output_vinfo();

    // Calculate buffer size for output frames
    size_t buf_size = get_video_frame_size(out_vinfo.fmt, config_.output_width, config_.output_height);

    if (!buf_size) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Invalid preprocess output buffer pool size: %ld", buf_size);
      throw std::runtime_error("Invalid pre-process output buffer pool size");
    }

    // Create device shared_ptr
    DevicePtr device_ptr(config_.device, [](vart::Device*) {});

    /* Calculate pool depth
     * - preprocess is quite fast ~2ms~5ms based on input size
     * - some inference can be slow >~30ms
     * - account for at-least 10-12 frames outstanding for preproc before
     *   inference returns them back to pool
     */
    uint32_t pool_depth = config_.batch_size * config_.frames_per_batch + 10;

    // Create the memory pool
    output_pool_ = std::make_unique<VideoFramePool>(pool_depth, DEFAULT_PREPROCESS_POOL_TYPE, buf_size,
                                                    config_.out_mem_bank, out_vinfo, device_ptr,
                                                    VIDEO_FRAME_TIMEOUT_DURATION(config_.num_model_instances));
    if (!output_pool_) {
      APP_LOG(AppLogLevel::ERROR, config_.log_level, "Failed to create output memory pool (depth=%u, %ux%u)",
              pool_depth, config_.output_width, config_.output_height);
      return false;
    }

    APP_LOG(AppLogLevel::DEBUG, config_.log_level,
            "[%s] Created output memory pool: depth=%u, dimensions=%ux%u, buf_size=%zu", inst_name_.c_str(), pool_depth,
            config_.output_width, config_.output_height, buf_size);

    return true;

  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Exception creating output memory pool: %s", e.what());
    return false;
  }
}

std::shared_ptr<vart::VideoFrame> AppPreProcess::acquire_output_frame() {
  if (!output_pool_) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "Output pool 1 not initialized");
    return nullptr;
  }

  try {
    return output_pool_->acquire_frame();
  } catch (const std::runtime_error& e) {
    const bool shutting_down = std::string(e.what()).find("shutting down") != std::string::npos;
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "%s while acquiring frame from preprocess output pool: %s",
            shutting_down ? "Pool shutdown" : "Timeout", e.what());
    return nullptr;
  }
}

void AppPreProcess::get_queue_depths(uint32_t& input_depth, uint32_t& output_depth) const {
  input_depth = static_cast<uint32_t>(input_queue_->size());
  output_depth = static_cast<uint32_t>(output_queue_->size());
}

vart::VideoInfo AppPreProcess::get_input_vinfo(uint32_t height, uint32_t width, vart::VideoFormat fmt) const {
  if (!pre_process_) {
    APP_LOG(AppLogLevel::ERROR, config_.log_level, "VART PreProcess not initialized, cannot get input video info");
    // Return empty VideoInfo on error
    vart::VideoInfo empty_vinfo = {};
    return empty_vinfo;
  }

  // Use first preprocessing engine to get input video info
  vart::VideoInfo input_vinfo;
  pre_process_->get_input_vinfo(height, width, fmt, input_vinfo);
  return input_vinfo;
}

float AppPreProcess::get_total_time_us() const {
  return total_time_;
}
