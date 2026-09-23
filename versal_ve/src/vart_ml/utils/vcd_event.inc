/**
 * @file vcd_event.inc
 *
 * @copyright Copyright 2025 Advanced Micro Devices Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

EVENT(EXECUTE)

/* NPU EVENTS */
EVENT(NPU_EXECUTE)
EVENT(NPU_RUN)
EVENT(NPU_UPLOAD_WAIT_MUTEX)
EVENT(NPU_UPLOAD)
EVENT(NPU_REORDER_IN)
EVENT(NPU_WRITE_DDR)
EVENT(NPU_DOWNLOAD_WAIT_MUTEX)
EVENT(NPU_INFERENCE_WAIT_MUTEX)
EVENT(NPU_SET_NBUFF_ADDR)
EVENT(NPU_INFERENCE_START)
EVENT(NPU_INFERENCE_MCTRL_CONFIG)
EVENT(NPU_INFERENCE_CORE_CONFIG)
EVENT(NPU_INFERENCE_IRQ_CONFIG)
EVENT(NPU_INFERENCE_WAIT)
EVENT(NPU_REORDER_OUT)
EVENT(NPU_ARM_OP_CAST)
EVENT(NPU_ARM_OP_PAD)
EVENT(NPU_ARM_OP_DEQUANTIZE)
EVENT(NPU_ARM_OP_QUANTIZE)
EVENT(NPU_ARM_OP_SLICE)
EVENT(NPU_ARM_OP_TRANSPOSE)
EVENT(NPU_READ_DDR)
EVENT(NPU_DOWNLOAD)

/* ONNX EVENTS */
EVENT(ONNX_RUN)
