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

/**
 * @file i_sampler.hpp
 * @brief Sampler interface and umbrella include for the llm_sampler library.
 *
 * Defines SamplerParams and ISampler (the abstract base class), then pulls in
 * all concrete sampler headers:
 *   - greedy_sampler.hpp             (GreedySampler)
 *   - temperature_sampler.hpp        (TemperatureSampler)
 *   - topk_sampler.hpp               (TopKSampler)
 *   - topp_sampler.hpp               (TopPSampler)
 *   - repetition_penalty_sampler.hpp (RepetitionPenaltySampler)
 *   - sampler_factory.hpp            (CreateSampler)
 */

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace llm_sampler {

/**
 * @brief Sampler configuration parameters.
 *
 * Passed to ISampler::Sample() on every call so a single sampler object can
 * be reused across different parameter sweeps without being reconstructed.
 */
struct SamplerParams {
  float temperature       = 1.0f;  ///< Temperature scaling factor (> 0)
  int   topK              = 0;     ///< Top-K filter (0 = disabled)
  float topP              = 0.0f;  ///< Top-P nucleus threshold (0.0 = disabled)
  float repetitionPenalty = 1.0f;  ///< Repetition penalty (1.0 = disabled)
  int   noRepeatNgram     = 0;     ///< No-repeat-ngram ban (0/1 = disabled, >=2 = n-gram size)
};

/**
 * @brief Abstract sampler interface.
 *
 * All samplers derive from ISampler and implement the three pure-virtual
 * methods. The protected helpers provide shared building blocks and are
 * intentionally not part of the public ABI.
 */
class ISampler {
 public:
  ISampler();
  virtual ~ISampler() = default;

  /** @brief Human-readable sampler name (e.g. "Greedy", "Top-K"). */
  virtual std::string GetName() const = 0;

  /** @brief Current configuration as a display string (may be empty). */
  virtual std::string GetValueString() const = 0;

  /**
   * @brief Select the next token from raw model logits.
   *
   * @param logits          Raw logit vector from the model (length = vocab_size).
   * @param params          Sampling hyper-parameters for this call.
   * @param generatedTokens Tokens produced so far (used by repetition penalty).
   * @return Selected token ID in [0, vocab_size).
   */
  virtual int32_t Sample(const std::vector<float>&   logits,
                         const SamplerParams&         params,
                         const std::vector<int32_t>& generatedTokens) = 0;

  /**
   * @brief Seed the random number generator for reproducible sampling.
   *
   * @param seed  When non-zero, seeds the RNG with this value for
   *              deterministic output.  When zero, re-seeds from
   *              std::random_device (non-deterministic).
   */
  void SetSeed(uint32_t seed);

 protected:
  /** @brief Divide every logit by @p temperature (no-op when temperature == 1). */
  void ApplyTemperature(std::vector<float>& logits, float temperature);

  /**
   * @brief Penalise previously generated tokens.
   *
   * Positive logits are divided by @p penalty; negative logits are multiplied.
   * No-op when penalty == 1.
   */
  void ApplyRepetitionPenalty(std::vector<float>&         logits,
                              const std::vector<int32_t>& generatedTokens,
                              float                        penalty);

  /**
   * @brief Ban tokens that would complete a previously seen n-gram.
   *
   * No-op when @p n < 2 or fewer than @p n generated tokens exist.
   */
  void ApplyNoRepeatNgram(std::vector<float>&         logits,
                          const std::vector<int32_t>& generatedTokens,
                          int                          n);

  /** @brief Numerically stable softmax — returns a probability vector. */
  std::vector<float> Softmax(const std::vector<float>& logits);

  /** @brief Per-instance RNG used by stochastic samplers. */
  std::mt19937 m_rng;
};

}  // namespace llm_sampler

// Concrete sampler headers — each includes i_sampler.hpp for the base class,
// which is safely skipped here by #pragma once.
#include "greedy_sampler.hpp"
#include "temperature_sampler.hpp"
#include "topk_sampler.hpp"
#include "topp_sampler.hpp"
#include "repetition_penalty_sampler.hpp"
#include "sampler_factory.hpp"
