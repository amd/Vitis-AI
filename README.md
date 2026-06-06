<!--
Copyright (C) 2025-2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0.

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->

# Vitis AI 6.2

Vitis AI is AMD's development stack for AI inference on AMD adaptive computing platforms. Release **6.2** supports two device series:

- **AMD Versal AI Edge Series**
- **AMD Versal AI Edge Series Gen 2**

The compiler, software dependencies, and associated tools and utilities are device-series specific, which is why content is organized per device series.

## Repository Layout

```text
.
├── versal_ve/            # Vitis AI for Versal AI Edge Series
├── versal_2ve/           # Vitis AI for Versal AI Edge Series Gen 2
│   ├── examples/         # Tutorials and C++/Python example applications
│   ├── reference_design/ # VEK385 reference design (Rev-A, Rev-B)
│   └── tools/            # Host setup and flash utilities
├── vitis_kernels/        # Shared PL HLS kernels
```

## Getting Started

Pick the device series you are targeting and follow the README in that folder:

| Device Series | Folder | Start Here |
|---------------|--------|------------|
| Versal AI Edge Series | [`versal_ve/`](versal_ve/) | [`versal_ve/README.md`](versal_ve/README.md) |
| Versal AI Edge Series Gen 2 | [`versal_2ve/`](versal_2ve/) | [`versal_2ve/README.md`](versal_2ve/README.md) |

## Documentation

Refer to the official [`Vitis AI documentation`](https://vitisai.docs.amd.com/) on the AMD documentation portal for user guides, API references, and tutorials.

## License

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for the full text.
