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

/**
 * @file video_frame_pool.hpp
 * @brief Thread-safe pool of vart::VideoFrames.
 *
 */

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>

#include <vart/vart_videoframe.hpp>
#include <vart/vart_videoframe_types.hpp>

/**
 * @class VideoFramePool
 * @brief Fixed-size pool of vart::VideoFrame objects backed by XRT buffer objects.
 *
 * @par Lifetime contract
 * Each shared_ptr from acquire_frame() carries a custom deleter that captures
 * shared pool State. The State outlives the pool object, so deleters never
 * touch a destroyed pool instance.
 *
 * Required teardown order (callers that own the pool):
 *   1. Stop pipeline consumers before producers (inference, postprocess,
 *      preprocess, then file readers) so no further acquire_frame() calls
 *      are made and in-flight frames can be released.
 *   2. Release every outstanding shared_ptr (let worker threads exit, drop
 *      queued pipeline frames, reset() any remaining holders).
 *   3. Destroy the pool object (typically when the owning file reader or
 *      preprocess instance is cleared).
 *
 * Destructor behaviour:
 *   - Sets stopping_, wakes blocked acquirers, and waits up to 5s for
 *     outstanding frames to return.
 *   - On timeout, logs the outstanding count, sets alive=false, and returns.
 *   - Any shared_ptr still alive after that is destroyed by its deleter
 *     without recycling (safe: no leak, no use-after-free).
 */
class VideoFramePool {
 public:
  /** @brief Type alias used by the generic acquire_tensors() template. */
  using buffer_type = vart::VideoFrame;

  /**
   * @brief Construct a pool of pre-allocated vart::VideoFrame objects.
   * @param pool_size  Number of frames to pre-allocate.
   * @param type       VideoFrame implementation type (e.g., XRT).
   * @param buf_size   Size of each frame buffer in bytes.
   * @param mbank_idx  DDR memory bank index.
   * @param vinfo      Video format descriptor (resolution, pixel format).
   * @param device     Shared pointer to the VART device.
   */
  VideoFramePool(size_t pool_size,
                 vart::VideoFrameImplType type,
                 size_t buf_size,
                 uint8_t mbank_idx,
                 vart::VideoInfo& vinfo,
                 std::shared_ptr<vart::Device> device,
                 std::chrono::milliseconds timeout = std::chrono::milliseconds(20000));

  /**
   * @brief Destructor.
   *
   * Signals shutdown (blocked acquire_frame() callers throw), waits up to
   * 5 seconds for outstanding frames to return, then sets alive=false.
   * If the drain times out, an error is logged; remaining shared_ptr
   * deleters destroy their frames without recycling.
   */
  ~VideoFramePool();

  /** @brief Acquire a frame from the pool (blocks with timeout if exhausted). */
  std::shared_ptr<vart::VideoFrame> acquire_frame();
  /** @brief Get the number of currently available frames. */
  size_t get_available_count();

  /** @brief Generic acquire interface for template compatibility. */
  std::shared_ptr<buffer_type> acquire() { return acquire_frame(); }

 private:
  struct State;

  std::shared_ptr<State> state_;
};
