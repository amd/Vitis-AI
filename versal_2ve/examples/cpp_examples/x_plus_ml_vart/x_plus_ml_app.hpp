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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <regex>
#include <set>
#include <vector>

#include <dlfcn.h>
#include <getopt.h>
#include <limits.h>
#include <unistd.h>
#include <atomic>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <condition_variable>
#include <mutex>
#include <queue>
#include "pool_timeouts.hpp"

#include <vart/vart_device.hpp>
#include <vart/vart_inferresult_types.hpp>
#include <vart/vart_memory.hpp>
#include <vart/vart_memory_types.hpp>
#include <vart/vart_preprocess.hpp>
#include <vart/vart_preprocess_types.hpp>
#include <vart/vart_runner_factory.hpp>
#include <vart/vart_videoframe.hpp>
#include <vart/vart_videoframe_types.hpp>

#include "app_queue.hpp"
#include "common/app_logger.hpp"
#include "file_reader.hpp"
#include "frame_types.hpp"
#include "inference.hpp"
#include "postprocess.hpp"
#include "preprocess.hpp"

// Queue type aliases for improved readability
using InputFrameQueue = AppQueue<InputFrame>;
using PreprocessedFrameQueue = AppQueue<PreprocessedFrame>;
using InferenceResultQueue = AppQueue<InferenceResult>;
using CompletionQueue = AppQueue<ProcessingComplete>;

// Queue pointer type aliases
using InputFrameQueuePtr = std::unique_ptr<InputFrameQueue>;
using PreprocessedFrameQueuePtr = std::unique_ptr<PreprocessedFrameQueue>;
using InferenceResultQueuePtr = std::unique_ptr<InferenceResultQueue>;
using CompletionQueuePtr = std::unique_ptr<CompletionQueue>;

#define DEFAULT_FRAME_MEMBANK 0
#define PL_DEVICE_INDEX 1

/* Process all frames in input_file */
#define APP_PROCESS_ALL_FRAMES -1
/* Default quantization factor */
#define DEFAULT_QUANT_FACTOR 1.0f

#define PREPROCESS_QUEUE_DEPTH 3
#define INFERENCE_QUEUE_DEPTH 3
#define POSTPROCESS_QUEUE_DEPTH 3

using namespace std;
namespace pt = boost::property_tree;

/**
 * @brief Binding structure for tight coupling between preprocessing and
 * inference instances Each binding represents a 1:1 relationship between a
 * preprocessing instance and inference instance
 */
struct PreprocessInferenceBinding {
  uint32_t preproc_inst_id;              // Index of preprocessing instance
  uint32_t inf_inst_id;                  // Index of coupled inference instance
  InputFrameQueue* preproc_inq;          // Dedicated input queue for this preprocessing instance
  PreprocessedFrameQueue* preproc_outq;  // Dedicated output queue for this preprocessing instance
  PreprocessedFrameQueue* inf_inq;       // Coupled inference input queue (receives PreprocessedFrame)
  InferenceResultQueue* inf_outq;        // Coupled inference output queue

  // Constructor for easy initialization
  PreprocessInferenceBinding(uint32_t prep_id, uint32_t inf_id)
      : preproc_inst_id(prep_id),
        inf_inst_id(inf_id),
        preproc_inq(nullptr),
        preproc_outq(nullptr),
        inf_inq(nullptr),
        inf_outq(nullptr) {}
};

struct AppContext {
  /* Log level for application */
  AppLogLevel log_level;

  /* Number of input frames to process */
  int32_t num_frame_to_process;

  /* Paths and indices */
  string app_config;

  string input_file_path;
  string output_dir_path;
  string xclbin_location;

  /* Input file stream for reading from input_file_path */
  ifstream input_file;
  ofstream dump_input_fp;

  /* OpenCV VideoCapture for video input */
  cv::VideoCapture* vid_capture;

  bool dump_all_inputs;

  /* Dimensions of input frames */
  uint32_t input_height;
  uint32_t input_width;

  /* Device context - shared across all pipelines */
  shared_ptr<vart::Device> device;
  int32_t device_idx;

  /* File Reader Component */
  std::vector<std::unique_ptr<AppFileReader>> file_readers;  // Multiple readers

  /* Input file configuration per model from ifms-config */
  struct IfmsConfig {
    std::string tensor_name;  // Must match a runner-reported input tensor name. Used for name-based
                              // binding in inference-only mode and cross-checked against the model's
                              // single input tensor in preprocessing (per-model) mode. Ignored when
                              // the CLI broadcast --input-file is supplied.
    std::string file_path;    // Full path to binary/image file
    uint32_t width;           // Image width (for NV12/BGR in preprocessing mode, 0 otherwise)
    uint32_t height;          // Image height (for NV12/BGR in preprocessing mode, 0 otherwise)
  };
  std::vector<std::vector<IfmsConfig>> model_ifms_configs;  // [model_idx][tensor_idx]

  /* Pre-process context */
  bool preprocess_enable;
  std::vector<std::unique_ptr<AppPreProcess>> preprocess;
  std::vector<PreProcessConfig> preproc_cfg;
  std::vector<string> preproc_json_str;
  uint8_t ppe_mbank_in;

  /* Inference Context */
  std::vector<std::unique_ptr<Inference>> inference;
  std::vector<InferenceConfig> model_info;
  std::vector<string> model_json_path;
  std::vector<string> model_snap_path;
  std::vector<uint32_t> batch_size_per_model;
  uint32_t num_model_instances;

  /* Per-model frame tracking for accurate benchmarking */
  std::vector<int64_t> frames_processed_per_model;  // [model_idx] -> actual frames processed by this model

  /* PostProcess Context */
  std::vector<bool> postprocess_enable;
  bool any_postprocess_enabled;  // Computed once from postprocess_enable vector
  std::vector<bool> metaconvert_enable;
  std::vector<std::unique_ptr<AppPostProcess>> postprocess;
  std::vector<PostProcessConfig> postproc_cfg;
  std::vector<string> postproc_json_str;

  /* Maximum number of iterations to run */
  uint32_t max_iterations;
  uint32_t iteration_counter;

  /* Flag to indicate if benchmarking is enabled */
  bool is_benchmark_enabled;

  /* Pipeline timeout configuration (seconds) */
  int pipeline_timeout_seconds;

  /* Pipeline-specific infrastructure */
  // Individual queues per preprocessing instance (for true isolation)
  std::vector<InputFrameQueuePtr> preproc_inqs_vec;          // Per-instance input queues
  std::vector<PreprocessedFrameQueuePtr> preproc_outqs_vec;  // Per-instance output queues

  // Individual queues per inference instance
  std::vector<PreprocessedFrameQueuePtr> inf_inqs_vec;  // Per-instance inference input queues
  std::vector<InferenceResultQueuePtr> inf_outqs_vec;   // Per-instance inference output queues

  // Individual queues for broadcasting original frames to postprocess (when preprocessing enabled)
  std::vector<InputFrameQueuePtr> orig_frame_qs_vec;  // Per-instance orig frame queues

  // Completion notification queues for postprocess instances
  std::vector<CompletionQueuePtr> postproc_outqs_vec;  // Per-instance completion queues

  // Pipeline flow control counters
  // NOTE: These track BATCHES, not individual frames
  // To get actual frame count: multiply counter value by batch_size
  std::atomic<bool> critical_error{false};
  std::atomic<int> frames_submitted{0};             // Total batches submitted to pipeline (only incremented)
  std::atomic<int> frames_completed{0};             // Total batches fully processed (only incremented)
  std::atomic<int64_t> last_completion_time_ns{0};  // Timestamp of last completion (nanoseconds)
  // Active batches in pipeline = frames_submitted - frames_completed (calculated, not stored)

  // Binding map for tight coupling (preprocessing instance ↔ inference instance)
  std::vector<PreprocessInferenceBinding> instance_bindings;

  // Default constructor
  AppContext() = default;
};

/**
 * @brief Initializes the application context.
 *
 * @param ctx A pointer to the AppContext structure.
 */
void init_app_context(AppContext* ctx);

const char* to_string(vart::MemoryLayout l);
const char* to_string(vart::DataType d);
const char* to_string(vart::VideoFormat f);

/**
 * @brief Extract the colour space ("RGB" or "BGR") from a VideoFormat enum.
 *
 * @param fmt  VideoFormat enum value.
 * @return "RGB", "BGR", or empty string for unmappable formats.
 */
string get_colour_space(vart::VideoFormat fmt);

/**
 * @brief Direct map of a colour-format string to vart::VideoFormat.
 *
 * Accepts the full set of explicit format strings (e.g. "RGBX", "RGBP_FP16", "BGR_FLOAT").
 *
 * @param fmt  Format string from JSON.
 * @return vart::VideoFormat, or VideoFormat::UNKNOWN if not recognised.
 */
vart::VideoFormat get_vart_video_format(const string& fmt);

/**
 * @brief Derive vart::VideoFormat from a simple colour-space string and inference tensor metadata.
 *
 * Fallback when colour-format is not specified in JSON. The user's default colour space
 * ("RGB" or "BGR") is combined with the first inference input tensor's layout and data type
 * to select the correct vart::VideoFormat enum.
 *
 * @param colour_space  Simple colour-space string ("RGB" or "BGR").
 * @param layout        MemoryLayout from the inference input tensor.
 * @param dtype         DataType from the inference input tensor.
 * @return vart::VideoFormat, or VideoFormat::UNKNOWN on unsupported combination.
 */
vart::VideoFormat derive_vart_video_format(const string& colour_space, vart::MemoryLayout layout, vart::DataType dtype);

/**
 * @brief Infer a preprocess-compatible layout for a GENERIC input tensor.
 *
 * Supported rules are intentionally strict:
 * - 3D tensor  -> NHW
 * - 4D tensor with shape[1] in {3,4} -> NCHW
 * - 4D tensor with shape[3] in {3,4} -> NHWC
 * - all other or ambiguous 4D shapes -> unsupported
 *
 * @param tensor            Input tensor metadata.
 * @param inferred_layout   Resolved layout when inference succeeds.
 * @param inferred_width    Resolved width when inference succeeds.
 * @param inferred_height   Resolved height when inference succeeds.
 * @param error_reason      Human-readable failure reason when unsupported.
 * @return true when the GENERIC tensor shape can be used for preprocessing.
 */
bool infer_generic_preprocess_layout(const vart::NpuTensorInfo& tensor,
                                     vart::MemoryLayout& inferred_layout,
                                     uint32_t& inferred_width,
                                     uint32_t& inferred_height,
                                     string& error_reason);

/**
 * @brief Creates all the necessary contexts for the application.
 *
 * This function is responsible for creating all the necessary contexts for the
 * application to function properly. It should be called once during the
 * initialization phase of the application.
 *
 * @param ctx A pointer to the AppContext structure.
 * @param num_instances Number of processing instances to create (for parallel
 * processing)
 * @return true if successful, false otherwise.
 */
bool create_all_context(AppContext* ctx);

/**
 * @brief Flushes the pipeline by stopping all components and draining remaining frames.
 *
 * This function stops all running threads (file readers, preprocessing,
 * inference, postprocessing) and drains the pipeline to ensure all frames
 * are processed. Should be called before printing final statistics.
 *
 * @param ctx A pointer to the AppContext structure.
 */
void flush_pipeline(AppContext* ctx);

/**
 * @brief Cleans up all resources after pipeline is stopped.
 *
 * This function clears all component instances, queues, and releases device
 * resources. Should be called after statistics are printed and all data
 * is no longer needed.
 *
 * @param ctx A pointer to the AppContext structure.
 */
void destroy_all_context(AppContext* ctx);

/**
 * @brief Collect preprocessing results asynchronously (with timeout)
 * @param ctx Application context containing preprocessing components
 * @return True if all frames were successfully collected, false otherwise
 */
uint32_t handle_preprocess_completion(AppContext* ctx);

/**
 * @brief Submit frames to inference asynchronously (non-blocking)
 * @param ctx Application context containing inference components
 * @param inst_index Index of the inference component to use (for parallel
 * processing)
 * @param frames_to_process Number of frames to submit for inference
 * @param preprocess_output Vector of preprocessed frames to submit to inference
 * @param batch_id Unique identifier for this batch of frames
 * @return True if all frames were successfully submitted, false otherwise
 */

/**
 * @brief Process available inference results when both engines have completed
 * This ensures synchronized processing and proper pipeline accounting
 * @param ctx Application context
 * @return count of results processed (0 if no results available)
 */
uint32_t handle_all_inference_completions(AppContext* ctx);

/**
 * @brief Trigger graceful pipeline shutdown on critical error
 * Stops all components in the correct order to ensure clean shutdown
 * @param ctx Application context
 * @param reason Description of why shutdown was triggered
 */
void trigger_pipeline_shutdown(AppContext* ctx, const std::string& reason);
