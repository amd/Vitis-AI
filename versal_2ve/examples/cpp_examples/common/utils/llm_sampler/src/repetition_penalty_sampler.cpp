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
#include <random>
#include <string>
#include "repetition_penalty_sampler.hpp"

namespace llm_sampler {

RepetitionPenaltySampler::RepetitionPenaltySampler(float repetitionPenalty)
    : m_repetitionPenalty(repetitionPenalty) {}

std::string RepetitionPenaltySampler::GetName() const {
  return "Repetition Penalty";
}

float RepetitionPenaltySampler::GetRepetitionPenalty() const {
  return m_repetitionPenalty;
}

std::string RepetitionPenaltySampler::GetValueString() const {
  return "penalty=" + std::to_string(m_repetitionPenalty);
}

int32_t RepetitionPenaltySampler::Sample(const std::vector<float>&   logits,
                                         const SamplerParams&         params,
                                         const std::vector<int32_t>& generatedTokens) {
  std::vector<float> processed = logits;

  if (m_repetitionPenalty != 1.0f && !generatedTokens.empty()) {
    ApplyRepetitionPenalty(processed, generatedTokens, m_repetitionPenalty);
  }
  if (params.noRepeatNgram >= 2 && !generatedTokens.empty()) {
    ApplyNoRepeatNgram(processed, generatedTokens, params.noRepeatNgram);
  }

  if (params.temperature > 0.0f && params.temperature != 1.0f) {
    ApplyTemperature(processed, params.temperature);
  }

  std::vector<float> probs = Softmax(processed);

  std::discrete_distribution<> dist(probs.begin(), probs.end());

  return static_cast<int32_t>(dist(m_rng));
}

}  // namespace llm_sampler
