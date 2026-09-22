/******************************************************************************
 * Copyright (C) 2026 Advanced Micro Devices, Inc.
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
 ******************************************************************************/
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include "sampler_factory.hpp"

namespace llm_sampler {

std::unique_ptr<ISampler> CreateSampler(const std::string& name,
                                        const SamplerParams& params) {
  // Normalise to lower-case for case-insensitive matching
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  if (lower == "greedy") {
    return std::make_unique<GreedySampler>();
  }
  if (lower == "temperature") {
    return std::make_unique<TemperatureSampler>(params.temperature);
  }
  if (lower == "topk" || lower == "top-k" || lower == "top_k") {
    return std::make_unique<TopKSampler>(params.topK);
  }
  if (lower == "topp" || lower == "top-p" || lower == "top_p") {
    return std::make_unique<TopPSampler>(params.topP);
  }
  if (lower == "repetition_penalty" || lower == "repetitionpenalty" ||
      lower == "repetition-penalty") {
    return std::make_unique<RepetitionPenaltySampler>(params.repetitionPenalty);
  }

  throw std::invalid_argument("Unknown sampler type: '" + name +
                              "'. Valid types: greedy, temperature, topk, topp, "
                              "repetition_penalty");
}

}  // namespace llm_sampler
