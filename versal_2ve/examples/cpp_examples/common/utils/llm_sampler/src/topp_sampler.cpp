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
#include <numeric>
#include <random>
#include <string>
#include "topp_sampler.hpp"

namespace llm_sampler {

TopPSampler::TopPSampler(float topP) : m_topP(topP) {}

std::string TopPSampler::GetName() const {
  return "Top-P";
}

float TopPSampler::GetTopP() const {
  return m_topP;
}

std::string TopPSampler::GetValueString() const {
  return "p=" + std::to_string(m_topP);
}

int32_t TopPSampler::Sample(const std::vector<float>&   logits,
                            const SamplerParams&         params,
                            const std::vector<int32_t>& generatedTokens) {
  std::vector<float> processed = logits;

  if (params.repetitionPenalty != 1.0f && !generatedTokens.empty()) {
    ApplyRepetitionPenalty(processed, generatedTokens, params.repetitionPenalty);
  }
  if (params.noRepeatNgram >= 2 && !generatedTokens.empty()) {
    ApplyNoRepeatNgram(processed, generatedTokens, params.noRepeatNgram);
  }

  ApplyTemperature(processed, params.temperature);

  std::vector<float> probs = Softmax(processed);

  if (m_topP > 0.0f && m_topP < 1.0f) {
    std::vector<std::pair<float, int>> prob_indices;
    prob_indices.reserve(probs.size());
    for (size_t i = 0; i < probs.size(); ++i) {
      prob_indices.push_back({probs[i], static_cast<int>(i)});
    }
    std::sort(prob_indices.begin(), prob_indices.end(), std::greater<>());

    float cumulative = 0.0f;
    size_t cutoff = prob_indices.size();
    for (size_t i = 0; i < prob_indices.size(); ++i) {
      cumulative += prob_indices[i].first;
      if (cumulative >= m_topP) {
        cutoff = i + 1;
        break;
      }
    }

    for (size_t i = cutoff; i < prob_indices.size(); ++i) {
      probs[prob_indices[i].second] = 0.0f;
    }

    float sum = std::accumulate(probs.begin(), probs.end(), 0.0f);
    if (sum > 0.0f) {
      for (float& p : probs)
        p /= sum;
    }
  }

  std::discrete_distribution<> dist(probs.begin(), probs.end());

  return static_cast<int32_t>(dist(m_rng));
}

}  // namespace llm_sampler
