# llm_sampler

<!--
## Copyright and license statement

Copyright (C) 2025-2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->

A standalone C++17 library that provides token-sampling strategies for LLM inference pipelines.  It mirrors the structure of the [`llm_tokenizer`](../llm_tokenizer/) library and integrates with the same build system.

## Samplers

All samplers derive from `ISampler` and accept the same `SamplerParams` struct. Each implements:

```cpp
int32_t Sample(const std::vector<float>& logits,
               const SamplerParams&      params,
               const std::vector<int32_t>& generatedTokens);
```

---

### GreedySampler

**Algorithm:** Argmax — always selects the token with the highest logit value.

Optionally applies repetition penalty (from `params.repetitionPenalty`) before taking the argmax. No randomness is involved; output is fully deterministic for a given set of logits.

- **Use when:** you want reproducible, highest-confidence outputs.
- **Parameter:** none (penalty read from `SamplerParams` at call time).

---

### TemperatureSampler

**Algorithm:** Temperature-scaled softmax followed by weighted random sampling.

Each logit `z_i` is divided by the temperature `T` before the softmax is applied:

```
p_i = exp(z_i / T) / Σ exp(z_j / T)
```

A token is then drawn from this distribution. Lower `T` sharpens the distribution (approaches greedy); higher `T` flattens it (more random).

- **Use when:** you want controllable randomness without hard truncation.
- **Parameter:** `temperature` — valid range `(0, 100]`. Values near `1.0` preserve the model's raw distribution; `< 1.0` focuses on high-probability tokens; `> 1.0` increases diversity.

---

### TopKSampler

**Algorithm:** Top-K filtering followed by weighted random sampling.

1. Compute softmax probabilities from raw logits.
2. Keep only the `K` tokens with the highest probability; zero out the rest.
3. Renormalise the retained probabilities to sum to 1.
4. Draw a token from the renormalised distribution.

- **Use when:** you want to prevent very low-probability tokens from ever being selected, while still allowing randomness among the top candidates.
- **Parameter:** `topK` (constructor) — number of candidates to keep. Must be `> 0`.

---

### TopPSampler

**Algorithm:** Nucleus (top-P) sampling.

1. Sort tokens by descending probability.
2. Accumulate probabilities until the cumulative mass reaches threshold `P`.
3. Discard all tokens outside this "nucleus"; renormalise the retained set.
4. Draw a token from the renormalised nucleus.

Unlike top-K, the nucleus size adapts to the model's confidence: a peaked distribution yields a small nucleus; a flat distribution yields a larger one.

- **Use when:** you want adaptive truncation that naturally handles both high- and low-entropy situations. Widely used in production LLM deployments.
- **Parameter:** `topP` (constructor) — nucleus probability threshold in `(0, 1]`. Typical values: `0.9`–`0.95`.

---

### RepetitionPenaltySampler

**Algorithm:** Repetition penalty applied to previously generated tokens, followed by temperature-scaled softmax and weighted random sampling.

For each token `t` in `generatedTokens`, its logit `z_t` is adjusted before sampling:

```
z_t = z_t / penalty   if z_t > 0
z_t = z_t * penalty   if z_t < 0
```

A penalty `> 1.0` reduces the probability of repeating tokens; `1.0` has no effect. After penalty application, temperature scaling and softmax are applied, and a token is sampled from the result.

- **Use when:** the model tends to repeat phrases or fall into loops. Effective for long-form generation tasks.
- **Parameter:** `repetitionPenalty` (constructor) — valid range `[0, 10]`. `1.0` = no penalty; values around `1.1`–`1.3` are common starting points.

---

## Directory layout

```
llm_sampler/
├── include/
│   ├── llm_sampler.hpp       # Public umbrella header — include this in application code
│   ├── i_sampler.hpp                  # ISampler base class + SamplerParams
│   ├── greedy_sampler.hpp
│   ├── temperature_sampler.hpp
│   ├── topk_sampler.hpp
│   ├── topp_sampler.hpp
│   ├── repetition_penalty_sampler.hpp
│   └── sampler_factory.hpp
├── src/
│   ├── i_sampler.cpp              # ISampler shared helpers (Softmax, temperature, penalty)
│   ├── greedy_sampler.cpp
│   ├── temperature_sampler.cpp
│   ├── topk_sampler.cpp
│   ├── topp_sampler.cpp
│   ├── repetition_penalty_sampler.cpp
│   └── sampler_factory.cpp        # CreateSampler() factory function
├── examples/
│   ├── CMakeLists.txt
│   └── simple_example.cpp
├── tests/
│   ├── Makefile                       # Standalone build + run (delegates to run_tests.sh)
│   ├── README.md
│   ├── run_tests.sh                   # Build and run logic; handles aarch64 noexec workaround
│   └── unit-test/
│       └── sampler_test_runner.cpp
├── CMakeLists.txt
├── Makefile
└── README.md
```

## Quick start

```bash
# Build the shared library
make

# Build library + example
make all-with-examples

# Install to a sysroot (e.g. during Yocto cross-build)
make install INSTALL_PREFIX=/path/to/sysroot/usr
```

## Testing

Unit tests live in `tests/`. See [tests/README.md](tests/README.md) for full details.

| Run from | Command | What it does |
|----------|---------|--------------|
| `llm_sampler/` | `make test` | Build library + build test binary (no run) |
| `llm_sampler/tests/` | `make` or `make test` | Build test binary + run all tests |
| `llm_sampler/tests/` | `make build` | Build test binary only (no run) |

Custom results directory (from `tests/`):

```bash
make RESULTS_DIR=/tmp/my_results
```

## API usage

```cpp
#include <llm_sampler.hpp>
using namespace llm_sampler;

// Build logits from your LLM output layer
std::vector<float> logits = model.GetLogits();  // shape: [vocab_size]
std::vector<int32_t> generated;                 // previously generated tokens

SamplerParams params;
params.temperature       = 0.8f;
params.topK              = 50;
params.repetitionPenalty = 1.1f;

// Direct construction
TopKSampler sampler(50);
int32_t next_token = sampler.Sample(logits, params, generated);
generated.push_back(next_token);

// Or via factory (useful when sampler type is selected at runtime)
auto sampler2 = CreateSampler("topp", params);
next_token = sampler2->Sample(logits, params, generated);
```

### SamplerParams

| Field | Default | Description |
|-------|---------|-------------|
| `temperature` | `1.0f` | Scaling factor applied before softmax. Values in (0, 1) sharpen; > 1 flatten. |
| `topK` | `0` | Restrict sampling to the top-K tokens (0 = disabled). |
| `topP` | `0.0f` | Nucleus threshold — keep smallest set with cumulative mass ≥ P (0.0 = disabled). |
| `repetitionPenalty` | `1.0f` | Penalise previously generated tokens (1.0 = no effect). |

### CreateSampler — recognised names

| Name strings | Sampler created |
|---|---|
| `"greedy"` | `GreedySampler` |
| `"temperature"` | `TemperatureSampler(params.temperature)` |
| `"topk"`, `"top-k"`, `"top_k"` | `TopKSampler(params.topK)` |
| `"topp"`, `"top-p"`, `"top_p"` | `TopPSampler(params.topP)` |
| `"repetition_penalty"`, `"repetitionpenalty"`, `"repetition-penalty"` | `RepetitionPenaltySampler(params.repetitionPenalty)` |

Name matching is case-insensitive.  Unknown names throw `std::invalid_argument`.

## Build configuration

| Variable | Default | Description |
|---|---|---|
| `BUILD_DIR` | `build` | CMake output directory |
| `BUILD_TYPE` | `Release` | `Release` / `Debug` |
| `BUILD_SHARED_LIBS` | `ON` | `ON` = shared `.so`; `OFF` = static `.a` |
| `CMAKE_FLAGS` | `-Wall -Wextra -Wpedantic` | Extra CXX flags |
| `INSTALL_PREFIX` | — | (install only) Destination prefix; headers → `PREFIX/include`, lib → `PREFIX/lib` |

Cross-compilation with a Yocto/OpenEmbedded SDK is detected automatically when `OECORE_NATIVE_SYSROOT` is set in the environment.

## Supported platforms

| Platform | Notes |
|---|---|
| **x86_64 Linux** | Native build and test |
| **aarch64 Linux** | Cross-compile via Yocto/OE SDK (`source environment-setup-*`) |

## Integration with simple_app

The top-level `Makefile` builds this library and installs it to
`build/libs/llm_sampler/` before linking the `simple_app` executable.
The `simple_app` includes this library via:

```
-I./build/libs/llm_sampler/include
-L./build/libs/llm_sampler/lib -lllm_sampler
```

## License

Apache License 2.0 — see the [LICENSE](../../../../LICENSE) file at the repository root.
