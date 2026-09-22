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
#include <utility>

/** @brief Pool synchronization state shared by the pool object and deleters. */
struct VideoFramePool::State {
  std::queue<std::shared_ptr<vart::VideoFrame>> free_frames;
  std::mutex mutex;
  std::condition_variable condition;
  std::chrono::milliseconds timeout_duration;
  bool stopping{false};
  bool alive{true};
  size_t outstanding{0};

  /**
   * @brief Return a frame to the pool when alive; otherwise let the deleter destroy it.
   *
   * Acquirers and the destructor share this single CV. notify_one() is enough
   * because the only waiter that can be "wrongly" woken is a shutdown-time
   * acquirer, and that acquirer re-notifies before throwing (see acquire_frame)
   * so the wake propagates to the destructor.
   */
  void release_frame(std::shared_ptr<vart::VideoFrame> frame) {
    std::lock_guard<std::mutex> lock(mutex);
    if (alive) {
      free_frames.push(std::move(frame));
      condition.notify_one();
    }
    --outstanding;
  }
};

/** @brief Pre-allocate pool_size VideoFrame objects on the given device. */
VideoFramePool::VideoFramePool(size_t pool_size,
                               vart::VideoFrameImplType type,
                               size_t buf_size,
                               uint8_t mbank_idx,
                               vart::VideoInfo& vinfo,
                               std::shared_ptr<vart::Device> device,
                               std::chrono::milliseconds timeout)
    : state_(std::make_shared<State>()) {
  state_->timeout_duration = timeout;
  for (size_t i = 0; i < pool_size; ++i) {
    std::shared_ptr<vart::VideoFrame> frame;
    try {
      frame = std::make_shared<vart::VideoFrame>(type, buf_size, mbank_idx, vinfo, device);
    } catch (std::exception& ex) {
      std::cerr << "failed to create VideoFrame. Reason: " << ex.what() << std::endl;
      throw;
    }
    state_->free_frames.push(std::move(frame));
  }
}

VideoFramePool::~VideoFramePool() {
  auto state = state_;
  std::unique_lock<std::mutex> lock(state->mutex);
  state->stopping = true;
  /* Wake every blocked acquirer so they can observe stopping and throw.
   * Each woken acquirer re-notifies before throwing (see acquire_frame),
   * so the wake is forwarded along the chain until it eventually reaches
   * the destructor or the chain runs out. */
  state->condition.notify_all();
  /* Bounded drain: wait up to 5s for outstanding frames to come back.
   * On timeout, set alive=false so remaining deleters destroy frames
   * without recycling instead of deadlocking or leaking. */
  if (!state->condition.wait_for(lock, std::chrono::milliseconds(5000), [&] { return state->outstanding == 0; })) {
    std::cerr << "VideoFramePool destroyed with " << state->outstanding
              << " frame(s) still outstanding after 5s drain timeout - releasing via deleters" << std::endl;
  }
  state->alive = false;
}

/** @brief Acquire a frame; blocks up to timeout_duration if pool is empty. */
std::shared_ptr<vart::VideoFrame> VideoFramePool::acquire_frame() {
  std::unique_lock<std::mutex> lock(state_->mutex);
  auto& state = state_;

  if (!state->condition.wait_for(lock, state->timeout_duration,
                                 [&] { return !state->free_frames.empty() || state->stopping; })) {
    throw std::runtime_error("Timeout waiting for a VideoFrame.");
  }
  if (state->stopping) {
    /* We were woken but are about to throw without consuming a frame.
     * Forward the wake so the destructor (or another waiter) is not
     * left stranded on a single-CV lost-wakeup. */
    state->condition.notify_one();
    throw std::runtime_error("VideoFramePool is shutting down.");
  }

  std::shared_ptr<vart::VideoFrame> frame = std::move(state->free_frames.front());
  state->free_frames.pop();
  ++state->outstanding;

  // Hoist raw pointer: arg evaluation order vs move-capture is unspecified (C++17).
  vart::VideoFrame* raw = frame.get();
  if (!raw) {
    --state->outstanding;
    throw std::runtime_error("VideoFramePool acquired an empty frame slot.");
  }
  return std::shared_ptr<vart::VideoFrame>(raw, [state, frame = std::move(frame)](vart::VideoFrame*) mutable {
    state->release_frame(std::move(frame));
  });
}

/** @brief Return the number of frames currently available in the pool. */
size_t VideoFramePool::get_available_count() {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->free_frames.size();
}
