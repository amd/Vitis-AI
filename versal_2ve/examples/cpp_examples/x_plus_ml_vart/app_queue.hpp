/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
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
 * @file app_queue.hpp
 * @brief Thread-safe queue implementation for pipeline communication
 * Based on the spatial_mt_ml_ort PipelineQueue implementation pattern
 */

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>

/**
 * @brief Thread-safe queue for pipeline communication
 * Based on the spatial_mt_ml_ort PipelineQueue implementation pattern
 */
template <typename T>
class AppQueue {
 private:
  std::queue<T> queue_;
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::atomic<bool> finished_{false};
  const size_t max_size_;

 public:
  /**
   * @brief Constructor
   * @param max_size Maximum queue size (default 3, following reference pattern)
   */
  explicit AppQueue(size_t max_size = 3) : max_size_(max_size) {}

  /**
   * @brief Push data to queue with optional timeout
   * @param data Data to push
   * @param timeout_ms Timeout in milliseconds (0 = block indefinitely)
   * @return true if push successful, false if timeout or queue finished
   */
  bool push(const T& data, uint32_t timeout_ms = 0) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (finished_) {
      return false;
    }

    if (timeout_ms == 0) {
      // Block indefinitely until space available
      condition_.wait(lock, [this] { return queue_.size() < max_size_ || finished_; });
    } else {
      // Wait with timeout
      if (!condition_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                               [this] { return queue_.size() < max_size_ || finished_; })) {
        return false;  // Timeout
      }
    }

    if (finished_) {
      return false;
    }

    queue_.push(data);
    condition_.notify_one();
    return true;
  }

  /**
   * @brief Pop data from queue (blocking)
   * @param data Reference to store popped data
   * @return true if pop successful, false if queue finished and empty
   */
  bool pop(T& data) {
    std::unique_lock<std::mutex> lock(mutex_);

    // Wait until data available or finished
    condition_.wait(lock, [this] { return !queue_.empty() || finished_; });

    if (queue_.empty()) {
      return false;  // Queue is finished and empty
    }

    data = queue_.front();
    queue_.pop();
    condition_.notify_one();
    return true;
  }

  /**
   * @brief Try to pop data from queue (non-blocking)
   * @param data Reference to store popped data
   * @return true if pop successful, false if queue empty or finished
   */
  bool try_pop(T& data) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (queue_.empty()) {
      return false;  // Queue is empty - return immediately
    }

    data = queue_.front();
    queue_.pop();
    condition_.notify_one();
    return true;
  }

  /**
   * @brief Signal that no more data will be pushed
   */
  void finish() {
    std::lock_guard<std::mutex> lock(mutex_);
    finished_ = true;
    condition_.notify_all();
  }

  /**
   * @brief Check if queue is finished
   */
  bool is_finished() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return finished_;
  }

  /**
   * @brief Get current queue size
   */
  size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

  /**
   * @brief Check if queue is empty
   */
  bool empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
  }
};