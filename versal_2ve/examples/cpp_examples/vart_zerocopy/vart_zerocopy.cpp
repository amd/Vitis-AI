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

/*
 * VART zero-copy classification sample:
 *   CPU decode (OpenCV) -> FPGA preprocess -> zero-copy IFM (same buffer as preprocess output VideoFrame;
 *   NpuTensor from dma-buf fd: vart::NpuTensor(ifm_meta, &fd, MemoryType::DMA_FD) after export_buffer().
 *   -> VAIML inference -> SOFTMAX postprocess on OFM.
 * See allocate_buffers() for buffer flow.
 */

#include "vart_zerocopy.hpp"

#include <algorithm>
#include <any>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include <opencv2/imgcodecs.hpp>

#include <vart/vart_device.hpp>
#include <vart/vart_inferresult_types.hpp>
#include <vart/vart_memory.hpp>
#include <vart/vart_memory_types.hpp>
#include <vart/vart_postprocess.hpp>
#include <vart/vart_postprocess_types.hpp>
#include <vart/vart_preprocess.hpp>
#include <vart/vart_videoframe.hpp>

#include <vart/vart_runner_factory.hpp>

namespace {

// Compile-time: xclbin location, which FPGA device slot, HLS vs SW preprocess,
// and DDR bank IDs for preprocess input/output VideoFrame allocations (must match platform memory map).
namespace pipeline_cfg {
constexpr const char* kXclbinPath = "/run/media/mmcblk0p1/x_plus_ml.xclbin";  // Preprocess xclbin path on this board
constexpr int kDeviceIndex = 1;  // FPGA device index for allocations; confirm with `xbutil examine` / `xrt-smi`
constexpr bool kPreprocessHls = true;
constexpr uint8_t kPreprocessInputMemBank = 2;   // Memory bank to which input port of preprocess is attached to
constexpr uint8_t kPreprocessOutputMemBank = 2;  // Memory bank to which output port of kernel is attached to
constexpr int kBatchSize = 1;                    // Must match compiled model batch
}  // namespace pipeline_cfg

// Inline JSON for softmax/top-k ImageNet labels. Root-level keys only — PostProcess does not read a nested
// "postprocess-config" wrapper.
std::string make_inline_postprocess_json() {
  return R"json({
  "topk": 1,
  "label-file-path": "/etc/vai/models/resnet50_int8/data/imagenet-classes-1000.txt",
  "post-process-print": "false",
  "quant-scale-factors": [1.0]
})json";
}

// Packed frame size for one full frame (bytes). NV12-style uses width*height*1.5; FP/BF16 packed formats use 8 B/px.
size_t bytes_for_video_format(vart::VideoFormat fmt, uint32_t width, uint32_t height) {
  switch (fmt) {
    case vart::VideoFormat::BGR:
    case vart::VideoFormat::RGB:
      return static_cast<size_t>(width) * static_cast<size_t>(height) * 3u;
    case vart::VideoFormat::BGRx:
    case vart::VideoFormat::RGBx:
      return static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
      // RGBx is the same per-pixel width as an HCWNC/4 tensor with N=1, which is supported by NPU.
    case vart::VideoFormat::BGRx_BF16:
    case vart::VideoFormat::RGBx_BF16:
    case vart::VideoFormat::BGRx_FP16:
    case vart::VideoFormat::RGBx_FP16:
      return static_cast<size_t>(width) * static_cast<size_t>(height) * 8u;  // WXHX(4*2) 2 bytes for 16 bits
    case vart::VideoFormat::RGBP_FP16:
      // 3 planes (R, G, B) x 2 bytes per component => 6 B/px.
      return static_cast<size_t>(width) * static_cast<size_t>(height) * 6u;
    case vart::VideoFormat::RGBP_FLOAT:
      // 3 planes (R, G, B) x 4 bytes per component => 12 B/px.
      return static_cast<size_t>(width) * static_cast<size_t>(height) * 12u;
    case vart::VideoFormat::Y_UV8_420:
      return static_cast<size_t>(static_cast<double>(width) * static_cast<double>(height) * 1.5);
    default:
      return 0u;
  }
}

// Short names for console logs (aligned with common format strings, not necessarily enum spellings).
const char* video_format_name(vart::VideoFormat fmt) {
  switch (fmt) {
    case vart::VideoFormat::BGR:
      return "BGR8";
    case vart::VideoFormat::RGB:
      return "RGB8";
    case vart::VideoFormat::BGRx:
      return "BGRx8";
    case vart::VideoFormat::RGBx:
      return "RGBx8";
    case vart::VideoFormat::BGRx_BF16:
      return "BGRx_BF16";
    case vart::VideoFormat::RGBx_BF16:
      return "RGBx_BF16";
    case vart::VideoFormat::BGRx_FP16:
      return "BGRx_FP16";
    case vart::VideoFormat::RGBx_FP16:
      return "RGBx_FP16";
    case vart::VideoFormat::RGBP_FP16:
      return "RGBP_FP16";
    case vart::VideoFormat::RGBP_FLOAT:
      return "RGBP_FLOAT";
    case vart::VideoFormat::Y_UV8_420:
      return "Y_UV8_420";
    default:
      return "(unknown VideoFormat)";
  }
}

// Default ImageNet ResNet50 values for vart::PreProcessInfo (mean/scale, PANSCAN). colour_format must match
// the runtime IFM dtype:
//   Zerocopy mode    : HW IFM dtype  -> RGBx / RGBx_BF16 / RGBx_FP16 (packed, channel-interleaved-pad-4).
//   NonZerocopy mode : CPU IFM dtype -> RGBP_FP16 / RGBP_FLOAT (planar float). Integer CPU IFM dtypes
//                                       are rejected by make_resnet50_preprocess_info.
// Same fields as set_preprocess_info.
namespace resnet50_preprocess_info {
constexpr float kMeanR = 123.675f;
constexpr float kMeanG = 116.28f;
constexpr float kMeanB = 103.53f;
constexpr float kScaleR = 0.017124f;
constexpr float kScaleG = 0.017507f;
constexpr float kScaleB = 0.017429f;
constexpr bool kMaintainAspectRatio = true;
constexpr bool kPanScanResizing = true;    // resizing-type "PANSCAN" (else "LETTERBOX")
constexpr bool kSymmetricPadding = false;  // used when resizing is LETTERBOX
}  // namespace resnet50_preprocess_info

// True when we should center-crop the input ROI to output aspect ratio before resize (see adjust_input_roi_pan_scan).
constexpr bool resnet50_preprocess_do_pan_scan() {
  return resnet50_preprocess_info::kMaintainAspectRatio && resnet50_preprocess_info::kPanScanResizing;
}

// Fills vart::PreProcessInfo from resnet50_preprocess_info::* defaults (model IFM height/width + runtime
// IFM dtype). `mode` selects which colour_format / qt_fctr policy applies:
//   Zerocopy mode: ifm_dtype is the HW IFM dtype. qt_fctr = 1.0 / runner quant scale (integer IFMs);
//                  float HW IFMs (BF16/FP16) get the same qt_fctr (the HLS kernel still uses it for
//                  mean/scale -> packed float conversion).
//   NonZerocopy mode: ifm_dtype is the CPU IFM dtype, restricted to FP16 / FLOAT32. qt_fctr is forced
//                     to 1.0 because the CPU runner consumes already-dequantized values. Any other
//                     CPU IFM dtype throws.
vart::PreProcessInfo make_resnet50_preprocess_info(uint32_t out_height,
                                                   uint32_t out_width,
                                                   vart::DataType ifm_dtype,
                                                   float ifm_quant_scale_from_runner,
                                                   TensorMode mode) {
  vart::PreProcessInfo info{};
  info.mean_r = resnet50_preprocess_info::kMeanR;
  info.mean_g = resnet50_preprocess_info::kMeanG;
  info.mean_b = resnet50_preprocess_info::kMeanB;
  info.scale_r = resnet50_preprocess_info::kScaleR;
  info.scale_g = resnet50_preprocess_info::kScaleG;
  info.scale_b = resnet50_preprocess_info::kScaleB;
  info.height = out_height;
  info.width = out_width;

  if (mode == TensorMode::Zerocopy) {
    // HW IFM path: HCWNC4-compatible packed formats. qt_fctr derives from the runner quant scale.
    if (ifm_quant_scale_from_runner <= 0.0f) {
      throw std::runtime_error(
          "Runner IFM quant scale from get_quant_parameters must be positive to set PreProcessInfo.qt_fctr "
          "(non-positive scale)");
    }
    info.qt_fctr = 1.0f / ifm_quant_scale_from_runner;
    switch (ifm_dtype) {
      case vart::DataType::INT8:
      case vart::DataType::UINT8:
        // RGBX == HCWNC/4 (N=1) for quantized IFM.
        info.colour_format = vart::VideoFormat::RGBx;
        break;
      case vart::DataType::BF16:
        info.colour_format = vart::VideoFormat::RGBx_BF16;
        break;
      case vart::DataType::FP16:
        info.colour_format = vart::VideoFormat::RGBx_FP16;
        break;
      default:
        throw std::runtime_error(
            std::string("zero-copy: HW IFM dtype is not supported. Expected INT8, UINT8, BF16, or "
                        "FLOAT16, got: ") +
            std::to_string(static_cast<int>(ifm_dtype)) +
            ". Run 'ml_vart --get-model-info <model>' to inspect the HW view of the input tensor.");
    }
  } else {
    // CPU IFM path: planar float (one byte size per format) chosen from the CPU IFM dtype. The runner
    // does not requantize CPU IFM tensors, so qt_fctr stays at 1.0.
    info.qt_fctr = 1.0f;
    switch (ifm_dtype) {
      case vart::DataType::FP16:
        info.colour_format = vart::VideoFormat::RGBP_FP16;
        break;
      case vart::DataType::FLOAT32:
        info.colour_format = vart::VideoFormat::RGBP_FLOAT;
        break;
      default:
        throw std::runtime_error(
            std::string("--non-zero-copy: CPU IFM dtype is not supported. Expected FP16 or FLOAT32, got: ") +
            std::to_string(static_cast<int>(ifm_dtype)) +
            ". Run 'ml_vart --get-model-info <model>' to inspect the CPU view of the input tensor.");
    }
  }
  // PANSCAN + maintain aspect → PreProcessType::DEFAULT and center-crop in_roi before resize;
  // LETTERBOX → PreProcessType::LETTERBOX + optional symmetric-padding.
  if (!resnet50_preprocess_info::kMaintainAspectRatio) {
    info.preprocess_type = vart::PreProcessType::DEFAULT;
    info.symmetric_padding = false;
  } else if (resnet50_preprocess_info::kPanScanResizing) {
    info.preprocess_type = vart::PreProcessType::DEFAULT;
    info.symmetric_padding = false;
  } else {
    info.preprocess_type = vart::PreProcessType::LETTERBOX;
    info.symmetric_padding = resnet50_preprocess_info::kSymmetricPadding;
  }
  return info;
}

// Human-readable preprocess resize policy for startup logs (PANSCAN vs LETTERBOX paths).
const char* preprocess_policy_description(const vart::PreProcessInfo& info) {
  if (resnet50_preprocess_do_pan_scan() && info.preprocess_type == vart::PreProcessType::DEFAULT) {
    return "PANSCAN + maintain-aspect";
  }
  if (info.preprocess_type == vart::PreProcessType::LETTERBOX) {
    return info.symmetric_padding ? "LETTERBOX + symmetric-padding" : "LETTERBOX";
  }
  return "DEFAULT (no PANSCAN in_roi crop)";
}

// Maps enum to fixed string for console (nullptr for unknown).
const char* postprocess_type_label(vart::PostProcessType t) {
  switch (t) {
    case vart::PostProcessType::SOFTMAX:
      return "SOFTMAX";
    case vart::PostProcessType::NMS:
      return "NMS";
    case vart::PostProcessType::RESNET50:
      return "RESNET50";
    case vart::PostProcessType::YOLOV2:
      return "YOLOV2";
    case vart::PostProcessType::TOPK:
      return "TOPK";
    case vart::PostProcessType::ARGMAX:
      return "ARGMAX";
    default:
      return nullptr;
  }
}

constexpr const char* kPipelinePreprocessBackendDescription =
    pipeline_cfg::kPreprocessHls ? "FPGA HLS (IMAGE_PROCESSING_HLS)" : "CPU software (IMAGE_PROCESSING_SW)";

// Host maps the preprocess-input VideoFrame for CPU memcpy into device memory — WRITE only.
constexpr vart::DataMapFlags kMapWrite = static_cast<vart::DataMapFlags>(static_cast<int>(vart::DataMapFlags::WRITE));

/**
 * VAIML Runner expects either:
 * - Path to a `.rai` file, or
 * - A directory that contains a `vaiml_par_*` partition layout (child named `vaiml_par_0`, …).
 * Passing only the parent folder when resources are a single `*.rai` fails validation (same as JSON `model-file`).
 */
std::string resolve_vaiml_model_path(const std::string& model_path) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path raw(model_path);
  if (!fs::exists(raw, ec)) {
    return model_path;
  }
  if (fs::is_regular_file(raw, ec)) {
    const fs::path c = fs::weakly_canonical(raw, ec);
    return ec ? model_path : c.string();
  }
  if (!fs::is_directory(raw, ec)) {
    return model_path;
  }
  const fs::path dir = fs::weakly_canonical(raw, ec);
  const fs::path dir_path = ec ? raw : dir;
  const std::string dir_str = dir_path.string();

  if (fs::is_directory(dir_path / "vaiml_par_0", ec)) {
    return dir_str;
  }

  std::vector<fs::path> rai_files;
  for (const auto& entry : fs::directory_iterator(dir_path, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_regular_file()) {
      continue;
    }
    if (entry.path().extension() == ".rai") {
      rai_files.push_back(entry.path());
    }
  }
  if (rai_files.size() == 1) {
    const fs::path c = fs::weakly_canonical(rai_files[0], ec);
    const std::string out = ec ? rai_files[0].string() : c.string();
    return out;
  }
  if (rai_files.size() > 1) {
    std::sort(rai_files.begin(), rai_files.end());
    const fs::path c = fs::weakly_canonical(rai_files.front(), ec);
    const std::string out = ec ? rai_files.front().string() : c.string();
    std::cerr << "[model] Multiple .rai files under \"" << dir_str << "\"; specify the full path to one "
              << "(using first lexicographically): \"" << out << "\"\n";
    return out;
  }

  return model_path;
}

// Maps TensorMode to the runner-options string used for input/output_tensor_type. Same value used for
// both directions in this app (HW or CPU end-to-end).
const char* tensor_type_option_for_mode(TensorMode mode) {
  return mode == TensorMode::Zerocopy ? "HW" : "CPU";
}

// vart::TensorType matching the runner's configured tensor mode (used when querying tensor metadata).
vart::TensorType tensor_type_for_mode(TensorMode mode) {
  return mode == TensorMode::Zerocopy ? vart::TensorType::HW : vart::TensorType::CPU;
}

const char* tensor_mode_label(TensorMode mode) {
  return mode == TensorMode::Zerocopy ? "zero-copy" : "non-zero-copy";
}

// Constructs a VAIML runner with input/output_tensor_type matching the requested mode.
//   Zerocopy    -> "HW"  (NPU-native shapes, zero-copy IFM/OFM via dma-buf fd).
//   NonZerocopy -> "CPU" (runner-translated CPU view; preprocess output is FP16/FP32 planar RGB).
std::shared_ptr<vart::Runner> create_infer_runner(const std::string& model_path, TensorMode mode) {
  const std::string tt = tensor_type_option_for_mode(mode);
  std::unordered_map<std::string, std::any> options{
      {"log_level", std::string{"WARNING"}},
      {"input_tensor_type", tt},
      {"output_tensor_type", tt},
  };
  return vart::RunnerFactory::create_runner(vart::RunnerType::VAIML, model_path, options);
}

// Clears batch vectors so NpuTensor destructors release runner-allocated OFM buffers (RAII). The
// postprocess-side vart::Memory views into the same buffers live in ofm_memory_ and are dropped
// separately by release_infer_tensors() before this function is called.
void deallocate_batch_tensors(std::vector<std::vector<vart::NpuTensor>>& batches) {
  // OFM tensors from Runner::allocate_npu_tensor are released when NpuTensor objects are destroyed (RAII).
  for (auto& batch : batches) {
    batch.clear();
  }
  batches.clear();
}

// Opens the shared XRT device handle used by PreProcess (and PostProcess on same device). When device_index >= 0,
// xclbin must exist on disk so VVAS/preprocess kernels can load.
std::shared_ptr<vart::Device> open_preprocess_device(int32_t device_index, const std::string& xclbin_path) {
  if (device_index >= 0) {
    if (xclbin_path.empty()) {
      throw std::runtime_error("open_preprocess_device: xclbin path is empty");
    }
    std::error_code ec;
    if (!std::filesystem::exists(xclbin_path, ec)) {
      throw std::runtime_error("open_preprocess_device: xclbin not found or inaccessible at \"" + xclbin_path +
                               "\" — edit pipeline_cfg::kXclbinPath in vart_zerocopy.cpp");
    }
  }
  return vart::Device::get_device_hdl(device_index, xclbin_path);
}

// Reads H x W from compiled tensor metadata. Only the IFM layouts that the HLS image_processing
// preprocess kernel can actually produce are accepted:
//   NHW    : shape = [N, H, W]
//   NHWC   : shape = [N, H, W, C]
//   NCHW   : shape = [N, C, H, W]
//   HCWNC4 : shape = [H, C/4, W, N, 4]   (N=1 in this app)
// Other VART memory layouts are rejected as vart::PreProcess can't support them.
bool model_input_hw_from_tensor_info(const vart::NpuTensorInfo& tensor_meta, uint32_t& out_h, uint32_t& out_w) {
  const auto& sh = tensor_meta.shape;
  switch (tensor_meta.memory_layout) {
    case vart::MemoryLayout::NHW:
      // [N, H, W]
      if (sh.size() >= 3) {
        out_h = sh[1];
        out_w = sh[2];
        return true;
      }
      break;
    case vart::MemoryLayout::NHWC:
      // [N, H, W, C]
      if (sh.size() >= 4) {
        out_h = sh[1];
        out_w = sh[2];
        return true;
      }
      break;
    case vart::MemoryLayout::NCHW:
      // [N, C, H, W]
      if (sh.size() >= 4) {
        out_h = sh[2];
        out_w = sh[3];
        return true;
      }
      break;
    case vart::MemoryLayout::HCWNC4:
      // [H, C/4, W, N, 4]  — N must be 1 (this app's pipeline_cfg::kBatchSize is 1).
      if (sh.size() >= 5) {
        out_h = sh[0];
        out_w = sh[2];
        return true;
      }
      break;
    default:
      // Every other MemoryLayout is rejected as unsupported for preprocess output.
      break;
  }
  return false;
}

// String for logging runner tensor dtypes.
const char* npu_data_type_name(vart::DataType dt) {
  switch (dt) {
    case vart::DataType::INT8:
      return "INT8";
    case vart::DataType::UINT8:
      return "UINT8";
    case vart::DataType::FLOAT32:
      return "FLOAT32";
    case vart::DataType::BF16:
      return "BF16";
    case vart::DataType::FP16:
      return "FP16";
    case vart::DataType::INT16:
      return "INT16";
    case vart::DataType::UINT16:
      return "UINT16";
    case vart::DataType::INT32:
      return "INT32";
    case vart::DataType::UINT32:
      return "UINT32";
    case vart::DataType::INT64:
      return "INT64";
    case vart::DataType::UINT64:
      return "UINT64";
    default:
      return "(unknown vart::DataType)";
  }
}

// Maps VAIML NPU dtypes to PostProcess TensorInfo::data_type. UINT8 uses INT8 in postprocess API (dequant via scale).
vart::TensorDataType npu_datatype_to_postprocess(vart::DataType dt) {
  switch (dt) {
    case vart::DataType::INT8:
      return vart::TensorDataType::INT8;
    case vart::DataType::UINT8:
      return vart::TensorDataType::INT8;
    case vart::DataType::FLOAT32:
      return vart::TensorDataType::FLOAT32;
    case vart::DataType::BF16:
      return vart::TensorDataType::BF16;
    case vart::DataType::FP16:
      return vart::TensorDataType::FP16;
    default:
      throw std::runtime_error("Unsupported NPU DataType for postprocess tensor config");
  }
}

// TensorInfo.scale_coeff: for INT8/UINT8 tensors use runner quant scale; float types use 1.0 (no extra dequant).
float quant_scale_coeff_for_postprocess(const vart::Runner& runner, const std::string& tensor_name, vart::DataType dt) {
  const vart::QuantParameters& qp = runner.get_quant_parameters(tensor_name);
  const float s = static_cast<float>(qp.scale);
  if (dt == vart::DataType::INT8 || dt == vart::DataType::UINT8) {
    if (s == 0.0f) {
      throw std::runtime_error("Quant scale is zero for INT8/UINT8 tensor: " + tensor_name);
    }
    return s;
  }
  return 1.0f;
}

// Center-crop input ROI to the output aspect ratio before resize (PANSCAN-style).
void adjust_input_roi_pan_scan(vart::PreProcessOp& preprocess_op) {
  const float current_aspect_ratio =
      static_cast<float>(preprocess_op.in_roi.width) / static_cast<float>(preprocess_op.in_roi.height);
  const float target_aspect_ratio =
      static_cast<float>(preprocess_op.out_roi.width) / static_cast<float>(preprocess_op.out_roi.height);

  int x = preprocess_op.in_roi.x;
  int y = preprocess_op.in_roi.y;
  int width = preprocess_op.in_roi.width;
  int height = preprocess_op.in_roi.height;

  if (current_aspect_ratio < target_aspect_ratio) {
    width = preprocess_op.in_roi.width;
    height = static_cast<int>(static_cast<float>(preprocess_op.in_roi.width) / target_aspect_ratio);
    x = 0;
    y = (preprocess_op.in_roi.height - height) / 2;
  } else {
    width = static_cast<int>(static_cast<float>(preprocess_op.in_roi.height) * target_aspect_ratio);
    height = preprocess_op.in_roi.height;
    x = (preprocess_op.in_roi.width - width) / 2;
    y = 0;
  }

  preprocess_op.in_roi.x = static_cast<uint16_t>(x);
  preprocess_op.in_roi.y = static_cast<uint16_t>(y);
  preprocess_op.in_roi.width = static_cast<uint16_t>(width);
  preprocess_op.in_roi.height = static_cast<uint16_t>(height);
}

// PreProcess::process(std::vector<PreProcessOp>& ops): full-frame in/out ROI, optional PANSCAN in_roi crop.
void run_preprocess_src_to_dest_frame(vart::PreProcess& preprocess,
                                      vart::VideoFrame& in_src_frame,
                                      vart::VideoFrame& out_dest_frame,
                                      bool do_pan_scan) {
  const vart::VideoInfo in_vinfo = in_src_frame.get_video_info();
  vart::VideoInfo out_vinfo = preprocess.get_output_vinfo();

  vart::RegionOfInterest in_roi{};
  in_roi.x = 0;
  in_roi.y = 0;
  in_roi.width = static_cast<uint16_t>(in_vinfo.width);
  in_roi.height = static_cast<uint16_t>(in_vinfo.height);

  vart::RegionOfInterest out_roi{};
  out_roi.x = 0;
  out_roi.y = 0;
  out_roi.width = static_cast<uint16_t>(out_vinfo.width);
  out_roi.height = static_cast<uint16_t>(out_vinfo.height);

  vart::PreProcessOp op{};
  op.in_roi = in_roi;
  op.out_roi = out_roi;
  op.in_frame = &in_src_frame;
  op.out_frame = &out_dest_frame;
  if (do_pan_scan) {
    adjust_input_roi_pan_scan(op);
  }
  std::vector<vart::PreProcessOp> ops = {op};
  // VART-X PreProcess API: submit one src→dst frame pair; the implementation reads the input VideoFrame,
  // applies resize/normalize/etc., and writes the preprocessed IFM-sized output VideoFrame.
  preprocess.process(ops);
}

constexpr vart::PreProcessImplType kPreprocessImpl = pipeline_cfg::kPreprocessHls
                                                         ? vart::PreProcessImplType::IMAGE_PROCESSING_HLS
                                                         : vart::PreProcessImplType::IMAGE_PROCESSING_SW;

// Depth-first print of InferResult tree (classification leaves show label/index/score).
void print_infer_result_node(vart::InferResult& node, int depth) {
  const std::string pad(static_cast<size_t>(depth) * 2, ' ');
  vart::InferResultData* raw = node.get_infer_result();
  if (raw == nullptr) {
    std::cout << pad << "(no leaf data on this node)\n";
  } else if (raw->result_type == vart::InferResultType::CLASSIFICATION) {
    const auto* c = static_cast<const vart::ClassificationResData*>(raw);
    for (size_t i = 0; i < c->label.size(); ++i) {
      std::cout << pad << "classification: label=\"" << c->label[i] << "\" index=" << static_cast<int>(c->index[i])
                << " score=" << c->confidence[i] << '\n';
    }
  } else {
    std::cout << pad << "result_type=" << static_cast<int>(raw->result_type) << '\n';
  }
  for (const auto& ch : node.get_children()) {
    print_infer_result_node(*ch, depth + 1);
  }
}

}  // namespace

// =====================================================================================
// VartZerocopyPipeline: lifecycle + initialization
// =====================================================================================

// Loads VAIML model, creates preprocess/postprocess on the XRT device, wires tensor metadata.
VartZerocopyPipeline::VartZerocopyPipeline(const std::string& model_path, TensorMode mode) : mode_(mode) {
  init_pipeline_(model_path);
}

// Tears down in reverse dependency order: release NPU buffers and shared device last.
VartZerocopyPipeline::~VartZerocopyPipeline() {
  rgb_preprocess_input_frame_.reset();
  release_infer_tensors();
  postprocess_.reset();
  preprocess_.reset();
  preprocess_device_.reset();
  runner_.reset();
}

// Sequencing matters: runner defines tensor geometry/quant; preprocess/postprocess are created from that metadata.
void VartZerocopyPipeline::init_pipeline_(const std::string& model_path) {
  // Order: runner first (IFM/OFM dtypes, shapes, batch from compiled model), then postprocess config string in code,
  // then preprocess + postprocess objects, then link postprocess tensor layout to runner metadata.
  std::cout << "\n"
               "--------------------------------------------------------------------------------\n"
               "Pipeline configuration (mode: "
            << tensor_mode_label(mode_)
            << ")\n"
               "--------------------------------------------------------------------------------\n";

  if (!setup_npu_runner_(model_path)) {
    throw std::runtime_error(
        "Could not load the compiled model for inference. This sample expects one input tensor, one output tensor, "
        "and batch size " +
        std::to_string(pipeline_cfg::kBatchSize) + ". Check the compiled model directory passed on the command line.");
  }

  if (!setup_preprocess_()) {
    throw std::runtime_error("Could not start the image preprocessor (step before the network).");
  }
  if (!setup_postprocess_()) {
    throw std::runtime_error("Could not start postprocess (built-in JSON config).");
  }
  if (!configure_postprocess_tensor_info_()) {
    throw std::runtime_error("Could not connect postprocess to the model's input/output sizes.");
  }

  std::cout << "\n  --- Build-time paths / config ---\n";
  std::cout << "  " << std::left << std::setw(26) << "postprocess config"
            << "inline (built-in string)" << '\n';
  std::cout << "  " << std::left << std::setw(26) << "batch size" << pipeline_cfg::kBatchSize << '\n';
  std::cout << "  " << std::left << std::setw(26) << "mem banks (pre in / out)"
            << static_cast<int>(pipeline_cfg::kPreprocessInputMemBank) << " / "
            << static_cast<int>(pipeline_cfg::kPreprocessOutputMemBank) << '\n';

  std::cout << "--------------------------------------------------------------------------------\n";
  std::cout
      << "Ready: " << tensor_mode_label(mode_)
      << " IFM/OFM via dma-buf fd (preprocess output -> runner IFM; runner OFM -> vart::Memory -> postprocess).\n";
}

// Creates VAIML runner, validates single IFM/OFM and batch, resolves H×W from tensor metadata into model_input_*.
bool VartZerocopyPipeline::setup_npu_runner_(const std::string& model_path) {
  // VAIML runner: model_path supplies tensor metadata. The runner is created with input/output_tensor_type
  // matching mode_, and tensor metadata is queried in that same view (HW for Zerocopy, CPU for NonZerocopy).
  // model_input_* (height/width) is read from whichever view exposes it; H/W is invariant across views.
  const std::string resolved = resolve_vaiml_model_path(model_path);
  runner_ = create_infer_runner(resolved, mode_);
  if (!runner_) {
    std::cerr << "Error: could not create the model runner from the given folder.\n";
    return false;
  }
  if (runner_->get_num_input_tensors() != 1u || runner_->get_num_output_tensors() != 1u) {
    std::cerr << "Error: this sample only supports models with exactly one input and one output tensor.\n";
    runner_.reset();
    return false;
  }
  if (runner_->get_batch_size() != static_cast<size_t>(pipeline_cfg::kBatchSize)) {
    std::cerr << "Error: model batch size is " << runner_->get_batch_size() << " but this app is built for batch "
              << pipeline_cfg::kBatchSize << ".\n";
    runner_.reset();
    return false;
  }
  const vart::TensorType ttype = tensor_type_for_mode(mode_);
  ifm_meta_ = runner_->get_tensors_info(vart::TensorDirection::INPUT, ttype)[0];
  ofm_meta_ = runner_->get_tensors_info(vart::TensorDirection::OUTPUT, ttype)[0];
  // Input tensor layout must be one the HLS preprocess kernel can actually produce: NHW, NHWC,
  // NCHW, or HCWNC4. Anything else (NHWC4/NHWC8, NC4HW4/NC8HW8, HCWNC8/HCWNC16, NHW16C4WC/
  // NHW16WC4C, GENERIC, ...) is rejected here so the dma-buf IFM bridge cannot silently feed
  // the runner an IFM in the wrong layout.
  if (!model_input_hw_from_tensor_info(ifm_meta_, model_input_height_, model_input_width_)) {
    std::cerr << "Error: input tensor \"" << ifm_meta_.name
              << "\" has an unsupported memory layout for this sample. Supported input layouts: "
                 "NHW, NHWC, NCHW, HCWNC4.\n";
    runner_.reset();
    return false;
  }

  std::cout << "\n  --- Model (VAIML runner) ---\n";
  if (model_path != resolved) {
    std::cout << "  " << std::left << std::setw(26) << "CLI -m (argument)" << model_path << '\n';
    std::cout << "  " << std::left << std::setw(26) << "Resolved path" << resolved << '\n';
  } else {
    std::cout << "  " << std::left << std::setw(26) << "Model path" << resolved << '\n';
  }
  std::cout << "  " << std::left << std::setw(26) << "Tensor view"
            << (mode_ == TensorMode::Zerocopy ? "HW (zero-copy)" : "CPU (non-zero-copy)") << '\n';
  std::cout << "  " << std::left << std::setw(26) << "IFM (runner input)" << '"' << ifm_meta_.name << "\"  "
            << model_input_width_ << " x " << model_input_height_ << "  " << npu_data_type_name(ifm_meta_.data_type)
            << "  " << ifm_meta_.size_in_bytes << " bytes\n";
  std::cout << "  " << std::left << std::setw(26) << "OFM (runner output)" << '"' << ofm_meta_.name << "\"  "
            << npu_data_type_name(ofm_meta_.data_type) << "  " << ofm_meta_.size_in_bytes << " bytes  batch "
            << runner_->get_batch_size() << '\n';
  return true;
}

// Opens preprocess device, constructs PreProcess (HLS or SW), applies ResNet50 PreProcessInfo + IFM qt_fctr.
bool VartZerocopyPipeline::setup_preprocess_() {
  // Preprocess uses the same XRT device as the preprocess xclbin. Device index and xclbin path are
  // compile-time constants in namespace pipeline_cfg (top of this file). Empty preprocess_json_: built-in
  // / default preprocess graph.
  std::cout << "\n  --- Device & preprocessor ---\n";
  std::cout << "  " << std::left << std::setw(26) << "Device index" << pipeline_cfg::kDeviceIndex << '\n';
  std::cout << "  " << std::left << std::setw(26) << "xclbin" << pipeline_cfg::kXclbinPath << '\n';
  try {
    preprocess_device_ = open_preprocess_device(static_cast<int32_t>(pipeline_cfg::kDeviceIndex),
                                                std::string(pipeline_cfg::kXclbinPath));
  } catch (const std::invalid_argument& e) {
    throw std::runtime_error(
        std::string("Preprocess/VVAS device failed after \"Failed to create VVAS context\" (see stderr). "
                    "Check xclbin path=\"") +
        pipeline_cfg::kXclbinPath +
        "\" matches the preprocess xclbin on this system and device index, "
        "XRT sees the device (xbutil examine), and VVAS libraries match the platform. Underlying: " +
        e.what());
  }
  preprocess_json_.clear();
  preprocess_ = std::make_unique<vart::PreProcess>(kPreprocessImpl, preprocess_json_, preprocess_device_);
  // Quant scale is only meaningful in Zerocopy mode (HW IFM is quantized). In NonZerocopy mode the CPU IFM
  // is float; pass a placeholder scale that make_resnet50_preprocess_info will ignore (qt_fctr is forced to
  // 1.0). Reading get_quant_parameters in CPU mode is allowed but the value is unused.
  float ifm_qscale = 1.0f;
  if (mode_ == TensorMode::Zerocopy) {
    ifm_qscale = static_cast<float>(runner_->get_quant_parameters(ifm_meta_.name).scale);
  }
  vart::PreProcessInfo preprocess_info =
      make_resnet50_preprocess_info(model_input_height_, model_input_width_, ifm_meta_.data_type, ifm_qscale, mode_);
  preprocess_->set_preprocess_info(preprocess_info);
  std::cout << "  " << std::left << std::setw(26) << "Backend" << kPipelinePreprocessBackendDescription << '\n';
  std::cout << "  " << std::left << std::setw(26) << "Output layout" << preprocess_info.width << " x "
            << preprocess_info.height << "  " << video_format_name(preprocess_info.colour_format) << "  |  "
            << preprocess_policy_description(preprocess_info) << '\n';
  if (mode_ == TensorMode::Zerocopy) {
    std::cout << "  [preprocess] " << std::fixed << std::setprecision(6) << "IFM quant scale " << ifm_qscale
              << "  |  qt_fctr " << preprocess_info.qt_fctr << "  (1 / IFM quant scale)\n"
              << std::defaultfloat;
  } else {
    std::cout << "  [preprocess] CPU IFM is float; qt_fctr forced to 1.0\n";
  }
  return true;
}

// Instantiates SOFTMAX PostProcess with inline JSON (labels path, top-k); shares preprocess_device_ for scheduling.
bool VartZerocopyPipeline::setup_postprocess_() {
  const vart::PostProcessType postproc_type = vart::PostProcessType::SOFTMAX;
  postprocess_config_data_ = make_inline_postprocess_json();
  postprocess_ = std::make_unique<vart::PostProcess>(postproc_type, postprocess_config_data_, preprocess_device_);
  const char* const ptl = postprocess_type_label(postproc_type);
  std::cout << "\n  --- Post-process (labels / softmax) ---\n";
  std::cout << "  " << std::left << std::setw(26) << "Config"
            << "inline (built-in string, no app-side JSON parsing)" << '\n';
  std::cout << "  " << std::left << std::setw(26) << "Type" << (ptl != nullptr ? ptl : "custom") << '\n';
  return static_cast<bool>(postprocess_);
}

// Calls set_config() with IFM/OFM TensorInfo matching runner names, shapes, sizes, and dequant scales.
bool VartZerocopyPipeline::configure_postprocess_tensor_info_() {
  if (!runner_ || !postprocess_) {
    return false;
  }
  // Postprocess must see the same IFM/OFM names, shapes, dtypes, and batch as the runner. The IFM/OFM
  // metadata we use here is the active view (HW or CPU) the runner was created with.
  //   Zerocopy mode    : scale_coeff = runner quant scale for INT8/UINT8, 1.0 for float dtypes (current
  //                      behaviour preserved via quant_scale_coeff_for_postprocess).
  //   NonZerocopy mode : scale_coeff forced to 1.0 — the runner returns dequantized values on the CPU
  //                      view, so postprocess must not apply quant scaling again.
  auto scale_for = [this](const vart::NpuTensorInfo& meta) -> float {
    if (mode_ == TensorMode::NonZerocopy) {
      return 1.0f;
    }
    return quant_scale_coeff_for_postprocess(*runner_, meta.name, meta.data_type);
  };

  /* Create vart::TensorInfos for vart::PostProcess */
  vart::TensorInfo ifm_ti{};
  ifm_ti.name = ifm_meta_.name;
  ifm_ti.direction = vart::TensorDataDirection::INPUT;
  ifm_ti.data_type = npu_datatype_to_postprocess(ifm_meta_.data_type);
  ifm_ti.shape = ifm_meta_.shape;
  ifm_ti.size = static_cast<uint32_t>(ifm_meta_.size_in_bytes);
  ifm_ti.scale_coeff = scale_for(ifm_meta_);

  vart::TensorInfo ofm_ti{};
  ofm_ti.name = ofm_meta_.name;
  ofm_ti.direction = vart::TensorDataDirection::OUTPUT;
  ofm_ti.data_type = npu_datatype_to_postprocess(ofm_meta_.data_type);
  ofm_ti.shape = ofm_meta_.shape;
  ofm_ti.size = static_cast<uint32_t>(ofm_meta_.size_in_bytes);
  ofm_ti.scale_coeff = scale_for(ofm_meta_);

  const uint32_t ofm_size = ofm_ti.size;
  const float ofm_scale_coeff = ofm_ti.scale_coeff;
  std::vector<vart::TensorInfo> cfg = {ifm_ti, std::move(ofm_ti)};
  postprocess_->set_config(cfg, static_cast<uint32_t>(runner_->get_batch_size()));
  std::cout << "  " << std::left << std::setw(26) << "PostProcess tensor sizes"
            << "IFM " << ifm_ti.size << " B  |  OFM \"" << ofm_meta_.name << "\" " << ofm_size << " B\n";
  std::cout << "  " << std::left << std::setw(26) << "Dequant scale (IFM/OFM)" << ifm_ti.scale_coeff << " / "
            << ofm_scale_coeff << '\n';
  return true;
}

// =====================================================================================
// VartZerocopyPipeline: decode + preprocess stage
// =====================================================================================

// Loads image to host BGR in input_packed_; clears any previous infer buffers and results.
bool VartZerocopyPipeline::read_and_decode(const std::string& image_path) {
  last_results_.clear();
  release_infer_tensors();
  rgb_preprocess_input_frame_.reset();
  return read_and_decode_impl_(image_path);
}

// Creates device-side preprocess input frame and runner OFM tensors (IFM tensors are bound later in preprocess).
bool VartZerocopyPipeline::allocate_buffers() {
  // ---------------------------------------------------------------------------
  //
  // Theoretical buffer flow (single image, batch element b):
  //
  //   (1) Decoder buffer on CPU
  //       - input_packed_ is host RAM (BGR888) filled by OpenCV decode.
  //       - This is software-owned data and is not directly visible to NPU kernels.
  //
  //   (2) Preprocess input buffer (VideoFrame on device)
  //       - rgb_preprocess_input_frame_ is an XRT-backed vart::VideoFrame.
  //       - We copy CPU decode bytes -> this VideoFrame (host-to-device staging step).
  //
  //   (3) Preprocess output buffer (VideoFrame on device)
  //       - preproc_output_frames_[b] is another XRT-backed vart::VideoFrame written by vart::PreProcess.
  //       - It holds model-ready IFM layout/format.
  //
  //   (4) NPU IFM (zero-copy: same backing memory as preprocess output VideoFrame; fd binding for runner)
  //       - infer_inputs_[b][0] is vart::NpuTensor(ifm_meta_, &fd, DMA_FD) after out_frame.export_buffer().
  //       - Same buffer preprocess wrote; fd value is copied inside NpuTensor (vart-ml). ifm_meta_ is the
  //         active view (HW for Zerocopy mode, CPU for NonZerocopy mode).
  //
  //   (5) NPU OFM (runner allocation + shared vart::Memory view for postprocess)
  //       - infer_outputs_[b][0] is allocated via runner_->allocate_npu_tensor(ofm_meta_) and written by
  //         the NPU during execute(). Its underlying buffer is exported as a dma-buf fd and imported as a
  //         vart::Memory in ofm_memory_[b][0] (XRT impl) so postprocess can consume the same bytes.
  //       - Runner::execute() handles cache/DMA sync internally; the host can read OFM as soon as it
  //         returns. (Explicit NpuTensor::sync_buffer() is only needed when the runner is created with
  //         "skip_in_bo_sync" / "skip_out_bo_sync" options.)
  //
  //   (6) Postprocess input view
  //       - postprocess_->process(ofm_memory_, batch) is called with the SAME vart::Memory handles. The
  //         modular backend map()s each Memory internally; no extra OFM copy.
  //       - PostProcess fills last_results_ (labels/scores).
  //
  // Buffer lifetime summary:
  //   - input_packed_ is refreshed by read_and_decode().
  //   - preprocess/npu buffers are recreated for each run path and released in release_infer_tensors().
  // ---------------------------------------------------------------------------
  if (!runner_ || !preprocess_) {
    std::cerr << "allocate_buffers: runner/preprocess not initialized.\n";
    return false;
  }
  if (input_packed_.empty()) {
    std::cerr << "allocate_buffers: call read_and_decode() first.\n";
    return false;
  }

  release_infer_tensors();
  rgb_preprocess_input_frame_.reset();
  preproc_output_frames_.clear();

  if (!create_preprocess_input_frame_()) {
    return false;
  }
  if (!create_infer_output_tensors_()) {
    rgb_preprocess_input_frame_.reset();
    return false;
  }
  std::cout << "[buffers] preprocess-in + NPU out allocated; IFM buffer appears during preprocess\n";
  return true;
}

// Allocates XRT VideoFrame for decoded BGR on pipeline_cfg::kPreprocessInputMemBank (HLS/SW preprocess consumes this).
bool VartZerocopyPipeline::create_preprocess_input_frame_() {
  // Packed BGR host buffer (input_packed_) is copied into this XRT VideoFrame (IFM lives in separate output frames).
  vart::VideoInfo in_vinfo{};
  preprocess_->get_input_vinfo(static_cast<int32_t>(image_height_), static_cast<int32_t>(image_width_),
                               vart::VideoFormat::BGR, in_vinfo);
  constexpr uint8_t kPreprocessInputMemBank = static_cast<uint8_t>(pipeline_cfg::kPreprocessInputMemBank);
  const size_t in_frame_bytes = bytes_for_video_format(in_vinfo.fmt, in_vinfo.width, in_vinfo.height);
  if (in_frame_bytes == 0u) {
    std::cerr << "create_preprocess_input_frame_: unsupported preprocess input format "
              << static_cast<int>(in_vinfo.fmt) << '\n';
    return false;
  }
  std::cout << "[buffers] preprocess in  " << video_format_name(in_vinfo.fmt) << " " << in_vinfo.width << "x"
            << in_vinfo.height << " " << in_frame_bytes << " B  bank " << static_cast<int>(kPreprocessInputMemBank)
            << '\n';
  try {
    rgb_preprocess_input_frame_ = std::make_unique<vart::VideoFrame>(
        vart::VideoFrameImplType::XRT, in_frame_bytes, kPreprocessInputMemBank, in_vinfo, preprocess_device_);
  } catch (const std::exception& e) {
    std::cerr << "create_preprocess_input_frame_: " << e.what() << '\n';
    return false;
  }
  return true;
}

// Allocates one OFM NpuTensor per batch element via the runner, then bridges each OFM into a vart::Memory
// (XRT, imported from the NpuTensor's exported fd) so PostProcess::process(...) can consume the same
// physical buffer the runner writes to.
bool VartZerocopyPipeline::create_infer_output_tensors_() {
  const size_t batch_size = runner_->get_batch_size();
  const auto& ofm_npu_meta = ofm_meta_;

  infer_inputs_.clear();
  infer_outputs_.clear();
  ofm_memory_.clear();
  infer_inputs_.resize(batch_size);
  infer_outputs_.resize(batch_size);
  ofm_memory_.resize(batch_size);
  preproc_output_frames_.resize(batch_size);
  for (size_t b = 0; b < batch_size; ++b) {
    vart::NpuTensor ofm;
    try {
      ofm = runner_->allocate_npu_tensor(ofm_npu_meta);
    } catch (const std::exception& e) {
      std::cerr << "create_infer_output_tensors_: allocate_npu_tensor (OFM) failed at batch " << b << ": " << e.what()
                << '\n';
      release_infer_tensors();
      return false;
    }
    // Export the runner-allocated OFM's underlying buffer as a dma-buf fd, then import it as a vart::Memory.
    // The vart::Memory refers to the same fd, so the original NpuTensor (which retains ownership
    // of the buffer) and the vart::Memory both reference the same CMA-backed allocation.
    const int ofm_fd = ofm.export_buffer();
    if (ofm_fd < 0) {
      std::cerr << "create_infer_output_tensors_: NpuTensor::export_buffer (OFM) failed at batch " << b << '\n';
      release_infer_tensors();
      return false;
    }
    std::shared_ptr<vart::Memory> ofm_mem;
    try {
      ofm_mem = std::make_shared<vart::Memory>(vart::MemoryImplType::XRT, ofm_fd, ofm_npu_meta.size_in_bytes,
                                               preprocess_device_);
    } catch (const std::exception& e) {
      std::cerr << "create_infer_output_tensors_: vart::Memory(XRT, fd, ...) failed at batch " << b << ": " << e.what()
                << '\n';
      release_infer_tensors();
      return false;
    }
    infer_inputs_[b].resize(1);
    infer_outputs_[b].push_back(std::move(ofm));
    ofm_memory_[b].push_back(std::move(ofm_mem));
  }
  std::cout << "[buffers] NPU out \"" << ofm_npu_meta.name << "\"  " << batch_size << "x" << ofm_npu_meta.size_in_bytes
            << " B  (runner-allocated; imported into vart::Memory for postprocess via dma-buf fd)\n";
  return true;
}

// First call (right after read_and_decode): allocate buffers, upload BGR, run HLS preprocess and bind
// the IFM dma-buf fd into infer_inputs_. Subsequent calls (benchmark loop) skip setup and just re-run
// the HLS preprocess into the same already-bound output frame.
bool VartZerocopyPipeline::preprocess() {
  if (input_packed_.empty()) {
    std::cerr << "preprocess: call read_and_decode() first.\n";
    return false;
  }
  try {
    // Hot path: setup already done by a prior preprocess() call after the most recent
    // read_and_decode(); just re-run HLS into the existing IFM-bound output frame.
    // Guard covers all three downstream invariants needed by run_hls_preprocess_only_ + the next
    // infer() / postprocess() pair:
    //   - OFM allocation + vart::Memory bridge done   -> ofm_memory_[0] non-empty
    //   - IFM dma-buf-bound NpuTensors populated     -> infer_inputs_[0] non-empty
    //   - preprocess output VideoFrame allocated      -> preproc_output_frames_[0] non-null
    if (!ofm_memory_.empty() && !ofm_memory_[0].empty() && !infer_inputs_.empty() && !infer_inputs_[0].empty() &&
        !preproc_output_frames_.empty() && preproc_output_frames_[0]) {
      return run_hls_preprocess_only_();
    }
    if (!allocate_buffers()) {
      return false;
    }
    return run_hw_preprocess_();
  } catch (const std::exception& e) {
    std::cerr << "preprocess: " << e.what() << '\n';
    release_infer_tensors();
    rgb_preprocess_input_frame_.reset();
    return false;
  }
}

// =====================================================================================
// VartZerocopyPipeline: public infer/postprocess/display wrappers
// =====================================================================================

bool VartZerocopyPipeline::infer() {
  return run_infer_();
}

// Runs softmax/top-k on the shared OFM vart::Memory handles (PostProcess::process maps them internally);
// fills last_results_.
bool VartZerocopyPipeline::postprocess() {
  last_results_.clear();
  if (!postprocess_ || !runner_) {
    return false;
  }
  if (ofm_memory_.empty() || ofm_memory_[0].empty()) {
    std::cerr << "postprocess: run preprocess() and infer() first.\n";
    return false;
  }
  try {
    const size_t batch_size = runner_->get_batch_size();
    if (verbose_) {
      std::cout << "[buffers] postprocess in: shared OFM vart::Memory (same dma-buf as runner OFM)\n";
      std::cout << "[buffers] postprocess out: InferResult tree in last_results_ (labels/scores)\n";
    }
    // PostProcess::process(vector<vector<shared_ptr<vart::Memory>>>, batch):
    //   ofm_memory_[b][t] is the SAME XRT buffer the runner wrote to (imported from the OFM NpuTensor's
    //   exported fd). The modular backend calls map()/unmap() internally; no extra OFM copy.
    if (verbose_) {
      std::cout << "[postprocess] decode labels  batch " << batch_size << '\n';
    }
    last_results_ = postprocess_->process(ofm_memory_, static_cast<uint32_t>(batch_size));
    return true;
  } catch (const std::exception& e) {
    std::cerr << "postprocess: " << e.what() << '\n';
    return false;
  }
}

// Prints classification tree from last_results_ to stdout.
void VartZerocopyPipeline::display() {
  print_infer_results_(last_results_);
}

// =====================================================================================
// VartZerocopyPipeline: inference + postprocess internals
// =====================================================================================

// Frees OFM NpuTensors and clears IFM slot vectors (new preprocess run reallocates as needed).
void VartZerocopyPipeline::release_infer_tensors() {
  // Teardown order matters: ofm_memory_ holds vart::Memory instances imported from each OFM
  // NpuTensor's exported dma-buf fd. The vart::Memory only ever held a dup of that fd — the
  // owning XRT BO lives in the runner-allocated NpuTensor in infer_outputs_. Drop the importer
  // (ofm_memory_) first, then the owner (infer_outputs_). Reversing this order would not corrupt
  // OFM today (the owner still has the BO), but keeps the invariant explicit so a future change
  // that switches vart::Memory to a take-ownership import doesn't silently free the buffer
  // out from under the NpuTensor.
  ofm_memory_.clear();
  if (runner_) {
    infer_inputs_.clear();
    deallocate_batch_tensors(infer_outputs_);
  } else {
    infer_inputs_.clear();
    infer_outputs_.clear();
  }
  preproc_output_frames_.clear();
}

// =====================================================================================
// VartZerocopyPipeline: decode/preprocess helper internals
// =====================================================================================

// Reads image with OpenCV as BGR888 into contiguous input_packed_; records native width/height.
bool VartZerocopyPipeline::load_input_image_bgr_(const std::string& image_path) {
  cv::Mat bgr = cv::imread(image_path, cv::IMREAD_COLOR);
  if (bgr.empty()) {
    std::cerr << "OpenCV imread failed (empty or unreadable): " << image_path << '\n';
    return false;
  }
  if (bgr.rows < 1 || bgr.cols < 1) {
    std::cerr << "Invalid image size after load.\n";
    return false;
  }

  // Native decoded resolution only: scaling to model IFM size is done in preprocess (HLS/SW), not OpenCV.
  const size_t row_bytes = static_cast<size_t>(bgr.cols) * 3u;
  const size_t nbytes = row_bytes * static_cast<size_t>(bgr.rows);
  input_packed_.resize(nbytes);
  if (bgr.isContinuous()) {
    std::memcpy(input_packed_.data(), bgr.data, nbytes);
  } else {
    for (int h = 0; h < bgr.rows; ++h) {
      uint8_t* dst = input_packed_.data() + static_cast<size_t>(h) * row_bytes;
      const uint8_t* src = bgr.ptr<uint8_t>(h);
      std::memcpy(dst, src, row_bytes);
    }
  }
  image_width_ = static_cast<uint32_t>(bgr.cols);
  image_height_ = static_cast<uint32_t>(bgr.rows);
  return true;
}

// Resets decode dimensions, loads file via load_input_image_bgr_.
bool VartZerocopyPipeline::read_and_decode_impl_(const std::string& image_path) {
  input_packed_.clear();
  image_width_ = image_height_ = 0;
  if (!load_input_image_bgr_(image_path)) {
    return false;
  }
  std::cout << "[decode] BGR " << image_width_ << "x" << image_height_ << "  " << input_packed_.size() << " B (host)\n";
  return true;
}

// Uploads host BGR to preprocess-in frame, runs HW/SW preprocess → IFM frames, binds IFM fd as infer_inputs_[b][0].
bool VartZerocopyPipeline::run_hw_preprocess_() {
  // Runtime path for preprocess input → IFM: upload_decoded_image_to_preprocess_input_, then
  // create_preprocess_outputs_and_bind_infer_inputs_ (see buffer-flow comment on allocate_buffers()).
  if (!runner_ || !preprocess_ || !rgb_preprocess_input_frame_) {
    return false;
  }
  const size_t batch_size = runner_->get_batch_size();
  if (infer_inputs_.size() != batch_size || infer_outputs_.size() != batch_size) {
    std::cerr << "run_hw_preprocess_: call allocate_buffers() first.\n";
    return false;
  }
  if (!upload_decoded_image_to_preprocess_input_()) {
    return false;
  }
  if (!create_preprocess_outputs_and_bind_infer_inputs_()) {
    return false;
  }
  std::cout << "[preprocess] done\n";
  return true;
}

// Hot path used by every preprocess() call after the first one (e.g. benchmark loop iterations).
// Skips the host->device BGR upload, the OFM/IFM allocations and the IFM dma-buf re-bind; just re-runs
// the HLS image_processing kernel into the already-allocated preprocess output frame. Same VideoFrame
// is bound to the runner IFM, so the next infer() picks up the newly-written IFM bytes without any
// further wiring.
bool VartZerocopyPipeline::run_hls_preprocess_only_() {
  if (!preprocess_ || !rgb_preprocess_input_frame_ || preproc_output_frames_.empty() || !preproc_output_frames_[0]) {
    std::cerr << "run_hls_preprocess_only_: pipeline buffers not initialised.\n";
    return false;
  }
  for (size_t b = 0; b < preproc_output_frames_.size(); ++b) {
    if (!preproc_output_frames_[b]) {
      std::cerr << "run_hls_preprocess_only_: output frame[" << b << "] is null.\n";
      return false;
    }
    run_preprocess_src_to_dest_frame(*preprocess_, *rgb_preprocess_input_frame_, *preproc_output_frames_[b],
                                     resnet50_preprocess_do_pan_scan());
  }
  return true;
}

// Maps preprocess-in VideoFrame, clears padded plane, copies decoded BGR rows (handles stride > row width).
bool VartZerocopyPipeline::upload_decoded_image_to_preprocess_input_() {
  // Decode path is host-side packed BGR (input_packed_); this step maps the preprocess-in VideoFrame and memcpy's in.
  vart::VideoFrame& rgb_frame = *rgb_preprocess_input_frame_;
  {
    const vart::VideoInfo src_vinfo = rgb_frame.get_video_info();
    const vart::VideoFrameMapInfo& map = rgb_frame.map(kMapWrite);
    if (map.nplanes < 1) {
      throw std::runtime_error("BGR input VideoFrame: expected at least one plane");
    }
    const vart::VideoPlaneInfo& pl = map.planes[0];
    const uint32_t frame_w = src_vinfo.width;
    const uint32_t frame_h = src_vinfo.height;
    const size_t copy_row_bytes = static_cast<size_t>(std::min(image_width_, frame_w)) * 3u;
    const uint32_t copy_rows = std::min(image_height_, frame_h);
    uint8_t* dst = pl.data + pl.offset;
    const uint8_t* src = input_packed_.data();
    // Zero full stride per row first: padded plane may be wider than copy_row_bytes; avoids garbage past image.
    for (uint32_t row = 0; row < frame_h; ++row) {
      std::memset(dst + static_cast<size_t>(row) * pl.stride, 0, static_cast<size_t>(pl.stride));
    }
    for (uint32_t row = 0; row < copy_rows; ++row) {
      const size_t src_row_bytes = static_cast<size_t>(image_width_) * 3u;
      std::memcpy(dst + static_cast<size_t>(row) * pl.stride, src + static_cast<size_t>(row) * src_row_bytes,
                  copy_row_bytes);
    }
    rgb_frame.unmap();
  }
  std::cout << "[upload] host BGR -> device preprocess input\n";
  return true;
}

// Per batch slot: allocate IFM-sized VideoFrame on output bank, run preprocess, zero-copy IFM bind infer_inputs_[b][0].
bool VartZerocopyPipeline::create_preprocess_outputs_and_bind_infer_inputs_() {
  // Zero-copy / non-zero-copy IFM bind: preprocess writes preproc_output_frames_[b]; NpuTensor(ifm_meta_,
  // &fd, DMA_FD) wraps the same buffer for the runner. The preprocess output byte size must equal the
  // runner IFM byte size — that is what makes the dma-buf import a valid IFM tensor.
  const size_t batch_size = runner_->get_batch_size();
  const auto& ifm_npu_meta = ifm_meta_;
  vart::VideoInfo out_vinfo = preprocess_->get_output_vinfo();
  const size_t out_frame_bytes = bytes_for_video_format(out_vinfo.fmt, out_vinfo.width, out_vinfo.height);
  if (out_frame_bytes == 0u) {
    std::cerr << "Unsupported preprocess output format " << static_cast<int>(out_vinfo.fmt) << '\n';
    return false;
  }
  if (out_frame_bytes != ifm_npu_meta.size_in_bytes) {
    std::cerr << "Preprocess output / runner IFM byte size mismatch: preprocess " << video_format_name(out_vinfo.fmt)
              << " " << out_vinfo.width << "x" << out_vinfo.height << " = " << out_frame_bytes << " B vs runner IFM \""
              << ifm_npu_meta.name << "\" " << ifm_npu_meta.size_in_bytes
              << " B. The chosen colour_format does not match the " << (mode_ == TensorMode::Zerocopy ? "HW" : "CPU")
              << " IFM layout.\n";
    return false;
  }
  constexpr uint8_t kPreprocessOutputMemBank = static_cast<uint8_t>(pipeline_cfg::kPreprocessOutputMemBank);
  std::cout << "[preprocess] IFM out  " << video_format_name(out_vinfo.fmt) << " " << out_vinfo.width << "x"
            << out_vinfo.height << " " << out_frame_bytes << " B  bank " << static_cast<int>(kPreprocessOutputMemBank)
            << '\n';
  vart::VideoFrame& rgb_frame = *rgb_preprocess_input_frame_;
  preproc_output_frames_.clear();
  preproc_output_frames_.resize(batch_size);
  for (size_t b = 0; b < batch_size; ++b) {
    preproc_output_frames_[b] = std::make_unique<vart::VideoFrame>(
        vart::VideoFrameImplType::XRT, out_frame_bytes, kPreprocessOutputMemBank, out_vinfo, preprocess_device_);
    vart::VideoFrame& out_frame = *preproc_output_frames_[b];
    run_preprocess_src_to_dest_frame(*preprocess_, rgb_frame, out_frame, resnet50_preprocess_do_pan_scan());

    // ifm_fd is a per-iteration local. NpuTensor(meta, &fd, DMA_FD) takes the *value* of the fd
    // (not a reference to this local). Do NOT hoist ifm_fd outside the loop or pass the same
    // pointer to multiple slots: each batch slot must capture its own fd value from its own
    // VideoFrame, otherwise every NpuTensor would end up pointing at the last-exported fd.
    int ifm_fd = out_frame.export_buffer();
    if (ifm_fd < 0) {
      std::cerr << "Failed to export dma-buf fd from preprocess output VideoFrame (batch " << b << ").\n";
      return false;
    }
    infer_inputs_[b].clear();
    infer_inputs_[b].emplace_back(ifm_npu_meta, &ifm_fd, vart::MemoryType::DMA_FD);
    if (b == 0u) {
      std::cout << "[" << tensor_mode_label(mode_) << " IFM] NpuTensor(meta, &fd, DMA_FD)  fd=" << ifm_fd << "  "
                << out_frame_bytes << " B\n";
    }
  }
  return true;
}

// Runs VAIML execute(infer_inputs_, infer_outputs_); IFM is zero-copy preprocess output (NpuTensor + DMA_FD).
bool VartZerocopyPipeline::run_infer_() {
  if (!runner_ || infer_inputs_.empty()) {
    std::cerr << "infer: no input tensors.\n";
    return false;
  }
  // infer_inputs_  : infer_inputs_[b][0] = NpuTensor(ifm_meta_, &fd, DMA_FD) — preprocess output (HW or
  //                  CPU view, depending on mode_) bound directly to the runner IFM.
  // infer_outputs_ : infer_outputs_[b][0] = runner-allocated OFM NpuTensor; its fd is also imported into
  //                  ofm_memory_[b][0] (vart::Memory) so postprocess sees the same bytes.
  // Runner::execute() syncs IFM/OFM buffers internally before returning; the host can read OFM as soon as
  // execute() returns. Explicit NpuTensor::sync_buffer() is only required if the runner is created with
  // "skip_in_bo_sync" / "skip_out_bo_sync" runner-options.
  if (verbose_) {
    std::cout << "[buffers] infer in : " << tensor_mode_label(mode_)
              << " IFM — NpuTensor(meta, &fd, DMA_FD) from preprocess output\n";
    std::cout << "[buffers] infer out: runner-allocated OFM (shared with postprocess via vart::Memory)\n";
    std::cout << "[infer] execute() batch " << infer_inputs_.size() << '\n';
  }
  const vart::StatusCode st = runner_->execute(infer_inputs_, infer_outputs_);
  if (st != vart::StatusCode::SUCCESS) {
    std::cerr << "infer: execute failed, status=" << static_cast<int>(st) << '\n';
    return false;
  }
  if (verbose_) {
    std::cout << "[infer] ok  OFM ready for host (runner-synced)\n";
  }
  return true;
}

// =====================================================================================
// VartZerocopyPipeline: result printing
// =====================================================================================

// Batch-major dump of InferResult trees using print_infer_result_node.
void VartZerocopyPipeline::print_infer_results_(
    const std::vector<std::vector<std::shared_ptr<vart::InferResult>>>& results) {
  for (size_t bi = 0; bi < results.size(); ++bi) {
    std::cout << "  batch " << bi << ":\n";
    for (const auto& node : results[bi]) {
      if (node) {
        print_infer_result_node(*node, 1);
      }
    }
  }
}
