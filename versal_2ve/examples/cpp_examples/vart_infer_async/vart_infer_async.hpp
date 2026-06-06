/*
 * Copyright (C) 2025-2026 Advanced Micro Devices, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "vart/vart_runner_factory.hpp"

/**
 * Async VART driver: creates the VAIML runner, exposes tensor metadata, and runs `execute_async` / `wait`
 * with I/O buffer pairs supplied by the caller (e.g. `main.cpp` allocates the job pool and handles IFM
 * file/random fill). NPU tensors allocated via `allocate_npu_tensor` are released when the runner is
 * destroyed (~`VartInferAsync`).
 */
class VartInferAsync {
 public:
  static constexpr std::uint32_t kNumConcurrentJobs =
      2;  ///< Parallel async job slots / tensor pools (see @c allocate_tensor_pools in @c main.cpp).
  static constexpr std::size_t kDefaultDryRunFrameCount =
      1;  ///< Dry-run IFM depth factor: multiplied by @c batch_size() sample rows in @c load_input_random.

  /**
   * @param model_path Path to the compiled model. Accepts either a `.rai` artifact file or
   *                   a VAIML compiled-model cache directory; the path is forwarded as-is to
   *                   `vart::RunnerFactory::create_runner`. Existence is checked, type is not.
   */
  explicit VartInferAsync(const std::string& model_path);
  ~VartInferAsync();

  VartInferAsync(const VartInferAsync&) = delete;
  VartInferAsync& operator=(const VartInferAsync&) = delete;

  /** Runner used to allocate @c vart::NpuTensor buffers; outlives those tensors until destruction. */
  std::shared_ptr<vart::Runner> runner() const { return runner_; }

  std::size_t batch_size() const { return batch_size_; }
  std::size_t num_input_tensors() const { return num_input_tensors_; }
  std::size_t num_output_tensors() const { return num_output_tensors_; }
  const std::vector<vart::NpuTensorInfo>& input_tensors_info() const { return input_tensors_info_; }
  const std::vector<vart::NpuTensorInfo>& output_tensors_info() const { return output_tensors_info_; }

  /**
   * Allocate one HW @c vart::NpuTensor via the underlying runner. Throws @c std::runtime_error
   * (wrapping any backend exception) on allocation failure so callers can fail loudly.
   */
  vart::NpuTensor allocate_npu_tensor(const vart::NpuTensorInfo& info);

  /** @overload (VART @c execute_async needs mutable output buffers; input is passed as @c const ref here.) */
  bool execute_async(const std::vector<std::vector<vart::NpuTensor>>& input_batch,
                     std::vector<std::vector<vart::NpuTensor>>& output_batch,
                     std::size_t frame_index);

  /**
   * Waits for the oldest in-flight job. The caller is responsible for writing OFMs after a successful return.
   * @param out_completed_outputs If non-null, on dequeue set to the output batch pointer passed to @c execute_async
   *        for that job before @c wait runs.
   * @param out_frame_index If non-null, set to the dequeued job's @c frame_index before @c wait runs.
   */
  bool wait_job(int timeout_ms,
                std::vector<std::vector<vart::NpuTensor>>** out_completed_outputs = nullptr,
                std::size_t* out_frame_index = nullptr);

  void dump_outputs(std::size_t total_input_frames) const;

 private:
  void create_runner();

  bool submit_async_job(const std::vector<std::vector<vart::NpuTensor>>& input_batch,
                        std::vector<std::vector<vart::NpuTensor>>& output_batch,
                        vart::JobHandle* out_handle);

  std::string model_path_;  ///< Compiled model path (`.rai` file or cache directory) passed to @c create_runner.
  std::shared_ptr<vart::Runner> runner_{};  ///< VAIML runner; owns allocation API for @c vart::NpuTensor.

  std::size_t batch_size_{1};                             ///< Model batch dimension from the runner.
  std::size_t num_input_tensors_{0};                      ///< Count of HW input tensors per batch row.
  std::size_t num_output_tensors_{0};                     ///< Count of HW output tensors per batch row.
  std::vector<vart::NpuTensorInfo> input_tensors_info_;   ///< Metadata for each input tensor (layout, size).
  std::vector<vart::NpuTensorInfo> output_tensors_info_;  ///< Metadata for each output tensor.

  /// One in-flight async job: runner handle, caller output batch pointer, app-defined frame id (FIFO queue).
  struct JobQueue {
    vart::JobHandle handle;                                     ///< Returned by @c Runner::execute_async.
    std::vector<std::vector<vart::NpuTensor>>* output_batch{};  ///< Same pointer passed to @c execute_async.
    std::size_t frame_index{0};                                 ///< Opaque tag (e.g. OFM naming).
  };
  std::deque<JobQueue> job_queue_;  ///< Oldest submitted job at front; paired with @c wait_job order.

  std::size_t frames_completed_{0};  ///< Successful @c wait_job completions (see @c dump_outputs).
};
