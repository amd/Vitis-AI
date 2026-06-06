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

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <vart/vart_inferresult.hpp>
#include <vart/vart_memory.hpp>
#include <vart/vart_npu_tensor.hpp>
#include <vart/vart_preprocess_types.hpp>
#include <vart/vart_runner_factory.hpp>

namespace vart {
class Device;
class PreProcess;
class PostProcess;
class VideoFrame;
}  // namespace vart

// Tensor-type / buffer-binding mode for the demo.
//   Zerocopy    : runner is created with input/output_tensor_type=HW; HLS preprocess output is bound to
//                 the runner IFM via dma-buf fd (zero-copy IFM), runner-allocated HW OFM is exported as
//                 dma-buf fd and wrapped in a vart::Memory for postprocess (zero-copy OFM).
//   NonZerocopy : runner is created with input/output_tensor_type=CPU; HLS preprocess output is shaped
//                 to match the CPU IFM dtype (RGBP_FP16 or RGBP_FLOAT). Same dma-buf bridge to runner
//                 IFM and to postprocess via vart::Memory. Postprocess uses scale_coeff=1.0.
enum class TensorMode { Zerocopy, NonZerocopy };

class VartZerocopyPipeline {
 public:
  // mode selects HW (zero-copy) vs CPU (non-zero-copy) tensor wiring. It is stored before init so the
  // runner is created with the right input/output_tensor_type options.
  VartZerocopyPipeline(const std::string& model_path, TensorMode mode);
  ~VartZerocopyPipeline();

  VartZerocopyPipeline() = delete;
  VartZerocopyPipeline(const VartZerocopyPipeline&) = delete;
  VartZerocopyPipeline& operator=(const VartZerocopyPipeline&) = delete;
  VartZerocopyPipeline(VartZerocopyPipeline&&) = delete;
  VartZerocopyPipeline& operator=(VartZerocopyPipeline&&) = delete;

  TensorMode mode() const { return mode_; }

  bool read_and_decode(const std::string& image_path);

  // See buffer types & management comment block on allocate_buffers() in the .cpp.
  bool allocate_buffers();

  bool preprocess();

  bool infer();

  bool postprocess();

  void display();

  // Toggle the chatty per-call console logging done by preprocess() / infer() / postprocess().
  // Setup-time logs (one-time buffer allocations, dma-buf binds) are unaffected because they only
  // run on the first preprocess() call after read_and_decode(). main flips this to false before
  // entering the benchmark loop so subsequent iterations are silent.
  void set_verbose(bool v) { verbose_ = v; }

 private:
  // ---- Pipeline initialization ---------------------------------------------------------
  void init_pipeline_(const std::string& model_path);
  bool setup_npu_runner_(const std::string& model_path);
  bool setup_preprocess_();
  bool setup_postprocess_();
  bool configure_postprocess_tensor_info_();

  // Tensor-type mode (selected at construction, fixed for the lifetime of the pipeline).
  TensorMode mode_{TensorMode::Zerocopy};

  // Controls per-call stdout logging in infer() / postprocess(). One-time setup logs in
  // preprocess()'s first-call branch are not gated by this — they only fire once anyway.
  bool verbose_{true};

  // ---- Common image decode state -------------------------------------------------------
  std::vector<uint8_t> input_packed_;
  uint32_t image_width_{0};
  uint32_t image_height_{0};

  bool read_and_decode_impl_(const std::string& image_path);
  bool load_input_image_bgr_(const std::string& image_path);

  // ---- Preprocess stage ---------------------------------------------------------------
  std::shared_ptr<vart::Device> preprocess_device_;
  std::string preprocess_json_;
  std::unique_ptr<vart::PreProcess> preprocess_;
  uint32_t model_input_height_{0};
  uint32_t model_input_width_{0};

  std::unique_ptr<vart::VideoFrame> rgb_preprocess_input_frame_;
  std::vector<std::unique_ptr<vart::VideoFrame>> preproc_output_frames_;

  bool create_preprocess_input_frame_();
  bool upload_decoded_image_to_preprocess_input_();
  bool create_preprocess_outputs_and_bind_infer_inputs_();
  bool run_hw_preprocess_();
  // HLS preprocess into the already-allocated and IFM-bound output frame(s). Used by every
  // preprocess() call after the first; the first call still goes through the full
  // allocate + upload + bind + HLS path via run_hw_preprocess_.
  bool run_hls_preprocess_only_();

  // ---- Inference stage ----------------------------------------------------------------
  std::shared_ptr<vart::Runner> runner_;
  std::vector<std::vector<vart::NpuTensor>> infer_inputs_;
  std::vector<std::vector<vart::NpuTensor>> infer_outputs_;
  // Per (batch, output-tensor) vart::Memory built from the OFM NpuTensor's exported fd. Same physical
  // buffer the runner writes; passed directly to PostProcess::process(...) so the runner -> postprocess
  // hand-off is zero-copy in both Zerocopy and NonZerocopy modes.
  std::vector<std::vector<std::shared_ptr<vart::Memory>>> ofm_memory_;
  // Cached runner tensor metadata for the active mode (HW view in Zerocopy mode, CPU view in NonZerocopy
  // mode). Stored so preprocess / postprocess wiring can read them without re-querying the runner.
  vart::NpuTensorInfo ifm_meta_{};
  vart::NpuTensorInfo ofm_meta_{};

  bool create_infer_output_tensors_();
  void release_infer_tensors();
  bool run_infer_();

  // ---- Postprocess stage --------------------------------------------------------------
  // Built-in postprocess config text passed by lvalue reference to vart::PostProcess ctor.
  std::string postprocess_config_data_;
  std::unique_ptr<vart::PostProcess> postprocess_;
  std::vector<std::vector<std::shared_ptr<vart::InferResult>>> last_results_;

  static void print_infer_results_(const std::vector<std::vector<std::shared_ptr<vart::InferResult>>>& results);
};
