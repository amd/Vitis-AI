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
#include "greedy_sampler.hpp"

namespace llm_sampler {

GreedySampler::GreedySampler() = default;

std::string GreedySampler::GetName() const {
  return "Greedy";
}

std::string GreedySampler::GetValueString() const {
  return "";
}

int32_t GreedySampler::Sample(const std::vector<float>&   logits,
                              const SamplerParams&         params,
                              const std::vector<int32_t>& generatedTokens) {
  std::vector<float> processed = logits;

  if (params.repetitionPenalty != 1.0f && !generatedTokens.empty()) {
    ApplyRepetitionPenalty(processed, generatedTokens, params.repetitionPenalty);
  }
  if (params.noRepeatNgram >= 2 && !generatedTokens.empty()) {
    ApplyNoRepeatNgram(processed, generatedTokens, params.noRepeatNgram);
  }

  auto max_it = std::max_element(processed.begin(), processed.end());
  return static_cast<int32_t>(std::distance(processed.begin(), max_it));
}

}  // namespace llm_sampler
