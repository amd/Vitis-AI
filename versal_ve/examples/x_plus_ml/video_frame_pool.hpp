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

#ifndef VIDEO_FRAME_POOL_HPP
#define VIDEO_FRAME_POOL_HPP

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <vector>

#include <vart/vart_videoframe.hpp>
#include <vart/vart_videoframe_types.hpp>

using namespace vart;

class VideoFramePool {
public:
  VideoFramePool(size_t pool_size, vart::VideoFrameImplType type,
                 size_t buf_size, std::vector<uint8_t> & mbanks_idx, VideoInfo &vinfo,
                 std::shared_ptr<Device> device);
  ~VideoFramePool();

  std::shared_ptr<VideoFrame> acquire_frame();
  void release_frame(std::shared_ptr<VideoFrame> frame);

private:
  std::queue<std::shared_ptr<VideoFrame>> free_frames;
  std::mutex mutex;
  std::condition_variable condition;
  std::chrono::milliseconds timeout_duration;
};

#endif /* VIDEO_FRAME_POOL_HPP */
