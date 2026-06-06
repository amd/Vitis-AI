# runner-options Reference

<!--
## Copyright and license statement

Copyright (C) 2025 - 2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->


## Overview

`runner-options` is a pass-through object inside `inference-config` that configures a single `vart::Runner`. `vart::Runner` itself is an abstract class with multiple backends; the fields documented here are the **RunnerType::VAIML** options (the backend used by the example apps for NPU inference). It is consumed by `ml_vart` and `x_plus_ml_vart` (in the latter, once per entry of `models-config`).

All fields are optional. Omitted fields fall back to each app's default (listed below) or, for the booleans not initialised by the app, to the `vart::Runner` backend default.

---

## Fields

| Field                   | Type    | Default        | Description                                                                                                                              |
| ----------------------- | ------- | -------------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| `config-file`           | string  | unset          | Path to the Vitis AI / EP configuration JSON used at compile time. Forwarded to `vart::Runner` only when set.                            |
| `log-level`             | string  | see notes      | `vart::Runner` log verbosity. Accepted: `ERROR`, `WARNING`, `INFO`, `DEBUG`. App defaults: `ml_vart` → `WARNING`, `x_plus_ml_vart` → `ERROR`. |
| `aie-columns-sharing`   | boolean | backend default | NPU column scheduling: `true` = shared (temporal multi-tenancy), `false` = exclusive (spatial multi-tenancy). See note below.           |
| `start-column`          | integer | backend default | Starting NPU column for the overlay's placement. When omitted, `vart::Runner` picks the first free column. See note below.              |
| `cma-index`             | integer | `0`            | CMA index used to allocate `vart::Runner` buffer objects. **Parsed by `ml_vart` only**; ignored if added to an `x_plus_ml_vart` config.  |
| `input-tensor-type`     | string  | `HW`           | Whether input tensors are prepared in CPU format or NPU-accepted HW format. Accepted: `CPU`, `HW`. Invalid values fall back to `HW` with a warning. |
| `output-tensor-type`    | string  | `HW`           | Whether output tensors are prepared in CPU format or NPU-accepted HW format. Accepted: `CPU`, `HW`. Invalid values fall back to `HW` with a warning. |
| `ai-analyzer-profiling` | boolean | `false`        | Enables AI Analyzer profiling capture for this `vart::Runner` instance.                                                                  |

> **NPU column placement.** `aie-columns-sharing` and `start-column` together control how each model is laid out across the NPU's AI Engine columns. For the underlying concepts (data vs. tensor parallelism, spatial vs. temporal multi-tenancy, column math per device) see **[multi_tenancy.md](multi_tenancy.md)**. For the default-placement behaviour when `start-column` is omitted, see **[auto_placement_policy.md](auto_placement_policy.md)**.

> **Tensor type and pre-processing.** When the app drives `vart::PreProcess` (the HLS `image_processing` kernel), `input-tensor-type` must match what the pre-processor produces. CPU-format inputs imply the pre-processor's output goes through a CPU-side reformat path; HW-format inputs are zero-copy into `vart::Runner`. The matching `colour-format` is picked from the model's input tensor metadata; see **[preprocessing_config.md](preprocessing_config.md)**.

---

## Example

```json
"inference-config": {
  "model-file": "/etc/vai/models/resnet50_int8/resnet50_int8.rai",
  "runner-options": {
    "log-level": "WARNING",
    "aie-columns-sharing": false,
    "start-column": 0,
    "input-tensor-type": "HW",
    "output-tensor-type": "HW",
    "ai-analyzer-profiling": false
  }
}
```

---

## Related documentation

- **[multi_tenancy.md](multi_tenancy.md)** — data vs. tensor parallelism and spatial vs. temporal multi-tenancy on the NPU.
- **[auto_placement_policy.md](auto_placement_policy.md)** — default `start-column` placement when not specified.
- **[preprocessing_config.md](preprocessing_config.md)** — `colour-format` selection driven by the chosen `input-tensor-type`.
- **[postprocessing_config.md](postprocessing_config.md)** — `postprocess-config` schema applied to `vart::Runner` outputs.
