# x_plus_ml_vart Implementation Internals
<!--
## Copyright and license statement

Copyright (C) 2025-2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->

This document captures the internals of the `x_plus_ml_vart` reference application: system architecture, the four operational modes, the threading model, the priority-based event loop, and the zero-copy / `NpuTensor` caching design used by the inference component. For application usage (CLI, JSON configuration, on-target run, benchmarking) see [README.md](README.md). For the JSON schema see [json_configs/README.md](json_configs/README.md).

## System Architecture

The application is built around four black-box components - file reader, preprocess, inference, and postprocess - wired together through bounded `AppQueue<T>` instances. Each component runs in its own worker thread; a single main thread acts as a priority-based scheduler that drains completion queues and routes frames between stages. Memory is managed via two RAII pools (`VideoFramePool` for `vart::VideoFrame` slots and `MemoryBufferPool` for `vart::Memory` slots), and zero-copy is the default I/O path between preprocess -> inference and inference -> postprocess.

Per-model instances are bound 1:1:1 across stages: `preprocess[i]`, `inference[i]`, and `postprocess[i]` always handle the same model. The `binding map` infrastructure routes batches between bound instances; queue depths are set per stage from the model's `batch_size` and the configured pipeline depth.

## Operating Modes

Two independent switches derive the four modes:

- **Preprocessing** is a single global flag in the top-level config (`preprocess-en`). When `true`, every model in `models-config` runs the preprocess stage; when `false`, every model runs in inference-only mode (binary `ifms-config` inputs). There is no per-model preprocess switch.
- **Postprocessing** is per-model: each model JSON enables it by including a `postprocess-config` block. A run can therefore mix postproc-enabled and postproc-disabled models; the per-model variant always wins for that model's pipeline.

For models whose input tensor reports `GENERIC` memory layout, preprocessing is allowed only for a small set of shape-driven special cases: 3D tensors are treated as `NHW`, 4D tensors with `shape[1]` equal to `3` or `4` are treated as `NCHW`, and 4D tensors with `shape[3]` equal to `3` or `4` are treated as `NHWC`. Unsupported or ambiguous `GENERIC` shapes still fail during preprocess context creation.

| Mode                       | `preprocess-en` | `postprocess-config` (per model) | Threads | File Reader Input  | Stage Threads (per model)                     | Output                          |
|----------------------------|-----------------|----------------------------------|---------|--------------------|-----------------------------------------------|---------------------------------|
| 1: Full Pipeline           | true            | set on every model               | 4N+1    | JPEG / NV12 / BGR  | preprocess + inference + postprocess          | Binary OFM + text + overlay     |
| 2: Preprocess + Inference  | true            | absent on every model            | 3N+1    | JPEG / NV12 / BGR  | preprocess + inference                        | Binary OFM only                 |
| 3: Inference + PostProcess | false           | set on every model               | 3N+1    | Binary `.bin` IFMs | inference + postprocess                       | Binary OFM + text               |
| 4: Inference Only          | false           | absent on every model            | 2N+1    | Binary `.bin` IFMs | inference                                     | Binary OFM only                 |

`N` is `num_model_instances` (the length of `models-config`). The `+1` is the priority-based main thread. Mode 1 also instantiates a per-instance original-frame queue between the file reader and postprocess for overlay generation; the other three modes do not allocate it. Memory pools follow the same axes: `VideoFramePool + MemoryBufferPool` when preprocessing is enabled (Modes 1, 2), `MemoryBufferPool` only when not (Modes 3, 4).

### Canonical Diagram (Full Pipeline)

The figure below shows one model instance `[i]`. For an `N`-model run, replicate the file reader / preprocess / inference / postprocess stripe `N` times; the main loop and the priority drains are shared across all instances.

```
+--------------------------------------------------------------------------------------+
|                      Main Event Loop (Priority-Based, single thread)                 |
|  Priority 1: PostProcess completions (frame accounting + drain)                      |
|  Priority 2: Inference completions   (route batch to bound postprocess[i])           |
|  Priority 3: Preprocess completions  (route batch to bound inference[i])             |
|  Priority 4: File reader status      (advance more_input flag)                       |
+--------------------------------------------------------------------------------------+
|                                                                                      |
|  +-----------------+   1:1   +-----------------+   1:1   +-----------------+         |
|  | File Reader[i]  | ------> | Preprocess[i]   | ------> | Inference[i]    |         |
|  | (CLI or ifms)   | image Q | worker thread   |  IFM Q  | worker thread   |         |
|  +--------+--------+         +-----------------+         +--------+--------+         |
|           |                                                       | OFM Q            |
|           | orig-frame Q                                          v                  |
|           | (Mode 1 only)                                  +-----------------+       |
|           +----------------------------------------------> | PostProcess[i]  |       |
|                                                            | worker thread   |       |
|                                                            | merges OFM with |       |
|                                                            | orig frame, then|       |
|                                                            | runs PostProcess|       |
|                                                            | + MetaConvert   |       |
|                                                            | + Overlay       |       |
|                                                            +--------+--------+       |
|                                                                     |                |
|                                                                     v                |
|                              postproc{i}_results.txt + overlay (.jpg / .nv12 / .bgr) |
+--------------------------------------------------------------------------------------+
```

Mode variants (changes from the diagram above):

- **Mode 2** drops the postprocess block, the inference-result queue, and the orig-frame queue; inference dumps binary OFMs directly to disk.
- **Mode 3** replaces the file reader's image source with a binary IFM reader feeding inference directly (no preprocess); the orig-frame queue is absent (no overlay).
- **Mode 4** is Mode 3 minus the postprocess block; only binary OFMs are written.

## Threading Model

Threads per mode (where `N = num_model_instances`):

| Mode                      | File Readers | Preprocess | Inference | PostProcess | Main | Total |
|---------------------------|--------------|------------|-----------|-------------|------|-------|
| 1 Full Pipeline           | N            | N          | N         | N           | 1    | 4N+1  |
| 2 Preprocess + Inference  | N            | N          | N         | -           | 1    | 3N+1  |
| 3 Inference + PostProcess | N            | -          | N         | N           | 1    | 3N+1  |
| 4 Inference Only          | N            | -          | N         | -           | 1    | 2N+1  |

Producer-consumer rules:

- Each producer owns its output memory pool. Frames acquired from the pool flow downstream as `shared_ptr` with a custom deleter that returns the slot.
- Each `Inference` instance is the sole writer/reader of its `vart::NpuTensor` cache (see [NpuTensor Caching](#nputensor-caching-for-vart-optimization)); no mutex is taken on the hot path.
- The main thread never owns frames; it only routes them between component input/output queues.


## Pipeline Event Loop

The single main thread is a priority-based scheduler that drains completion queues in mode-specific priority order (see the canonical diagram above) and then yields. Each iteration:

1. Checks the global `critical_error` flag. If any worker has set it, the loop calls `trigger_pipeline_shutdown()` and breaks.
2. Polls the file readers for "still running / had error" status to update the `more_input` flag.
3. Drains stage completion queues in the mode's priority order. Higher-priority drains route batches into the next stage's input queue (e.g. inference completions are pushed into the bound postprocess instance's input queue).
4. Updates `frames_submitted` and `frames_completed` atomic counters; the main loop continues while `frames_in_pipeline = frames_submitted - frames_completed > 0` or `more_input` is `true`.
5. Sleeps briefly when no work was done in the iteration to keep idle CPU usage low.

The loop also enforces a **pipeline idle timeout**: if no frame completes for `pipeline_timeout_seconds` (`30s` in normal runs, `5s` when `--benchmark` is set), the loop logs `[WARNING] Application exited with N frames still in pipeline` and exits early. This timeout is the externally observable behaviour documented in [README.md](README.md) under "Additional Considerations". After the loop exits, the main thread calls `flush_pipeline()` (stops every component and drains residual frames) and then `destroy_all_context()` before returning.

## Zero-Copy Support

The application runs the model in zero-copy mode for both input and output tensors, meaning it directly uses hardware (HW) tensors. The `vart::Runner` is created with `input_tensor_type = "HW"` and `output_tensor_type = "HW"` (both hard-coded in `vart_context.cpp`). Zero-copy between Preprocess and Inference is therefore the default - provided that Preprocess emits the HW format expected by Inference. This is why `colour-format` in `preprocess-config` must match the model's `inputs->hw_format` from `flexmlrt-hsi.json` (see [json_configs/README.md](json_configs/README.md)). On the output side, the tensors from Inference stay in HW format and are handed directly to the postprocess stage when enabled.

## NpuTensor Caching for VART Optimization

The inference component caches `vart::NpuTensor` objects to avoid re-exporting the underlying buffer's dma-buf fd on every inference call. Caches are kept per tensor index: one `std::unordered_map<vart::VideoFrame*, vart::NpuTensor>` per input tensor and one `std::unordered_map<vart::Memory*, vart::NpuTensor>` per output tensor. The pool object pointer (the `VideoFrame*` from the preprocess pool, or the `Memory*` from the OFM pool) is used as the cache key because pool slots have stable identities for the lifetime of the pool, so the same pointer always maps to the same buffer.

On the first encounter of a pool slot the inference component calls `export_buffer()` on the underlying frame/memory, constructs a `vart::NpuTensor` from the exported fd, and inserts it into the cache. On subsequent encounters the cached `NpuTensor` is read out and copied into the per-call input/output batch passed to `runner_->execute()`; the cache entry stays in place. This satisfies VART's expectation that the same `NpuTensor` object identity is reused for the same BO, which lets `vart::Runner` skip its internal BO-export bookkeeping.

Cache sizing is driven by `vart::Runner`: the per-tensor-index vectors are resized to `runner_->get_num_input_tensors()` and `runner_->get_num_output_tensors()` at `vart::Runner` creation time. There is no hard upper bound on the number of distinct pool slots cached per tensor index. The cache is cleared whenever the `vart::Runner` is (re)created and on `Inference` shutdown. Concurrency safety relies on each `Inference` instance owning its dedicated worker thread, which is the only writer and reader of its caches; no mutex is taken on the hot path.
