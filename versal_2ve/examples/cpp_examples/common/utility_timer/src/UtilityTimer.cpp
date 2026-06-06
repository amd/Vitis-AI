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

#include "UtilityTimer.hpp"

#include <unistd.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace perf {

// -------------------------------------------------------------------
// Performance instance struct
// -------------------------------------------------------------------
struct UtilityTimer::PerfInstance {
  std::chrono::high_resolution_clock::time_point startTime{};  // Start of timing.
  std::chrono::high_resolution_clock::time_point stopTime{};   // End of timing.
  bool timingActive{false};                                    // Is timing ongoing for this label?
  size_t order{0};                                             // The global ordering of time starts.

  PerfInstance(size_t globalOrder)
      : startTime(std::chrono::high_resolution_clock::now()),
        timingActive(true),
        order(globalOrder){
            // Empty
        };

  void stop() {
    if (timingActive) {
      stopTime = std::chrono::high_resolution_clock::now();
      timingActive = false;
    }
  }
};

// -------------------------------------------------------------------
// Thread-local data struct
// -------------------------------------------------------------------
// Note: No destructor. Thread local data persists until program exit.
// This ensures timing data remains available for print() even after threads exit,
// preventing data loss when threads complete before print() is called.
// Memory impact is minimal (~100-200 bytes per thread + timing instances).
struct UtilityTimer::ThreadLocalData {
  std::unordered_map<std::string, std::vector<PerfInstance>> performanceInstances;
  std::thread::id threadId;

  ThreadLocalData() : threadId(std::this_thread::get_id()) {
    // Empty
  }
};

// Initialize thread-local storage pointer.
// Note: Uses shared_ptr for automatic memory management with reference counting.
// Data persists after thread exit (registry holds reference) but is eventually freed.
thread_local std::shared_ptr<UtilityTimer::ThreadLocalData> UtilityTimer::threadLocalStorageData = nullptr;

// -------------------------------------------------------------------
// ScopedTimer implementations
// -------------------------------------------------------------------
UtilityTimer::ScopedTimer::ScopedTimer(const std::string& label)
    : m_label(label), m_threadId(std::this_thread::get_id()), m_stopped(false) {
  m_timingEnabled = getSingletonInstance().m_enablePerfTiming;
  if (m_timingEnabled) {
    getSingletonInstance().timerStart(label);
  }
}

UtilityTimer::ScopedTimer::~ScopedTimer() {
  if (!m_stopped && m_timingEnabled) {
    getSingletonInstance().timerStop(m_label);
  }
}

void UtilityTimer::ScopedTimer::stop() {
  if (!m_stopped && m_timingEnabled) {
    getSingletonInstance().timerStop(m_label);
    m_stopped = true;
  }
}

// -------------------------------------------------------------------
// UtilityTimer implementations
// -------------------------------------------------------------------

UtilityTimer::UtilityTimer() : m_enablePerfTiming(false), m_warningOutStream(nullptr), m_globalOrderCounter(0) {
  // Empty
}

UtilityTimer::~UtilityTimer() {
  // Empty
}

UtilityTimer& UtilityTimer::getSingletonInstance() {
  static UtilityTimer instance;
  return instance;
}

void UtilityTimer::enable(std::ostream& warningOutStream) {
  getSingletonInstance().m_enablePerfTiming = true;
  getSingletonInstance().m_warningOutStream = &warningOutStream;
}

void UtilityTimer::disable() {
  getSingletonInstance().m_enablePerfTiming = false;
}

UtilityTimer::ThreadLocalData* UtilityTimer::getThreadLocalData() {
  if (!threadLocalStorageData) {
    // On first access this thread creates thread-local data using shared_ptr.
    threadLocalStorageData = std::make_shared<ThreadLocalData>();

    // Register thread data with the singleton (copies shared_ptr, increments ref count).
    auto& instance{getSingletonInstance()};
    std::lock_guard<std::mutex> lock(instance.m_threadRegistry.registryMutex);
    instance.m_threadRegistry.allThreadData.push_back(threadLocalStorageData);
  }
  return threadLocalStorageData.get();
}

void UtilityTimer::timerStart(const std::string& label) {
  // Get global order atomically.
  const size_t globalOrder{m_globalOrderCounter.fetch_add(1, std::memory_order_relaxed)};

  // Check if exceeded the maximum number of instances.
  if (globalOrder >= m_kMaxPerfInstances) {
    return;  // Don't create new instance.
  }

  // Get thread-local data without locking after first access.
  ThreadLocalData* localData{getThreadLocalData()};

  // Access thread data without locking.
  localData->performanceInstances[label].emplace_back(globalOrder);
}

void UtilityTimer::timerStop(const std::string& label) {
  // Get thread-local data without locking after first access.
  ThreadLocalData* localData{getThreadLocalData()};

  // Access thread data without locking.
  auto iterator{localData->performanceInstances.find(label)};

  if (iterator == localData->performanceInstances.end()) {
    if (m_warningOutStream) {
      *m_warningOutStream << "Warning - Trying to stop timing label that has not been started: '" << label
                          << "' on thread " << std::hash<std::thread::id>{}(localData->threadId) << std::endl;
    }
    return;
  }

  // Stop most recently added PerfInstance without locking.
  iterator->second.back().stop();
}

void UtilityTimer::print(TimerOutputFormat format, std::ostream& outputStream) {
  auto& instance{getSingletonInstance()};

  // Determine limit based on stream type and format
  size_t maxIndividualResults{0};  // Default: unlimited

  // Use RTTI to detect stream buffer type for robust stream classification
  auto* buf{outputStream.rdbuf()};
  bool isFileStream{false};

  // Check if it's a file stream (unlimited output)
  if (dynamic_cast<std::filebuf*>(buf)) {
    isFileStream = true;
  }

  // Apply limits for non-file streams (console, string streams, etc.)
  if (!isFileStream) {
    if (format == TimerOutputFormat::CHROME_TRACE_JSON) {
      maxIndividualResults = 10000;
    } else {
      maxIndividualResults = 100;
    }
  }
  // File streams get unlimited output (maxIndividualResults = 0)

  // Collect all stopped instances sorted by global order.
  std::map<size_t, std::tuple<std::string, std::chrono::microseconds, std::thread::id>> results;
  std::chrono::high_resolution_clock::time_point firstStartTime;
  bool firstTimeSet{false};
  bool hasData{false};

  // Check if we exceeded max number of PerfInstances to prevent memory leak.
  if (instance.m_globalOrderCounter.load(std::memory_order_seq_cst) >= m_kMaxPerfInstances) {
    if (instance.m_warningOutStream) {
      *instance.m_warningOutStream << "Warning - Maximum timing instances (" << m_kMaxPerfInstances
                                   << ") reached. No new timings were recorded. "
                                   << "Consider calling reset() or increasing the limit." << std::endl;
    }
  }

  // Lock registry to iterate through all threads.
  {
    std::lock_guard<std::mutex> lock(instance.m_threadRegistry.registryMutex);

    for (const auto& threadDataPtr : instance.m_threadRegistry.allThreadData) {
      for (const auto& labelPair : threadDataPtr->performanceInstances) {
        const auto& label{labelPair.first};
        const auto& instances{labelPair.second};

        for (const auto& perfInst : instances) {
          hasData = true;

          if (!perfInst.timingActive) {
            const auto duration{
                std::chrono::duration_cast<std::chrono::microseconds>(perfInst.stopTime - perfInst.startTime)};
            results[perfInst.order] = std::make_tuple(label, duration, threadDataPtr->threadId);

            // Track the earliest start time for relative timestamps.
            if (!firstTimeSet || perfInst.startTime < firstStartTime) {
              firstStartTime = perfInst.startTime;
              firstTimeSet = true;
            }
          } else {
            if (instance.m_warningOutStream) {
              *instance.m_warningOutStream << "Warning - ['" << label << "'] on thread "
                                           << std::hash<std::thread::id>{}(threadDataPtr->threadId)
                                           << " was not stopped before calculating time and print." << std::endl;
            }
          }
        }
      }
    }
  }

  if (!hasData) {
    if (instance.m_warningOutStream) {
      *instance.m_warningOutStream << "Warning - utiltimer::print called but no timing data to print." << std::endl;
    }
    return;
  }

  switch (format) {
    case TimerOutputFormat::CHROME_TRACE_JSON: {
      instance.printChromeTraceJson(outputStream, results, firstStartTime, maxIndividualResults);
      break;
    }
    case TimerOutputFormat::HUMAN_READABLE_TEXT: {
      instance.printHumanReadableText(outputStream, results, maxIndividualResults);
      break;
    }
  }
}

/*
Chrome trace json format example:
"traceEvents": [
    {
      "name": "main postprocess_process_frames 0",
      "cat": "test_pipeline",
      "ph": "X",
      "ts": 0,
      "dur": 90,
      "pid": 1504,
      "tid": 1,
      "args": {
        "order": 1
      }
    },
    {
      ...
    }
  ],
  "displayTimeUnit": "ms"
}
*/
void UtilityTimer::printChromeTraceJson(
    std::ostream& outputStream,
    const std::map<size_t, std::tuple<std::string, std::chrono::microseconds, std::thread::id>>& results,
    const std::chrono::high_resolution_clock::time_point& firstStartTime,
    size_t maxIndividualResults) const {
  // Output Chrome trace JSON format
  const size_t totalResults{results.size()};
  const bool isTruncated{maxIndividualResults > 0 && totalResults > maxIndividualResults};

  outputStream << "{\n";

  // Add truncation comment if applicable
  if (isTruncated) {
    outputStream << "  \"_comment\": \"Showing first " << maxIndividualResults << " of " << totalResults
                 << " total events\",\n";
  }

  outputStream << "  \"traceEvents\": [\n";

  bool firstEvent{true};
  size_t eventCount{0};

  // Lock registry to access thread data for finding start times.
  std::lock_guard<std::mutex> lock(m_threadRegistry.registryMutex);

  for (const auto& resultPair : results) {
    // Check if we've reached the limit
    if (maxIndividualResults > 0 && eventCount >= maxIndividualResults) {
      break;
    }
    eventCount++;

    const size_t order{resultPair.first};
    const auto& [label, duration, threadId]{resultPair.second};

    // Find the specific PerfInstance with this order number across all threads.
    std::chrono::high_resolution_clock::time_point startTime;
    bool found{false};

    for (const auto& threadDataPtr : m_threadRegistry.allThreadData) {
      if (threadDataPtr->threadId == threadId) {
        auto labelIt{threadDataPtr->performanceInstances.find(label)};
        if (labelIt != threadDataPtr->performanceInstances.end()) {
          for (const auto& perfInst : labelIt->second) {
            if (perfInst.order == order) {
              startTime = perfInst.startTime;
              found = true;
              break;
            }
          }
        }
        if (found) {
          break;
        }
      }
    }

    if (found) {
      // Calculate relative timestamp in microseconds.
      const auto relativeTimestamp{
          std::chrono::duration_cast<std::chrono::microseconds>(startTime - firstStartTime).count()};

      // Convert thread ID to numeric value for Chrome trace.
      const size_t tid{std::hash<std::thread::id>{}(threadId)};

      if (!firstEvent) {
        outputStream << ",\n";
      }
      firstEvent = false;

      outputStream << "    {\n";
      outputStream << "      \"name\": \"" << label << "\",\n";
      outputStream << "      \"cat\": \"performance\",\n";
      outputStream << "      \"ph\": \"X\",\n";
      outputStream << "      \"ts\": " << relativeTimestamp << ",\n";
      outputStream << "      \"dur\": " << duration.count() << ",\n";
      outputStream << "      \"pid\": " << getpid() << ",\n";
      outputStream << "      \"tid\": " << tid << ",\n";
      outputStream << "      \"args\": {\n";
      outputStream << "        \"order\": " << order << "\n";
      outputStream << "      }\n";
      outputStream << "    }";
    }
  }

  outputStream << "\n  ],\n";
  outputStream << "  \"displayTimeUnit\": \"ms\"\n";
  outputStream << "}\n";
}

/*
Human readable format example:
[UtilityTimer][Thread:12345][main() postprocess_process_frames() 0]: 99 us
[UtilityTimer][Thread:12345][postprocess_process_frames process()]: 73 us
[UtilityTimer][Thread:12346][main() transform_infer_result() 0]: 2 us
[UtilityTimer][Thread:12346][main() draw_infer_result() 0]: 170 us
[UtilityTimer][main() postprocess_process_frames() 0]: n-measurements: 2, avg: 86 us
[UtilityTimer][main() transform_infer_result() 0]: n-measurements: 1, avg: 2 us
[UtilityTimer][main() draw_infer_result() 0]: n-measurements: 1, avg: 170 us
[UtilityTimer] Total average sum: 258 us
*/
void UtilityTimer::printHumanReadableText(
    std::ostream& outputStream,
    const std::map<size_t, std::tuple<std::string, std::chrono::microseconds, std::thread::id>>& results,
    size_t maxIndividualResults) const {
  // Group measurements by label for statistics calculation.
  std::map<std::string, std::vector<uint64_t>> labelDurations;

  const size_t totalResults{results.size()};
  const bool isTruncated{maxIndividualResults > 0 && totalResults > maxIndividualResults};

  // First pass: output individual measurements and collect durations by label.
  size_t measurementCount{0};
  for (const auto& resultPair : results) {
    const auto& [label, duration, threadId]{resultPair.second};

    // Always collect duration for statistics (even if not printing individual measurement)
    labelDurations[label].push_back(duration.count());

    // Check if we should print this individual measurement
    if (maxIndividualResults == 0 || measurementCount < maxIndividualResults) {
      // Convert thread ID to numeric value for display.
      const size_t tid{std::hash<std::thread::id>{}(threadId)};

      outputStream << "[UtilityTimer][Thread:" << tid << "][" << label << "]: " << duration.count() << " us"
                   << std::endl;
      measurementCount++;
    }
  }

  // Print truncation message if applicable
  if (isTruncated) {
    outputStream << "[UtilityTimer] ... (showing first " << maxIndividualResults << " of " << totalResults
                 << " total measurements) ..." << std::endl;
  }

  // Second pass: calculate and output statistics for each label.
  uint64_t totalAverageSum{0};
  for (const auto& [label, durations] : labelDurations) {
    const size_t n{durations.size()};

    // Calculate average.
    uint64_t sum{0};
    for (const auto& dur : durations) {
      sum += dur;
    }

    uint64_t avg{0};
    if (n > 0) {
      avg = sum / static_cast<uint64_t>(n);
    }

    // Output statistics.
    outputStream << "[UtilityTimer][" << label << "]: n-measurements: " << n << ", avg: " << avg << " us" << std::endl;

    // Accumulate for total average sum.
    totalAverageSum += avg;
  }

  // Output total average sum.
  outputStream << "[UtilityTimer] Total average sum: " << totalAverageSum << " us" << std::endl;
}

void UtilityTimer::reset() {
  auto& instance{getSingletonInstance()};
  if (instance.m_enablePerfTiming) {
    // Lock registry to clear all thread data.
    std::lock_guard<std::mutex> lock(instance.m_threadRegistry.registryMutex);

    for (const auto& threadDataPtr : instance.m_threadRegistry.allThreadData) {
      threadDataPtr->performanceInstances.clear();
    }

    // Reset global order counter with release semantics to ensure all previous writes
    // (clearing performanceInstances) are visible to other threads.
    // IMPORTANT: reset() should only be called when no timing operations are active
    // or all timing threads have finished to avoid race conditions.
    instance.m_globalOrderCounter.store(0, std::memory_order_release);
  }
}

UtilityTimer::ScopedTimer UtilityTimer::startScoped(const std::string& label) {
  return ScopedTimer(label);
}

void UtilityTimer::start(const std::string& label) {
  if (getSingletonInstance().m_enablePerfTiming == true) {
    getSingletonInstance().timerStart(label);
  }
}

void UtilityTimer::stop(const std::string& label) {
  if (getSingletonInstance().m_enablePerfTiming == true) {
    getSingletonInstance().timerStop(label);
  }
}

}  // namespace perf
