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

#include <memory>
#include <string>
#include "greedy_sampler.hpp"
#include "temperature_sampler.hpp"
#include "topk_sampler.hpp"
#include "topp_sampler.hpp"
#include "repetition_penalty_sampler.hpp"

namespace llm_sampler {

/**
 * @brief Create a sampler by name.
 *
 * Supported names (case-insensitive):
 *   "greedy"                                  → GreedySampler
 *   "temperature"                             → TemperatureSampler(params.temperature)
 *   "topk" / "top-k" / "top_k"               → TopKSampler(params.topK)
 *   "topp" / "top-p" / "top_p"               → TopPSampler(params.topP)
 *   "repetition_penalty" / "repetition-penalty" → RepetitionPenaltySampler(params.repetitionPenalty)
 *
 * @param name   Sampler type string (case-insensitive).
 * @param params Initial parameters used to seed per-sampler defaults.
 * @return Owning pointer to the created sampler.
 * @throws std::invalid_argument if @p name is not recognised.
 */
std::unique_ptr<ISampler> CreateSampler(const std::string&  name,
                                        const SamplerParams& params = {});

}  // namespace llm_sampler
