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
 * The pool MUST outlive every shared_ptr<vart::VideoFrame> it issues from
 * acquire_frame(). The shared_ptr returned by acquire_frame() carries a
 * custom deleter that calls back into the pool to recycle the frame; if
 * the pool is destroyed while a shared_ptr is still alive, the deleter
 * will later run on a destroyed object (undefined behavior).
 *
 * Required teardown order:
 *   1. Stop all producers/consumers so no further acquire_frame() calls
 *      are made and no acquired shared_ptrs are still in flight.
 *   2. Release every shared_ptr returned by acquire_frame() (drop them,
 *      reset() them, or let them go out of scope).
 *   3. Then destroy the pool.
 *
 * The destructor performs a bounded 5s drain wait to detect contract
 * violations and logs an error if outstanding frames remain after the
 * timeout. The wait is a diagnostic, not a license to violate the
 * contract: any shared_ptr still alive after the destructor returns
 * will trigger undefined behavior when its refcount reaches zero.
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
   * Signals shutdown (so blocked acquire_frame() callers throw cleanly)
   * and then waits up to 5 seconds for every outstanding frame to be
   * returned to the pool. If the drain times out, an error is logged
   * with the leaked count and the destructor proceeds.
   *
   * @warning Proceeding past the drain timeout violates the lifetime
   *          contract documented on the class. Callers must ensure all
   *          issued shared_ptrs have been released before the destructor
   *          runs; the timeout exists only to surface the bug, not to
   *          make the failure safe.
   */
  ~VideoFramePool();

  /** @brief Acquire a frame from the pool (blocks with timeout if exhausted). */
  std::shared_ptr<vart::VideoFrame> acquire_frame();
  /** @brief Get the number of currently available frames. */
  size_t get_available_count();

  /** @brief Generic acquire interface for template compatibility. */
  std::shared_ptr<buffer_type> acquire() { return acquire_frame(); }

 private:
  /**
   * @brief Return a frame to the pool. Invoked exclusively by the custom
   *        deleter installed on the shared_ptr handed out by acquire_frame().
   *        Not part of the public API – calling this directly while still
   *        holding the shared_ptr would corrupt the free queue and the
   *        outstanding_ counter.
   */
  void release_frame(std::shared_ptr<vart::VideoFrame> frame);

  std::queue<std::shared_ptr<vart::VideoFrame>> free_frames_;  ///< Available frames
  std::mutex mutex_;                                           ///< Protects free_frames_
  std::condition_variable condition_;                          ///< Signaled on release and on shutdown
  std::chrono::milliseconds timeout_duration_;                 ///< Acquire timeout
  bool stopping_{false};                                       ///< Set during shutdown
  size_t outstanding_{0};                                      ///< Frames currently checked out
};
