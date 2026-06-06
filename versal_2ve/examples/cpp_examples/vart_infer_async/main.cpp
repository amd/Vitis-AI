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

#include <boost/program_options.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "vart_infer_async.hpp"

namespace po = boost::program_options;
namespace fs = std::filesystem;

namespace {

void add_options(po::options_description& desc) {
  // clang-format off
  desc.add_options()
      ("help,h",                                           "Print this help message")
      ("num-iteration,n", po::value<uint32_t>()->default_value(10),
          "Number of full passes over the input IFM (>=1; default 10)")
      ("dry-run,d", po::bool_switch()->default_value(false),
          "Dry-run: fill IFMs with random bytes; skip reading the IFM file and writing OFM files")
      ("benchmark", po::bool_switch()->default_value(false),
          "Profile async (and sync) infer passes, skip OFM file writes, and run the sync comparison pass")
      ("model-path", po::value<std::string>()->required(),
          "Compiled model path: VAIML `.rai` file or compiled-model cache directory (positional: first, or this flag)")
      ("input_binary,input-binary", po::value<std::string>()->default_value(std::string()),
          "Input binary IFM (positional: second, or this flag); not used with --dry-run");
  // clang-format on
}

po::variables_map parse_arguments(int argc, char* argv[], const po::options_description& desc) {
  po::positional_options_description pos;
  pos.add("model-path", 1);
  pos.add("input_binary", 1);

  po::variables_map vm;
  po::store(po::command_line_parser(argc, argv).options(desc).positional(pos).run(), vm);

  if (vm.count("help")) {
    std::cout << desc << "\n";
    std::exit(0);
  }

  po::notify(vm);
  return vm;
}

/**
 * @return 0 if @p num_iteration, @p dry_run, and @p input_path are consistent; 1 to exit with an error.
 */
int validate_parsed_options(std::uint32_t num_iteration, bool dry_run, const std::string& input_path) {
  if (num_iteration < 1) {
    std::cerr << "[ERROR] --num-iteration must be >= 1.\n";
    return 1;
  }
  if (!dry_run && input_path.empty()) {
    std::cerr << "[ERROR] input_binary is required unless --dry-run is set \n"
                 "(Use --dry-run or provide a input binary with --input-binary option).\n";
    return 1;
  }
  if (dry_run) {
    std::cout << "Mode               : dry-run (no IFM/OFM file I/O; random IFM fill)\n\n";
  }
  return 0;
}

/** One IFM file frame: sum of input tensor sizes for a single batch row (one logical sample). */
std::size_t bytes_per_frame(const VartInferAsync& app) {
  std::size_t n = 0;
  for (std::size_t t = 0; t < app.num_input_tensors(); ++t) {
    n += app.input_tensors_info()[t].size_in_bytes;
  }
  return n;
}

/**
 * Reads up to @p batch_size contiguous IFM sample rows (@p bpf bytes each) from the start of the file.
 * If the file has fewer full rows than @p batch_size, reads all complete rows available (at least one required).
 * Trailing bytes shorter than one row are ignored. @p batch_size must be non-zero.
 */
std::size_t load_input_binary(const std::string& path,
                              std::size_t bpf,
                              std::size_t batch_size,
                              std::vector<std::uint8_t>& out_data) {
  if (!fs::exists(path)) {
    throw std::runtime_error("input file not found: " + path);
  }
  if (bpf == 0) {
    throw std::runtime_error("bytes per frame is zero");
  }
  if (batch_size == 0) {
    throw std::runtime_error("batch size is zero");
  }
  const std::uintmax_t file_size = fs::file_size(path);
  if (file_size < bpf) {
    throw std::runtime_error("input binary too small: need at least " + std::to_string(bpf) +
                             " bytes for one sample row (got " + std::to_string(file_size) + " byte(s))");
  }
  const std::size_t frames_available = static_cast<std::size_t>(file_size / bpf);
  const std::size_t frames_to_read = std::min(batch_size, frames_available);
  const std::size_t bytes_to_read = frames_to_read * bpf;
  out_data.resize(bytes_to_read);
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("failed to open input binary: " + path);
  }
  in.read(reinterpret_cast<char*>(out_data.data()), static_cast<std::streamsize>(bytes_to_read));
  if (static_cast<std::size_t>(in.gcount()) != bytes_to_read) {
    throw std::runtime_error("failed to read input binary");
  }
  return frames_to_read;
}

/** Dry-run IFM: fills @p out_data with @p num_frames * @p bpf random bytes (sample rows × bytes per row).
 *  Returns @p num_frames; throws if @p num_frames < 1 or @p bpf == 0. */
std::size_t load_input_random(std::size_t num_frames, std::size_t bpf, std::vector<std::uint8_t>& out_data) {
  if (num_frames < 1) {
    throw std::runtime_error("load_input_random: num_frames must be >= 1");
  }
  if (bpf == 0) {
    throw std::runtime_error("bytes per frame is zero");
  }
  out_data.assign(num_frames * bpf, 0);
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(0, 255);
  for (std::size_t i = 0; i < out_data.size(); ++i) {
    out_data[i] = static_cast<std::uint8_t>(dist(gen));
  }
  std::cout << "[INFO] dry-run: filled " << num_frames << " frame(s) with random bytes (no IFM file read).\n";
  return num_frames;
}

struct LoadedIfmBuffer {
  /** Bytes per IFM sample row (sum of input tensor sizes for one batch index). */
  std::size_t bytes_per_frame{0};
  /** Sample rows loaded; logical IFM frames = ceil(num_frames / model batch_size). */
  std::size_t num_frames{0};
  std::vector<std::uint8_t> data;
};

/**
 * Builds a contiguous IFM byte buffer for the compiled model: computes bytes per sample row from
 * input tensor metadata, then either reads from disk or fills with random data.
 *
 * Bytes per row: @c out.bytes_per_frame is @c bytes_per_frame(infer) — the sum of
 * @c size_in_bytes over all input tensors for one batch index (one logical sample).
 *
 * Non–dry-run: calls @c load_input_binary: reads up to @c infer.batch_size() consecutive sample
 * rows from the start of @p input_path (each row @c bytes_per_frame bytes). The value stored in
 * @c out.num_frames is the number of sample rows read (see @c load_input_binary). May throw if the
 * file is missing, too small, or unreadable.
 *
 * Dry-run: calls @c load_input_random with @c kDefaultDryRunFrameCount * infer.batch_size()
 * sample rows of random bytes (no file access). @p input_path is unused.
 *
 * @param infer       Constructed @c VartInferAsync for the target model (batch size and tensor layout).
 * @param dry_run     If @c true, random IFM; if @c false, read @p input_path.
 * @param input_path  Path to raw IFM binary; ignored when @p dry_run is @c true (may be empty).
 *
 * @return @c LoadedIfmBuffer with @c data populated and @c bytes_per_frame / @c num_frames set.
 * @throws std::runtime_error If @c bytes_per_frame is zero (no input tensors or zero-sized layout).
 */
LoadedIfmBuffer load_ifm_buffer_for_model(VartInferAsync& infer, bool dry_run, const std::string& input_path) {
  LoadedIfmBuffer out;
  out.bytes_per_frame = bytes_per_frame(infer);
  if (out.bytes_per_frame == 0) {
    throw std::runtime_error("bytes per input frame is zero");
  }
  out.num_frames = dry_run ? load_input_random(VartInferAsync::kDefaultDryRunFrameCount * infer.batch_size(),
                                               out.bytes_per_frame, out.data)
                           : load_input_binary(input_path, out.bytes_per_frame, infer.batch_size(), out.data);
  return out;
}

/**
 * This function is used for copying the IFM data for one batch to the HW input tensors at given slot.
 * If available data is less than the number of frames needed for the batch, the remaining rows are zero-filled.
 *
 * @param app             Model metadata and batch size (tensor shapes and @c get_virtual_address targets).
 * @param input_data      Contiguous IFM bytes: @c num_rows_in_buffer = size / bytes_per_frame sample rows.
 * @param bytes_per_frame Bytes per sample row (must be > 0).
 * @param total_frames    Upper bound on valid @p frame_index for this run (caller-defined logical frame count).
 * @param frame_index     Which logical frame to stage (0-based).
 * @param pool_index      Job slot index into @p input_tensors (outer dimension).
 * @param input_tensors   @c [pool][batch][tensor] @c vart::NpuTensor buffers to fill via @c memcpy / @c memset.
 *
 * @return @c false if @p frame_index >= @p total_frames, @p bytes_per_frame == 0, or any input tensor has a
 *         null virtual address; @c true if staging completed.
 */
bool copy_frame_to_slot(const VartInferAsync& app,
                        const std::vector<std::uint8_t>& input_data,
                        std::size_t bytes_per_frame,
                        std::size_t total_frames,
                        std::size_t frame_index,
                        std::uint32_t pool_index,
                        std::vector<std::vector<std::vector<vart::NpuTensor>>>& input_tensors) {
  if (frame_index >= total_frames) {
    return false;
  }
  if (bytes_per_frame == 0) {
    return false;
  }
  const std::size_t num_rows_in_buffer = input_data.size() / bytes_per_frame;
  if (num_rows_in_buffer < app.batch_size()) {
    std::cout << "[INFO] copy_frame_to_slot: IFM has " << num_rows_in_buffer << " full row(s); need "
              << app.batch_size() << " row(s) for frame_index=" << frame_index << " and batch_size=" << app.batch_size()
              << ". Short slots are zero-filled.\n";
  }
  for (std::size_t b = 0; b < app.batch_size(); ++b) {
    const std::size_t row_index = frame_index * app.batch_size() + b;
    const std::uint8_t* row_base =
        (row_index < num_rows_in_buffer) ? (input_data.data() + row_index * bytes_per_frame) : nullptr;
    std::size_t off = 0;
    for (std::size_t t = 0; t < app.num_input_tensors(); ++t) {
      const std::size_t sz = app.input_tensors_info()[t].size_in_bytes;
      void* va = input_tensors[pool_index][b][t].get_virtual_address();
      if (!va) {
        return false;
      }
      if (row_base) {
        std::memcpy(va, row_base + off, sz);
      } else {
        std::memset(va, 0, sz);
      }
      off += sz;
    }
  }
  return true;
}

/** Index of @p batch in @p pool, or @c pool.size() if @p batch is null or not found. */
std::uint32_t pool_index_for_batch(const std::vector<std::vector<std::vector<vart::NpuTensor>>>& pool,
                                   const std::vector<std::vector<vart::NpuTensor>>* batch) {
  if (!batch) {
    return static_cast<std::uint32_t>(pool.size());
  }
  for (std::uint32_t i = 0; i < pool.size(); ++i) {
    if (&pool[i] == batch) {
      return i;
    }
  }
  return static_cast<std::uint32_t>(pool.size());
}

static std::string data_type_to_string(vart::DataType data_type) {
  switch (data_type) {
    case vart::DataType::BOOLEAN:
      return "boolean";
    case vart::DataType::INT8:
      return "int8";
    case vart::DataType::UINT8:
      return "uint8";
    case vart::DataType::INT16:
      return "int16";
    case vart::DataType::UINT16:
      return "uint16";
    case vart::DataType::BF16:
      return "bf16";
    case vart::DataType::FP16:
      return "fp16";
    case vart::DataType::INT32:
      return "int32";
    case vart::DataType::UINT32:
      return "uint32";
    case vart::DataType::FLOAT32:
      return "float32";
    case vart::DataType::INT64:
      return "int64";
    case vart::DataType::UINT64:
      return "uint64";
    case vart::DataType::UNKNOWN:
      return "unknown";
    default:
      return "unrecognized";
  }
}

/**
 * Writes one completed batch of OFM tensors to disk: for each output tensor @e t, one file concatenates
 * batch rows @c b = 0 .. (rows_to_write - 1) in order. When @p num_total_passes is 1: @c output_f{frame_index}_{t}.bin
 * ; for multiple full passes: @c output_p{pass_1based}_f{frame_index}_{t}.bin .
 *
 * @param N_output_frames If @c 0, writes all @c app.batch_size() rows; otherwise writes at most this many batch
 *                         rows (capped by batch size). Use @c 1 when the IFM held only one sample row so OFMs
 *                         do not repeat padded batch rows.
 */
void write_outputs_for_frame(const VartInferAsync& app,
                             const std::vector<std::vector<vart::NpuTensor>>& ofm_batch,
                             std::size_t frame_index,
                             std::uint32_t pass_0based,
                             std::uint32_t num_total_passes,
                             std::size_t N_output_frames) {
  const std::size_t bsz = app.batch_size();
  const std::size_t rows_to_write = (N_output_frames == 0) ? bsz : std::min(N_output_frames, bsz);
  const std::size_t num_t = app.num_output_tensors();
  for (std::size_t t = 0; t < num_t; ++t) {
    const auto& info = app.output_tensors_info()[t];
    std::string path;
    if (num_total_passes <= 1) {
      path = "output_f" + std::to_string(frame_index) + "_" + std::to_string(t) + ".bin";
    } else {
      path = "output_p" + std::to_string(pass_0based + 1U) + "_f" + std::to_string(frame_index) + "_" +
             std::to_string(t) + ".bin";
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
      std::cerr << "Failed to open for write: " << path << '\n';
      continue;
    }
    std::vector<std::uint8_t> zero_row(info.size_in_bytes, 0);
    std::size_t total_written = 0;
    for (std::size_t b = 0; b < rows_to_write; ++b) {
      const void* va = ofm_batch[b][t].get_virtual_address();
      if (!va) {
        std::cerr << "output batch=" << b << " tensor=" << t << " (" << info.name
                  << "): no virtual address; writing zeros for this batch row in " << path << '\n';
        out.write(reinterpret_cast<const char*>(zero_row.data()), static_cast<std::streamsize>(info.size_in_bytes));
      } else {
        out.write(static_cast<const char*>(va), static_cast<std::streamsize>(info.size_in_bytes));
      }
      if (!out) {
        std::cerr << "Failed to write " << path << " at batch row " << b << '\n';
        break;
      }
      total_written += info.size_in_bytes;
    }
    if (out) {
      std::cout << "Wrote " << path << " (" << total_written << " bytes, " << rows_to_write
                << " batch rows) name=" << info.name << " dtype=" << data_type_to_string(info.data_type)
                << " frame=" << frame_index << " tensor=" << t << '\n';
    }
  }
}

/**
 * Completes one asynchronous job: waits on @c VartInferAsync::wait_job, returns the freed slot to the
 * submission queue, and optionally persists OFM tensors to disk for the **last** logical frame only.
 *
 * Calls @c infer.wait_job(30000, …) with a @b 30 second timeout. On success, resolves which job slot
 * finished by matching @c completed_outputs against @p output_tensors via @c pool_index_for_batch,
 * then @c buffer_index_queue.push_back(slot) so @c run_async_infer_pass can dequeue that slot for a new
 * @c execute_async.
 *
 * OFM file output: when @p write_ofm_files is @c true and @c completed_frame equals @c num_iteration - 1,
 * invokes @c write_outputs_for_frame for that batch (async path writes disk only for that final completed
 * frame in this sample). @p iter and @p num_iteration are forwarded only for output filename patterns in
 * @c write_outputs_for_frame.
 *
 * @param infer               Async runner wrapper (must match the instance used for @c execute_async).
 * @param output_tensors      Full tensor pool @c [slot][batch][tensor]; used to map @c completed_outputs to @c slot.
 * @param buffer_index_queue  Free-slot indices; receives @c slot after this wait completes successfully.
 * @param write_ofm_files     If @c false (e.g. @c --dry-run / @c --benchmark), skips @c write_outputs_for_frame.
 * @param iter                Pass index for OFM naming (see @c write_outputs_for_frame); async pass uses @c 0.
 * @param num_iteration       CLI @c num_iteration; last completed frame index for OFM write is @c num_iteration - 1;
 *                            also affects multi-pass OFM filenames when @c > 1.
 * @param N_output_frames     @c 0 = write full model batch in OFM files; @c 1 when IFM had a single sample row
 *                            (see @c write_outputs_for_frame).
 *
 * @return @c false if @c wait_job fails (timeout or runner error); @c true otherwise.
 */
bool wait_and_handle_completed_job(VartInferAsync& infer,
                                   const std::vector<std::vector<std::vector<vart::NpuTensor>>>& output_tensors,
                                   std::deque<std::uint32_t>& buffer_index_queue,
                                   bool write_ofm_files,
                                   std::uint32_t iter,
                                   std::uint32_t num_iteration,
                                   std::size_t N_output_frames) {
  std::vector<std::vector<vart::NpuTensor>>* completed_outputs = nullptr;
  std::size_t completed_frame = 0;
  if (!infer.wait_job(30000, &completed_outputs, &completed_frame)) {
    return false;
  }
  const std::uint32_t slot = pool_index_for_batch(output_tensors, completed_outputs);
  buffer_index_queue.push_back(slot);
  const bool is_last_frame = num_iteration > 0 && completed_frame == num_iteration - 1;
  if (write_ofm_files && is_last_frame) {
    write_outputs_for_frame(infer, output_tensors[slot], completed_frame, iter, num_iteration, N_output_frames);
  }
  return true;
}

/**
 * Runs one full asynchronous inference pass over @p N_frames logical frames using @c execute_async / @c wait_job.
 *
 * **Pipeline shape**
 * 1. **Prime**: While slots remain in @p buffer_index_queue and frames remain, dequeue a slot index, advance the
 *    next frame counter, and call @c VartInferAsync::execute_async for that slot (fills up to
 *    @c kNumConcurrentJobs in-flight submissions).
 * 2. **Steady state**: For each remaining frame index beyond the primed batch, @c wait_job completes the oldest
 *    job (FIFO), returns the finished slot to @p buffer_index_queue via @c wait_and_handle_completed_job
 *    (optionally writing OFMs for the final frame only when @p write_ofm_files), then if another frame remains and a
 * slot is free, submits @c execute_async for that slot.
 * 3. **Drain**: Issues @c kNumConcurrentJobs further @c wait_job calls so every outstanding submission completes.
 *
 * The caller must populate each slot's input tensors (e.g. @c copy_frame_to_slot) before the corresponding
 * @c execute_async for that frame runs; this function does not copy IFM bytes itself.
 *
 * @param infer               Async app owning the VAIML runner and tensor metadata.
 * @param input_tensors       Outer length @c kNumConcurrentJobs; each element is one job slot's HW input batch.
 * @param output_tensors      Same layout as @p input_tensors for outputs.
 * @param buffer_index_queue  Indices of slots currently free for submission; initialized by caller with
 *                            @c 0 .. kNumConcurrentJobs-1, updated as jobs complete.
 * @param write_ofm_files     When true, OFMs are written once via @c write_outputs_for_frame for the last completed
 * frame only.
 * @param num_iteration       Passed through for OFM naming only (@c write_outputs_for_frame).
 * @param N_frames            Total frames to process this pass (logical invocation count).
 * @param N_output_frames       Passed to @c write_outputs_for_frame when writing the last async frame (@c 0 = full
 * batch).
 *
 * @throws std::runtime_error On failed @c execute_async or @c wait_job (after dequeue).
 */
void run_async_infer_pass(VartInferAsync& infer,
                          std::vector<std::vector<std::vector<vart::NpuTensor>>>& input_tensors,
                          std::vector<std::vector<std::vector<vart::NpuTensor>>>& output_tensors,
                          std::deque<std::uint32_t>& buffer_index_queue,
                          bool write_ofm_files,
                          std::uint32_t num_iteration,
                          std::size_t N_frames,
                          std::size_t N_output_frames) {
  std::size_t next_frame = 0;
  const std::size_t iter = 0;

  std::cout << "---- Async infer (binary IFM) pass "
            << " ----\n";

  // ----------------------------- 1. Fill the pipeline -----------------------------
  // Submit up to kNumConcurrentJobs execute_async calls without waiting—each takes a free slot
  // and the next frame index—so several jobs are in flight before the steady wait/submit loop.
  for (std::uint32_t k = 0; k < VartInferAsync::kNumConcurrentJobs; ++k) {
    if (buffer_index_queue.empty() || next_frame >= N_frames) {
      break;
    }
    const std::uint32_t slot = buffer_index_queue.front();
    buffer_index_queue.pop_front();
    const std::size_t frame_index = next_frame++;
    // Queue this frame on the chosen buffer slot: non-blocking submission so several inferences can
    // overlap; input_tensors[slot] must already contain the frame's IFM bytes (see copy_frame_to_slot).
    const bool submitted =
        infer.execute_async(static_cast<const std::vector<std::vector<vart::NpuTensor>>&>(input_tensors[slot]),
                            output_tensors[slot], frame_index);
    if (!submitted) {
      throw std::runtime_error("execute_async failed");
    }
  }

  // -------- 2. Wait for one completed job and submit the next frame --------
  // For each remaining frame, complete one job (FIFO wait), recycle its slot to the free queue,
  // then submit the next frame_index on that slot if any frames are left—overlap wait with new submissions.
  for (std::size_t i = VartInferAsync::kNumConcurrentJobs; i < num_iteration; ++i) {
    // Block until the next completed async job (FIFO order): pushes its buffer slot back onto
    // buffer_index_queue and may write OFM files when that job was the last logical frame.
    const bool waited_ok = wait_and_handle_completed_job(infer, output_tensors, buffer_index_queue, write_ofm_files,
                                                         iter, num_iteration, N_output_frames);
    if (!waited_ok) {
      throw std::runtime_error("wait_job failed");
    }
    if (!buffer_index_queue.empty() && next_frame < N_frames) {
      const std::uint32_t slot = buffer_index_queue.front();
      buffer_index_queue.pop_front();
      const std::size_t frame_index = next_frame++;
      // Queue this frame on the slot just recycled by wait—non-blocking; input_tensors[slot] must already
      // contain the frame's IFM bytes (see copy_frame_to_slot).
      const bool submitted =
          infer.execute_async(static_cast<const std::vector<std::vector<vart::NpuTensor>>&>(input_tensors[slot]),
                              output_tensors[slot], frame_index);
      if (!submitted) {
        throw std::runtime_error("execute_async failed");
      }
    }
  }

  // ----------------------------- 3. Drain the pipeline -----------------------------
  // Wait for all still-in-queue jobs (up to kNumConcurrentJobs) so the pipeline is empty.
  for (std::uint32_t k = 0; k < VartInferAsync::kNumConcurrentJobs; ++k) {
    // Drain one in-flight job: blocking wait until it completes, then return its slot to buffer_index_queue
    // (same semantics as the steady-state wait above; no further execute_async after the main loop).
    const bool waited_ok = wait_and_handle_completed_job(infer, output_tensors, buffer_index_queue, write_ofm_files,
                                                         iter, num_iteration, N_output_frames);
    if (!waited_ok) {
      throw std::runtime_error("wait_job failed");
    }
  }
}

/** N_frames synchronous `Runner::execute` calls over the same pools as the async path (`vart_batching_app`). */
void run_sync_infer_pass(VartInferAsync& infer,
                         std::vector<std::vector<std::vector<vart::NpuTensor>>>& input_tensors,
                         std::vector<std::vector<std::vector<vart::NpuTensor>>>& output_tensors,
                         std::size_t N_frames,
                         bool write_ofm_files,
                         std::uint32_t num_iteration,
                         std::size_t N_output_frames) {
  const std::shared_ptr<vart::Runner> runner = infer.runner();
  if (!runner) {
    throw std::runtime_error("runner is null");
  }
  std::cout << "---- Sync infer (binary IFM) pass ----\n";

  for (std::size_t frame_index = 0; frame_index < N_frames; ++frame_index) {
    const std::uint32_t slot = static_cast<std::uint32_t>(frame_index % VartInferAsync::kNumConcurrentJobs);
    vart::StatusCode ret = vart::StatusCode::FAILURE;
    try {
      ret = runner->execute(input_tensors[slot], output_tensors[slot]);
    } catch (const std::exception& e) {
      throw std::runtime_error(std::string("execute failed: ") + e.what());
    }
    if (ret != vart::StatusCode::SUCCESS) {
      throw std::runtime_error("execute failed with status " + std::to_string(static_cast<int>(ret)));
    }
    if (write_ofm_files) {
      write_outputs_for_frame(infer, output_tensors[slot], frame_index, 1U, num_iteration, N_output_frames);
    }
  }
}

/** Wall-clock timing for an infer pass (same pattern as `vart_multimodel_seq/main.cpp`). */
void log_infer_pass_wall_time(const char* label,
                              std::chrono::high_resolution_clock::time_point t0,
                              std::chrono::high_resolution_clock::time_point t1,
                              std::size_t n_frames) {
  const auto dur_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
  const double total_ms = static_cast<double>(dur_ns) / 1.0e6;
  std::cout << "[INFO] " << label << ": " << std::fixed << std::setprecision(3) << total_ms << " ms";
  if (n_frames > 0) {
    std::cout << " (" << std::setprecision(3) << (total_ms / static_cast<double>(n_frames)) << " ms/frame)";
  }
  std::cout << " [" << n_frames << " frames]\n";
}

/**
 * Allocates one full input batch and one full output batch of @c vart::NpuTensor buffers per
 * concurrent async job. Each **job** (outer index @c 0 .. @p num_concurrent_jobs - 1) is an
 * independent slot used with @c VartInferAsync::execute_async / @c wait_job: the host can
 * fill one slot’s inputs while another job is in flight on the NPU.
 *
 * **Layout** (same for @p input_tensors and @p output_tensors):
 *   - @c [job][batch][tensor]  —  @c job indexes the pool slot; @c batch is the model batch
 *     index @c 0 .. @c batch_size-1; @c tensor is the input or output tensor index.
 *   - Every @c vart::NpuTensor is allocated via @c a.allocate_npu_tensor() using
 *     @c input_tensors_info() / @c output_tensors_info() from the @c VartInferAsync runner.
 *
 * @param a                    Open @c VartInferAsync instance (model loaded; provides tensor metadata
 *                            and the underlying @c vart::Runner for allocation).
 * @param num_concurrent_jobs  Number of job slots; should match @c VartInferAsync::kNumConcurrentJobs
 *                            and the @c run_async_infer_pass / @c run_sync_infer_pass schedule.
 * @param input_tensors        Appended in place: after the call, size is @p num_concurrent_jobs;
 *                            each element is @c batch_size × @c num_input_tensors HW tensors.
 * @param output_tensors       Same structure for outputs (@c batch_size × @c num_output_tensors
 *                            per job).
 *
 * @return Always @c true (no failure path; allocation throws from @c allocate_npu_tensor on error).
 */
bool allocate_tensor_pools(VartInferAsync& a,
                           std::uint32_t num_concurrent_jobs,
                           std::vector<std::vector<std::vector<vart::NpuTensor>>>& input_tensors,
                           std::vector<std::vector<std::vector<vart::NpuTensor>>>& output_tensors) {
  const auto& ii = a.input_tensors_info();
  const auto& oi = a.output_tensors_info();
  for (std::uint32_t job = 0; job < num_concurrent_jobs; ++job) {
    std::vector<std::vector<vart::NpuTensor>> batch_in;
    std::vector<std::vector<vart::NpuTensor>> batch_out;
    for (std::size_t b = 0; b < a.batch_size(); ++b) {
      std::vector<vart::NpuTensor> input_tensors;
      std::vector<vart::NpuTensor> output_tensors;
      for (std::size_t t = 0; t < ii.size(); ++t) {
        input_tensors.push_back(a.allocate_npu_tensor(ii[t]));
      }
      for (std::size_t t = 0; t < oi.size(); ++t) {
        output_tensors.push_back(a.allocate_npu_tensor(oi[t]));
      }
      batch_in.push_back(std::move(input_tensors));
      batch_out.push_back(std::move(output_tensors));
    }
    input_tensors.push_back(std::move(batch_in));
    output_tensors.push_back(std::move(batch_out));
  }
  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    std::vector<std::vector<std::vector<vart::NpuTensor>>> input_tensors;
    std::vector<std::vector<std::vector<vart::NpuTensor>>> output_tensors;

    // ---------------------------------------------------------------------------
    // Argument parsing: CLI definition, parse + notify, then typed bindings.
    // ---------------------------------------------------------------------------
    po::options_description desc("\n**** vart_infer_async Options ****");
    add_options(desc);
    const auto vm = parse_arguments(argc, argv, desc);
    const std::string model_path = vm["model-path"].as<std::string>();
    const std::string input_path = vm["input_binary"].as<std::string>();
    std::uint32_t num_iteration = vm["num-iteration"].as<std::uint32_t>();
    const bool dry_run = vm["dry-run"].as<bool>();
    const bool benchmark = vm["benchmark"].as<bool>();
    const bool write_ofm_files = !dry_run && !benchmark;
    if (int rc = validate_parsed_options(num_iteration, dry_run, input_path); rc != 0) {
      return rc;
    }
    if (num_iteration < VartInferAsync::kNumConcurrentJobs) {
      const std::uint32_t requested = num_iteration;
      num_iteration = VartInferAsync::kNumConcurrentJobs;
      std::cerr << "[WARN] --num-iteration (" << requested << ") is less than kNumConcurrentJobs ("
                << VartInferAsync::kNumConcurrentJobs
                << "); In order to showcase the async path, using num_iteration=" << num_iteration << ".\n";
    }
    if (benchmark) {
      std::cout << "Mode               : benchmark (timed infer, no OFM file writes, sync pass enabled)\n\n";
    }

    // ---------------------------------------------------------------------------
    // Model init and buffer allocation: VAIML runner + per-slot input/output pools.
    // ---------------------------------------------------------------------------
    // Opens the compiled model cache at model_path and constructs a VAIML runner; runner options
    // (tensor layouts, log level, etc.) are fixed in VartInferAsync::create_runner() in
    // vart_infer_async.cpp.
    VartInferAsync infer(model_path);
    if (infer.input_tensors_info().size() > 1) {
      std::cerr
          << "[ERROR] This example supports only single input tensor. Please use a model with single input tensor.\n";
      return 1;
    }
    // allocate_tensor_pools: one input batch + one output batch per concurrent job slot
    // (second argument = slot count, here kNumConcurrentJobs).
    if (!allocate_tensor_pools(infer, VartInferAsync::kNumConcurrentJobs, input_tensors, output_tensors)) {
      return 1;
    }

    // ---------------------------------------------------------------------------
    // IFM load and zero-copy staging: Loads data from input file (or random fill),
    // then memcpy into each pool slot's HW input tensors (buffers already backed by NPU memory).
    // ---------------------------------------------------------------------------
    // load_ifm_buffer_for_model: IFM file is concatenated sample rows; load_input_binary uses model batch_size.
    // Partial batches are supported: fewer than batch_size full rows may be read when the file is short,
    // as long as at least one complete sample row exists. If the file size is smaller than one frame
    // (bytes_per_frame), load_input_binary throws exception.
    // If --dry-run is set, generates random bytes for kDefaultDryRunFrameCount * batch_size sample rows.
    LoadedIfmBuffer ifm = load_ifm_buffer_for_model(infer, dry_run, input_path);
    // ifm.num_frames is the number of sample rows loaded (see load_input_binary / load_input_random).
    const std::size_t ifm_sample_rows = ifm.num_frames;
    const std::size_t N_frames = ifm_sample_rows * num_iteration;
    // Single-row IFM: OFM files list only one batch row per tensor (no padded replicate rows in the dump).
    const std::size_t N_output_frames = (ifm_sample_rows == 1) ? 1 : 0;
    const std::size_t bpf = ifm.bytes_per_frame;
    std::vector<std::uint8_t> input_file_data = std::move(ifm.data);
    std::deque<std::uint32_t> buffer_index_queue;
    for (std::uint32_t j = 0; j < VartInferAsync::kNumConcurrentJobs; ++j) {
      buffer_index_queue.push_back(j);
    }
    // 1) To showcase a zero-copy path into NPU-backed tensors, this sample loads every job slot
    //    with the same IFM payload (logical frame 0) — memcpy into preallocated vart::NpuTensor
    //    buffers, no extra host staging heap for per-slot distinct frames.
    // 2) copy_frame_to_slot copies one full batch (batch_size sample rows) into each pool slot k.
    // 3) In a real deployment, each slot would typically be fed from a camera stream, a
    //    preprocessor, or another producer instead of duplicating the same frame everywhere.
    for (std::uint32_t k = 0; k < VartInferAsync::kNumConcurrentJobs; ++k) {
      if (!copy_frame_to_slot(infer, input_file_data, bpf, N_frames, 0, k, input_tensors)) {
        throw std::runtime_error("copy_frame_to_slot failed (slot=" + std::to_string(k) +
                                 ", frame_index=0, N_frames=" + std::to_string(N_frames) +
                                 "): invalid frame range, zero bytes_per_frame, or null input tensor VA");
      }
    }

    // ---------------------------------------------------------------------------
    // Async inference: submit/wait over the pooled HW input/output tensors filled above.
    // ---------------------------------------------------------------------------
    // Async execution call of VART is non-blocking call. Unlike the sync path,
    // the application can continue to submit jobs even before the previous ones have completed.
    // The pipeline is filled with execute_async on every concurrent slot,
    // then for each remaining frame, wait for one completed job (wait_job),
    // recycle that slot, and submit the next frame.
    std::optional<std::chrono::high_resolution_clock::time_point> t_async_0;
    std::optional<std::chrono::high_resolution_clock::time_point> t_async_1;
    if (benchmark) {
      t_async_0 = std::chrono::high_resolution_clock::now();
    }
    // run_async_infer_pass: fill the pipeline with execute_async on every concurrent slot,
    // then for each remaining frame, wait for one completed job (wait_job),
    // recycle that slot, and submit the next frame.
    run_async_infer_pass(infer, input_tensors, output_tensors, buffer_index_queue, write_ofm_files, num_iteration,
                         N_frames, N_output_frames);
    if (benchmark) {
      t_async_1 = std::chrono::high_resolution_clock::now();
      log_infer_pass_wall_time("async infer pass", *t_async_0, *t_async_1, N_frames);
    }

    // ---------------------------------------------------------------------------
    // Sync inference: Sequential execution of all frames one at a time.
    // ---------------------------------------------------------------------------
    if (benchmark) {
      const auto t_sync_0 = std::chrono::high_resolution_clock::now();
      // run_sync_infer_pass: execute one frame at a time, waiting for each to complete before starting the next.
      run_sync_infer_pass(infer, input_tensors, output_tensors, N_frames, write_ofm_files, num_iteration,
                          N_output_frames);
      const auto t_sync_1 = std::chrono::high_resolution_clock::now();
      log_infer_pass_wall_time("sync infer pass", t_sync_0, t_sync_1, N_frames);
    }

    if (!benchmark) {
      std::cout << "---- output dump ----\n";
      infer.dump_outputs(num_iteration);
    }
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
