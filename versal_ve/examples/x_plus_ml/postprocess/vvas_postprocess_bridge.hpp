/*
 *
 * Copyright (C) 2026 Advanced Micro Devices, Inc.
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

/**
 * @file vvas_postprocess_bridge.hpp
 * @brief App-side driver for postprocess models built in x_plus_ml (resnet50,
 * yolox) that are invoked directly through the vvas_core postprocess C API
 * (vvas_postprocess_create/tensor/destroy), instead of the vart_x PostProcess
 * dispatch. This keeps src/vart_x (shared gen1/gen2) untouched: the model .so
 * is dlopened by path, and results are converted back to vart::InferResult so
 * the existing transform/metaconvert/overlay pipeline is unchanged.
 *
 * Non-native (mapped-pointer) path only: native zero-copy would require vart_x
 * internal memory handles, which are not part of the public API.
 */

#ifndef X_PLUS_ML_VVAS_POSTPROCESS_BRIDGE_HPP
#define X_PLUS_ML_VVAS_POSTPROCESS_BRIDGE_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <vart/vart_inferresult.hpp>
#include <vart/vart_inferresult_types.hpp>
#include <vart/vart_postprocess_types.hpp>  // TensorInfo, TensorDataType, TensorDataDirection

namespace xplusml {

/**
 * @brief Result kind the model .so emits, used to build the matching
 * vart::InferResult (classification for resnet50, detection for yolox).
 */
enum class VvasPPResultKind { CLASSIFICATION, DETECTION };

/**
 * @class VvasPostProcessBridge
 * @brief Opaque driver around a single vvas_core postprocess .so.
 *
 * Construction creates its OWN VvasContext (via the public
 * vvas_context_create using device index + xclbin location), so it does not
 * depend on any vart_x internal device handle. set_config forwards the model
 * OUTPUT tensor info to vvas_postprocess_create. process() maps the mapped
 * output tensor host pointers into VvasMemory, runs the plugin, and converts
 * the VvasInferPrediction tree into vart::InferResult objects.
 */
class VvasPostProcessBridge {
 public:
  /**
   * @param json_config   the "postprocess-config" JSON sub-string.
   * @param lib_path      absolute path of the model .so (dlopened by vvas_core).
   * @param result_kind   CLASSIFICATION (resnet50) or DETECTION (yolox).
   * @param device_idx    device index for vvas_context_create.
   * @param xclbin_loc    xclbin path for vvas_context_create.
   */
  VvasPostProcessBridge(std::string json_config, std::string lib_path,
                        VvasPPResultKind result_kind, int32_t device_idx,
                        std::string xclbin_loc);
  ~VvasPostProcessBridge();

  VvasPostProcessBridge(const VvasPostProcessBridge&) = delete;
  VvasPostProcessBridge& operator=(const VvasPostProcessBridge&) = delete;

  /**
   * @brief Forward model tensor info to vvas_postprocess_create. Only OUTPUT
   * tensors are passed to the plugin (INPUT tensors are skipped).
   */
  void set_config(const std::vector<vart::TensorInfo>& tensor_info,
                  uint32_t batch_size);

  /**
   * @brief Run postprocess on the mapped output-tensor host pointers of one
   * batch and return per-frame vart::InferResult lists. @data holds
   * (num_out_tensors * current_batch) pointers, frame-major.
   */
  std::vector<std::vector<std::shared_ptr<vart::InferResult>>> process(
      const std::vector<int8_t*>& data, uint32_t current_batch_size);

 private:
  struct Impl;
  std::unique_ptr<Impl> pimpl_;
};

}  // namespace xplusml

#endif  // X_PLUS_ML_VVAS_POSTPROCESS_BRIDGE_HPP
