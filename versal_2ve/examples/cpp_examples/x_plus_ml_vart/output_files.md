# x_plus_ml_vart Output Files Reference
<!--
## Copyright and license statement

Copyright (C) 2025-2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->

This document is the full naming reference for files written under `output/`. For the at-a-glance summary, see [README.md](README.md#output).

## Filename rules

All artifacts share an iteration prefix: a single-iteration run (`--runs 1`, the default) emits clean names; `--runs N` with `N>1` prefixes per-iteration files with `iter{I}_` (`{I}` is the 0-based iteration number).

| Artefact                | Single iteration                                                                          | Multi-iteration                                                                                       |
|-------------------------|-------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------------|
| Binary OFM (per tensor) | `infer{N}_out{X}-{dtype}_{shape}_{tensor_name}.bin`                                       | `iter{I}_infer{N}_out{X}-{dtype}_{shape}_{tensor_name}.bin`                                           |
| PostProcess results     | `postproc{N}_results.txt`                                                                 | `postproc{N}_results.txt` (no prefix; all iterations appended into the same file)                     |
| Overlay (JPG input)     | `postproc{N}_overlay.jpg` (one file per frame)                                            | `iter{I}_postproc{N}_overlay.jpg` (one file per frame within each iteration)                          |
| Overlay (NV12 input)    | `postproc{N}_overlay.nv12` (raw `Y_UV8_420`, all frames concatenated)                     | `iter{I}_postproc{N}_overlay.nv12` (one container per iteration)                                      |
| Overlay (BGR input)     | `postproc{N}_overlay.bgr` (raw BGR, all frames concatenated)                              | `iter{I}_postproc{N}_overlay.bgr` (one container per iteration)                                       |

Filename components:

- `{N}` - inference / postprocess instance index from `models-config` (0-based).
- `{X}` - output tensor index within the model (0-based).
- `{I}` - iteration number (0-based), present only when `--runs N` with `N>1`.
- `{dtype}` - output tensor data type (`int8`, `bf16`, `fp32`, ...).
- `{shape}` - output tensor shape, e.g. `1x1000`.
- `{tensor_name}` - model output tensor name with `/` replaced by `-` and any leading `-` stripped. For example, `/compute_graph.ofm_ddr` becomes `compute_graph.ofm_ddr`.

Binary OFM file layout:

- One file per output tensor, not per batch element or per frame.
- The first frame creates/truncates the file; subsequent frames in the same iteration append.
- Multi-frame and batched inputs concatenate sequentially into the same file.
- For models with batch size `N > 1`, each inference call appends `N` frames per output tensor in the same order as the corresponding input batch. The per-frame size is the `vart::Runner`-reported per-tensor `size_in_bytes` (query with `ml_vart --get-model-info <model-path>`; the same dump exposes `batch_size` for `N`). Partial-batch runs at end-of-file only append the slots that were actually populated.

## Example output trees

The two trees below cover every combination - the only axis the table above adds is the input format suffix on the overlay file (`.jpg` per-frame, `.nv12`/`.bgr` per-iteration container).

Single iteration, full pipeline, JPG input:

```
output/
├── infer0_out0-bf16_1x1000_compute_graph.ofm_ddr.bin
├── postproc0_results.txt
└── postproc0_overlay.jpg
```

Multi-iteration (`--runs 3`), full pipeline, NV12 input:

```
output/
├── iter0_infer0_out0-bf16_1x1000_compute_graph.ofm_ddr.bin
├── iter1_infer0_out0-bf16_1x1000_compute_graph.ofm_ddr.bin
├── iter2_infer0_out0-bf16_1x1000_compute_graph.ofm_ddr.bin
├── postproc0_results.txt                                  # all iterations appended
├── iter0_postproc0_overlay.nv12                           # all frames of iteration 0
├── iter1_postproc0_overlay.nv12
└── iter2_postproc0_overlay.nv12
```

For BGR input, swap the overlay extension to `.bgr` (same per-iteration container layout). For PostProcess-disabled runs, drop the `postproc{N}_*` entries; only the binary OFM rows remain.
