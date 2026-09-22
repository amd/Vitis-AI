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
 * @file llm_sampler.hpp
 * @brief Public entry-point for the llm_sampler library.
 *
 * Include this header in application code to access the full sampler API:
 *
 * @code
 * #include <llm_sampler.hpp>
 * using namespace llm_sampler;
 *
 * SamplerParams params;
 * params.temperature = 0.8f;
 *
 * auto sampler = CreateSampler("top-k", params);
 * int32_t token = sampler->Sample(logits, params, generated);
 * @endcode
 *
 * Provided types (all in namespace llm_sampler):
 *   - SamplerParams              — hyper-parameter struct
 *   - ISampler                   — abstract base class
 *   - GreedySampler              — argmax
 *   - TemperatureSampler         — temperature-scaled softmax
 *   - TopKSampler                — top-K filter
 *   - TopPSampler                — nucleus (top-P)
 *   - RepetitionPenaltySampler   — repetition penalty
 *   - CreateSampler()            — factory function
 */

#include "i_sampler.hpp"
