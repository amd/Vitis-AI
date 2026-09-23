/*
 * Copyright (C) 2024 Advanced Micro Devices, Inc.
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
 * EVENT SHALL XILINX BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 * OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. Except as contained in this notice, the name of the Xilinx shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from Xilinx.
 */

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <regex>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "video_frame_pool.hpp"
#include "postprocess/vvas_postprocess_bridge.hpp"
#include <cmath>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <dlfcn.h>
#include <limits.h>

#include <vart_ml_runner/vart_runner_factory.hpp>
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

/* To dump application input frames and inference input frames in bgr */
#define DUMP_INPUTS

/* TODO membank should come for infer */
#define DEFAULT_FRAME_MEMBANK 0
#define DEFAULT_DEVICE_INDEX 0
/* HLS HW accelerated image pre-processing IP */
#define DEFAULT_PREPROCESS_TYPE vart::PreProcessImplType::IMAGE_PROCESSING_HLS
/* XRT based buffer allocation */
#define DEFAULT_PREPROCESS_POOL_TYPE vart::VideoFrameImplType::XRT
/* OPENCV based ovelay */
#define DEFAULT_OVERLAY_TYPE vart::OverlayImplType::OPENCV
/* Process all frames in input_file */
#define APP_PROCESS_ALL_FRAMES -1

using namespace std;
namespace pt = boost::property_tree;

enum AppLogLevel {
  APP_LOG_LEVEL_NONE = 0,
  APP_LOG_LEVEL_ERROR,
  APP_LOG_LEVEL_WARNING,
  /* Display Infer result alone with Warn and error */
  APP_LOG_LEVEL_RESULT,
  APP_LOG_LEVEL_INFO,
  APP_LOG_LEVEL_DEBUG
};

#define APP_LOG_MESSAGE(level, set_level, ...)                                     \
  do {                                                                         \
    const char *tag = nullptr;                                                 \
    switch (level) {                                                           \
    case APP_LOG_LEVEL_ERROR:                                                      \
      tag = "ERROR";                                                           \
      break;                                                                   \
    case APP_LOG_LEVEL_WARNING:                                                    \
      tag = "WARNING";                                                         \
      break;                                                                   \
    case APP_LOG_LEVEL_RESULT:                                                     \
      tag = "RESULT";                                                          \
      break;                                                                   \
    case APP_LOG_LEVEL_INFO:                                                       \
      tag = "INFO";                                                            \
      break;                                                                   \
    case APP_LOG_LEVEL_DEBUG:                                                      \
      tag = "DEBUG";                                                           \
      break;                                                                   \
    default:                                                                   \
      tag = "UNKNOWN";                                                         \
      break;                                                                   \
    }                                                                          \
    if (set_level >= level) {                                                  \
      printf("[%s] %s:%d  ", tag, __FILE__, __LINE__);                         \
      printf(__VA_ARGS__);                                                     \
      printf("\n");                                                            \
    }                                                                          \
  } while (0)

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
  APP_VIDEO_INPUT_FORMAT_MP4,
  APP_VIDEO_INPUT_FORMAT_JPEG,
  APP_VIDEO_INPUT_FORMAT_NV12,
  APP_VIDEO_INPUT_FORMAT_RGB,
  APP_VIDEO_INPUT_FORMAT_BGR,
  APP_VIDEO_INPUT_FORMAT_BGR_FLOAT,
  APP_VIDEO_INPUT_FORMAT_RGBX,
  APP_VIDEO_INPUT_FORMAT_BGRX
} AppVideoInputFormat;

typedef struct InferTensorInfo {
  /* Infer generate Quantized data, quantization_factor is used to quantized the
   * output of infer */
  float quantization_factor;
  /* Tensor metadata */
  vart::NpuTensorInfo meta;
} InferTensorInfo;

/* Contains information related to model configuration */
typedef struct {
  /* Width of model extract from snapshot */
  uint32_t model_width;
  /* Height of model extract from snapshot */
  uint32_t model_height;
  /* Batch with which snapshot is created */
  uint32_t batch_size;
  /* number of input tensor required by model */
  size_t num_in_tensors;
  /* number of output tensor generated by model */
  size_t num_out_tensors;
  /* Input tensor information */
  vector<InferTensorInfo> in_tensors;
  /* Output tensor information */
  vector<InferTensorInfo> out_tensors;
} InferModelConf;

typedef struct {
  /* Log level for application */
  AppLogLevel log_level;

  /* Flag to do PanScan cropping while maintaining aspect-ratio */
  bool do_pan_scan;

  /* Number of input frames to process */
  int64_t num_frame_to_process;

  /* Paths and indices */
  string config_json_path;
  string snap_path;
  string xclbin_location;

  /* To decide on native/model tensor format */
  int use_native_output_format;

  /* File name and path of input file */
  string input_file_path;
  /* Output file dump path for processed frames with detection drawing */
  string out_file_path;

  /* Input file stream for reading from input_file_path */
  ifstream input_file;
  /* Output file stream for dumping output to output_file_path */
  ofstream output_file;
  /* Debug-specific file paths and stream (enabled with DUMP_INPUTS flag) */
#ifdef DUMP_INPUTS
  /* Path for input video dump in bgr */
  string dump_input_path;

  /* Path for dumped inference input in bgr */
  string dump_infer_input_path;

  ofstream dump_input_fp;
  ofstream dump_infer_input_fp;
#endif

  /* OpenCV VideoCapture for video input */
  cv::VideoCapture *vid_capture;

  /* Dimensions of input frames */
  uint32_t input_height;
  uint32_t input_width;
  uint32_t in_frame_size;

  /* Input video format */
  AppVideoInputFormat input_fmt;

  /* Declare json string to hold json config */
  string json_str;

  /* Device context */
  shared_ptr<vart::Device> device;
  int32_t device_idx;

  /* Pre-process context */
  bool preprocess_enable;
  vart::PreProcess *pre_process;
  vart::PreProcessInfo preprocess_info;

  /* Pre-process input memory bank */
  vector<uint8_t> ppe_mbank_in;

  /* Pre-process output memory bank */
  vector<uint8_t> ppe_mbank_out;

  /* Pre-process in pool */
  VideoFramePool *in_pool;

  /* Pre-process out pool */
  VideoFramePool *preprocess_out_pool;

  /* Post-process context */
  bool postprocess_enable;
  vart::PostProcess *post_process;
  vart::PostProcessType postprocess_type;
  vector<uint8_t> post_process_mem_banks;

  /* App-side vvas_core postprocess C-API path (resnet50, yolox) — invoked via
   * vvas_postprocess_create by .so path instead of the vart_x dispatch, so
   * src/vart_x stays untouched. When use_vvas_pp_api is true, post_process is
   * unused and vvas_pp_bridge drives the model .so. */
  bool use_vvas_pp_api;
  string vvas_pp_lib_path;
  xplusml::VvasPPResultKind vvas_pp_result_kind;
  xplusml::VvasPostProcessBridge *vvas_pp_bridge;

  /* vart Npu runner context */
  shared_ptr<vart::Runner> runner;

  /* infer model configuration */
  InferModelConf model_info;

  bool dump_all_inputs;
  
  bool dump_npu_output;

  /* Meta convert context */
  bool metaconvert_enable;
  vart::MetaConvert *meta_convert;

  /* Overlay context */
  vart::Overlay *overlay;

  /* Maximum number of iterations to run */
  int64_t max_iterations;

  /* iteration counter */
  int64_t iteration_counter;

  /* Execute CPU subgraph */
  bool exec_cpu_subgraph;

  /* Snapshot id */
  int32_t snap_id;

  /* Multiple instance */
  bool is_multi_instance;
} AppContext;

/**
 * @brief Initializes the application context.
 *
 * @param ctx A pointer to the AppContext structure.
 */
void init_app_context(AppContext *ctx);

/**
 * @brief Creates all the necessary contexts for the application.
 *
 * This function creates all the required contexts for the application to run
 * properly. It initializes the AppContext structure pointed to by ctx.
 *
 * @param ctx A pointer to the AppContext structure.
 * @return True if all the contexts were successfully created, false otherwise.
 */
bool create_all_context(AppContext *ctx);

bool create_preprocess_context(AppContext *ctx);
bool create_inference_context(AppContext *ctx);
bool create_postprocess_context(AppContext *ctx);

/**
 * @brief Destroys all contexts associated with the given AppContext.
 *
 * This function is responsible for destroying all contexts associated with the
 * given AppContext. It should be called when the application is finished using
 * the contexts to free up resources.
 *
 * @param ctx A pointer to the AppContext structure.
 */
void destroy_all_context(AppContext *ctx);

string extract_component_json(const string &json_string,
                              const string &component);

AppReadStatus read_input(AppContext *ctx, vart::VideoFrame *video_frame);

bool open_files(AppContext *ctx);

void close_files(AppContext *ctx);

bool dump_video_frame(AppContext *ctx, ofstream &fp,
                      shared_ptr<vart::VideoFrame> video_frame);

/**
 * @brief Preprocesses the input frame.
 *
 * This function takes an input frame, pre-processes it and gives the processed
 * result in the output frame.
 *
 * @param ctx The application context.
 * @param input_frame The input frame to be pre-processed.
 * @param output_frame The output frame to store the pre-processed frame.
 * @return True if the frame was successfully pre-processed , false otherwise.
 */
bool preprocess_process_frame(AppContext *ctx,
                              shared_ptr<vart::VideoFrame> input_frame,
                              shared_ptr<vart::VideoFrame> output_frame);

/**
 * @brief Processes frames for inference.
 *
 * This function performs inference on the given batch of frames.
 *
 * @param ctx Pointer to the application context.
 * @param current_batch_size The size of the current batch of frames to be
 * processed.
 * @param inputs A vector of shared pointers to the video frames to be
 * processed.
 * @param npu_out_tensors_memory A reference to a vector of vectors where the
 * output tensor data will be stored.
 * @return true if the processing is successful, false otherwise.
 */

bool infer_process_frames(
    AppContext *ctx, uint32_t current_batch_size,
    vector<shared_ptr<vart::VideoFrame>> inputs,
    vector<vector<shared_ptr<vart::Memory>>> &npu_out_tensors_memory);

/**
 * @brief Post-processes the frames after inference.
 *
 * This function post-processes the output tensor data to interpret the
 * inference results.
 *
 * @param ctx Pointer to the application context.
 * @param current_batch The current batch number being processed.
 * @param npu_out_tensors_memory A reference to a vector of vectors containing
 * int8_t tensors.
 * @param num_frame_processed The number of frames that have been processed.
 * @return A vector of vectors containing shared pointers to InferResult
 * objects.
 */
vector<vector<shared_ptr<InferResult>>> postprocess_process_frames(
    AppContext *ctx, uint32_t current_batch,
    vector<vector<shared_ptr<vart::Memory>>> &npu_out_tensors_memory,
    int64_t num_frame_processed);

/**
 * @brief Create VART::Memory required for all tensors.
 *
 * This function create memory for npu output tensors.
 *
 * @param ctx Pointer to the application context.
 * @param npu_out_tensors_memory A reference to a vector of vectors containing
 * input tensors memory.
 * @return True if the contexts were successfully created, false otherwise.
 */
bool create_out_tensor_memory(
    AppContext *ctx,
    vector<vector<shared_ptr<vart::Memory>>> &npu_out_tensors_memory);

/* Get the file extension from a given filename */
string get_file_extension(const string &filename);