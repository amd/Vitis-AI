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
 * EVENT SHALL AMD BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 * OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR other DEALINGS IN THE
 * SOFTWARE. Except as contained in this notice, the name of the AMD shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from AMD.
 */

/**
 * @file frame_types.hpp
 * @brief Frame data structures for pipeline processing
 */

#pragma once

#include <memory>
#include <stdexcept>
#include <vart/vart_device.hpp>
#include <vart/vart_videoframe.hpp>
#include <vector>

namespace vart {
class Memory;
class InferResult;
}  // namespace vart

// Type aliases for improved readability

// VART device type
using DevicePtr = std::shared_ptr<vart::Device>;

// Video frame types
using VideoFramePtr = std::shared_ptr<vart::VideoFrame>;
using FrameList = std::vector<VideoFramePtr>;  // List of frames (single batch element)
using BatchedFrames = std::vector<FrameList>;  // Multiple batches of frames

// Memory/tensor types
using MemoryPtr = std::shared_ptr<vart::Memory>;
using TensorList = std::vector<MemoryPtr>;       // List of tensors
using BatchedTensors = std::vector<TensorList>;  // Multiple batches of tensors

// Inference result types
using InferResultPtr = std::shared_ptr<vart::InferResult>;
using InferResultList = std::vector<InferResultPtr>;       // Results for one frame
using BatchedInferResults = std::vector<InferResultList>;  // Results for multiple frames

/**
 * @brief Frame batch structure for pipeline processing
 * Represents a batch of frames flowing through the pipeline.
 * Currently used with batch_size=1, but designed to support future batch processing.
 *
 * The frame_index represents the starting frame index for the batch.
 * Individual frame indices are calculated as: frame_index + position_in_batch
 */
struct InputFrame {
  BatchedFrames video_frame;  // Batch of frames [batch_idx][frame_idx]
  int frame_index;            // Starting frame index for this batch
  int64_t iteration_number;   // Iteration number this frame belongs to

  InputFrame() : frame_index(0), iteration_number(0) {}

  // Constructor for batch processing: batch_size elements, each with num_frames_per_batch frames
  InputFrame(size_t batch_size, size_t num_frames_per_batch = 1, int start_idx = 0)
      : frame_index(start_idx), iteration_number(0) {
    video_frame.resize(batch_size);
    for (size_t i = 0; i < batch_size; i++) {
      video_frame[i].resize(num_frames_per_batch);
    }
  }

  // Set starting frame index for the batch
  void set_batch_start_index(int id) { frame_index = id; }

  // Get frame index for a specific position in the batch
  int get_frame_index(size_t batch_position) const {
    if (batch_position >= video_frame.size()) {
      throw std::out_of_range("Batch position " + std::to_string(batch_position) + " out of range");
    }
    return frame_index + static_cast<int>(batch_position);
  }

  // Get batch size (number of batch elements)
  size_t size() const { return video_frame.size(); }

  // Get number of frames in a specific batch
  size_t frames_per_batch(size_t batch_idx) const {
    if (batch_idx >= video_frame.size()) {
      throw std::out_of_range("Batch index " + std::to_string(batch_idx) + " out of range");
    }
    return video_frame[batch_idx].size();
  }

  // Get number of frames per batch element (assumes all batch elements have same frame count)
  size_t frames_per_batch() const {
    if (video_frame.empty()) {
      return 0;
    }
    return video_frame[0].size();  // Use first batch element as reference
  }

  // Check if all frames in the batch are properly initialized
  bool is_valid() const {
    if (video_frame.empty()) {
      return false;
    }
    for (const auto& batch_element : video_frame) {
      if (batch_element.empty()) {
        return false;
      }
      for (const auto& frame : batch_element) {
        if (!frame) {
          return false;
        }
      }
    }
    return true;
  }

  // Index operator to access batch elements (returns vector of frames for that batch index)
  FrameList& operator[](size_t batch_idx) {
    if (batch_idx >= video_frame.size()) {
      throw std::out_of_range("Batch index " + std::to_string(batch_idx) + " out of range");
    }
    return video_frame[batch_idx];
  }

  const FrameList& operator[](size_t batch_idx) const {
    if (batch_idx >= video_frame.size()) {
      throw std::out_of_range("Batch index " + std::to_string(batch_idx) + " out of range");
    }
    return video_frame[batch_idx];
  }
};

/**
 * @brief Preprocessed frame data ready for inference
 */
struct PreprocessedFrame {
  BatchedFrames preprocessed_frame;  //[batch_idx][frame_idx]
  int frame_index;                   // Sequential frame number
  int64_t iteration_number;          // Iteration number this frame belongs to

  PreprocessedFrame() : frame_index(0), iteration_number(0) {}

  // Constructor for batch processing
  PreprocessedFrame(size_t batch_size, size_t num_frames_per_batch = 1, int idx = 0)
      : frame_index(idx), iteration_number(0) {
    preprocessed_frame.resize(batch_size);
    for (size_t i = 0; i < batch_size; i++) {
      preprocessed_frame[i].resize(num_frames_per_batch);
    }
  }

  // Get batch size
  size_t size() const { return preprocessed_frame.size(); }

  // Index operator to access batch elements
  FrameList& operator[](size_t batch_idx) {
    if (batch_idx >= preprocessed_frame.size()) {
      throw std::out_of_range("Batch index " + std::to_string(batch_idx) + " out of range");
    }
    return preprocessed_frame[batch_idx];
  }

  const FrameList& operator[](size_t batch_idx) const {
    if (batch_idx >= preprocessed_frame.size()) {
      throw std::out_of_range("Batch index " + std::to_string(batch_idx) + " out of range");
    }
    return preprocessed_frame[batch_idx];
  }
};

/**
 * @brief Inference result data
 * Contains results from single inference engine (black box operation)
 */
struct InferenceResult {
  BatchedTensors inference_output;  // Inference results (vector of output tensors)
  int frame_index;                  // Sequential frame number
  int64_t iteration_number;         // Iteration number this frame belongs to

  InferenceResult() : frame_index(0), iteration_number(0) {}

  InferenceResult(TensorList output, int idx) : frame_index(idx), iteration_number(0) {
    inference_output.push_back(std::move(output));
  }
};

/**
 * @brief Lightweight completion notification structure
 * Used by postprocess threads to notify main thread of frame completion
 */
struct ProcessingComplete {
  int64_t iteration_number;  ///< Iteration number
  int frame_index;           ///< Frame index
  uint32_t instance_id;      ///< Instance that completed processing
  uint32_t batch_size;       ///< Actual batch size processed (for partial batch support)

  ProcessingComplete() : iteration_number(0), frame_index(0), instance_id(0), batch_size(0) {}

  ProcessingComplete(int64_t iter, int idx, uint32_t inst, uint32_t batch)
      : iteration_number(iter), frame_index(idx), instance_id(inst), batch_size(batch) {}
};

/* Unified state management for threads */
enum class ThreadState {
  IDLE,          ///< not running
  RUNNING,       ///< actively processing
  SHUTTING_DOWN  ///< shutting down gracefully
};
