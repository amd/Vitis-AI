/*
 * Copyright (C) 2026 Advanced Micro Devices, Inc.
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
 * EVENT SHALL AMD BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 * OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. Except as contained in this notice, the name of the AMD shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from AMD.
 */

/**
 * @file pool_timeouts.hpp
 * @brief Pipeline-scaled acquire timeouts and unique_ptr aliases for the
 *        common/ buffer pools, kept app-local for x_plus_ml_vart.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>

#include "common/memory_buffer_pool.hpp"
#include "common/video_frame_pool.hpp"

/* These default-timeouts are based on XRT timeout for AIE execution.
 * They scale with the number of model instances driving the pool so a
 * waiter doesn't trip the per-pipeline budget. */
#define VIDEO_FRAME_DEFAULT_TIMEOUT_DURATION 20000
#define VIDEO_FRAME_TIMEOUT_DURATION(num_pipelines) \
  std::chrono::milliseconds(VIDEO_FRAME_DEFAULT_TIMEOUT_DURATION* static_cast<int64_t>(num_pipelines))

#define MEMORY_BUFFER_DEFAULT_TIMEOUT_DURATION 20000
#define MEMORY_BUFFER_TIMEOUT_DURATION(num_pipelines) \
  std::chrono::milliseconds(MEMORY_BUFFER_DEFAULT_TIMEOUT_DURATION* static_cast<int64_t>(num_pipelines))

using VideoFramePoolPtr = std::unique_ptr<VideoFramePool>;
using MemoryBufferPoolPtr = std::unique_ptr<MemoryBufferPool>;
