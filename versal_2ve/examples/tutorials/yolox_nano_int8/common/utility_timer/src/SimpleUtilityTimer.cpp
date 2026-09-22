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

#include "SimpleUtilityTimer.hpp"

namespace utiltimer {

// Enable performance timing with optional warning output stream
void enable(std::ostream& warningOutStream) {
  perf::UtilityTimer::enable(warningOutStream);
}

// Disable performance timing
void disable() {
  perf::UtilityTimer::disable();
}

// Print all timing results
void print(TimerOutputFormat format, std::ostream& outputStream) {
  perf::UtilityTimer::print(format, outputStream);
}

// Reset/clear all timing data
void reset() {
  perf::UtilityTimer::reset();
}

// Start timing with a label
void start(const std::string& label) {
  perf::UtilityTimer::start(label);
}

// Stop timing for a label
void stop(const std::string& label) {
  perf::UtilityTimer::stop(label);
}

// Create a scoped timer
ScopedTimer startScoped(const std::string& label) {
  return perf::UtilityTimer::startScoped(label);
}

}  // namespace utiltimer
