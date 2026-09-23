/*
 *
 * Copyright (C) 2024 Advanced Micro Devices, Inc.
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

/* The VideoFramePool class manages a pool of reusable video frames for
 * efficient memory utilization. Upon initialization, it creates a specified
 * number of video frames and stores them in a queue called free_frames.
 * The acquire_frame method allows requesting a frame from the pool, waiting
 * for a specified timeout if necessary. When a frame is no longer needed,
 * it can be released back to the pool using the release_frame method.
 */

#include "video_frame_pool.hpp"

VideoFramePool::VideoFramePool(size_t pool_size, vart::VideoFrameImplType type,
                               size_t buf_size, std::vector<uint8_t> & mbanks_idx,
                               VideoInfo &vinfo, std::shared_ptr<Device> device)
    : timeout_duration(std::chrono::milliseconds(1000)) {
  auto mem_bank_idx_size = mbanks_idx.size();
  for (size_t i = 0; i < pool_size; ++i) {
    std::shared_ptr<VideoFrame> frame;
    try {
      uint8_t mbank_idx = mbanks_idx[i % mem_bank_idx_size];
      frame = std::make_shared<VideoFrame>(type, buf_size, mbank_idx, vinfo,
                                           device);
    } catch (std::exception &ex) {
      std::cerr << "failed to create VideoFrame. Reason: " << ex.what()
                << std::endl;
      /* Re-throw the exception to indicate failure */
      throw;
    }
    free_frames.push(frame);
  }
}

VideoFramePool::~VideoFramePool() {
  while (!free_frames.empty()) {
    /* This will destruct shared_ptr elements automatically */
    free_frames.pop();
  }
}

std::shared_ptr<VideoFrame> VideoFramePool::acquire_frame() {
  std::unique_lock<std::mutex> lock(mutex);

  if (!condition.wait_for(lock, timeout_duration,
                          [this] { return !free_frames.empty(); })) {
    throw std::runtime_error("Timeout waiting for a VideoFrame.");
  }

  std::shared_ptr<VideoFrame> frame = free_frames.front();
  free_frames.pop();
  return frame;
}

void VideoFramePool::release_frame(std::shared_ptr<VideoFrame> frame) {
  std::lock_guard<std::mutex> lock(mutex);
  free_frames.push(frame);
  condition.notify_one();
}
