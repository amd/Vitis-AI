/*
 * Copyright (C) 2024-2025 Advanced Micro Devices, Inc.
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

#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <getopt.h>
#include <map>
#include <unordered_set>
#include <future>

#include "x_plus_ml_app.hpp"

/* Map input json string to Vart VideoFormat */
static vart::VideoFormat get_vart_video_format(const string &fmt) {

  if (fmt.compare(0, 9, "RGBX_BF16") == 0)
    return VideoFormat::RGBx_BF16;
  else if (fmt.compare(0, 9, "BGRX_BF16") == 0)
    return VideoFormat::BGRx_BF16;
  else if (fmt.compare(0, 9, "BGR_FLOAT") == 0)
    return VideoFormat::BGR_FLOAT;
  else if (fmt.compare(0, 9, "RGB_FLOAT") == 0)
    return VideoFormat::RGB_FLOAT;
  else if (fmt.compare(0, 10, "RGBP_FLOAT") == 0)
    return VideoFormat::RGBP_FLOAT;
  else if (fmt.compare(0, 4, "RGBX") == 0)
    return VideoFormat::RGBx;
  else if (fmt.compare(0, 4, "BGRX") == 0)
    return VideoFormat::BGRx;
  else if (fmt.compare(0, 3, "RGB") == 0)
    return VideoFormat::RGB;
  else if (fmt.compare(0, 3, "BGR") == 0)
    return VideoFormat::BGR;
  else if (fmt.compare(0, 9, "Y_UV8_420") == 0)
    return VideoFormat::Y_UV8_420;
  else
    return VideoFormat::UNKNOWN;
}

/* Map Vart VideoFormat to AppVideoInputFormat in string format
 * As JPEG and MP4 decoder return BGR output, the function return accordingly
 */
static string map_input_fmt_to_vart_fmt_string(AppVideoInputFormat app_fmt) {
  string fmt_str;
  switch (app_fmt) {
  case APP_VIDEO_INPUT_FORMAT_MP4:
  case APP_VIDEO_INPUT_FORMAT_JPEG:
  case APP_VIDEO_INPUT_FORMAT_BGR:
    fmt_str = "BGR";
    break;
  case APP_VIDEO_INPUT_FORMAT_NV12:
    fmt_str = "NV12";
    break;
  case APP_VIDEO_INPUT_FORMAT_RGB:
    fmt_str = "RGB";
    break;
  case APP_VIDEO_INPUT_FORMAT_BGR_FLOAT:
    fmt_str = "BGR_FLOAT";
    break;
  case APP_VIDEO_INPUT_FORMAT_RGBX:
    fmt_str = "RGBX";
    break;
  case APP_VIDEO_INPUT_FORMAT_BGRX:
    fmt_str = "BGRX";
    break;
  default:
    fmt_str = "UNKNOWN";
    break;
  }
  return fmt_str;
}

/* Print help text for the command-line options */
static void print_help_text(char *pn) {
  /* Display usage information and available options */
  cout << "Usage: " << pn << " [OPTIONS]" << endl;
  cout << "  -i      Input file path (mandatory)" <<  endl;
  cout << "  -c      Config file path (mandatory)" << endl;
  cout << "  -s      Snapshot path (mandatory)" << endl;
  cout << "  -o      Output file path (optional)" << endl;
  cout << "\t\t If provided, inference results overlayed on the frame and "
         "dumped into this file." << endl;
  cout << "  -n      Number of frames to process (optional, default is to "
         "process all frames)" << endl;
  cout << "  -l      Application log level to print logs (optional, default is "
         "ERROR and WARNING)." << endl;
  cout << "\t\t Accepted log levels: 1 for ERROR, 2 for WARNING, 3 for "
         "INFERENCE RESULT, 4 for INFO, 5 for DEBUG." << endl;
  cout << "\t\t Logs at the provided level and all levels below will be "
         "printed." << endl;
  cout << "  -r      Dump NPU output tensors to files (optional, default is "
         "disabled)" << endl;
  cout <<
      "  -d      WidthxHeight of the input (mandatory for raw input files)" << endl;
  cout << "\t\t (required only in case of nv12 input, Ex : 224x224)" << endl;
  cout << "  -h      Print this help and exit" << endl;
  cout << endl;
  cout << "\tSample CLIs :- " << endl;
  cout << "\t\tSingle snapshot execution :- \"x_plus_ml_app -i dog.jpg -c /etc/vai/json-config/resnet50.json -s snapshot.resnet50 -l 3\"" << endl;
  cout << "\t\tMulti snapshots execution :- \"x_plus_ml_app -i dog.jpg+dog.jpg -c /etc/vai/json-config/resnet50.json+/etc/vai/json-config/resnet50.json -s snapshot.resnet50+snapshot.resnet50 -l 3+3\"" << endl;
}

string extract_component_json(const string &json_string,
                              const string &component) {
  try {
    pt::ptree config;
    istringstream iss(json_string);
    pt::read_json(iss, config);
    pt::ptree specificConfig = config.get_child(component);
    ostringstream oss;
    pt::write_json(oss, specificConfig);
    return oss.str();
  } catch (const exception &e) {
    cerr << "Error parsing " << component << " config: " << e.what() << endl;
    return "{}";
  }
}

static bool parse_preprocess_config(AppContext *ctx, pt::ptree &config) {
  bool maintain_aspect_ratio = false;
  string preprocess_out_fmt_str, resizing_type_str;
  PreProcessInfo *preprocess_info = &ctx->preprocess_info;
  AppLogLevel log_level = ctx->log_level;
  ctx->preprocess_enable = true;
  preprocess_info->mean_r = config.get<float>("preprocess-config.mean-r");
  preprocess_info->mean_g = config.get<float>("preprocess-config.mean-g");
  preprocess_info->mean_b = config.get<float>("preprocess-config.mean-b");
  preprocess_info->scale_r = config.get<float>("preprocess-config.scale-r");
  preprocess_info->scale_g = config.get<float>("preprocess-config.scale-g");
  preprocess_info->scale_b = config.get<float>("preprocess-config.scale-b");
  preprocess_out_fmt_str =
      config.get<string>("preprocess-config.colour-format");
  preprocess_info->colour_format =
      get_vart_video_format(preprocess_out_fmt_str);
  if (preprocess_info->colour_format == VideoFormat::UNKNOWN) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                "Unknown preprocessing Video Format: %s",
                preprocess_out_fmt_str.c_str());
    return false;
  }

  /* Get maintain_aspect_ratio value and perform corresponding resizing
   * technique based on the resizing-type value provided */
  maintain_aspect_ratio =
      config.get<bool>("preprocess-config.maintain-aspect-ratio", false);
  if (maintain_aspect_ratio) {
    if (!config.get_child("preprocess-config").count("resizing-type")) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                  "Please provide resizing-type to maintain-aspect-ratio. "
                  "Valid values are LETTERBOX / PANSCAN");
      return false;
    }
    resizing_type_str =
        config.get<string>("preprocess-config.resizing-type");
    if (resizing_type_str.compare(0, 7, "PANSCAN") == 0) {
      preprocess_info->preprocess_type = PreProcessType::DEFAULT;
      ctx->do_pan_scan = true;
    } else if (resizing_type_str.compare(0, 9, "LETTERBOX") == 0) {
      preprocess_info->preprocess_type = PreProcessType::LETTERBOX;
      preprocess_info->symmetric_padding =
          config.get<bool>("preprocess-config.symmetric-padding", false);
    } else {
      APP_LOG_MESSAGE(
          APP_LOG_LEVEL_ERROR, log_level,
          "Unknown resizing-type: %s. Valid values are LETTERBOX / PANSCAN",
          resizing_type_str.c_str());
      return false;
    }
  } else {
    /* Use default preprocess type if maintain-aspect-ratio is not provided */
    preprocess_info->preprocess_type = PreProcessType::DEFAULT;
  }

  /* Read the input and output memory bank indices for pre-processing module */
  ctx->ppe_mbank_in.push_back (config.get<uint8_t>("preprocess-config.in-mem-bank"));
  auto &pre_proc = config.get_child("preprocess-config");
  if (pre_proc.find ("out-mem-banks") != pre_proc.not_found()) {
    auto & array = pre_proc.get_child("out-mem-banks");
    for (auto & value: array) {
      int mem_index = std::stoi(value.second.data());
      ctx->ppe_mbank_out.push_back(mem_index);
    }
  } else {
    APP_LOG_MESSAGE (APP_LOG_LEVEL_ERROR, log_level, "preprocess-config.out-mem-banks not found.");
    return false;
  }

  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "mean-r: %f",
                preprocess_info->mean_r);
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "mean-g: %f",
              preprocess_info->mean_g);
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "mean-b: %f",
              preprocess_info->mean_b);
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "scale-r: %f",
              preprocess_info->scale_r);
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "scale-g: %f",
              preprocess_info->scale_g);
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "scale-b: %f",
              preprocess_info->scale_b);
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "colour-format: %s",
              preprocess_out_fmt_str.c_str());
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "maintain-aspect-ratio: %d",
              maintain_aspect_ratio);
  if (maintain_aspect_ratio) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "resizing-type: %s",
                resizing_type_str.c_str());
  }
  if (preprocess_info->preprocess_type == PreProcessType::LETTERBOX) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "symmetric-padding: %d",
                preprocess_info->symmetric_padding);
  }
  return true;
}

static bool parse_input_config(AppContext *ctx, pt::ptree &config) {
  AppLogLevel log_level = ctx->log_level;
  auto &input_config = config.get_child("input-config");
  if (input_config.find("mem-banks") != input_config.not_found()) {
    auto & array = input_config.get_child("mem-banks");
    for (auto & value: array) {
      int mem_index = std::stoi(value.second.data());
      ctx->ppe_mbank_in.push_back(mem_index);
    }
  } else {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "input-config.in-mem-bank not found.");
    return false;
  }
  if (input_config.find("in-format") != input_config.not_found()) {
    auto fmt = input_config.get<string>("in-format");
    if(fmt.compare(0, 4, "RGBX") == 0) {
      ctx->input_fmt = APP_VIDEO_INPUT_FORMAT_RGBX;
    } else if (fmt.compare(0, 3, "RGB") == 0) {
      ctx->input_fmt = APP_VIDEO_INPUT_FORMAT_RGB;
    } else if (fmt.compare(0, 4, "BGRX") == 0) {
      ctx->input_fmt = APP_VIDEO_INPUT_FORMAT_BGRX;
    } else if (fmt.compare(0, 3, "BGR") == 0) {
      ctx->input_fmt = APP_VIDEO_INPUT_FORMAT_BGR;
    } else {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Invalid input-config.in-format: %s", fmt.c_str());
      return false;
    }
  } else {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "input-config.in-format not found.");
    return false;
  }
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "input-config.in-format: %s",
              map_input_fmt_to_vart_fmt_string(ctx->input_fmt).c_str());
  return true;
}


static bool parse_postprocess_config(AppContext *ctx, pt::ptree &config) {
  string postprocess_type_str = config.get<string>("postprocess-config.type");
  AppLogLevel log_level = ctx->log_level;
  ctx->postprocess_enable = true;

  /* Get mem-banks for Post processing */
  auto &post_proc = config.get_child("postprocess-config");
  if (post_proc.find("mem-banks") != post_proc.not_found()) {
    auto & array = post_proc.get_child("mem-banks");
    for (auto & value: array) {
      int mem_index = std::stoi(value.second.data());
      ctx->post_process_mem_banks.push_back(mem_index);
    }
  } else {
    APP_LOG_MESSAGE (APP_LOG_LEVEL_ERROR, log_level, "postprocess-config.mem-banks not found");
    return false;
  }

  /* RESNET50 and YOLOX are handled app-side via the vvas_core postprocess
   * C API (dlopen the x_plus_ml-built .so by path) so that src/vart_x — shared
   * across gen1/gen2 — is not modified. YOLOV2 and SSDRESNET34 keep using the
   * vart_x PostProcess dispatch (status quo). */
  ctx->use_vvas_pp_api = false;
  if (postprocess_type_str == "RESNET50") {
    ctx->use_vvas_pp_api = true;
    ctx->vvas_pp_lib_path = "/usr/lib/libpostprocess_resnet50-1.0.so";
    ctx->vvas_pp_result_kind = xplusml::VvasPPResultKind::CLASSIFICATION;
  } else if (postprocess_type_str == "YOLOX") {
    ctx->use_vvas_pp_api = true;
    ctx->vvas_pp_lib_path = "/usr/lib/libpostprocess_yolo-1.0.so";
    ctx->vvas_pp_result_kind = xplusml::VvasPPResultKind::DETECTION;
  } else if (postprocess_type_str == "YOLOV2") {
    ctx->postprocess_type = vart::PostProcessType::YOLOV2;
  } else if (postprocess_type_str == "SSDRESNET34") {
    ctx->postprocess_type = vart::PostProcessType::SSDRESNET34;
  } else {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                "Invalid postprocess_type_str: %s",
                postprocess_type_str.c_str());
    return false;
  }

  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "postprocess-config.type: %s",
              postprocess_type_str.c_str());
  return true;
}

/* Parse the Json to get xclbin location which is required to create device
 * context and model name which will help to find which post-process need to
 * call
 */
static bool parse_json_config(AppContext *ctx) {
  if (!ctx || ctx->config_json_path.empty())
    return false;

  AppLogLevel log_level = ctx->log_level;
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Parsing configuration json file");

  /* Read the JSON string from the file */
  ifstream fileStream(ctx->config_json_path);
  if (!fileStream.is_open()) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Error opening file: %s",
                ctx->config_json_path.c_str());
    return false;
  }

  /* Read the entire file into the global variable json_str */
  ctx->json_str = string((istreambuf_iterator<char>(fileStream)),
                         istreambuf_iterator<char>());

  try {
    pt::ptree config;
    istringstream iss(ctx->json_str);
    pt::read_json(iss, config);

    bool has_preprocess_config = static_cast<bool>(config.get_child_optional("preprocess-config"));
    bool has_input_config      = static_cast<bool>(config.get_child_optional("input-config"));

    if (has_preprocess_config && has_input_config) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                  "Either preprocess-config or input-config may be provided, not both.");
      return false;
    }

    /* Extract "xclbin-location" from "root" json config */
    ctx->xclbin_location = config.get<string>("xclbin-location");
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "xclbin-location: %s", ctx->xclbin_location.c_str());

    /* Extract "use-native-output-format" from "root" json config */
    ctx->use_native_output_format = config.get<int>("use-native-output-format", 0);
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "use-native-output-format: %d", ctx->use_native_output_format);

    /* Extract "exec-cpu-subgraph" from "root" json config */
    ctx->exec_cpu_subgraph = config.get<bool>("exec-cpu-subgraph", false);
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "exec-cpu-subgraph: %d", ctx->exec_cpu_subgraph);

    if (ctx->exec_cpu_subgraph && (0 != ctx->use_native_output_format)) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
        "exec-cpu-subgraph is supported only with use-native-output-format = 0");
      return false;
    }

    /* Extract Pre-process info from "preprocess-config" */
    if (has_preprocess_config && !parse_preprocess_config(ctx, config)) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Failed to parse preprocess-config.");
      return false;
    }
    /* Extract Input config from "input-config" */
    if (has_input_config && !parse_input_config(ctx, config)) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Failed to parse input-config.");
      return false;
    }
    /* Extract Post-process info from "postprocess-config" */
    if (config.get_child_optional("postprocess-config") && !parse_postprocess_config(ctx, config)) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Failed to parse postprocess-config.");
      return false;
    }
    /* Check for plkernel-config presence in json config */
    if (config.get_child_optional("plkernel-config")) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "CPU subgraphs on PL is no more supported. Modify JSON config to run subgraphs on NPU/CPU");
      return false;
    }
    /* Check for metaconvert-config presence in json config */
    if (config.get_child_optional("metaconvert-config"))
      ctx->metaconvert_enable = true;

    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "xclbin-location: %s",
                ctx->xclbin_location.c_str());

  } catch (const exception &e) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Error reading config: Reason: %s",
                e.what());
    return false;
  }
  return true;
}

/* Transform the post-processed prediction back to the original resolution of
 * input frame */
bool transform_infer_result(AppContext *ctx,
                            vector<shared_ptr<vart::InferResult>> &root_res) {
  const vector<shared_ptr<vart::InferResult>> &result =
      (root_res.back())->get_children();

  AppLogLevel log_level = ctx->log_level;
  InferResScaleInfo info = {};
  info.model_input_width = ctx->model_info.model_width;
  info.model_input_height = ctx->model_info.model_height;
  info.input_frame_width = ctx->input_width;
  info.input_frame_height = ctx->input_height;
  /* LETTERBOX keeps aspect ratio via symmetric padding, so bbox mapping needs an
   * equal scale factor on both axes. The shared transform divides input_frame by
   * model dims per-axis; feed it adjusted frame dims (max-scale * model dim) so
   * the effective scale is equal. App-only: the shared vart_x transform is
   * untouched. */
  if (ctx->preprocess_info.preprocess_type == vart::PreProcessType::LETTERBOX) {
    float ws = static_cast<float>(ctx->input_width) / ctx->model_info.model_width;
    float hs = static_cast<float>(ctx->input_height) / ctx->model_info.model_height;
    float s = std::max(ws, hs);
    info.input_frame_width =
        static_cast<uint32_t>(nearbyintf(s * ctx->model_info.model_width));
    info.input_frame_height =
        static_cast<uint32_t>(nearbyintf(s * ctx->model_info.model_height));
  }
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Model input width %u",
              info.model_input_width);
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Model input height %u",
              info.model_input_height);
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Input frame width %u",
              info.input_frame_width);
  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Input frame height %u",
              info.input_frame_height);



  APP_LOG_MESSAGE(APP_LOG_LEVEL_INFO, log_level, "Results after transform:");
  for (auto &itr : result) {
    try {
      itr->transform(info);
      InferResultData *base_infer_result = itr->get_infer_result();
      if (base_infer_result->result_type == vart::InferResultType::DETECTION) {
        DetectionResData *infer_result =
            static_cast<DetectionResData *>(base_infer_result);
        APP_LOG_MESSAGE(APP_LOG_LEVEL_INFO, log_level,
                    "Detection bbox  x : %u y : %u width  : %u height : %u and "
                    "label : %s",
                    infer_result->x, infer_result->y, infer_result->width,
                    infer_result->height, infer_result->label.c_str());
      }
    } catch (const exception &e) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Error in transform function: %s",
                  e.what());
      return false;
    }
  }
  return true;
}

/* Draw inference result on the input video buffer using overlay */
bool draw_infer_result(AppContext *ctx,
                       const shared_ptr<vart::InferResult> &root_result,
                       shared_ptr<vart::VideoFrame> input_frame) {
  AppLogLevel log_level = ctx->log_level;
  shared_ptr<OverlayShapeInfo> shape_info = {};

  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
              "Convert inference metadata to overlay metadata");
  try {
    shape_info = ctx->meta_convert->prepare_overlay_meta(root_result);
  } catch (const exception &e) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                "Error in prepare overlay metadata: %s", e.what());
    return false;
  }

  APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Draw overlay data on frame");
  try {
    ctx->overlay->draw_overlay(*input_frame, *shape_info);
  } catch (const exception &e) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Error in drawing overlay: %s",
                e.what());
    return false;
  }

  return true;
}

bool write_tensors_to_files(
    AppContext *ctx,
    const vector<vector<shared_ptr<vart::Memory>>> &npu_out_tensors_memory) {
  AppLogLevel log_level = ctx->log_level;

  /* Write input tensors to files */
  if (ctx->dump_all_inputs || ctx->dump_npu_output) {
    for (size_t b = 0; b < npu_out_tensors_memory.size(); ++b) {
      for (size_t i = 0; i < npu_out_tensors_memory[b].size(); ++i) {
        string filename = "/tmp/app_npu_output" + to_string(i) + "_" +
                          to_string(b) + "_" +
                          to_string(ctx->iteration_counter) + "_snap_" + to_string(ctx->snap_id) + ".bin";
        ofstream outfile(filename, ios::binary);
        APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
                    "Write NPU out tensor %ld_%ld to %s", b, i,
                    filename.c_str());
        if (!outfile.is_open()) {
          APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                      "Failed to open file %s for writing",
                      filename.c_str());
          return false;
        }

        const unsigned char *mapped_memory =
            npu_out_tensors_memory[b][i]->map(vart::DataMapFlags::READ);

        outfile.write(reinterpret_cast<const char *>(mapped_memory),
                      ctx->model_info.out_tensors[i].meta.size_in_bytes);

        outfile.close();

        npu_out_tensors_memory[b][i]->unmap();
      }
    }
  }
  return true;
}

int single_instance_execution(AppContext &ctx) {
  try {
  AppReadStatus read_status = APP_READ_SUCCESS;
  int64_t num_frame_processed = 0;
  bool print_iteration_info = false;
  float total_inference_time = 0.0;
  float total_npu_time = 0.0;
  int ret = -1;

  /* Vector to hold output tensors for post-processing */
  vector<vector<shared_ptr<vart::Memory>>> npu_out_tensors_memory;

  /* Set log level based on handle configuration */
  AppLogLevel log_level = ctx.log_level;
  if (create_out_tensor_memory(&ctx, npu_out_tensors_memory) != true) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                "Unable to create tensor out memory");
    goto killall;
  }

  /* Main loop for video processing until end-of-file */
  while (read_status != APP_EOF) {
    /* Counter for the number of frames read */
    uint32_t frame_read = 0;
    /* Number of frames to read in the current iteration */
    uint32_t frames_to_read;
    vector<vector<shared_ptr<InferResult>>> inference_results;
    if (ctx.num_frame_to_process != APP_PROCESS_ALL_FRAMES) {
      /* Calculate the remaining frames to read in this iteration, considering
       * the batch size and the frames already processed */
      uint64_t remaining_frames_to_process =
          ctx.num_frame_to_process - num_frame_processed;
      frames_to_read = (remaining_frames_to_process < ctx.model_info.batch_size)
                           ? (remaining_frames_to_process)
                           : ctx.model_info.batch_size;
    } else {
      /* If processing all frames, set frames_to_read equal to the batch size
       */
      frames_to_read = ctx.model_info.batch_size;
    }

    /* Maintain an array of input frames until overlaying predictions on them
     * and then dump the results into a file. */
    vector<shared_ptr<vart::VideoFrame>> input_frames(frames_to_read);
    vector<shared_ptr<vart::VideoFrame>> preprocess_out_frames(frames_to_read);

    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
                "***** Start of new iteration *****");
    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
                "frame to read in this iteration = %d", frames_to_read);

    /* Perform operation per frame */
    /* Loop over the frames to be read for a batch or any remaining frames at
     * the end of file as per frames_to_read */
    for (uint32_t frm_idx = 0; frm_idx < frames_to_read; frm_idx++) {
      /* Acquire a buffer from the pre-process input pool */
    read_more:
      input_frames[frm_idx] = ctx.in_pool->acquire_frame();
      if (!input_frames[frm_idx]) {
        APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                    "Failed to acquire buffer from pre-process pool");
        goto killall;
      }
      APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
                  "Acquired buffer from input pool for frame index %d",
                  frm_idx);

      if (ctx.preprocess_enable) {
        /* Acquire a frame from the pre-process output pool */
        preprocess_out_frames[frm_idx] =
            ctx.preprocess_out_pool->acquire_frame();
        if (!preprocess_out_frames[frm_idx]) {
          APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                      "Failed to acquire buffer from output pre-process pool");
          goto killall;
        }
        APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
                    "Acquired frame from output pool");
      } else {
        /* As preprocess data is read from file, preprocess output frame is same
         * as of read input */
        preprocess_out_frames[frm_idx] = input_frames[frm_idx];
        APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
                    "Preprocess output frame is same as input");
      }

      /* Read input data directly to pre-process input buffer */
      read_status = read_input(&ctx, input_frames[frm_idx].get());
      if (read_status == APP_READ_SUCCESS) {
        APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Successfully read input data");

#ifdef DUMP_INPUTS
        if (ctx.dump_all_inputs) {
          /* dump the input frame to pre-process */
          APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
                      "Dumping input for pre-process");
          if (dump_video_frame(&ctx, ctx.dump_input_fp, input_frames[frm_idx]) != true) {
            ctx.in_pool->release_frame(input_frames[frm_idx]);
            if (ctx.preprocess_enable)
              ctx.preprocess_out_pool->release_frame(preprocess_out_frames[frm_idx]);
            APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Failed to dump input");
            goto killall;
          }
        }
#endif

        if (ctx.preprocess_enable) {
          /* Perform the pre-process step */
          if (preprocess_process_frame(&ctx, input_frames[frm_idx],
                                       preprocess_out_frames[frm_idx]) !=
              true) {
            ctx.in_pool->release_frame(input_frames[frm_idx]);
            ctx.preprocess_out_pool->release_frame(preprocess_out_frames[frm_idx]);
            goto killall;
          }
        }

#ifdef DUMP_INPUTS
        if (ctx.dump_all_inputs) {
          /* dump the pre-processed frame to a file */
          APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Dumping output of pre-process");
          if (dump_video_frame(&ctx, ctx.dump_infer_input_fp,
                               preprocess_out_frames[frm_idx]) != true) {
            APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Failed to dump pre-process output");
            ctx.in_pool->release_frame(input_frames[frm_idx]);
            if (ctx.preprocess_enable)
              ctx.preprocess_out_pool->release_frame(preprocess_out_frames[frm_idx]);
            goto killall;
          }
        }
#endif
        /* Increment the counter for frames read if data read successfully */
        frame_read++;
      } else if (read_status == APP_READ_FAILED) {
        APP_LOG_MESSAGE(APP_LOG_LEVEL_WARNING, log_level, "Failed to read input data");
        ctx.in_pool->release_frame(input_frames[frm_idx]);
        if (ctx.preprocess_enable)
          ctx.preprocess_out_pool->release_frame(preprocess_out_frames[frm_idx]);
        /* Set the read_status to APP_EOF and exit the loop */
        read_status = APP_EOF;
        break;
      }

      if (((ctx.input_fmt == APP_VIDEO_INPUT_FORMAT_JPEG ||
            ctx.input_fmt == APP_VIDEO_INPUT_FORMAT_BGR_FLOAT) &&
           (APP_READ_SUCCESS == read_status)) ||
          (read_status == APP_EOF)) {
        ctx.iteration_counter++;
        if (ctx.iteration_counter >= ctx.max_iterations) {
          read_status = APP_EOF;
          print_iteration_info = false;
          if (ctx.max_iterations > 1) {
            //cout << "Completed " << ctx.iteration_counter << "/"
            //     << ctx.max_iterations << " iteration(s)" << endl;
          }
          break;
        } else {
          print_iteration_info = true;
          if (ctx.input_fmt == APP_VIDEO_INPUT_FORMAT_NV12) {
            ctx.input_file.clear();
            ctx.input_file.seekg(0, ctx.input_file.beg);
          }
          if (APP_EOF == read_status) {
            APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Got APP_EOF");
            APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Read more from start of file");
            ctx.in_pool->release_frame(input_frames[frm_idx]);
            if (ctx.preprocess_enable)
              ctx.preprocess_out_pool->release_frame(preprocess_out_frames[frm_idx]);
            read_status = APP_READ_SUCCESS;
            goto read_more;
          }
        }
      }
    }

    APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
                "%d frame read from input file in this iteration", frame_read);
    /* Check if no frames were read, and exit the loop if so */
    if (frame_read <= 0)
      break;

    std::chrono::time_point<std::chrono::steady_clock> npu_start;
    std::chrono::time_point<std::chrono::steady_clock> postprocess_start;
    std::chrono::time_point<std::chrono::steady_clock> postprocess_end;
    /* Perform inference, postprocess on the batch of frames */
    npu_start = chrono::steady_clock::now();
    {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Do infer for %d frames",
                  frame_read);
      /* Perform inference on the batch of frames */
      if (infer_process_frames(&ctx, frame_read, preprocess_out_frames,
                               npu_out_tensors_memory) != true) {
        APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Failed to do inference");
        for (uint32_t i = 0; i < frame_read; i++) {
          /* Release acquired pre-process input buffers on error */
          ctx.in_pool->release_frame(input_frames[i]);
          ctx.preprocess_out_pool->release_frame(preprocess_out_frames[i]);
        }
        goto killall;
      }
      postprocess_start = chrono::steady_clock::now();
      if (ctx.postprocess_enable) {
        /* Perform the post-processing step on the inference predictions
         * The full batch is processed once in this step. After this call,
         * inference_results holds the results for each frame in batch */
        APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Do post process for %d frames",
                    frame_read);
        inference_results = postprocess_process_frames(
            &ctx, frame_read, npu_out_tensors_memory, num_frame_processed);
      }
    }

    postprocess_end = chrono::steady_clock::now();
    write_tensors_to_files(&ctx, npu_out_tensors_memory);
    if (ctx.postprocess_enable) {
      if (!inference_results.empty() && inference_results[0].size() == 1 &&
          inference_results[0][0] == nullptr) {
        APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Failed to do post-process");
        goto killall;
      }

      vector<shared_ptr<InferResult>> root_res;
      /* Perform operation per frame */
      for (uint32_t i = 0; i < frame_read; i++) {
        root_res.push_back(make_shared<InferResult>(InferResultType::ROOT));
        if (inference_results.size())
          (root_res.back())->add_children(inference_results[i]);
        else
          APP_LOG_MESSAGE(APP_LOG_LEVEL_WARNING, log_level,
                      "No infer result for current frame");
        /* The predictions need to scaled/transformed to match the original
         * input before drawing */
        if (transform_infer_result(&ctx, root_res) != true) {
          APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Failed to do transform");
          for (uint32_t i = 0; i < frame_read; i++) {
            ctx.in_pool->release_frame(input_frames[i]);
            ctx.preprocess_out_pool->release_frame(preprocess_out_frames[i]);
          }
          goto killall;
        }

        if (!ctx.out_file_path.empty() && ctx.output_file.is_open()) {
          /* Draw predictions on the input frame */
          APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level,
                      "Draw Prediction for %d frame", i);
          if (draw_infer_result(&ctx, root_res.back(), input_frames[i]) !=
              true) {
            APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level,
                        "Failed to do drawing on input");
            for (uint32_t i = 0; i < frame_read; i++) {
              ctx.in_pool->release_frame(input_frames[i]);
              ctx.preprocess_out_pool->release_frame(preprocess_out_frames[i]);
            }
            goto killall;
          }

          /* Dump the video frame with prediction drawing */
          APP_LOG_MESSAGE(APP_LOG_LEVEL_DEBUG, log_level, "Dump output frame %d", i);
          dump_video_frame(&ctx, ctx.output_file, input_frames[i]);
        }
      }
    }
    /* Log the number of processed frames */
    num_frame_processed += frame_read;
    APP_LOG_MESSAGE(APP_LOG_LEVEL_INFO, log_level, "num_frame_processed %ld",
                num_frame_processed);
    /* Check if the required number of frames has been processed */
    if (ctx.num_frame_to_process != APP_PROCESS_ALL_FRAMES &&
        num_frame_processed >= ctx.num_frame_to_process) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_INFO, log_level, "Required frames processed");
      read_status = APP_EOF;
      /* no need to break as while(read_status != APP_EOF) take care of this */
    }

    if (ctx.postprocess_enable)
      inference_results.clear();
    /* Release buffers */
    for (uint32_t i = 0; i < frame_read; i++) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_INFO, log_level, "release in/out frames %d", i);
      ctx.in_pool->release_frame(input_frames[i]);
      if (ctx.preprocess_enable)
        ctx.preprocess_out_pool->release_frame(preprocess_out_frames[i]);
      input_frames[i] = NULL;
      preprocess_out_frames[i] = NULL;
    }

    /* Reset the buffer pool index and processed frame count for the next
     * iteration */
    frame_read = 0;

    print_iteration_info = false; /* Do not print */
    if (print_iteration_info && (ctx.max_iterations > 1)) {
      cout << "Completed " << ctx.iteration_counter << "/" << ctx.max_iterations
           << " iteration(s)" << endl;
      print_iteration_info = false;
    }

    float inference_time = chrono::duration_cast<chrono::microseconds>(postprocess_end - npu_start).count() / 1000.0;
    float npu_time = chrono::duration_cast<chrono::microseconds>(postprocess_start - npu_start).count() / 1000.0;
    total_inference_time += inference_time;
    total_npu_time += npu_time;
    if (!ctx.is_multi_instance){
      cout << "Inference time for frame " << num_frame_processed << " : " << npu_time << " ms" << endl;
    }

#if 0
    cout << "Total Inference took "
         << (float)(chrono::duration_cast<chrono::microseconds>(t1 - t0)
                        .count()) /
                1000
         << " ms\n";

    cout << "Total processing took "
         << (float)(chrono::duration_cast<chrono::microseconds>(t1 - preprocess_start)
                        .count()) /
                1000
         << " ms\n";
#endif
  }

  ret = 0;

killall:
  if (!ctx.is_multi_instance){
    cout << "\nNumber of frames processed: " << num_frame_processed << endl;
    if (num_frame_processed > 0) {
      cout << "Average Inference Time for " << num_frame_processed << " Frames: " << total_npu_time/num_frame_processed << " ms" << endl;
    } else {
      cout << "No frames processed. Unable to calculate averages." << endl;
    }
  }
  if (!ctx.out_file_path.empty() && ctx.output_file.is_open()) {
    cout << "Output dumped at " << ctx.out_file_path << " with "
         << ctx.input_width << "x" << ctx.input_height << " resolution and "
         << map_input_fmt_to_vart_fmt_string(ctx.input_fmt) << " format"
         << endl;
  }
  if (ctx.log_level < APP_LOG_LEVEL_RESULT)
    cout << "To view results on console, enable logs using the option -l "
         << APP_LOG_LEVEL_RESULT << endl;

  return ret;

  } catch (const std::regex_error& e) {
    std::cerr << "Regex error: " << e.what() << std::endl;
    return -1; /* Exit with error code */
  } catch (const std::runtime_error& e) {
    std::cerr << "Runtime error: " << e.what() << std::endl;
    return -1; /* Exit with error code */
  } catch (const std::exception& e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    return -1; /* Exit with error code */
  } catch (...) {
    std::cerr << "Unknown error occurred." << std::endl;
    return -1; /* Exit with error code */
  }
}

/* Argument parser supporting both single and multiple options argumnets */
class MultiOption {
public:
  MultiOption() = default;

  /* Splits each argument into multiple tokens, using '+' as the default delimiter */
  void set_raw(const std::string& raw_value) {
    raw = raw_value;
    values = split_by_delimiter(raw);
  }

  const std::vector<std::string>& get_values() const {
    return values;
  }

  /* Expands the vector to the specified target size */
  void extend_to_size(size_t target_size) {
    if (values.empty()) return;
    while (values.size() < target_size) {
      values.push_back(values.back()); // repeat last
    }
  }

private:
  std::string raw;
  std::vector<std::string> values;

  std::vector<std::string> split_by_delimiter(const std::string& str, char delimiter = '+') {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delimiter)) {
      if (!token.empty()) tokens.push_back(token);
    }
    return tokens;
  }
};

static bool parse_bool(const std::string& val) {
  std::string lower_val = val;
  for (char& c : lower_val) {
        c = std::tolower(static_cast<unsigned char>(c));
  }
  return lower_val == "1" || lower_val == "true";
}

static std::tuple<int, int> parse_resolution(const std::string& val) {
  size_t x_pos = val.find('x');
  if (x_pos == std::string::npos) {
    throw std::invalid_argument("Invalid resolution format (missing 'x'): " + val);
  }

  try {
    int width = std::stoi(val.substr(0, x_pos));
    int height = std::stoi(val.substr(x_pos + 1));
    return std::make_tuple(width, height);
  } catch (const std::exception& e) {
    throw std::invalid_argument("Invalid resolution numbers in: " + val);
  }
}

void debug_cli_options(const std::vector<AppContext>& context_vec) {
  for (size_t i = 0; i < context_vec.size(); ++i) {
    const auto& ctx = context_vec[i];
    std::cout << "context" << (i + 1) << " {\n";
    if (!ctx.input_file_path.empty() ) std::cout << "input_file_path      " << ctx.input_file_path      << std::endl;
    if (!ctx.out_file_path.empty()   ) std::cout << "out_file_path        " << ctx.out_file_path        << std::endl;
    if (!ctx.snap_path.empty()       ) std::cout << "snapshot_path        " << ctx.snap_path            << std::endl;
    if (!ctx.config_json_path.empty()) std::cout << "config_json_path     " << ctx.config_json_path     << std::endl;
    if (ctx.num_frame_to_process     ) std::cout << "num_frame_to_process " << ctx.num_frame_to_process << std::endl;
    if (ctx.log_level                ) std::cout << "log_level            " << ctx.log_level            << std::endl;
    if (ctx.dump_all_inputs          ) std::cout << "dump_all_inputs      " << ctx.dump_all_inputs      << std::endl;
    if (ctx.dump_npu_output          ) std::cout << "dump_npu_output      " << ctx.dump_npu_output      << std::endl;
    if (ctx.max_iterations           ) std::cout << "max_iterations       " << ctx.max_iterations       << std::endl;
    if (ctx.input_width              ) std::cout << "input_width          " << ctx.input_width          << std::endl;
    if (ctx.input_height             ) std::cout << "input_height         " << ctx.input_height         << std::endl;
    std::cout << "}\n\n";
  }
  return;
}

int validate_cli_options(const std::vector<AppContext>& context_vec)
{
  if (context_vec.empty()) {
    APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, APP_LOG_LEVEL_ERROR, "Missing mandatory arguments\n");
    return -1;
  }

  std::unordered_set<std::string> output_file_names;
  for(const auto& ctx : context_vec) {
    if (ctx.input_file_path.empty() || ctx.config_json_path.empty() ||
        ctx.snap_path.empty()) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, ctx.log_level, "Missing mandatory arguments\n");
      return -1;
    }
    if(ctx.log_level > APP_LOG_LEVEL_DEBUG)
    {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, APP_LOG_LEVEL_ERROR, "Unsupported log level: %d\n", ctx.log_level);
      return -1;
    }
    /* Validate the output files are not duplicated */
    if(ctx.out_file_path.length()>0 && output_file_names.count(ctx.out_file_path)) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, ctx.log_level, "Output filenames should be different\n");
      return -1;
    }
    if(!ctx.out_file_path.empty()) {
      output_file_names.insert(ctx.out_file_path);
    }
  }

  /* For raw input format, output overlay is not supported now*/
  if(!output_file_names.empty()) {
    for(const auto& ctx : context_vec) {
      if(get_file_extension(ctx.input_file_path)=="raw") {
        APP_LOG_MESSAGE(
          APP_LOG_LEVEL_ERROR, ctx.log_level,
          "For raw format, output overlay not supported, remove \"-o\" option");
        return -1;
      }
    }
  }

  return 0;
}

int parse_cli_options(int argc, char* argv[], std::vector<AppContext>& context_vec)
{
  int opt;
  std::map<std::string, MultiOption> options;

  while ((opt = getopt(argc, argv, "i:o:s:c:n:d:l:a:r:m:h")) != -1) {
    switch (opt) {
      case 'i': options["input_files"].set_raw(optarg); break;
      case 'o': options["output_files"].set_raw(optarg); break;
      case 's': options["snapshots"].set_raw(optarg); break;
      case 'c': options["config_files"].set_raw(optarg); break;
      case 'n': options["num_frames"].set_raw(optarg); break;
      case 'd': options["resolution"].set_raw(optarg); break;
      case 'l': options["log_level"].set_raw(optarg); break;
      case 'a': options["dump_all_inputs"].set_raw(optarg); break;
      case 'r': options["dump_npu_output"].set_raw(optarg); break;
      case 'm': options["max_iterations"].set_raw(optarg); break;
      case 'h': 
      default : print_help_text(argv[0]); return -1;
    }
  }

  /* Exit if snapshots are not provided */
  if(!options.count("snapshots")) {
     APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, APP_LOG_LEVEL_ERROR, "Missing mandatory arguments\n");
     print_help_text(argv[0]);
     return -1;
  }

  size_t snap_count = options["snapshots"].get_values().size();
  for (const auto& [key, opt] : options) {
    if(snap_count!=opt.get_values().size()) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, APP_LOG_LEVEL_ERROR, "snapshots(%ld) are not matching with %s(%ld) provided.\n", snap_count, key.c_str(), opt.get_values().size());
      return -1;
    }
  }

  /* Resize of Contexts vec */
  context_vec.resize(snap_count);

  for (size_t i = 0; i < snap_count; ++i) {
    /* Initialize handle parameters */
    init_app_context(&context_vec[i]);
    context_vec[i].snap_id = i;
  
    if (options.count("input_files"))     context_vec[i].input_file_path      = options["input_files"].get_values()[i];
    if (options.count("output_files"))    context_vec[i].out_file_path        = options["output_files"].get_values()[i];
    if (options.count("snapshots"))       context_vec[i].snap_path            = options["snapshots"].get_values()[i];
    if (options.count("config_files"))    context_vec[i].config_json_path     = options["config_files"].get_values()[i];
    if (options.count("num_frames"))      context_vec[i].num_frame_to_process = atoi(options["num_frames"].get_values()[i].c_str());
    if (options.count("log_level"))       context_vec[i].log_level            = static_cast<AppLogLevel>(atoi(options["log_level"].get_values()[i].c_str()));
    if (options.count("dump_all_inputs")) context_vec[i].dump_all_inputs      = parse_bool(options["dump_all_inputs"].get_values()[i]);
    if (options.count("dump_npu_output")) context_vec[i].dump_npu_output      = parse_bool(options["dump_npu_output"].get_values()[i]);
    if (options.count("max_iterations"))  context_vec[i].max_iterations       = atoi(options["max_iterations"].get_values()[i].c_str());
    if (options.count("resolution"))
      try {
        std::tie(context_vec[i].input_width, context_vec[i].input_height) = parse_resolution(options["resolution"].get_values()[i]);
      } catch (const std::exception& e) {
        std::cerr << "Error parsing resolution: " << e.what() << "\n";
        return -1;
      }
    if (context_vec[i].num_frame_to_process < 1 && options.count("num_frames")) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, APP_LOG_LEVEL_ERROR, "Invalid number of frames to process: %ld, Enter a valid positive number\n", context_vec[i].num_frame_to_process);
      return -1;
    }
    if (snap_count > 1){
      context_vec[i].is_multi_instance = true;
    }
  }

  /* Validate command line options */
  if(validate_cli_options(context_vec))
  {
    print_help_text(argv[0]);
    return -1;
  }

  // debug_cli_options(context_vec);
  return 0;
}

int main(int argc, char* argv[])
{
  int32_t ret=0;
  std::vector<AppContext> context_vec;

  /* Parse and validate command line arguments */
  if (parse_cli_options(argc, argv, context_vec) != 0) {
    return -1;
  }

  /* Read the input files and update the APP contexts */
  for (auto& ctx : context_vec) {
    /* Set log level based on handle configuration */
    AppLogLevel log_level = ctx.log_level;

    /* parse configurations from json file */
    if (!parse_json_config(&ctx)) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Json parsing failed");
      return -1;
    }

    if (open_files(&ctx) != true) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Error opening files");
      return -1;
    }

    /* Create the contexts */
    if (create_all_context(&ctx) != true) {
      APP_LOG_MESSAGE(APP_LOG_LEVEL_ERROR, log_level, "Create context failed");
      return -1;
    }
  }

  /* Spawn thread for each instance execution */
  if(context_vec.size() > 1){
    std::vector<std::future<int>> ret_vec;
    for (auto& ctx : context_vec) {
      ret_vec.emplace_back(std::async(std::launch::async,single_instance_execution, std::ref(ctx)));
    }

    for (auto& f : ret_vec) {
      int rc = f.get();
      if (rc != 0 && ret == 0) {
      ret = rc;
      }
    }
  }
  else{
    ret = single_instance_execution(context_vec[0]);
  }



  /* Destroy all contexts */
  for (auto& ctx : context_vec) {
    destroy_all_context(&ctx);
    close_files(&ctx);
  }

  return ret;
}
