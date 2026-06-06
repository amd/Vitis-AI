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

#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace perf {

class UtilityTimer {
 public:
  // -------------------------------------------------------------------
  // Output format enum
  // -------------------------------------------------------------------
  enum class TimerOutputFormat {
    HUMAN_READABLE_TEXT,  // Output as a human readable format (See function definition)
    CHROME_TRACE_JSON     // Output to Chrome trace json format (See function definition)
  };

  // -------------------------------------------------------------------
  // Public functions
  // -------------------------------------------------------------------
  // Enable performance timing with required warning output stream.
  static void enable(std::ostream& warningOutStream);

  // Disable performance timing.
  static void disable();

  // Print times with required format and output stream.
  // Automatically limits output based on stream type:
  // - File streams: unlimited
  // - All other streams (console, string streams, etc.): 100 for text, 10000 for Chrome trace JSON
  static void print(TimerOutputFormat format, std::ostream& outputStream);

  // Clear unordered_map and zero the timing instance counter.
  static void reset();

  // -------------------------------------------------------------------
  // Nested ScopedTimer class for RAII-based timing
  // -------------------------------------------------------------------
  class ScopedTimer {
   public:
    // Checks if timing is enabled and starts timing if so.
    // Automatically captures the current thread ID.
    ScopedTimer(const std::string& label);

    // Stops timing if not already stopped and timing was enabled.
    ~ScopedTimer();

    // Manual stop method.
    void stop();

    // Delete copy operations.
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

    // Delete move operations.
    ScopedTimer(ScopedTimer&&) = delete;
    ScopedTimer& operator=(ScopedTimer&&) = delete;

   private:
    const std::string m_label{};         // Start label saved to stop with later.
    const std::thread::id m_threadId{};  // Thread ID captured at construction.
    bool m_stopped{false};               // Prevent double stop.
    bool m_timingEnabled{false};         // Saved timing enabled.
  };

  // Static factory function for creating scoped timers.
  static ScopedTimer startScoped(const std::string& label);

  // -------------------------------------------------------------------
  // Public wrappers around timing functionalities
  // -------------------------------------------------------------------
  // Create a new perf time instance using the label as a key. Starts timing.
  static void start(const std::string& label);

  // Stop the perf time of a specific label.
  static void stop(const std::string& label);

 private:
  // -------------------------------------------------------------------
  // Private member variables
  // -------------------------------------------------------------------
  bool m_enablePerfTiming{false};      // UtilityTimer enabled or disabled.
  std::ostream* m_warningOutStream{};  // User set ostream for warnings. No set default avoids unexpected prints.
  std::atomic<size_t> m_globalOrderCounter{
      0};  // Global atomic order counter for all timing instances across all threads.
  static constexpr size_t m_kMaxPerfInstances =
      1000000;  // Maximum number of performance instances to prevent unbounded memory growth.

  // Forward declarations.
  struct PerfInstance;
  struct ThreadLocalData;

  // Thread registry for managing all thread-local data instances.
  struct ThreadRegistry {
    mutable std::mutex registryMutex;                             // Mutable to allow locking in const functions
    std::vector<std::shared_ptr<ThreadLocalData>> allThreadData;  // Shared pointers to all thread-local data
  };

  ThreadRegistry m_threadRegistry;  // Registry of all thread-local instances.

  // Thread local storage where each thread gets its own instance.
  static thread_local std::shared_ptr<ThreadLocalData> threadLocalStorageData;

  // -------------------------------------------------------------------
  // Deleted (for singleton)
  // -------------------------------------------------------------------
  // Copy constructor and assignment operator.
  UtilityTimer(const UtilityTimer&) = delete;
  UtilityTimer& operator=(const UtilityTimer&) = delete;

  // Move constructor and move assignment operator.
  UtilityTimer(UtilityTimer&&) = delete;
  UtilityTimer& operator=(UtilityTimer&&) = delete;

  // -------------------------------------------------------------------
  // Private functions
  // -------------------------------------------------------------------
  // Default constructor for singleton instance.
  UtilityTimer();

  // Default destructor.
  ~UtilityTimer();

  // Get the singleton instance.
  static UtilityTimer& getSingletonInstance();

  // Get or create thread-local data for the current thread (lock-free after first access).
  static ThreadLocalData* getThreadLocalData();

  // Called by static start. Automatically captures current thread ID.
  void timerStart(const std::string& label);

  // Called by static stop. Automatically captures current thread ID.
  void timerStop(const std::string& label);

  // Print timing results in Chrome trace JSON format.
  void printChromeTraceJson(
      std::ostream& outputStream,
      const std::map<size_t, std::tuple<std::string, std::chrono::microseconds, std::thread::id>>& results,
      const std::chrono::high_resolution_clock::time_point& firstStartTime,
      size_t maxIndividualResults) const;

  // Print timing results in human-readable text format.
  void printHumanReadableText(
      std::ostream& outputStream,
      const std::map<size_t, std::tuple<std::string, std::chrono::microseconds, std::thread::id>>& results,
      size_t maxIndividualResults) const;
};

}  // namespace perf
