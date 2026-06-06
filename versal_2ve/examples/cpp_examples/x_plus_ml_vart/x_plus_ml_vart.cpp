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

#include <filesystem>
#include <iomanip>
#include <set>
#include <vart/vart_postprocess_types.hpp>  //for TensorDataType definition
#include "common/app_utils.hpp"
#include "x_plus_ml_app.hpp"

using namespace vart;

namespace fs = std::filesystem;

const char* to_string(vart::MemoryLayout l) {
  switch (l) {
    case vart::MemoryLayout::UNKNOWN:
      return "UNKNOWN";
    case vart::MemoryLayout::NC:
      return "NC";
    case vart::MemoryLayout::NCH:
      return "NCH";
    case vart::MemoryLayout::NHC:
      return "NHC";
    case vart::MemoryLayout::NHW:
      return "NHW";
    case vart::MemoryLayout::NHWC:
      return "NHWC";
    case vart::MemoryLayout::NCHW:
      return "NCHW";
    case vart::MemoryLayout::NHWC4:
      return "NHWC4";
    case vart::MemoryLayout::NHWC8:
      return "NHWC8";
    case vart::MemoryLayout::NC4HW4:
      return "NC4HW4";
    case vart::MemoryLayout::NC8HW8:
      return "NC8HW8";
    case vart::MemoryLayout::HCWNC4:
      return "HCWNC4";
    case vart::MemoryLayout::HCWNC8:
      return "HCWNC8";
    case vart::MemoryLayout::HCWNC16:
      return "HCWNC16";
    case vart::MemoryLayout::NHW16C4WC:
      return "NHW16C4WC";
    case vart::MemoryLayout::GENERIC:
      return "GENERIC";
    default:
      return "?";
  }
}

const char* to_string(vart::DataType d) {
  switch (d) {
    case vart::DataType::UNKNOWN:
      return "UNKNOWN";
    case vart::DataType::BOOLEAN:
      return "BOOLEAN";
    case vart::DataType::INT8:
      return "INT8";
    case vart::DataType::UINT8:
      return "UINT8";
    case vart::DataType::INT16:
      return "INT16";
    case vart::DataType::UINT16:
      return "UINT16";
    case vart::DataType::BF16:
      return "BF16";
    case vart::DataType::FP16:
      return "FP16";
    case vart::DataType::INT32:
      return "INT32";
    case vart::DataType::UINT32:
      return "UINT32";
    case vart::DataType::FLOAT32:
      return "FLOAT32";
    case vart::DataType::INT64:
      return "INT64";
    case vart::DataType::UINT64:
      return "UINT64";
    default:
      return "?";
  }
}

const char* to_string(vart::VideoFormat f) {
  switch (f) {
    case vart::VideoFormat::UNKNOWN:
      return "UNKNOWN";
    case vart::VideoFormat::RGBx:
      return "RGBX";
    case vart::VideoFormat::BGRx:
      return "BGRX";
    case vart::VideoFormat::RGB:
      return "RGB";
    case vart::VideoFormat::BGR:
      return "BGR";
    case vart::VideoFormat::Y_UV8_420:
      return "Y_UV8_420";
    case vart::VideoFormat::BGR_FLOAT:
      return "BGR_FLOAT";
    case vart::VideoFormat::RGBP_FLOAT:
      return "RGBP_FLOAT";
    case vart::VideoFormat::RGBP:
      return "RGBP";
    case vart::VideoFormat::RGBx_BF16:
      return "RGBX_BF16";
    case vart::VideoFormat::RGBx_FP16:
      return "RGBX_FP16";
    case vart::VideoFormat::RGB_FLOAT:
      return "RGB_FLOAT";
    case vart::VideoFormat::BGRx_BF16:
      return "BGRX_BF16";
    case vart::VideoFormat::BGRx_FP16:
      return "BGRX_FP16";
    case vart::VideoFormat::RGBP_BF16:
      return "RGBP_BF16";
    case vart::VideoFormat::RGBP_FP16:
      return "RGBP_FP16";
    case vart::VideoFormat::RGB_BF16:
      return "RGB_BF16";
    case vart::VideoFormat::RGB_FP16:
      return "RGB_FP16";
    default:
      return "?";
  }
}

/**
 * @brief Extract the colour space from a vart::VideoFormat enum.
 *
 * Maps a fully-qualified VideoFormat back to the base colour space it
 * represents ("RGB" or "BGR"). RGBP variants are treated as RGB.
 *
 * @param fmt  VideoFormat enum value.
 * @return "RGB", "BGR", or empty string if the format has no mappable colour space.
 */
string get_colour_space(vart::VideoFormat fmt) {
  switch (fmt) {
    case vart::VideoFormat::RGBx:
    case vart::VideoFormat::RGB:
    case vart::VideoFormat::RGBP:
    case vart::VideoFormat::RGBP_FLOAT:
    case vart::VideoFormat::RGBx_BF16:
    case vart::VideoFormat::RGBx_FP16:
    case vart::VideoFormat::RGB_FLOAT:
    case vart::VideoFormat::RGBP_BF16:
    case vart::VideoFormat::RGBP_FP16:
    case vart::VideoFormat::RGB_BF16:
    case vart::VideoFormat::RGB_FP16:
      return "RGB";

    case vart::VideoFormat::BGRx:
    case vart::VideoFormat::BGR:
    case vart::VideoFormat::BGR_FLOAT:
    case vart::VideoFormat::BGRx_BF16:
    case vart::VideoFormat::BGRx_FP16:
      return "BGR";

    default:
      return "";
  }
}

/**
 * @brief Direct map of a colour-format string to vart::VideoFormat.
 *
 * Accepts the full set of format strings (e.g. "RGBX", "RGBP_FP16", "BGR_FLOAT", etc.)
 * that explicitly encode colour space, layout, and data type.
 *
 * @param fmt  Format string from JSON.
 * @return Corresponding vart::VideoFormat, or VideoFormat::UNKNOWN if not recognised.
 */
vart::VideoFormat get_vart_video_format(const string& fmt) {
  static const std::unordered_map<std::string, vart::VideoFormat> format_map = {
      {"RGBX", vart::VideoFormat::RGBx},
      {"BGRX", vart::VideoFormat::BGRx},
      {"RGB", vart::VideoFormat::RGB},
      {"BGR", vart::VideoFormat::BGR},
      {"Y_UV8_420", vart::VideoFormat::Y_UV8_420},
      {"BGR_FLOAT", vart::VideoFormat::BGR_FLOAT},
      {"RGBP_FLOAT", vart::VideoFormat::RGBP_FLOAT},
      {"RGBP", vart::VideoFormat::RGBP},
      {"RGBX_BF16", vart::VideoFormat::RGBx_BF16},
      {"RGBX_FP16", vart::VideoFormat::RGBx_FP16},
      {"RGB_FLOAT", vart::VideoFormat::RGB_FLOAT},
      {"BGRX_BF16", vart::VideoFormat::BGRx_BF16},
      {"BGRX_FP16", vart::VideoFormat::BGRx_FP16},
      {"RGBP_BF16", vart::VideoFormat::RGBP_BF16},
      {"RGBP_FP16", vart::VideoFormat::RGBP_FP16},
      {"RGB_BF16", vart::VideoFormat::RGB_BF16},
      {"RGB_FP16", vart::VideoFormat::RGB_FP16}};

  auto it = format_map.find(fmt);
  return (it != format_map.end()) ? it->second : vart::VideoFormat::UNKNOWN;
}

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
                                     string& error_reason) {
  // GENERIC tensors do not carry a fixed layout, so infer layout from common
  // shape conventions used by image models.
  const auto& shape = tensor.shape;

  inferred_layout = vart::MemoryLayout::GENERIC;
  inferred_width = 0;
  inferred_height = 0;
  error_reason.clear();

  if (shape.size() == 3) {
    // 3D image tensors are interpreted as NHW.
    inferred_layout = vart::MemoryLayout::NHW;
    inferred_height = static_cast<uint32_t>(shape[1]);
    inferred_width = static_cast<uint32_t>(shape[2]);
  } else if (shape.size() == 4) {
    // For 4D tensors, detect channel-first (NCHW) vs channel-last (NHWC)
    // by checking where channel count appears.
    const bool nchw_candidate = (shape[1] == 3 || shape[1] == 4);
    const bool nhwc_candidate = (shape[3] == 3 || shape[3] == 4);

    if (nchw_candidate && nhwc_candidate) {
      error_reason = "ambiguous 4D tensor: cannot be inferred as NCHW or NHWC";
      return false;
    }

    if (nchw_candidate) {
      inferred_layout = vart::MemoryLayout::NCHW;
      inferred_height = static_cast<uint32_t>(shape[2]);
      inferred_width = static_cast<uint32_t>(shape[3]);
    } else if (nhwc_candidate) {
      inferred_layout = vart::MemoryLayout::NHWC;
      inferred_height = static_cast<uint32_t>(shape[1]);
      inferred_width = static_cast<uint32_t>(shape[2]);
    } else {
      error_reason =
          "4D GENERIC preprocessing expects channels to be 3 or 4 in either channel-first (N,C,H,W) or channel-last "
          "(N,H,W,C) order";
      return false;
    }
  } else {
    error_reason = "GENERIC preprocessing supports only 3D tensors (NHW) or 4D tensors";
    return false;
  }

  if (inferred_width == 0 || inferred_height == 0) {
    // Width/height must be resolved for preprocessing to configure output.
    error_reason = "resolved GENERIC tensor width/height is zero";
    inferred_layout = vart::MemoryLayout::GENERIC;
    inferred_width = 0;
    inferred_height = 0;
    return false;
  }

  return true;
}

/**
 * @brief Derive the VART VideoFormat from a simple colour-space string and inference tensor metadata.
 *
 * Used as a fallback when the user omits colour-format from the JSON config. The memory
 * layout and data type are read from the first input tensor of the inference model, and the
 * three pieces are combined to select the correct vart::VideoFormat enum.
 *
 * @param colour_space  Simple colour-space string ("RGB" or "BGR").
 * @param layout        MemoryLayout of the first input tensor reported by the inference runner.
 * @param dtype         DataType of the first input tensor reported by the inference runner.
 * @return Corresponding vart::VideoFormat, or VideoFormat::UNKNOWN on unsupported combination.
 */
vart::VideoFormat derive_vart_video_format(const string& colour_space,
                                           vart::MemoryLayout layout,
                                           vart::DataType dtype) {
  using VF = vart::VideoFormat;
  using ML = vart::MemoryLayout;
  using DT = vart::DataType;

  bool is_rgb = (colour_space == "RGB");
  bool is_bgr = (colour_space == "BGR");
  if (!is_rgb && !is_bgr) {
    return VF::UNKNOWN;
  }

  switch (layout) {
    case ML::HCWNC4:
      switch (dtype) {
        case DT::INT8:
        case DT::UINT8:
          return is_rgb ? VF::RGBx : VF::BGRx;
        case DT::BF16:
          return is_rgb ? VF::RGBx_BF16 : VF::BGRx_BF16;
        case DT::FP16:
          return is_rgb ? VF::RGBx_FP16 : VF::BGRx_FP16;
        default:
          return VF::UNKNOWN;
      }

    case ML::NCHW:
      switch (dtype) {
        case DT::INT8:
        case DT::UINT8:
          return VF::RGBP;
        case DT::FLOAT32:
          return VF::RGBP_FLOAT;
        case DT::BF16:
          return VF::RGBP_BF16;
        case DT::FP16:
          return VF::RGBP_FP16;
        default:
          return VF::UNKNOWN;
      }

    case ML::NHWC:
      switch (dtype) {
        case DT::INT8:
        case DT::UINT8:
          return is_rgb ? VF::RGB : VF::BGR;
        case DT::FLOAT32:
          return is_rgb ? VF::RGB_FLOAT : VF::BGR_FLOAT;
        case DT::BF16:
          return is_rgb ? VF::RGB_BF16 : VF::BGR_BF16;
        case DT::FP16:
          return is_rgb ? VF::RGB_FP16 : VF::BGR_FP16;
        default:
          return VF::UNKNOWN;
      }

    default:
      return VF::UNKNOWN;
  }
}

/**
 * @brief Print help text for the command-line options
 * @param pn Program name
 */
static void print_help_text(char* pn) {
  std::cout << "Usage: " << pn << " [OPTIONS]" << std::endl;

  std::cout << "  --app-config\t\tConfig file path (mandatory)" << std::endl;
  std::cout << "  --input-file\t\tInput file path. In preprocessing mode (preprocess-en=true) it is OPTIONAL:"
            << std::endl;
  std::cout << "\t\t\tif provided it broadcasts to all models (overriding ifms-config) and must be one of" << std::endl;
  std::cout << "\t\t\t.jpg, .jpeg, .nv12, .bgr; if omitted, each model reads its own image from" << std::endl;
  std::cout << "\t\t\tifms-config. In inference-only mode (preprocess-en=false) --input-file must NOT be" << std::endl;
  std::cout << "\t\t\tprovided; per-tensor binary inputs come from ifms-config." << std::endl;
  std::cout << "  --runs\t\tNumber of iterations app should run "
               "(optional, default "
               "is 1)"
            << std::endl;
  std::cout << "  --benchmark\t\tBenchmark the metrics of all components "
               "(optional, default is false)"
            << std::endl;
  std::cout << "  --log-level\t\tApplication log level to print logs "
               "(optional, default is "
               "ERROR and WARNING)."
            << std::endl;
  std::cout << "\t\t\tAccepted log levels: 1 for ERROR, 2 for WARNING, 3 for "
               "INFERENCE RESULT, "
               "4 for FIXME, 5 for INFO, 6 for DEBUG."
            << std::endl;
  std::cout << "\t\t\tLogs at the provided level and all levels below will be "
               "printed."
            << std::endl;
  std::cout << "  --dim\t\t\tResolution (wxh) for raw NV12/BGR file inputs (mandatory when --input-file is"
            << std::endl;
  std::cout << "\t\t\t.nv12 or .bgr; ignored otherwise)." << std::endl;
  std::cout << "  --frames\t\tNumber of frames to process per iteration "
               "(optional, default is -1 for all frames)"
            << std::endl;
  std::cout << "  --help\t\tPrint this help and exit" << std::endl;
}

/**
 * @brief Parses a model config JSON file and fills preprocess and model info
 * @param ctx Pointer to the AppContext structure to be populated.
 * @param idx Model index to populate.
 * @return true on success, false on failure.
 */
static bool parse_model_config(AppContext* ctx, uint32_t idx) {
  AppLogLevel log_level = ctx->log_level;

  const std::string& json_path = ctx->model_json_path[idx];
  std::string& model_snap_path = ctx->model_snap_path[idx];
  std::string& preproc_json_str = ctx->preproc_json_str[idx];
  PreProcessConfig& preprocess_cfg = ctx->preproc_cfg[idx];

  /* Read the pipeline JSON string from the file */
  ifstream fileStream(json_path);
  if (!fileStream.is_open()) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Error opening file: %s", json_path.c_str());
    return false;
  }

  /* Read the entire file into the prperoc variable */
  preproc_json_str = string((istreambuf_iterator<char>(fileStream)), istreambuf_iterator<char>());

  try {
    bool maintain_aspect_ratio = false;
    string resizing_type_str;
    pt::ptree config;
    istringstream iss(preproc_json_str);
    pt::read_json(iss, config);

    /* Extract Pre-process info from "preprocess-config" */
    if (ctx->preprocess_enable) {
      if (config.get_child_optional("preprocess-config")) {
        preprocess_cfg.preprocess_info.mean_r = config.get<float>("preprocess-config.mean-r");
        preprocess_cfg.preprocess_info.mean_g = config.get<float>("preprocess-config.mean-g");
        preprocess_cfg.preprocess_info.mean_b = config.get<float>("preprocess-config.mean-b");
        preprocess_cfg.preprocess_info.scale_r = config.get<float>("preprocess-config.scale-r");
        preprocess_cfg.preprocess_info.scale_g = config.get<float>("preprocess-config.scale-g");
        preprocess_cfg.preprocess_info.scale_b = config.get<float>("preprocess-config.scale-b");
        /* Optional: when omitted, VideoFormat is derived from default colour space (RGB)
         * combined with the inference tensor's MemoryLayout and DataType. */
        preprocess_cfg.colour_format_str = config.get<string>("preprocess-config.colour-format", "");

        /* Get maintain_aspect_ratio value and perform corresponding resizing
         * technique based on the resizing-type value provided */
        maintain_aspect_ratio = config.get<bool>("preprocess-config.maintain-aspect-ratio", false);
        if (maintain_aspect_ratio) {
          if (!config.get_child("preprocess-config").count("resizing-type")) {
            APP_LOG(AppLogLevel::ERROR, log_level,
                    "Please provide resizing-type to maintain-aspect-ratio. "
                    "Valid values are LETTERBOX / PANSCAN");
            return false;
          }
          resizing_type_str = config.get<string>("preprocess-config.resizing-type");
          if (resizing_type_str.compare(0, 7, "PANSCAN") == 0) {
            preprocess_cfg.preprocess_info.preprocess_type = PreProcessType::DEFAULT;
            preprocess_cfg.do_pan_scan = true;
          } else if (resizing_type_str.compare(0, 9, "LETTERBOX") == 0) {
            preprocess_cfg.preprocess_info.preprocess_type = PreProcessType::LETTERBOX;
            preprocess_cfg.preprocess_info.symmetric_padding =
                config.get<bool>("preprocess-config.symmetric-padding", false);
          } else {
            APP_LOG(AppLogLevel::ERROR, log_level, "Unknown resizing-type: %s. Valid values are LETTERBOX / PANSCAN",
                    resizing_type_str.c_str());
            return false;
          }
        } else {
          /* Use default preprocess type if maintain-aspect-ratio is not provided */
          preprocess_cfg.preprocess_info.preprocess_type = PreProcessType::DEFAULT;
        }
        /* Read the input and output memory bank indices for  pre-processing module */
        preprocess_cfg.in_mem_bank = config.get<uint8_t>("preprocess-config.in-mem-bank");
        preprocess_cfg.out_mem_bank = config.get<uint8_t>("preprocess-config.out-mem-bank");
        try {
          preprocess_cfg.quant_scale_factor_conf_set = true;
          preprocess_cfg.quant_scale_factor = config.get<float>("preprocess-config.quant-scale-factor");
        } catch (const std::exception& e) {
          APP_LOG(AppLogLevel::INFO, log_level, "Failed to get quant-scale-factor: %s", e.what());
          preprocess_cfg.quant_scale_factor_conf_set = false;
        }
        // print debug log info
        APP_LOG(AppLogLevel::DEBUG, log_level, "mean-r: %f", preprocess_cfg.preprocess_info.mean_r);
        APP_LOG(AppLogLevel::DEBUG, log_level, "mean-g: %f", preprocess_cfg.preprocess_info.mean_g);
        APP_LOG(AppLogLevel::DEBUG, log_level, "mean-b: %f", preprocess_cfg.preprocess_info.mean_b);
        APP_LOG(AppLogLevel::DEBUG, log_level, "scale-r: %f", preprocess_cfg.preprocess_info.scale_r);
        APP_LOG(AppLogLevel::DEBUG, log_level, "scale-g: %f", preprocess_cfg.preprocess_info.scale_g);
        APP_LOG(AppLogLevel::DEBUG, log_level, "scale-b: %f", preprocess_cfg.preprocess_info.scale_b);
        APP_LOG(AppLogLevel::DEBUG, log_level, "colour-format: %s", preprocess_cfg.colour_format_str.c_str());
        APP_LOG(AppLogLevel::DEBUG, log_level, "maintain-aspect-ratio: %d", maintain_aspect_ratio);
        if (maintain_aspect_ratio) {
          APP_LOG(AppLogLevel::DEBUG, log_level, "resizing-type: %s", resizing_type_str.c_str());
        }
        if (preprocess_cfg.preprocess_info.preprocess_type == PreProcessType::LETTERBOX) {
          APP_LOG(AppLogLevel::DEBUG, log_level, "symmetric-padding: %d",
                  preprocess_cfg.preprocess_info.symmetric_padding);
        }
      }
    }  // if (ctx->preprocess_enable)

    /*Extract inference configuration from "inference-config"*/
    if (config.get_child_optional("inference-config")) {
      model_snap_path = config.get<std::string>("inference-config.model-file");
      if (!fs::exists(model_snap_path)) {
        APP_LOG(AppLogLevel::ERROR, log_level, "Model file doesn't exist: %s", model_snap_path.c_str());
        return false;
      }
    } else {
      APP_LOG(AppLogLevel::ERROR, log_level, "Missing inference-config in json config");
      return false;
    }

    /* Extract postprocess configuration from "postprocess-config" and "metaconvert-config" */
    PostProcessConfig& postproc_cfg = ctx->postproc_cfg[idx];
    std::string& postproc_json_str = ctx->postproc_json_str[idx];

    if (config.get_child_optional("postprocess-config")) {
      ctx->postprocess_enable[idx] = true;
      // Store the entire JSON string for postprocess component
      postproc_json_str = preproc_json_str;  // Use same JSON file

      // Extract postprocess type
      std::string postproc_type_str = config.get<std::string>("postprocess-config.type", "");

      // Use string-to-enum map for postprocess type lookup
      static const std::unordered_map<std::string, vart::PostProcessType> postprocess_type_map = {
          {"RESNET50", vart::PostProcessType::RESNET50},
          {"ARGMAX", vart::PostProcessType::ARGMAX},
          {"BIAS_CORRECTION", vart::PostProcessType::BIAS_CORRECTION},
          {"CALIBRATION_PLATT", vart::PostProcessType::CALIBRATION_PLATT},
          {"CALIBRATION_TEMPERATURE", vart::PostProcessType::CALIBRATION_TEMPERATURE},
          {"LABEL_MAPPING", vart::PostProcessType::LABEL_MAPPING},
          {"NORMALIZATION", vart::PostProcessType::NORMALIZATION},
          {"OUTLIER_DETECTION", vart::PostProcessType::OUTLIER_DETECTION},
          {"SOFTMAX", vart::PostProcessType::SOFTMAX},
          {"THRESHOLD", vart::PostProcessType::THRESHOLD},
          {"TOPK", vart::PostProcessType::TOPK},
          {"UNCERTAINTY_ESTIMATION", vart::PostProcessType::UNCERTAINTY_ESTIMATION},
          {"DISTANCE_IOU_NMS", vart::PostProcessType::DISTANCE_IOU_NMS},
          {"SOFT_NMS", vart::PostProcessType::SOFT_NMS},
          {"CLASSWISE_NMS", vart::PostProcessType::CLASSWISE_NMS},
          {"OBJECT_COUNT", vart::PostProcessType::OBJECT_COUNT},
          {"NMS", vart::PostProcessType::NMS},
          {"ANCHOR_ADJUSTMENT", vart::PostProcessType::ANCHOR_ADJUSTMENT},
          {"YOLOV2", vart::PostProcessType::YOLOV2},
          {"SSDRESNET34", vart::PostProcessType::SSDRESNET34},
          {"SOFTMAXSEG", vart::PostProcessType::SOFTMAXSEG},
          {"SIGMOIDSEG", vart::PostProcessType::SIGMOIDSEG},
          {"ARGMAXSEG", vart::PostProcessType::ARGMAXSEG}};

      auto it = postprocess_type_map.find(postproc_type_str);
      if (it != postprocess_type_map.end()) {
        postproc_cfg.postprocess_type = it->second;
      } else {
        APP_LOG(AppLogLevel::WARNING, log_level, "Unknown postprocess type: %s", postproc_type_str.c_str());
        return false;
      }
      APP_LOG(AppLogLevel::DEBUG, log_level, "Postprocess type: %s", postproc_type_str.c_str());
    } else {
      ctx->postprocess_enable[idx] = false;
      APP_LOG(AppLogLevel::INFO, log_level, "No postprocess-config found, skip");
    }

    if (config.get_child_optional("metaconvert-config")) {
      ctx->metaconvert_enable[idx] = (ctx->preprocess_enable && ctx->postprocess_enable[idx]) ? true : false;
    }

    /* Validate ifms-config file extensions based on preprocessing mode.
     * Skipped when CLI --input-file was provided (CLI broadcasts to all
     * models and overrides ifms-config). Otherwise this runs for both
     * per-model preprocess mode (image inputs) and inference-only mode
     * (binary inputs); the per-mode checks below enforce the right format. */
    if (ctx->input_file_path.empty()) {
      auto ifms_config_opt = config.get_child_optional("ifms-config");
      if (ifms_config_opt) {
        for (const auto& ifm : ifms_config_opt.get()) {
          std::string file_path = ifm.second.get<std::string>("file", "");
          if (!file_path.empty()) {
            // Extract file extension (lowercase)
            std::string extension = get_file_extension_lowercase(file_path);

            // Validate extension based on preprocessing mode
            if (ctx->preprocess_enable) {
              // Preprocessing mode: only image formats allowed
              if (extension == "bin") {
                APP_LOG(AppLogLevel::ERROR, log_level,
                        "Model %u: preprocess-en is true but ifms-config contains .bin file: %s", idx,
                        file_path.c_str());
                APP_LOG(AppLogLevel::ERROR, log_level,
                        "For preprocessing mode, only .jpg/.jpeg/.nv12/.bgr files are supported");
                return false;
              }
              // Verify it's a supported image format
              if (extension != "jpg" && extension != "jpeg" && extension != "nv12" && extension != "bgr") {
                APP_LOG(AppLogLevel::ERROR, log_level,
                        "Model %u: Unsupported file format '%s' in ifms-config for preprocessing mode", idx,
                        extension.c_str());
                APP_LOG(AppLogLevel::ERROR, log_level, "Supported formats: .jpg, .jpeg, .nv12, .bgr");
                return false;
              }
            } else {
              // Inference-only mode: only binary format allowed
              if (extension != "bin") {
                APP_LOG(AppLogLevel::ERROR, log_level,
                        "Model %u: preprocess-en is false (inference-only) but ifms-config contains non-.bin file: %s",
                        idx, file_path.c_str());
                APP_LOG(AppLogLevel::ERROR, log_level, "For inference-only mode, only .bin files are supported");
                return false;
              }
            }  // if (ctx->preprocess_enable)
          }    // if (!file_path.empty())
        }      // for
      }        // if (ifms_config_opt)
    } else {
      APP_LOG(AppLogLevel::DEBUG, log_level, "Model %u: Skipping ifms-config validation (CLI --input-file provided)",
              idx);
    }

  } catch (const exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Error reading config: Reason: %s", e.what());
    return false;
  }
  return true;
}

/**
 * @brief Parse the JSON configuration file and populate the AppContext
 * structure.
 * @param ctx Pointer to the AppContext structure to be populated.
 * @return True if parsing is successful, false otherwise.
 */
static bool parse_toplevel_json_config(AppContext* ctx) {
  /* Read main app config to get individual model config paths */
  if (!ctx || ctx->app_config.empty())
    return false;

  // Check if the config file exists
  if (!fs::exists(ctx->app_config)) {
    APP_LOG(AppLogLevel::ERROR, ctx->log_level, "App config file does not exist: %s", ctx->app_config.c_str());
    return false;
  }

  APP_LOG(AppLogLevel::DEBUG, ctx->log_level, "Parsing top level configuration json file: %s", ctx->app_config.c_str());

  pt::ptree top_level_config;

  // validate top_level_config file path
  try {
    pt::read_json(ctx->app_config, top_level_config);
  } catch (const pt::json_parser_error& e) {
    APP_LOG(AppLogLevel::ERROR, ctx->log_level, "Failed to parse JSON config file '%s': %s", ctx->app_config.c_str(),
            e.what());
    return false;
  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, ctx->log_level, "Error reading config file '%s': %s", ctx->app_config.c_str(),
            e.what());
    return false;
  }

  /* Extract global application-level config */
  ctx->xclbin_location = top_level_config.get<string>("xclbin-location");
  APP_LOG(AppLogLevel::DEBUG, ctx->log_level, "xclbin-location: %s", ctx->xclbin_location.c_str());

  ctx->device_idx = top_level_config.get<int>("device-index", PL_DEVICE_INDEX);
  APP_LOG(AppLogLevel::DEBUG, ctx->log_level, "device-index: %d", ctx->device_idx);

  ctx->preprocess_enable = top_level_config.get<bool>("preprocess-en", false);
  APP_LOG(AppLogLevel::INFO, ctx->log_level, "preprocess_en: %d", ctx->preprocess_enable);

  /* Count models first to determine required vector size */
  const auto& models_config = top_level_config.get_child("models-config");
  ctx->num_model_instances = models_config.size();

  APP_LOG(AppLogLevel::INFO, ctx->log_level, "Detected %u model(s) in the pipeline", ctx->num_model_instances);

  /* Resize vectors to accommodate all model instances */
  ctx->model_json_path.resize(ctx->num_model_instances);
  ctx->model_snap_path.resize(ctx->num_model_instances);
  ctx->preproc_json_str.resize(ctx->num_model_instances);
  ctx->preproc_cfg.resize(ctx->num_model_instances);
  ctx->postproc_json_str.resize(ctx->num_model_instances);
  ctx->postproc_cfg.resize(ctx->num_model_instances);
  ctx->postprocess_enable.resize(ctx->num_model_instances);
  ctx->metaconvert_enable.resize(ctx->num_model_instances);

  // Initialize per-model frame tracking
  ctx->frames_processed_per_model.resize(ctx->num_model_instances, 0);

  /* Get model config-paths in top level cfg file */
  uint32_t idx = 0;
  for (const auto& model : top_level_config.get_child("models-config")) {
    ctx->model_json_path[idx] = model.second.get<string>("config-path");
    idx++;
  }

  for (idx = 0; idx < ctx->num_model_instances; ++idx) {
    APP_LOG(AppLogLevel::INFO, ctx->log_level, "----Parsing model %u configuration json file----", idx);

    /* Parse model configs */
    if (!parse_model_config(ctx, idx)) {
      APP_LOG(AppLogLevel::ERROR, ctx->log_level, "Failed to parse model %u config.", idx);
      return false;
    }
  }
  // all preprocessing blocks will have same DDR bank setting
  if (ctx->preprocess_enable)
    ctx->ppe_mbank_in = ctx->preproc_cfg[0].in_mem_bank;

  return true;
}

/**
 * @brief Reads and processes user inputs from command line arguments.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param ctx Pointer to the application context to populate.
 * @return 0 on success, -1 on failure.
 */
int read_user_inputs(int argc, char* argv[], AppContext* ctx) {
  opterr = 0;
  int opt;
  int option_index = 0;
  static struct option long_options[] = {

      {"app-config", required_argument, 0, 0}, {"runs", required_argument, 0, 1},
      {"log-level", required_argument, 0, 2},  {"dump-all", no_argument, 0, 3},
      {"benchmark", no_argument, 0, 4},        {"help", no_argument, 0, 5},
      {"input-file", required_argument, 0, 6}, {"dim", required_argument, 0, 7},
      {"frames", required_argument, 0, 8},     {0, 0, 0, 0}};

  /* Parse command-line options */
  while ((opt = getopt_long(argc, argv, "", long_options, &option_index)) != -1) {
    switch (opt) {
      case 0:  // --app-config
        ctx->app_config = (optarg != nullptr) ? optarg : "";
        break;
      case 1:  // --runs
      {
        const uint32_t MAX_ALLOWED_ITERATIONS = 10000;  // 10K
        char* endptr;
        unsigned long val = strtoul((optarg != nullptr) ? optarg : "1", &endptr, 10);
        if (*endptr != '\0' || val == 0) {
          APP_LOG(AppLogLevel::ERROR, ctx->log_level, "Invalid number of runs. Enter valid positive number");
          return -1;
        }
        if (val > MAX_ALLOWED_ITERATIONS) {
          APP_LOG(AppLogLevel::ERROR, ctx->log_level,
                  "Number of runs (%lu) exceeds maximum allowed (%u). Please use a value <= %u", val,
                  MAX_ALLOWED_ITERATIONS, MAX_ALLOWED_ITERATIONS);
          return -1;
        }
        ctx->max_iterations = static_cast<uint32_t>(val);
      } break;
      case 2:  // --log-level
        ctx->log_level = (optarg != nullptr) ? static_cast<AppLogLevel>(atoi(optarg)) : AppLogLevel::WARNING;
        break;
      case 3:  // --dump-all
        ctx->dump_all_inputs = true;
        break;
      case 4:  // --benchmark
        ctx->is_benchmark_enabled = true;
        break;
      case 5:  // --help
        print_help_text(argv[0]);
        return -1;
      case 6:  // --input-file
        ctx->input_file_path = (optarg != nullptr) ? optarg : "";
        break;
      case 7:  // --dim
        if (optarg == nullptr || sscanf(optarg, "%ux%u", &ctx->input_width, &ctx->input_height) != 2) {
          APP_LOG(AppLogLevel::ERROR, ctx->log_level, "Invalid input dimension size format. Use WIDTHxHEIGHT.");
        }
        break;
      case 8:  // --frames
        if (optarg) {
          int frames = atoi(optarg);
          if (frames < -1 || frames == 0) {
            APP_LOG(AppLogLevel::ERROR, ctx->log_level,
                    "Invalid number of frames. Use -1 for all frames or positive number");
            return -1;
          }
          ctx->num_frame_to_process = frames;
        }
        break;
      case '?':  // Unknown option
        std::cerr << "Unknown option: " << argv[optind - 1] << std::endl;
        print_help_text(argv[0]);
        return -1;
      default:
        print_help_text(argv[0]);
        return -1;
    }
  }
  if (ctx->app_config.empty()) {
    APP_LOG(AppLogLevel::ERROR, ctx->log_level, "No app config - Invalid argument(s)");
    print_help_text(argv[0]);
    return -1;
  }
  return 0;
}

/**
 * @brief Check if pipeline wait has timed out based on completion tracking
 * @param frames_submitted Total frames submitted to pipeline
 * @param frames_completed Total frames that completed processing
 * @param last_completion_time_ns Timestamp of last completion (nanoseconds since epoch)
 * @param timeout_seconds Maximum seconds to wait before timeout
 * @param log_level Log level for messages
 * @return true if timeout occurred (should break), false to continue waiting
 */
static bool check_pipeline_timeout(int frames_submitted,
                                   int frames_completed,
                                   int64_t last_completion_time_ns,
                                   int timeout_seconds,
                                   AppLogLevel log_level) {
  // Track last logged second to avoid duplicate logs
  static long last_logged_second = -1;

  // Calculate frames currently pending
  int frames_pending = frames_submitted - frames_completed;

  // If no frames pending, reset log tracker and return
  if (frames_pending == 0) {
    last_logged_second = -1;
    return false;
  }

  // Calculate time since last COMPLETION (not just any change)
  auto now = std::chrono::steady_clock::now();
  auto last_completion = std::chrono::steady_clock::time_point(std::chrono::nanoseconds(last_completion_time_ns));
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_completion).count();

  // Check for timeout - only when frames are stuck AND no completions
  if (frames_pending > 0 && elapsed >= timeout_seconds) {
    APP_LOG(AppLogLevel::ERROR, log_level,
            "Pipeline timeout: No completions for %ld seconds with %d frames stuck (submitted=%d, completed=%d)",
            elapsed, frames_pending, frames_submitted, frames_completed);
    last_logged_second = -1;  // Reset for next wait session
    return true;              // Timeout occurred
  }

  // Log status every second (only when frames are pending and transitioning to new second)
  if (frames_pending > 0 && elapsed > 0 && elapsed != last_logged_second) {
    APP_LOG(AppLogLevel::INFO, log_level,
            "Waiting for pipeline (%d frames pending) - %ld/%d seconds since last completion", frames_pending, elapsed,
            timeout_seconds);
    last_logged_second = elapsed;
  }

  return false;  // No timeout, continue waiting
}

/**
 * @brief Cross-validate --input-file against the parsed preprocess mode.
 *
 * Contract enforced here matches the user documentation:
 *   - Preprocessing mode (preprocess-en = true): --input-file is OPTIONAL.
 *     - If provided (CLI broadcast mode), it must be one of
 *       .jpg / .jpeg / .nv12 / .bgr and it overrides every model's
 *       ifms-config. For .nv12/.bgr, --dim WxH is mandatory.
 *     - If omitted (per-model mode), each model supplies its own image
 *       file via ifms-config in the model JSON; the per-entry extension
 *       check there enforces the same supported-format list. CLI --dim
 *       is unused in this mode and a warning is logged if provided.
 *   - Inference-only mode (preprocess-en = false): --input-file must NOT
 *     be provided, and CLI --dim is meaningless (warning logged if set).
 *     Each model supplies its own per-tensor binary inputs via
 *     ifms-config in the model JSON.
 *
 * @param ctx Application context (already populated by parse_toplevel_json_config).
 * @return true on success, false on a contract violation.
 */
static bool validate_input_file_against_mode(const AppContext* ctx) {
  AppLogLevel log_level = ctx->log_level;
  const bool dim_provided = (ctx->input_width != 0 || ctx->input_height != 0);

  if (ctx->preprocess_enable) {
    /* CLI broadcast is optional in preprocess mode; per-model mode reads
     * inputs from ifms-config and is validated separately by
     * parse_model_config(). Only do the extension check when the user
     * actually passed --input-file. */
    if (ctx->input_file_path.empty()) {
      if (dim_provided) {
        APP_LOG(AppLogLevel::WARNING, log_level,
                "--dim is ignored in per-model preprocessing mode; dimensions come from ifms-config.");
      }
      return true;
    }

    std::string extension = get_file_extension_lowercase(ctx->input_file_path);

    if (extension == "bin") {
      APP_LOG(AppLogLevel::ERROR, log_level, "preprocess-en is true but --input-file is a .bin file: %s",
              ctx->input_file_path.c_str());
      APP_LOG(AppLogLevel::ERROR, log_level, "For preprocessing mode, only .jpg/.jpeg/.nv12/.bgr files are supported.");
      return false;
    }
    if (extension != "jpg" && extension != "jpeg" && extension != "nv12" && extension != "bgr") {
      APP_LOG(AppLogLevel::ERROR, log_level,
              "Unsupported --input-file extension '%s' for preprocessing mode (file: %s)", extension.c_str(),
              ctx->input_file_path.c_str());
      APP_LOG(AppLogLevel::ERROR, log_level, "Supported formats: .jpg, .jpeg, .nv12, .bgr");
      return false;
    }

    /* For raw NV12/BGR inputs, --dim is mandatory; for JPEG it is unused. */
    if (extension == "nv12" || extension == "bgr") {
      if (!dim_provided) {
        APP_LOG(AppLogLevel::ERROR, log_level,
                "--input-file is %s (%s) but --dim WxH was not provided. NV12/BGR inputs require --dim.",
                extension.c_str(), ctx->input_file_path.c_str());
        return false;
      }
    } else if (dim_provided) {
      APP_LOG(AppLogLevel::WARNING, log_level,
              "--dim is ignored for JPEG --input-file (%s); dimensions are auto-detected.",
              ctx->input_file_path.c_str());
    }
  } else {
    if (!ctx->input_file_path.empty()) {
      APP_LOG(AppLogLevel::ERROR, log_level,
              "preprocess-en is false (inference-only) but --input-file was provided: %s",
              ctx->input_file_path.c_str());
      APP_LOG(AppLogLevel::ERROR, log_level,
              "In inference-only mode binary tensor inputs must be specified per model via ifms-config; "
              "do not pass --input-file.");
      return false;
    }
    if (dim_provided) {
      APP_LOG(AppLogLevel::WARNING, log_level,
              "--dim is ignored in inference-only mode; dimensions come from each model's ifms-config.");
    }
  }

  return true;
}

/**
 * @brief Main function for the application.
 *         File reading is now handled by AppFileReader thread
 *         The file reader thread automatically:
 *           - Reads frames from file
 *           - Manages its own buffer pool
 *           - Submits frames to preprocessing input queue
 *           - Handles iteration boundaries and file rewinding
 *           - Tracks frames in pipeline via queue submissions
 *         Main thread only monitors pipeline progress and handles results
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 if successful, non-zero error code otherwise.
 */
int main(int argc, char* argv[]) {
  int64_t num_frame_processed = 0;
  int ret = 0;

  AppContext ctx = {};

  /* Initialize handle parameters */
  init_app_context(&ctx);

  ret = read_user_inputs(argc, argv, &ctx);
  if (ret) {
    return ret;
  }

  /* Set log level based on handle configuration */
  AppLogLevel log_level = ctx.log_level;

  /* Set pipeline timeout based on benchmark mode */
  // Shorter timeout in benchmark mode (no file I/O), longer otherwise
  ctx.pipeline_timeout_seconds = ctx.is_benchmark_enabled ? 5 : 30;

  /* parse configurations from json file */
  try {
    if (!parse_toplevel_json_config(&ctx)) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Json parsing failed");
      return -1;
    }
  } catch (const std::exception& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Exception during JSON config parsing: %s", e.what());
    return -1;
  }

  /* Cross-validate --input-file against preprocess-en now that JSON is parsed.
   * Doing this here (rather than in read_user_inputs) lets the validator key
   * off ctx->preprocess_enable, which is read from the top-level JSON. */
  if (!validate_input_file_against_mode(&ctx)) {
    return -1;
  }

  /* Trace the parsed model list at RESULT level so it appears alongside
   * per-model inference results when the user runs with --log-level 3. */
  for (size_t idx = 0; idx < ctx.model_snap_path.size(); ++idx) {
    APP_LOG(AppLogLevel::RESULT, log_level, "Model %zu: %s", idx, ctx.model_snap_path[idx].c_str());
  }

  bool more_input = true;
  uint32_t batch_id = 0;
  int final_pipeline_count = 0;
  int frames_in_pipeline = 0;

  /* Create output directory if it doesn't exist */
  try {
    std::filesystem::create_directories(ctx.output_dir_path);

    /* Validate directory was created successfully */
    if (!std::filesystem::exists(ctx.output_dir_path)) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Failed to create output directory: %s", ctx.output_dir_path.c_str());
      /* Nothing has been built yet; skip pipeline flush / per-model reporting. */
      return 1;
    }
  } catch (const std::filesystem::filesystem_error& e) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Error creating output directory '%s': %s", ctx.output_dir_path.c_str(),
            e.what());
    /* Nothing has been built yet; skip pipeline flush / per-model reporting. */
    return 1;
  }

  /* Create the application contexts */
  if (create_all_context(&ctx) != true) {
    APP_LOG(AppLogLevel::ERROR, log_level, "Application context creation failed");
    /* create_all_context() may have partially populated ctx (e.g. parsed
     * num_model_instances) but not the per-model vectors that the
     * post-pipeline reporting block reads. Tear down whatever was built
     * and exit directly instead of running the summary on a ragged
     * context. */
    destroy_all_context(&ctx);
    return 1;
  }

  // Initialize completion timestamp to current time
  {
    auto init_time = std::chrono::steady_clock::now();
    ctx.last_completion_time_ns.store(
        std::chrono::duration_cast<std::chrono::nanoseconds>(init_time.time_since_epoch()).count());
  }

  APP_LOG(AppLogLevel::INFO, log_level, "\n\n****Starting main-thread (%u iterations) ****\n\n", ctx.max_iterations);
  /* Priority-based scheduling main loop */
  // Calculate active frames in pipeline
  frames_in_pipeline = ctx.frames_submitted.load() - ctx.frames_completed.load();
  while (frames_in_pipeline > 0 || more_input) {
    bool work_done_this_iteration = false;

    // EVENT-DRIVEN ERROR CHECK: Check for critical errors from any component
    if (ctx.critical_error.load()) {
      APP_LOG(AppLogLevel::ERROR, log_level, "Critical error detected from pipeline component");
      trigger_pipeline_shutdown(&ctx, "Component reported critical error");
      break;
    }

    // Update more_input flag based on file reader status
    // Check if any file reader is still running
    bool any_reader_running = false;
    bool any_reader_error = false;

    for (const auto& reader : ctx.file_readers) {
      if (reader && reader->is_running()) {
        any_reader_running = true;
      }
      if (reader && reader->has_error()) {
        any_reader_error = true;
      }
    }

    if (!any_reader_running) {
      more_input = false;
    }

    // Check for file reader errors (legacy check - now redundant with critical_error)
    if (any_reader_error) {
      APP_LOG(AppLogLevel::ERROR, log_level, "One or more file readers reported error, exiting application");
      break;
    }

    // Priority 1: Handle completed inference frames (write outputs)
    uint32_t inference_results_processed = handle_all_inference_completions(&ctx);
    if (inference_results_processed > 0) {
      work_done_this_iteration = true;

      uint32_t unique_frames_completed = inference_results_processed;

      // Update completion counter and timestamp
      ctx.frames_completed.fetch_add(unique_frames_completed);
      auto now = std::chrono::steady_clock::now();
      ctx.last_completion_time_ns.store(
          std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());

      // unique_frames_completed already represents actual frames (not batches)
      num_frame_processed += unique_frames_completed;
      // Log progress periodically
      if (num_frame_processed % 10 == 0) {
        APP_LOG(AppLogLevel::INFO, log_level, "Processed %ld frames", num_frame_processed);
      }
    }

    // Priority 2: Handle preprocessing completion (only if preprocessing is enabled)
    if (ctx.preprocess_enable) {
      uint32_t preprocess_results_routed = handle_preprocess_completion(&ctx);
      if (preprocess_results_routed > 0) {
        work_done_this_iteration = true;
        batch_id += preprocess_results_routed;
      }
    }

    if (work_done_this_iteration) {
      // Work was done, minimal sleep to check for more work quickly
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    } else if (frames_in_pipeline > 0) {
      // Check for pipeline timeout using new completion-based tracking
      if (check_pipeline_timeout(ctx.frames_submitted.load(), ctx.frames_completed.load(),
                                 ctx.last_completion_time_ns.load(), ctx.pipeline_timeout_seconds, log_level)) {
        break;  // Timeout occurred, exit loop
      }
      std::this_thread::sleep_for(std::chrono::microseconds(500));
    } else if (more_input) {
      // No frames in pipeline but more input expected
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    } else {
      // No work and no input expected, longer sleep
      APP_LOG(AppLogLevel::INFO, log_level, "Just waiting to finish - no work");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Recalculate active frames for next iteration
    frames_in_pipeline = ctx.frames_submitted.load() - ctx.frames_completed.load();
  }

  // Check if pipeline completed successfully or had errors
  final_pipeline_count = ctx.frames_submitted.load() - ctx.frames_completed.load();
  if (final_pipeline_count > 0) {
    APP_LOG(AppLogLevel::ERROR, log_level,
            "Pipeline exited with %d frames still in pipeline - possible timeout or error", final_pipeline_count);
    APP_LOG(AppLogLevel::ERROR, log_level, "Application exiting with errors - pipeline not fully processed");
  } else {
    APP_LOG(AppLogLevel::INFO, log_level, "Pipeline processing completed successfully");
  }

  // Flush pipeline - stop and drain all remaining frames (updates all counters)
  flush_pipeline(&ctx);

  // Report final status (after flush completes)
  final_pipeline_count = ctx.frames_submitted.load() - ctx.frames_completed.load();
  if (final_pipeline_count > 0) {
    cout << "\n[WARNING] Application exited with " << final_pipeline_count << " frames still in pipeline" << endl;
    cout << "This may indicate a timeout or error during processing" << endl;
  }

  // Recalculate total frames processed from per-model counters
  // This gives accurate count across all models (important for broadcast mode)
  num_frame_processed = 0;
  for (uint32_t i = 0; i < ctx.num_model_instances; i++) {
    if (i < ctx.frames_processed_per_model.size()) {
      num_frame_processed += ctx.frames_processed_per_model[i];
    }
  }

  cout << "\nTotal number of frames processed: " << num_frame_processed << endl;

  std::cout << "---------------------------------------------------------------------------------------" << std::endl;
  for (uint32_t i = 0; i < ctx.num_model_instances; i++) {
    // Use per-model frame count instead of global count
    int64_t model_frames = (i < ctx.frames_processed_per_model.size()) ? ctx.frames_processed_per_model[i] : 0;

    cout << "Model [" << ctx.model_snap_path[i] << "] with device batch size " << ctx.batch_size_per_model[i]
         << " processed " << model_frames << " frames \n";
    APP_LOG(AppLogLevel::INFO, log_level, "Model [%s] with device batch size %d processed %ld frames",
            ctx.model_snap_path[i].c_str(), ctx.batch_size_per_model[i], model_frames);

    if (model_frames == 0 && ctx.is_benchmark_enabled) {
      cout << "Model [" << ctx.model_snap_path[i] << "] - No frames processed. Skipping benchmark\n";
      if (i < (ctx.num_model_instances - 1)) {
        std::cout << std::endl;
      }
      continue;
    }

    if (ctx.batch_size_per_model[i] == 0) {
      APP_LOG(AppLogLevel::INFO, log_level, "Model [%s] has batch size 0. Skipping processed batch calculations",
              ctx.model_snap_path[i].c_str());
      if (i < (ctx.num_model_instances - 1)) {
        std::cout << std::endl;
      }
      continue;
    }

    // Calculate batch statistics for this model
    int64_t model_full_batches = model_frames / ctx.batch_size_per_model[i];
    int64_t model_remaining_frames = model_frames % ctx.batch_size_per_model[i];
    int64_t model_total_batches = model_full_batches + (model_remaining_frames > 0 ? 1 : 0);

    if (model_remaining_frames > 0) {
      // TODO maintain partial batches count per run
      APP_LOG(AppLogLevel::INFO, log_level, "(%ld full batches, 1 partial with %ld frames)", model_full_batches,
              model_remaining_frames);
    } else {
      APP_LOG(AppLogLevel::INFO, log_level, "(%ld full batches)", model_total_batches);
    }

    if (ctx.is_benchmark_enabled) {
      /* Steady state is the state of the pipeline where all stages are processing frames simultaneously i.e
       * steady state doesn't consider pipeline fill and drain times */
      double avg_preprocess_time = 0;
      double avg_inference_time = 0;
      double avg_postprocess_time = 0;

      // Build pipeline composition string
      std::string process_str = (ctx.preprocess.size() && ctx.preprocess[i]) ? "Preprocess + Inference" : "Inference";
      if (ctx.postprocess.size() && ctx.postprocess[i]) {
        process_str += " + Postprocess";
      }

      cout << "Steady-State Benchmark Results [Pipeline: " << process_str << "]...\n";

      if (ctx.preprocess.size() && ctx.preprocess[i]) {
        /* PreProcess processes frames sequentially */
        avg_preprocess_time = (ctx.preprocess[i]->get_total_time_us() / 1000.0) / model_frames;
        cout << std::left << std::setw(29) << "Average PreProcess latency"
             << ": " << avg_preprocess_time << " ms/frame\n";
      }

      if (ctx.inference.size() && ctx.inference[i]) {
        /* Inference processes frames parallelly */
        avg_inference_time = (ctx.inference[i]->get_total_time_us() / 1000.0) / model_total_batches;
        cout << std::left << std::setw(29) << "Average Inference latency"
             << ": " << avg_inference_time << " ms/batch\n";
      }

      if (ctx.postprocess.size() && ctx.postprocess[i]) {
        /* PostProcess processes frames sequentially */
        avg_postprocess_time = (ctx.postprocess[i]->get_total_time_us() / 1000.0) / model_frames;
        cout << std::left << std::setw(29) << "Average PostProcess latency"
             << ": " << avg_postprocess_time << " ms/frame\n";
      }

      /* Average latency for a frame is the accumulated time of all stages */
      auto avg_latency = avg_preprocess_time + avg_inference_time + avg_postprocess_time;

      auto pre_process_fps = (avg_preprocess_time > 0) ? (1000.0 / avg_preprocess_time) : 0;
      auto inference_fps = (avg_inference_time > 0) ? (1000.0 * ctx.batch_size_per_model[i] / avg_inference_time) : 0;
      auto post_process_fps = (avg_postprocess_time > 0) ? (1000.0 / avg_postprocess_time) : 0;

      /* In pipeline, throughput is limited by the slowest stage */
      auto pipeline_fps = std::min({pre_process_fps > 0 ? pre_process_fps : inference_fps, inference_fps,
                                    post_process_fps > 0 ? post_process_fps : inference_fps});

      cout << std::left << std::setw(29) << "Average pipeline latency"
           << ": " << avg_latency << " ms/frame\n";
      cout << std::left << std::setw(29) << "Average throughput"
           << ": " << pipeline_fps << " FPS\n";

      if (i < (ctx.num_model_instances - 1)) {
        std::cout << std::endl;
      }
    }
  }

  std::cout << "---------------------------------------------------------------------------------------" << std::endl;

  destroy_all_context(&ctx);
  return 0;
}
