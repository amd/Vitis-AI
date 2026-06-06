/*
 * Copyright (C) 2024-2026 Advanced Micro Devices, Inc.
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

#include <getopt.h>
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
#include <vector>

#include <dlfcn.h>
#include <limits.h>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include "common/video_frame_pool.hpp"

#include <vart/vart_device.hpp>
#include <vart/vart_inferresult_types.hpp>
#include <vart/vart_memory.hpp>
#include <vart/vart_memory_types.hpp>
#include <vart/vart_metaconvert.hpp>
#include <vart/vart_overlay.hpp>
#include <vart/vart_overlay_types.hpp>
#include <vart/vart_postprocess.hpp>
#include <vart/vart_postprocess_types.hpp>
#include <vart/vart_preprocess.hpp>
#include <vart/vart_preprocess_types.hpp>
#include <vart/vart_videoframe.hpp>
#include <vart/vart_videoframe_types.hpp>

#include <onnxruntime_cxx_api.h>

#include "common/app_logger.hpp"

/* To dump application input frames and inference input frames in bgr */
#define DUMP_INPUTS

/* TODO membank should come for infer */
#define DEFAULT_FRAME_MEMBANK 2
#define DEFAULT_DEVICE_INDEX 1
/* HLS HW accelerated image pre-processing IP */
#define DEFAULT_PREPROCESS_TYPE vart::PreProcessImplType::IMAGE_PROCESSING_HLS
/* XRT based buffer allocation */
#define DEFAULT_PREPROCESS_POOL_TYPE vart::VideoFrameImplType::XRT
/* OPENCV based ovelay */
#define DEFAULT_OVERLAY_TYPE vart::OverlayImplType::OPENCV
/* Process all frames in input_file */
#define APP_PROCESS_ALL_FRAMES -1
/* Default quantization factor */
#define DEFAULT_QUANT_FACTOR 1.0
/* Default output directory */
#define DEFAULT_OUTPUT_DIR "output"
using namespace std;
namespace pt = boost::property_tree;

/**
 * @brief Enumeration for application read status.
 */
typedef enum {
  /* Input file read operation was successful */
  APP_READ_SUCCESS = 0,
  /* End-of-file (EOF) reached */
  APP_EOF,
  /* Input file read operation encountered an error */
  APP_READ_FAILED
} AppReadStatus;

/**
 * @brief Enumeration for application video input formats.
 */
typedef enum {
  APP_VIDEO_INPUT_FORMAT_UNKNOWN = 0,
  APP_VIDEO_INPUT_FORMAT_JPEG,
  APP_VIDEO_INPUT_FORMAT_NV12,
  APP_VIDEO_INPUT_FORMAT_BGR
} AppVideoInputFormat;

/**
 * @brief Enumeration for application memory layouts.
 */
enum class MemoryLayout {
  UNKNOWN,
  NHW,
  NHWC,
  NCHW,
  NHWC4,
  NC4HW4,
  NC8HW8,
  HCWNC4,
  HCWNC8,
  GENERIC,
};

/**
 * @brief Enumeration for application data types.
 */
enum class DataType {
  UNKNOWN,
  INT8,
  UINT8,
  INT16,
  UINT16,
  BF16,
  FP16,
  INT32,
  UINT32,
  FLOAT32,
  INT64,
  UINT64,
};

/**
 * @brief Enumeration for tensor data types.
 */
struct TensorMeta {
  /* Name of the tensor */
  std::string name;
  /* Data type of the tensor */
  DataType data_type;
  /* Memory layout of the tensor */
  MemoryLayout memory_layout;
  /* Order of dimensions in memory layout */
  std::vector<uint32_t> memory_layout_order;
  /* Size of the tensor in number of elements */
  size_t size;
  /* Size of the tensor in bytes */
  size_t size_in_bytes;
  /* Shape of the tensor */
  std::vector<uint32_t> shape;
  /* Shape of the tensor */
  std::vector<int64_t> shape_i64;
  /* ONNX data type of the tensor */
  ONNXTensorElementDataType type;
};

/**
 * @brief Structure to hold information about inference tensors.
 */
typedef struct InferTensorInfo {
  /* OrtTensorInfo metadata */
  TensorMeta meta;
  /* Infer generate Quantized data, quantization_factor is used to quantized the
   * output of infer */
  uint32_t quantization_factor;
  /* ddr memory index on which physical buffer need to create */
  int mem_index;
} InferTensorInfo;

/**
 * @brief Structure to hold model configuration information.
 */
typedef struct {
  /* Input Width of model */
  int64_t model_width;
  /* Input Height of model */
  int64_t model_height;
  /* Input batchsize of the model */
  int64_t batch_size;
  /* Input channels of the model */
  int64_t channels;
  /* Common shape format of the model */
  string shape_format;
  /* OnnxRuntime default allocator */
  Ort::AllocatorWithDefaultOptions allocator;
  /* number of input tensor required by model */
  size_t num_in_tensors;
  /* number of output tensor required by model */
  size_t num_out_tensors;
  /* Input tensors information */
  vector<InferTensorInfo> in_tensors_info;
  /* Output tensors information */
  vector<InferTensorInfo> out_tensors_info;
  /* Vector of input tensors */
  std::vector<Ort::Value> input_tensors;
  /* Vector of output tensors */
  std::vector<Ort::Value> output_tensors;
  /* Vector of input tensors' names */
  std::vector<const char*> input_names;
  /* Vector of output tensors' names */
  std::vector<const char*> output_names;
} InferModelConf;

/**
 * @brief Structure to hold pipeline-specific context for each model.
 * Contains all the resources needed to run preprocess, inference, and
 * postprocess for one model.
 */
typedef struct {
  /* Pipeline identifier */
  int pipeline_id;

  /* Model configuration path */
  string model_path;
  string pipeline_config_json;

  /* Input/Output file paths for this pipeline */
  string input_file_path;
  string out_file_path;
  /* Output directory path for processed frames */
  string output_dir_path;

  /* Input file stream for reading from input_file_path */
  ifstream input_file;
  /* Output file stream for dumping output to output_file_path */
  ofstream output_file;

  /* Input video format and dimensions for this pipeline */
  AppVideoInputFormat input_fmt;
  uint32_t input_height;
  uint32_t input_width;

  /* Flag to do PanScan cropping while maintaining aspect-ratio for this
   * pipeline */
  bool do_pan_scan;

  /* Debug-specific file paths and stream (enabled with DUMP_INPUTS flag) */
#ifdef DUMP_INPUTS
  /* Path for input video dump in bgr */
  string dump_input_path;
  /* Path for dumped inference input in bgr */
  string dump_infer_input_path;
  ofstream dump_input_fp;
  ofstream dump_infer_input_fp;
#endif

  /* Pre-process context */
  bool preprocess_enable;
  vart::PreProcess* pre_process;
  vart::PreProcessInfo preprocess_info;

  /* Pre-process input memory bank */
  uint8_t ppe_mbank_in;

  /* Pre-process output memory bank */
  uint8_t ppe_mbank_out;

  /* Quantization scale factor */
  float quant_scale_factor;
  /* Is quant-scale-factor set in the json config? */
  bool quant_scale_factor_conf_set;

  /* Pre-process in pool */
  VideoFramePool* in_pool;

  /* Pre-process out pool */
  VideoFramePool* preprocess_out_pool;

  /* Post-process context */
  bool postprocess_enable;
  vart::PostProcess* post_process;
  /**
   * @brief Type of postprocessing.
   * @see vart::PostProcessType for full list of supported legacy and modular types.
   */
  vart::PostProcessType postprocess_type;
  string postprocess_type_str;

  /* Onnx Session context */
  std::unique_ptr<Ort::Session> ort_session;

  /* infer model configuration */
  InferModelConf model_info;

  /* VitisAI configuration file path for batch size configuration */
  string vitisai_config_file_path;

  /* Sometime NPU output are not one to one map with PL input,
   * tensor_mapping create mapping */
  unordered_map<string, int> tensor_mapping;
  bool dump_pl_results;
  bool dump_all_inputs;

  /* Meta convert context */
  bool metaconvert_enable;
  vart::MetaConvert* meta_convert;

  /* Overlay context */
  vart::Overlay* overlay;

  /*track frames_processed per pipeline*/
  uint32_t frames_processed;
  /* Benchmarking statistics for this pipeline */
  bool is_benchmark_enabled;
  float total_preprocess_time;
  float total_infer_time;
  float total_postprocess_time;
  float total_overlay_time;
  float total_time;

} PipelineContext;

/**
 * @brief Structure to hold the application context.
 * Contains common application-level settings and resources shared across all
 * pipelines.
 */
typedef struct {
  /* Log level for application */
  AppLogLevel log_level;

  /* Number of input frames to process */
  int64_t num_frame_to_process;

  string input_file_path;
  string config_json_path;
  string xclbin_location;

  /* Input video dimensions */
  uint32_t input_width;
  uint32_t input_height;

  /* Declare json string to hold json config */
  string json_str;

  /* Device context - shared across all pipelines */
  shared_ptr<vart::Device> device;
  int32_t device_idx;

  /* ONNX Runtime environment - shared across all pipelines */
  std::unique_ptr<Ort::Env> ort_env;

  /* Pipeline contexts - dynamic vector for each model */
  vector<PipelineContext> pipelines;

  /* Number of active pipelines */
  int num_active_pipelines;

  /* Number of models supported (set dynamically from JSON) */
  int num_models;

  bool dump_all_inputs;

  /* Maximum number of iterations to run */
  int64_t max_iterations;

  /* iteration counter */
  int64_t iteration_counter;

  /* Enable benchmarking */
  bool is_benchmark_enabled;
  /* Overall benchmarking statistics */
  float total_time;

  /* utiltimer configuration */
  bool timing_enable;                  // Enable/disable perf timing instrumentation.
  bool text_output_enable;             // Print human readable perf timing to given ostream.
  bool chrome_trace_json_enable;       // Write perf timing in Chrome trace json format.
  std::string chrome_trace_json_file;  // Chrome trace json file name.

} AppContext;

/**
 * @brief Initializes the application context.
 *
 * @param ctx A pointer to the AppContext structure.
 */
void init_app_context(AppContext* ctx);

/**
 * @brief Initialize a pipeline context with default values.
 *
 * @param pipeline Pointer to the pipeline context to initialize.
 * @param pipeline_id ID for this pipeline.
 */
void init_pipeline_context(PipelineContext* pipeline, int pipeline_id);

/**
 * @brief Creates all the necessary contexts for the application.
 *
 * This function creates all the required contexts for the application to run
 * properly. It initializes the AppContext structure pointed to by ctx.
 *
 * @param ctx A pointer to the AppContext structure.
 * @return True if all the contexts were successfully created, false otherwise.
 */
bool create_all_context(PipelineContext* pipeline_ctx,
                        AppLogLevel log_level,
                        shared_ptr<vart::Device> device,
                        const string& json_str,
                        Ort::Env* ort_env);

/**
 * @brief Creates context for a specific pipeline.
 *
 * @param ctx A pointer to the AppContext structure.
 * @param pipeline A pointer to the PipelineContext structure.
 * @return True if the pipeline context was successfully created, false
 * otherwise.
 */
bool create_pipeline_context(AppContext* ctx, PipelineContext* pipeline);

bool create_preprocess_context(PipelineContext* pipeline_ctx,
                               AppLogLevel log_level,
                               const string& json_str,
                               const shared_ptr<vart::Device>& device);
bool create_inference_context(PipelineContext* pipeline,
                              AppLogLevel log_level,
                              const string& json_str,
                              Ort::Env* ort_env);
bool create_postprocess_context(PipelineContext* pipeline_ctx,
                                AppLogLevel log_level,
                                vart::PostProcessType postprocess_type,
                                const string& json_str,
                                const shared_ptr<vart::Device>& device);

/**
 * @brief Destroys all contexts associated with the given AppContext.
 *
 * This function is responsible for destroying all contexts associated with the
 * given AppContext. It should be called when the application is finished using
 * the contexts to free up resources.
 *
 * @param ctx A pointer to the AppContext structure.
 */
void destroy_all_context(AppContext* ctx);

/**
 * @brief Destroys a specific pipeline context.
 *
 * @param pipeline A pointer to the PipelineContext structure.
 */
void destroy_pipeline_context(PipelineContext* pipeline);

/**
 * @brief Destroys all contexts associated with the given AppContext.
 *
 * This function is responsible for destroying all contexts associated with the
 * given AppContext. It should be called when the application is finished using
 * the contexts to free up resources.
 *
 * @param ctx A pointer to the AppContext structure.
 */
void destroy_all_context(AppContext* ctx);

string extract_component_json(const string& json_string, const string& component);

AppReadStatus read_input(PipelineContext* pipeline, AppLogLevel log_level, vart::VideoFrame* video_frame);

bool open_files(PipelineContext* pipeline,
                const string& output_dir_path,
                int64_t max_iterations,
                int64_t iteration_counter,
                AppLogLevel log_level,
                bool dump_all_inputs);

void close_files(PipelineContext* pipeline, AppLogLevel log_level);

bool dump_video_frame_as_jpeg(AppLogLevel log_level, ofstream& fp, shared_ptr<vart::VideoFrame> video_frame);

bool dump_video_frame(AppLogLevel log_level, ofstream& fp, shared_ptr<vart::VideoFrame> video_frame);

/**
 * @brief Preprocesses the input frame.
 *
 * This function takes an input frame, pre-processes it and gives the processed
 * result in the output frame.
 *
 * @param pipeline The pipeline context.
 * @param input_frame The input frame to be pre-processed.
 * @param output_frame The output frame to store the pre-processed frame.
 * @return True if the frame was successfully pre-processed , false otherwise.
 */
bool preprocess_process_frame(PipelineContext* pipeline,
                              AppLogLevel log_level,
                              shared_ptr<vart::VideoFrame> input_frame,
                              shared_ptr<vart::VideoFrame> output_frame);

/**
 * @brief Helper function to read device_batch_size from VitisAI config file.
 * @param vitisai_config_file Path to the VitisAI configuration file.
 * @param log_level Application log level for logging.
 * @return The device batch size read from config, or 1 as default.
 */
int64_t read_device_batch_size_from_config(const std::string& vitisai_config_file, AppLogLevel log_level);

/**
 * @brief Create a batched input tensor from multiple video frames.
 * @param log_level Application log level for logging.
 * @param tensor_info Information about the tensor to be created.
 * @param frames Vector of video frames to be batched.
 * @param start_idx Starting index in the frames vector.
 * @param batch_size Number of frames to include in the batch.
 * @param shape Target tensor shape including batch dimension.
 * @return Created batched ONNX Runtime tensor.
 */
Ort::Value create_batched_input_tensor(AppLogLevel log_level,
                                       const InferTensorInfo& tensor_info,
                                       vector<shared_ptr<vart::VideoFrame>>& frames,
                                       size_t start_idx,
                                       size_t batch_size,
                                       const vector<int64_t>& shape);

/**
 * @brief Processes frames for inference.
 *
 * This function performs inference on the given batch of frames.
 *
 * @param pipeline Pointer to the pipeline context.
 * @param current_batch_size The size of the current batch of frames to be
 * processed.
 * @param inputs A vector of shared pointers to the video frames to be
 * processed.
 * @param inference_out_tensors_memory A reference to a vector of vectors where
 * the output tensor data will be stored.
 * @return true if the processing is successful, false otherwise.
 */

bool infer_process_frames(PipelineContext* pipeline,
                          AppLogLevel log_level,
                          uint32_t current_batch_size,
                          vector<shared_ptr<vart::VideoFrame>> inputs,
                          vector<vector<shared_ptr<vart::Memory>>>& inference_out_tensors_memory,
                          int frame_index,
                          int64_t iteration_number,
                          int64_t max_iterations);

/**
 * @brief Post-processes the frames after inference.
 *
 * This function post-processes the output tensor data to interpret the
 * inference results.
 *
 * @param pipeline Pointer to the pipeline context.
 * @param current_batch The current batch number being processed.
 * @param inference_out_tensors_memory A reference to a vector of vectors
 * containing int8_t tensors.
 * @param num_frame_processed The number of frames that have been processed.
 * @return A vector of vectors containing shared pointers to InferResult
 * objects.
 */
vector<vector<shared_ptr<vart::InferResult>>> postprocess_process_frames(
    PipelineContext* pipeline_ctx,
    AppLogLevel log_level,
    uint32_t current_batch,
    vector<vector<shared_ptr<vart::Memory>>>& inference_out_tensors_memory,
    int64_t num_frame_processed);

/**
 * @brief Create VART::Memory required for all tensors.
 *
 * This function create memory for npu output and pl output tensors.
 *
 * @param pipeline_ctx Pointer to the pipeline context.
 * @param log_level Application log level.
 * @param device Device handle.
 * @param inference_out_tensors_memory A reference to a vector of vectors
 * containing input tensors memory.
 * @return True if the contexts were successfully created, false otherwise.
 */
bool create_out_tensor_memory(PipelineContext* pipeline_ctx,
                              AppLogLevel log_level,
                              shared_ptr<vart::Device> device,
                              vector<vector<shared_ptr<vart::Memory>>>& inference_out_tensors_memory);

vart::TensorDataType map_data_type(DataType d_type);
const char* to_string(vart::PostProcessType type);
