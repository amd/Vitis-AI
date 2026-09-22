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
#pragma once

#include "i_sampler.hpp"

namespace llm_sampler {

/**
 * @brief Top-K sampler — samples from the K most probable tokens.
 *
 * Probabilities outside the top-K are zeroed and the remaining
 * distribution is renormalised before sampling.
 */
class TopKSampler : public ISampler {
 public:
  explicit TopKSampler(int topK = 50);

  std::string GetName() const override;

  std::string GetValueString() const override;

  int32_t Sample(const std::vector<float>&   logits,
                 const SamplerParams&         params,
                 const std::vector<int32_t>& generatedTokens) override;

  int GetTopK() const;

 private:
  int m_topK;
};

}  // namespace llm_sampler
