/*
 *
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
 * EVENT SHALL XILINX BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 * OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. Except as contained in this notice, the name of the Xilinx shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from Xilinx.
 */

#include "vvas_postprocess_bridge.hpp"

#include <cstring>
#include <stdexcept>

#include <vvas_core/vvas_context.h>
#include <vvas_core/vvas_memory.h>
#include <vvas_core/vvas_postprocess.h>
/* vvas_postprocess.h -> vvas_infer_prediction.h -> vvas_infer_results.h pulls in
 * VvasInferDetection / VvasInferClassification and the tree/list helpers. */
#include <vvas_utils/vvas_utils.h>

using vart::TensorDataDirection;
using vart::TensorDataType;
using vart::TensorInfo;

namespace xplusml {

/* Map vart TensorDataType -> vvas tensor data type. */
static VvasTensorDataType to_vvas_tensor_type(TensorDataType t) {
  switch (t) {
    case TensorDataType::INT8:    return VVAS_TENSOR_DATA_TYPE_INT8;
    case TensorDataType::BF16:    return VVAS_TENSOR_DATA_TYPE_BF16;
    case TensorDataType::FP16:    return VVAS_TENSOR_DATA_TYPE_FP16;
    case TensorDataType::FLOAT32: return VVAS_TENSOR_DATA_TYPE_FLOAT32;
    default:                      return VVAS_TENSOR_DATA_TYPE_UNKNOWN;
  }
}

struct VvasPostProcessBridge::Impl {
  VvasContext* vvas_ctx{nullptr};
  VvasPostProcess* pp_handle{nullptr};
  std::string json_config;
  std::string lib_path;
  VvasPPResultKind kind;
  /* Output tensor byte size (single output tensor for resnet50/yolox), used to
   * wrap the mapped host pointer into a VvasMemory. */
  uint32_t out_tensor_size{0};

  ~Impl() {
    if (pp_handle) vvas_postprocess_destroy(pp_handle);
    if (vvas_ctx) vvas_context_destroy(vvas_ctx);
  }
};

/* Collect immediate children of a prediction node into a VvasList. */
static void collect_child(VvasTreeNode* node, void* data) {
  VvasList** children = static_cast<VvasList**>(data);
  VvasInferPrediction* pred = static_cast<VvasInferPrediction*>(node->data);
  *children = vvas_list_append(*children, pred);
}

VvasPostProcessBridge::VvasPostProcessBridge(std::string json_config,
                                             std::string lib_path,
                                             VvasPPResultKind result_kind,
                                             int32_t device_idx,
                                             std::string xclbin_loc)
    : pimpl_(std::make_unique<Impl>()) {
  pimpl_->json_config = std::move(json_config);
  pimpl_->lib_path = std::move(lib_path);
  pimpl_->kind = result_kind;

  VvasReturnType ret = VVAS_RET_ERROR;
  pimpl_->vvas_ctx = vvas_context_create(
      device_idx, const_cast<char*>(xclbin_loc.c_str()),
      VVAS_LOG_LEVEL_ERROR, &ret);
  if (!pimpl_->vvas_ctx || ret != VVAS_RET_SUCCESS) {
    throw std::runtime_error("VvasPostProcessBridge: vvas_context_create failed");
  }
}

VvasPostProcessBridge::~VvasPostProcessBridge() = default;

void VvasPostProcessBridge::set_config(
    const std::vector<TensorInfo>& tensor_info, uint32_t batch_size) {
  /* Count OUTPUT tensors; resnet50/yolox each have exactly one. */
  uint32_t out_count = 0;
  for (const auto& ti : tensor_info) {
    if (ti.direction == TensorDataDirection::OUTPUT) out_count++;
  }
  if (out_count != 1) {
    throw std::runtime_error(
        "VvasPostProcessBridge: expected exactly 1 output tensor, got " +
        std::to_string(out_count));
  }

  /* Build the VvasTensorInfo array from OUTPUT tensors only (skip INPUT). */
  auto vvas_tinfo =
      std::make_unique<std::unique_ptr<VvasTensorInfo>[]>(out_count);
  uint32_t idx = 0;
  for (const auto& ti : tensor_info) {
    if (ti.direction != TensorDataDirection::OUTPUT) continue;
    if (ti.data_type == TensorDataType::UNKNOWN) {
      throw std::runtime_error("VvasPostProcessBridge: unknown tensor data type");
    }
    vvas_tinfo[idx] = std::make_unique<VvasTensorInfo>();
    std::memset(vvas_tinfo[idx].get(), 0, sizeof(VvasTensorInfo));
    vvas_tinfo[idx]->size = ti.size;
    vvas_tinfo[idx]->scale_coeff = ti.scale_coeff;
    vvas_tinfo[idx]->valid_shapes = ti.shape.size();
    vvas_tinfo[idx]->data_type = to_vvas_tensor_type(ti.data_type);
    vvas_tinfo[idx]->direction = VVAS_TENSOR_DATA_DIRECTION_OUTPUT;
    uint8_t s = 0;
    for (auto dim : ti.shape) {
      if (s >= MAX_SHAPE_SIZE) break;
      vvas_tinfo[idx]->shape[s++] = dim;
    }
    pimpl_->out_tensor_size = ti.size;
    idx++;
  }

  char* json = strdup(pimpl_->json_config.c_str());
  pimpl_->pp_handle = vvas_postprocess_create(
      json, const_cast<char*>(pimpl_->lib_path.c_str()),
      reinterpret_cast<VvasTensorInfo**>(vvas_tinfo.get()), out_count,
      batch_size, VVAS_LOG_LEVEL_ERROR);
  free(json);
  if (!pimpl_->pp_handle) {
    throw std::runtime_error(
        "VvasPostProcessBridge: vvas_postprocess_create failed for " +
        pimpl_->lib_path);
  }
}

std::vector<std::vector<std::shared_ptr<vart::InferResult>>>
VvasPostProcessBridge::process(const std::vector<int8_t*>& data,
                               uint32_t current_batch_size) {
  /* Wrap each frame's mapped output-tensor host pointer into a VvasMemory.
   * Non-native path: one output tensor per frame. */
  std::vector<VvasMemory*> tensor_memory;
  tensor_memory.reserve(current_batch_size);
  for (uint32_t j = 0; j < current_batch_size; j++) {
    VvasMemory* mem = vvas_memory_alloc_from_data(
        pimpl_->vvas_ctx, reinterpret_cast<uint8_t*>(data[j]),
        pimpl_->out_tensor_size, nullptr, nullptr, nullptr);
    tensor_memory.push_back(mem);
  }

  auto parent = std::make_unique<std::unique_ptr<VvasInferPrediction>[]>(
      current_batch_size);

  VvasReturnType ret = vvas_postprocess_tensor(
      pimpl_->pp_handle, tensor_memory.data(),
      static_cast<int>(current_batch_size),
      reinterpret_cast<VvasInferPrediction**>(parent.get()));

  for (uint32_t j = 0; j < current_batch_size; j++) {
    if (tensor_memory[j]) vvas_memory_free(tensor_memory[j]);
  }

  if (ret != VVAS_RET_SUCCESS) {
    /* Free any partial predictions before erroring. */
    for (uint32_t j = 0; j < current_batch_size; j++) {
      if (parent[j]) vvas_inferprediction_free(parent[j].release());
    }
    throw std::runtime_error("VvasPostProcessBridge: vvas_postprocess_tensor failed");
  }

  std::vector<std::vector<std::shared_ptr<vart::InferResult>>> results;
  results.reserve(current_batch_size);

  for (uint32_t b = 0; b < current_batch_size; b++) {
    std::vector<std::shared_ptr<vart::InferResult>> frame_results;
    VvasInferPrediction* pred = parent[b].get();
    if (pred && pred->node) {
      VvasList* children = nullptr;
      vvas_treenode_traverse_child(pred->node, TRAVERSE_ALL, collect_child,
                                   &children);
      uint32_t n = children ? vvas_list_length(children) : 0;
      for (uint32_t c = 0; c < n; c++) {
        VvasInferPrediction* child =
            static_cast<VvasInferPrediction*>(vvas_list_nth_data(children, c));
        if (!child || !child->infer_result) continue;

        if (pimpl_->kind == VvasPPResultKind::DETECTION) {
          VvasInferDetection* det =
              static_cast<VvasInferDetection*>(child->infer_result->data);
          if (!det) continue;
          auto ir = std::make_shared<vart::InferResult>(
              vart::InferResultType::DETECTION);
          auto* out = static_cast<vart::DetectionResData*>(
              ir->get_infer_result());
          out->x = det->bbox.x;
          out->y = det->bbox.y;
          out->width = det->bbox.width;
          out->height = det->bbox.height;
          out->label = det->label ? det->label : "";
          out->confidence = det->probability;
          out->class_id = det->class_id;
          out->obj_track_label =
              det->obj_track_label ? det->obj_track_label : "";
          out->result_type = vart::InferResultType::DETECTION;
          frame_results.push_back(std::move(ir));
        } else {
          /* Classification: one VvasInferResult whose data is a list of
           * VvasInferClassification; fold into one ClassificationResData. */
          auto ir = std::make_shared<vart::InferResult>(
              vart::InferResultType::CLASSIFICATION);
          auto* out = static_cast<vart::ClassificationResData*>(
              ir->get_infer_result());
          VvasList* clist =
              static_cast<VvasList*>(child->infer_result->data);
          uint32_t cn = clist ? vvas_list_length(clist) : 0;
          for (uint32_t k = 0; k < cn; k++) {
            VvasInferClassification* c =
                static_cast<VvasInferClassification*>(
                    vvas_list_nth_data(clist, k));
            if (!c) continue;
            out->label.push_back(c->label ? c->label : "");
            out->confidence.push_back(c->probability);
            out->index.push_back(static_cast<uint8_t>(c->id));
          }
          out->result_type = vart::InferResultType::CLASSIFICATION;
          frame_results.push_back(std::move(ir));
        }
      }
      if (children) vvas_list_free(children);
    }
    if (parent[b]) vvas_inferprediction_free(parent[b].release());
    results.push_back(std::move(frame_results));
  }

  return results;
}

}  // namespace xplusml
