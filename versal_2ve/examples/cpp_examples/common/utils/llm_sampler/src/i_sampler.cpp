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
#include <cmath>
#include "i_sampler.hpp"

/* Large negative logit assigned to a banned token so it is effectively
 * excluded from sampling (softmax(exp(-1e30)) -> 0). */
#define BANNED_TOKEN_LOGIT (-1e30f)

namespace llm_sampler {

ISampler::ISampler() : m_rng(std::random_device{}()) {}

void ISampler::SetSeed(uint32_t seed) {
  if (seed != 0) {
    m_rng.seed(seed);
  } else {
    m_rng.seed(std::random_device{}());
  }
}

void ISampler::ApplyTemperature(std::vector<float>& logits, float temperature) {
  if (temperature != 1.0f && temperature > 0.0f) {
    for (float& logit : logits) {
      logit /= temperature;
    }
  }
}

void ISampler::ApplyRepetitionPenalty(std::vector<float>&         logits,
                                      const std::vector<int32_t>& generatedTokens,
                                      float                        penalty) {
  if (penalty == 1.0f)
    return;

  for (int32_t token : generatedTokens) {
    if (token >= 0 && token < static_cast<int32_t>(logits.size())) {
      if (logits[token] < 0.0f) {
        logits[token] *= penalty;
      } else {
        logits[token] /= penalty;
      }
    }
  }
}

void ISampler::ApplyNoRepeatNgram(std::vector<float>&         logits,
                                  const std::vector<int32_t>& generatedTokens,
                                  int                          n) {
  if (n < 2 || static_cast<int>(generatedTokens.size()) < n)
    return;

  const int64_t V = static_cast<int64_t>(logits.size());
  const size_t L = generatedTokens.size();
  const auto prefix_begin = generatedTokens.end() - (n - 1);
  for (size_t i = 0; i + static_cast<size_t>(n) <= L; ++i) {
    bool eq = true;
    for (int k = 0; k < n - 1; ++k) {
      if (generatedTokens[i + k] != *(prefix_begin + k)) {
        eq = false;
        break;
      }
    }
    if (eq) {
      const int32_t ban = generatedTokens[i + n - 1];
      if (ban >= 0 && ban < V)
        logits[static_cast<size_t>(ban)] = BANNED_TOKEN_LOGIT;
    }
  }
}

std::vector<float> ISampler::Softmax(const std::vector<float>& logits) {
  if (logits.empty()) {
    return {};
  }

  std::vector<float> probs(logits.size());

  // Subtract max for numerical stability
  float max_logit = *std::max_element(logits.begin(), logits.end());

  float sum_exp = 0.0f;
  for (size_t i = 0; i < logits.size(); ++i) {
    probs[i] = std::exp(logits[i] - max_logit);
    sum_exp += probs[i];
  }

  if (sum_exp > 0.0f) {
    for (float& p : probs) {
      p /= sum_exp;
    }
  }

  return probs;
}

}  // namespace llm_sampler
