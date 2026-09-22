/*
 * Copyright 2026 Advanced Micro Devices Inc.
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
 */

#pragma once

#include <iostream>
#include <string>
#include "UtilityTimer.hpp"

namespace utiltimer {

// Type alias for TimerOutputFormat enum for easier access
using TimerOutputFormat = perf::UtilityTimer::TimerOutputFormat;

// Enable performance timing with required warning output stream.
void enable(std::ostream& warningOutStream);

// Disable performance timing.
void disable();

// Start timing with a label.
void start(const std::string& label);

// Stop timing for a label.
void stop(const std::string& label);

// Print all timing results with required output stream and format.
// -------------------------------------------------------------------
// Output format enum
// -------------------------------------------------------------------
// enum class TimerOutputFormat {
//   HUMAN_READABLE_TEXT, // Output as a human readable format (See function definition)
//   CHROME_TRACE_JSON    // Output to Chrome trace json format (See function definition)
// };
void print(TimerOutputFormat format, std::ostream& outputStream);

// Reset/clear all timing data
void reset();

// Type alias for ScopedTimer.
using ScopedTimer = perf::UtilityTimer::ScopedTimer;

// Create a scoped timer that automatically stops when it goes out of scope.
// Can also be manually stopped with "scopedInstance.stop()" if needed.
ScopedTimer startScoped(const std::string& label);

}  // namespace utiltimer
