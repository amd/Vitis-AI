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

#include "vart_infer_async.hpp"

#include <any>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace {

constexpr const char* kInputTensorType = "HW";
constexpr const char* kOutputTensorType = "HW";
constexpr const char* kRunnerLogLevel = "INFO";
constexpr int kResourceRetryDelayMs = 10;

static const std::unordered_map<vart::StatusCode, std::string> kStatusStr = {
    {vart::StatusCode::SUCCESS, "SUCCESS"},
    {vart::StatusCode::FAILURE, "FAILURE"},
    {vart::StatusCode::INVALID_INPUT, "INVALID_INPUT"},
    {vart::StatusCode::INVALID_OUTPUT, "INVALID_OUTPUT"},
    {vart::StatusCode::OUT_OF_MEMORY, "OUT_OF_MEMORY"},
    {vart::StatusCode::RUNTIME_ERROR, "RUNTIME_ERROR"},
    {vart::StatusCode::JOB_PENDING, "JOB_PENDING"},
    {vart::StatusCode::INVALID_JOB_ID, "INVALID_JOB_ID"},
    {vart::StatusCode::RESOURCE_UNAVAILABLE, "RESOURCE_UNAVAILABLE"},
};

}  // namespace

namespace fs = std::filesystem;

VartInferAsync::VartInferAsync(const std::string& model_path) : model_path_(model_path) {
  if (!fs::exists(model_path_)) {
    throw std::runtime_error("compiled model path does not exist: " + model_path_);
  }
  create_runner();
}

VartInferAsync::~VartInferAsync() {
  runner_.reset();
}

vart::NpuTensor VartInferAsync::allocate_npu_tensor(const vart::NpuTensorInfo& info) {
  try {
    return runner_->allocate_npu_tensor(info);
  } catch (const std::exception& e) {
    throw std::runtime_error(std::string("allocate_npu_tensor failed for tensor '") + info.name + "': " + e.what());
  }
}

void VartInferAsync::create_runner() {
  std::unordered_map<std::string, std::any> options = {
      {"log_level", std::string(kRunnerLogLevel)},
      {"input_tensor_type", std::string(kInputTensorType)},
      {"output_tensor_type", std::string(kOutputTensorType)},
  };
  try {
    runner_ = vart::RunnerFactory::create_runner(vart::RunnerType::VAIML, model_path_, options);
  } catch (const std::exception& e) {
    throw std::runtime_error("RunnerFactory::create_runner failed for '" + model_path_ + "': " + e.what());
  }
  if (!runner_) {
    throw std::runtime_error("RunnerFactory::create_runner returned null for '" + model_path_ + "'");
  }
  try {
    num_input_tensors_ = runner_->get_num_input_tensors();
    num_output_tensors_ = runner_->get_num_output_tensors();
    batch_size_ = runner_->get_batch_size();
    input_tensors_info_ = runner_->get_tensors_info(vart::TensorDirection::INPUT, vart::TensorType::HW);
    output_tensors_info_ = runner_->get_tensors_info(vart::TensorDirection::OUTPUT, vart::TensorType::HW);
  } catch (const std::exception& e) {
    throw std::runtime_error(std::string("Runner metadata query failed: ") + e.what());
  }
}

/**
 * Submits one inference job to the underlying @c vart::Runner without blocking for completion.
 *
 * Wraps @c runner_->execute_async(input_batch, output_batch). If the runner reports
 * @c vart::StatusCode::RESOURCE_UNAVAILABLE, sleeps @c kResourceRetryDelayMs milliseconds and retries in a
 * loop until a non-retry status is returned. Any other non-@c SUCCESS status is logged and fails the call.
 * Exceptions from @c execute_async are caught, logged, and treated as failure.
 *
 * Does not enqueue @c JobHandle entries; see @c execute_async for pairing with @c wait_job.
 *
 * @param input_batch  HW input tensors for one batch (caller-owned; must remain valid until the matching wait).
 * @param output_batch HW output tensors for one batch (same lifetime as inputs).
 * @param out_handle   Non-null pointer receiving the @c vart::JobHandle on successful submission.
 *
 * @return @c true if @p out_handle is non-null and submission returns @c SUCCESS; @c false otherwise.
 */
bool VartInferAsync::submit_async_job(const std::vector<std::vector<vart::NpuTensor>>& input_batch,
                                      std::vector<std::vector<vart::NpuTensor>>& output_batch,
                                      vart::JobHandle* out_handle) {
  if (!out_handle) {
    return false;
  }
  for (;;) {
    try {
      vart::JobHandle job_handle = runner_->execute_async(input_batch, output_batch);
      if (job_handle.status == vart::StatusCode::RESOURCE_UNAVAILABLE) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kResourceRetryDelayMs));
        continue;
      }
      if (job_handle.status != vart::StatusCode::SUCCESS) {
        std::cerr << "execute_async failed to start: " << kStatusStr.at(job_handle.status) << '\n';
        return false;
      }
      *out_handle = job_handle;
      return true;
    } catch (const std::exception& e) {
      std::cerr << "execute_async exception: " << e.what() << '\n';
      return false;
    }
  }
}

/**
 * Starts one asynchronous inference and records it for FIFO completion via @c wait_job.
 *
 * Calls @c submit_async_job; on success appends a @c JobQueue entry holding the returned @c JobHandle, the
 * address of @p output_batch (for caller correlation after @c wait_job), and @p frame_index (opaque tag for
 * the application). Does not copy or validate tensor payloads—the caller must populate @p input_batch before
 * calling.
 *
 * @param input_batch  Const reference to HW inputs (passed through to @c Runner::execute_async).
 * @param output_batch Mutable HW outputs for this job; pointer is stored until @c wait_job completes.
 * @param frame_index  Logical frame index echoed back from @c wait_job (e.g. for OFM naming).
 *
 * @return @c true if @c submit_async_job succeeded; @c false if submission failed (nothing enqueued).
 */
bool VartInferAsync::execute_async(const std::vector<std::vector<vart::NpuTensor>>& input_batch,
                                   std::vector<std::vector<vart::NpuTensor>>& output_batch,
                                   size_t frame_index) {
  vart::JobHandle job_handle;
  if (!submit_async_job(input_batch, output_batch, &job_handle)) {
    return false;
  }
  job_queue_.push_back(JobQueue{std::move(job_handle), &output_batch, frame_index});
  return true;
}

/**
 * Completes the oldest in-flight job (FIFO): dequeues metadata first, exposes output pointer and
 * @p frame_index to the caller, then blocks on @c runner_->wait for that job’s @c JobHandle.
 *
 * Ordering: if @p out_completed_outputs or @p out_frame_index are non-null, they are set from the dequeued
 * job before @c runner_->wait runs, so the caller knows which submission is being completed. If
 * @p out_completed_outputs is non-null, it is first cleared to @c nullptr when the call starts.
 *
 * On successful wait, increments @c frames_completed_. Does not write OFM files.
 *
 * @param timeout_ms          Maximum time for @c runner_->wait (implementation-defined interpretation).
 * @param out_completed_outputs If non-null, set to the output batch pointer registered at @c execute_async
 *                            for the dequeued job (before @c wait).
 * @param out_frame_index     If non-null, set to the @c frame_index stored for that job (before @c wait).
 *
 * @return @c false if the internal queue is empty, @c wait fails, or the job completes with non-@c SUCCESS;
 *         @c true when the job completes successfully.
 */
bool VartInferAsync::wait_job(int timeout_ms,
                              std::vector<std::vector<vart::NpuTensor>>** out_completed_outputs,
                              std::size_t* out_frame_index) {
  if (out_completed_outputs) {
    *out_completed_outputs = nullptr;
  }
  if (job_queue_.empty()) {
    return false;
  }
  JobQueue job = job_queue_.front();
  job_queue_.pop_front();
  if (out_completed_outputs) {
    *out_completed_outputs = job.output_batch;
  }
  if (out_frame_index) {
    *out_frame_index = job.frame_index;
  }

  try {
    job.handle.status = runner_->wait(job.handle, std::chrono::milliseconds{timeout_ms});
  } catch (const std::exception& e) {
    std::cerr << "wait exception: " << e.what() << " job_id=" << job.handle.job_id << '\n';
    return false;
  }
  if (job.handle.status != vart::StatusCode::SUCCESS) {
    std::cerr << "wait failed: " << kStatusStr.at(job.handle.status) << " job_id=" << job.handle.job_id << '\n';
    return false;
  }
  ++frames_completed_;
  return true;
}

void VartInferAsync::dump_outputs(std::size_t total_input_frames) const {
  std::cout << "Only last iteration OFM is written (see messages above).\n";
  std::cout << "Batches completed: " << frames_completed_ << " / " << total_input_frames << '\n';
}
