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
 * EVENT SHALL "AMD" BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 * OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. Except as contained in this notice, the name of the AMD shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from AMD.
 */

/**
 * @file memory_buffer_pool.hpp
 * @brief Thread-safe pool of reusable vart::Memory buffers for NPU I/O.
 *
 * Buffers are pre-allocated at construction time and recycled via
 * acquire/release. The acquire call blocks (with timeout) when the
 * pool is exhausted.
 */

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>

#include <vart/vart_memory.hpp>

/**
 * @class MemoryBufferPool
 * @brief Fixed-size pool of vart::Memory objects backed by XRT buffer objects.
 *
 * @par Lifetime contract
 * The pool MUST outlive every shared_ptr<vart::Memory> it issues from
 * acquire_buffer(). The shared_ptr returned by acquire_buffer() carries
 * a custom deleter that calls back into the pool to recycle the buffer;
 * if the pool is destroyed while a shared_ptr is still alive, the
 * deleter will later run on a destroyed object (undefined behavior).
 *
 * Required teardown order:
 *   1. Stop all producers/consumers so no further acquire_buffer() calls
 *      are made and no acquired shared_ptrs are still in flight.
 *   2. Release every shared_ptr returned by acquire_buffer() (drop them,
 *      reset() them, or let them go out of scope).
 *   3. Then destroy the pool.
 *
 * The destructor performs a bounded 5s drain wait to detect contract
 * violations and logs an error if outstanding buffers remain after the
 * timeout. The wait is a diagnostic, not a license to violate the
 * contract: any shared_ptr still alive after the destructor returns
 * will trigger undefined behavior when its refcount reaches zero.
 */
class MemoryBufferPool {
 public:
  /** @brief Type alias used by the generic acquire_tensors() template. */
  using buffer_type = vart::Memory;

  /**
   * @brief Construct a pool of pre-allocated vart::Memory buffers.
   * @param pool_size  Number of buffers to pre-allocate.
   * @param type       XRT memory implementation type.
   * @param buf_size   Size of each buffer in bytes.
   * @param mbank_idx  DDR memory bank index.
   * @param device     Shared pointer to the VART device.
   */
  MemoryBufferPool(size_t pool_size,
                   vart::MemoryImplType type,
                   size_t buf_size,
                   uint8_t mbank_idx,
                   std::shared_ptr<vart::Device> device,
                   std::chrono::milliseconds timeout = std::chrono::milliseconds(20000));

  /**
   * @brief Destructor.
   *
   * Signals shutdown (so blocked acquire_buffer() callers throw cleanly)
   * and then waits up to 5 seconds for every outstanding buffer to be
   * returned to the pool. If the drain times out, an error is logged
   * with the leaked count and the destructor proceeds.
   *
   * @warning Proceeding past the drain timeout violates the lifetime
   *          contract documented on the class. Callers must ensure all
   *          issued shared_ptrs have been released before the destructor
   *          runs; the timeout exists only to surface the bug, not to
   *          make the failure safe.
   */
  ~MemoryBufferPool();

  /** @brief Acquire a buffer from the pool (blocks with timeout if exhausted). */
  std::shared_ptr<vart::Memory> acquire_buffer();
  /** @brief Get the number of currently available buffers. */
  size_t get_available_count();

  /** @brief Generic acquire interface for template compatibility. */
  std::shared_ptr<buffer_type> acquire() { return acquire_buffer(); }

 private:
  /**
   * @brief Return a buffer to the pool. Invoked exclusively by the custom
   *        deleter installed on the shared_ptr handed out by acquire_buffer().
   *        Not part of the public API – calling this directly while still
   *        holding the shared_ptr would corrupt the free queue and the
   *        outstanding_ counter.
   */
  void release_buffer(std::shared_ptr<vart::Memory> buffer);

  std::queue<std::shared_ptr<vart::Memory>> free_buffers_;  ///< Available buffers
  std::mutex mutex_;                                        ///< Protects free_buffers_
  std::condition_variable condition_;                       ///< Signaled on release and on shutdown
  std::chrono::milliseconds timeout_duration_;              ///< Acquire timeout
  bool stopping_{false};                                    ///< Set during shutdown
  size_t outstanding_{0};                                   ///< Buffers currently checked out
};
