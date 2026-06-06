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
 * @file video_frame_pool.cpp
 * @brief Implementation of VideoFramePool – acquire/release with timeout.
 */

#include "common/video_frame_pool.hpp"

#include <iostream>

/** @brief Pre-allocate pool_size VideoFrame objects on the given device. */
VideoFramePool::VideoFramePool(size_t pool_size,
                               vart::VideoFrameImplType type,
                               size_t buf_size,
                               uint8_t mbank_idx,
                               vart::VideoInfo& vinfo,
                               std::shared_ptr<vart::Device> device,
                               std::chrono::milliseconds timeout)
    : timeout_duration_(timeout) {
  for (size_t i = 0; i < pool_size; ++i) {
    std::shared_ptr<vart::VideoFrame> frame;
    try {
      frame = std::make_shared<vart::VideoFrame>(type, buf_size, mbank_idx, vinfo, device);
    } catch (std::exception& ex) {
      std::cerr << "failed to create VideoFrame. Reason: " << ex.what() << std::endl;
      /* Re-throw the exception to indicate failure */
      throw;
    }
    free_frames_.push(frame);
  }
}

VideoFramePool::~VideoFramePool() {
  std::unique_lock<std::mutex> lock(mutex_);
  stopping_ = true;
  /* Wake every blocked acquirer so they can observe stopping_ and throw.
   * Each woken acquirer re-notifies before throwing (see acquire_frame),
   * so the wake is forwarded along the chain until it eventually reaches
   * the destructor or the chain runs out. */
  condition_.notify_all();
  /* Bounded drain: wait up to 5s for outstanding frames to come back.
   * If a caller leaks a shared_ptr we log loudly and proceed instead of
   * deadlocking the destructor. */
  if (!condition_.wait_for(lock, std::chrono::milliseconds(5000), [this] { return outstanding_ == 0; })) {
    std::cerr << "VideoFramePool destroyed with " << outstanding_
              << " frame(s) still outstanding after 5s drain timeout" << std::endl;
  }
}

/** @brief Acquire a frame; blocks up to timeout_duration if pool is empty. */
std::shared_ptr<vart::VideoFrame> VideoFramePool::acquire_frame() {
  std::unique_lock<std::mutex> lock(mutex_);

  if (!condition_.wait_for(lock, timeout_duration_, [this] { return !free_frames_.empty() || stopping_; })) {
    throw std::runtime_error("Timeout waiting for a VideoFrame.");
  }
  if (stopping_) {
    /* We were woken but are about to throw without consuming a frame.
     * Forward the wake so the destructor (or another waiter) is not
     * left stranded on a single-CV lost-wakeup. */
    condition_.notify_one();
    throw std::runtime_error("VideoFramePool is shutting down.");
  }

  std::shared_ptr<vart::VideoFrame> frame = free_frames_.front();
  free_frames_.pop();
  ++outstanding_;

  // Return a shared_ptr with custom deleter that releases back to pool
  return std::shared_ptr<vart::VideoFrame>(frame.get(),
                                           [this, frame](vart::VideoFrame*) { this->release_frame(frame); });
}

/** @brief Return a frame to the pool and wake one waiter.
 *
 *  Acquirers and the destructor share this single CV. notify_one() is
 *  enough because the only waiter that can be "wrongly" woken is a
 *  shutdown-time acquirer, and that acquirer re-notifies before throwing
 *  (see acquire_frame) so the wake propagates to the destructor.
 */
void VideoFramePool::release_frame(std::shared_ptr<vart::VideoFrame> frame) {
  std::lock_guard<std::mutex> lock(mutex_);
  free_frames_.push(frame);
  --outstanding_;
  condition_.notify_one();
}

/** @brief Return the number of frames currently available in the pool. */
size_t VideoFramePool::get_available_count() {
  std::lock_guard<std::mutex> lock(mutex_);
  return free_frames_.size();
}
