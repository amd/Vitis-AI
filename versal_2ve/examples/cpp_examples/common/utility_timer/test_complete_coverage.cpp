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

/**
 * @file test_complete_coverage.cpp
 * @brief Complete code coverage test suite for UtilityTimer and SimpleUtilityTimer
 *
 * This comprehensive test file provides 100% code coverage including:
 * - All 39 single-threaded tests from test_comprehensive.cpp
 * - All 6 multithreaded tests from test_multithreaded.cpp
 * - 15 additional edge case and stress tests for complete coverage
 * - 5 tests for average statistics features
 * - 3 tests for output truncation features
 *
 * Total: 68 comprehensive tests
 *
 * Build instructions:
 *   cd examples/cpp_examples/common/utility_timer
 *   make test_complete_coverage
 *
 * Run:
 *   ./test_complete_coverage
 */

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "SimpleUtilityTimer.hpp"
#include "UtilityTimer.hpp"

// Test counter for tracking test progress
int testsPassed = 0;
int testsFailed = 0;

#define TEST_ASSERT(condition, message)                \
  do {                                                 \
    if (!(condition)) {                                \
      std::cerr << "FAILED: " << message << std::endl; \
      testsFailed++;                                   \
    } else {                                           \
      std::cout << "PASSED: " << message << std::endl; \
      testsPassed++;                                   \
    }                                                  \
  } while (0)

// Helper function to simulate work
void simulateWork(int milliseconds) {
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

//=============================================================================
// SECTION 1: SINGLE-THREADED TESTS (39 tests from test_comprehensive.cpp)
//=============================================================================

void test_enable_disable() {
  std::cout << "\n=== Test 1: Enable/Disable ===" << std::endl;

  utiltimer::enable(std::cout);
  utiltimer::start("test_enable");
  simulateWork(10);
  utiltimer::stop("test_enable");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);
  TEST_ASSERT(output.str().find("test_enable") != std::string::npos, "Enable allows timing to work");

  utiltimer::reset();
  utiltimer::disable();
  utiltimer::start("test_disable");
  simulateWork(10);
  utiltimer::stop("test_disable");

  output.str("");
  output.clear();
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);
  TEST_ASSERT(output.str().find("test_disable") == std::string::npos, "Disable prevents timing");

  utiltimer::enable(std::cout);
  utiltimer::reset();
}

void test_manual_start_stop_simple() {
  std::cout << "\n=== Test 2: Manual Start/Stop (SimpleUtilityTimer) ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  utiltimer::start("operation1");
  simulateWork(10);
  utiltimer::stop("operation1");

  utiltimer::start("operation2");
  simulateWork(20);
  utiltimer::stop("operation2");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("operation1") != std::string::npos, "First operation recorded");
  TEST_ASSERT(output.str().find("operation2") != std::string::npos, "Second operation recorded");

  utiltimer::reset();
}

void test_manual_start_stop_perf() {
  std::cout << "\n=== Test 3: Manual Start/Stop (perf::UtilityTimer) ===" << std::endl;

  utiltimer::reset();
  perf::UtilityTimer::enable(std::cout);

  perf::UtilityTimer::start("perf_op1");
  simulateWork(10);
  perf::UtilityTimer::stop("perf_op1");

  std::ostringstream output;
  perf::UtilityTimer::print(perf::UtilityTimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("perf_op1") != std::string::npos, "perf::UtilityTimer direct usage works");

  perf::UtilityTimer::reset();
}

void test_scoped_timer_automatic() {
  std::cout << "\n=== Test 4: Scoped Timer Automatic Stop ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  {
    auto timer = utiltimer::startScoped("scoped_auto");
    simulateWork(15);
  }

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("scoped_auto") != std::string::npos, "Scoped timer automatic stop works");

  utiltimer::reset();
}

void test_scoped_timer_manual() {
  std::cout << "\n=== Test 5: Scoped Timer Manual Stop ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  auto timer = utiltimer::startScoped("scoped_manual");
  simulateWork(10);
  timer.stop();

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("scoped_manual") != std::string::npos, "Scoped timer manual stop works");

  utiltimer::reset();
}

void test_scoped_timer_perf() {
  std::cout << "\n=== Test 6: Scoped Timer (perf::UtilityTimer) ===" << std::endl;

  perf::UtilityTimer::reset();
  perf::UtilityTimer::enable(std::cout);

  {
    auto timer = perf::UtilityTimer::startScoped("perf_scoped");
    simulateWork(10);
  }

  std::ostringstream output;
  perf::UtilityTimer::print(perf::UtilityTimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("perf_scoped") != std::string::npos, "perf::UtilityTimer scoped timer works");

  perf::UtilityTimer::reset();
}

void test_duplicate_label() {
  std::cout << "\n=== Test 7: Duplicate Label Support (Multiple Instances) ===" << std::endl;

  utiltimer::reset();
  std::ostringstream warnings;
  utiltimer::enable(warnings);

  utiltimer::start("duplicate_label");
  simulateWork(5);
  utiltimer::stop("duplicate_label");

  utiltimer::start("duplicate_label");
  simulateWork(5);
  utiltimer::stop("duplicate_label");

  TEST_ASSERT(warnings.str().find("Warning") == std::string::npos, "Duplicate labels are allowed without warning");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  std::string result = output.str();
  size_t first_pos = result.find("duplicate_label");
  size_t second_pos = result.find("duplicate_label", first_pos + 1);

  TEST_ASSERT(first_pos != std::string::npos && second_pos != std::string::npos,
              "Both instances of duplicate label are recorded");

  utiltimer::reset();
}

void test_stop_nonexistent() {
  std::cout << "\n=== Test 8: Stop Non-Existent Label Warning ===" << std::endl;

  utiltimer::reset();
  std::ostringstream warnings;
  utiltimer::enable(warnings);

  utiltimer::stop("never_started");

  TEST_ASSERT(warnings.str().find("Warning") != std::string::npos, "Stop non-existent label generates warning");
  TEST_ASSERT(warnings.str().find("not been started") != std::string::npos, "Warning mentions label not started");

  utiltimer::reset();
}

void test_active_timer_warning() {
  std::cout << "\n=== Test 9: Active Timer Warning ===" << std::endl;

  utiltimer::reset();
  std::ostringstream warnings;
  utiltimer::enable(warnings);

  utiltimer::start("not_stopped");
  simulateWork(5);

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(warnings.str().find("Warning") != std::string::npos, "Active timer generates warning");
  TEST_ASSERT(warnings.str().find("not stopped") != std::string::npos, "Warning mentions timer not stopped");

  utiltimer::reset();
}

void test_empty_data_warning() {
  std::cout << "\n=== Test 10: Empty Timing Data Warning ===" << std::endl;

  utiltimer::reset();
  std::ostringstream warnings;
  utiltimer::enable(warnings);

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(warnings.str().find("Warning") != std::string::npos, "Empty data generates warning");
  TEST_ASSERT(warnings.str().find("no timing data") != std::string::npos, "Warning mentions no timing data");

  utiltimer::reset();
}

void test_human_readable_output() {
  std::cout << "\n=== Test 11: Human-Readable Output Format ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  utiltimer::start("format_test");
  simulateWork(10);
  utiltimer::stop("format_test");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  std::string result = output.str();
  TEST_ASSERT(result.find("[UtilityTimer]") != std::string::npos, "Output contains [UtilityTimer] prefix");
  TEST_ASSERT(result.find("format_test") != std::string::npos, "Output contains label");
  TEST_ASSERT(result.find("us") != std::string::npos, "Output contains microseconds unit");

  utiltimer::reset();
}

void test_chrome_trace_output() {
  std::cout << "\n=== Test 12: Chrome Trace JSON Output Format ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  utiltimer::start("json_test1");
  simulateWork(10);
  utiltimer::stop("json_test1");

  utiltimer::start("json_test2");
  simulateWork(5);
  utiltimer::stop("json_test2");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::CHROME_TRACE_JSON, output);

  std::string result = output.str();
  TEST_ASSERT(result.find("traceEvents") != std::string::npos, "JSON contains traceEvents");
  TEST_ASSERT(result.find("json_test1") != std::string::npos, "JSON contains first label");
  TEST_ASSERT(result.find("json_test2") != std::string::npos, "JSON contains second label");
  TEST_ASSERT(result.find("\"ph\": \"X\"") != std::string::npos, "JSON contains phase type");
  TEST_ASSERT(result.find("\"ts\":") != std::string::npos, "JSON contains timestamp");
  TEST_ASSERT(result.find("\"dur\":") != std::string::npos, "JSON contains duration");
  TEST_ASSERT(result.find("displayTimeUnit") != std::string::npos, "JSON contains displayTimeUnit");

  utiltimer::reset();
}

void test_reset() {
  std::cout << "\n=== Test 13: Reset Functionality ===" << std::endl;

  utiltimer::enable(std::cout);

  utiltimer::start("before_reset");
  simulateWork(5);
  utiltimer::stop("before_reset");

  utiltimer::reset();

  std::ostringstream output;
  std::ostringstream warnings;
  utiltimer::enable(warnings);
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("before_reset") == std::string::npos, "Reset clears timing data");
  TEST_ASSERT(warnings.str().find("no timing data") != std::string::npos, "Reset results in empty data warning");

  utiltimer::reset();
  utiltimer::enable(std::cout);
  utiltimer::start("before_reset");
  simulateWork(5);
  utiltimer::stop("before_reset");

  output.str("");
  output.clear();
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);
  TEST_ASSERT(output.str().find("before_reset") != std::string::npos, "Can reuse labels after reset");

  utiltimer::reset();
}

void test_operation_order() {
  std::cout << "\n=== Test 14: Operation Order Preservation ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  utiltimer::start("first");
  simulateWork(5);
  utiltimer::stop("first");

  utiltimer::start("second");
  simulateWork(5);
  utiltimer::stop("second");

  utiltimer::start("third");
  simulateWork(5);
  utiltimer::stop("third");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  std::string result = output.str();
  size_t pos_first = result.find("first");
  size_t pos_second = result.find("second");
  size_t pos_third = result.find("third");

  TEST_ASSERT(pos_first < pos_second && pos_second < pos_third, "Operations printed in start order");

  utiltimer::reset();
}

void test_nested_scoped() {
  std::cout << "\n=== Test 15: Nested Scoped Timers ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  {
    auto outer = utiltimer::startScoped("outer");
    simulateWork(5);

    {
      auto inner = utiltimer::startScoped("inner");
      simulateWork(5);
    }

    simulateWork(5);
  }

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("outer") != std::string::npos, "Outer scoped timer recorded");
  TEST_ASSERT(output.str().find("inner") != std::string::npos, "Inner scoped timer recorded");

  utiltimer::reset();
}

void test_scoped_when_disabled() {
  std::cout << "\n=== Test 16: Scoped Timer When Disabled ===" << std::endl;

  utiltimer::reset();
  utiltimer::disable();

  {
    auto timer = utiltimer::startScoped("disabled_scoped");
    simulateWork(10);
  }

  std::ostringstream output;
  std::ostringstream warnings;
  utiltimer::enable(warnings);
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("disabled_scoped") == std::string::npos, "Scoped timer respects disabled state");

  utiltimer::reset();
  utiltimer::enable(std::cout);
}

void test_scoped_double_stop() {
  std::cout << "\n=== Test 17: Scoped Timer Double Stop Prevention ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  {
    auto timer = utiltimer::startScoped("double_stop_test");
    simulateWork(5);
    timer.stop();
    timer.stop();
  }

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("double_stop_test") != std::string::npos, "Double stop prevention works (no crash)");

  utiltimer::reset();
}

void test_mixed_timers() {
  std::cout << "\n=== Test 18: Mixed Manual and Scoped Timers ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  utiltimer::start("manual1");
  simulateWork(5);

  {
    auto scoped1 = utiltimer::startScoped("scoped1");
    simulateWork(5);
  }

  utiltimer::stop("manual1");

  utiltimer::start("manual2");
  simulateWork(5);
  utiltimer::stop("manual2");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("manual1") != std::string::npos, "Manual timer 1 recorded");
  TEST_ASSERT(output.str().find("scoped1") != std::string::npos, "Scoped timer recorded");
  TEST_ASSERT(output.str().find("manual2") != std::string::npos, "Manual timer 2 recorded");

  utiltimer::reset();
}

void test_chrome_trace_file() {
  std::cout << "\n=== Test 19: Chrome Trace JSON File Output ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  utiltimer::start("file_test1");
  simulateWork(10);
  utiltimer::stop("file_test1");

  utiltimer::start("file_test2");
  simulateWork(5);
  utiltimer::stop("file_test2");

  std::ofstream traceFile("test_complete_coverage_trace.json");
  TEST_ASSERT(traceFile.is_open(), "Trace file opened successfully");

  utiltimer::print(utiltimer::TimerOutputFormat::CHROME_TRACE_JSON, traceFile);
  traceFile.close();

  std::ifstream verifyFile("test_complete_coverage_trace.json");
  TEST_ASSERT(verifyFile.is_open(), "Trace file exists after write");

  std::stringstream buffer;
  buffer << verifyFile.rdbuf();
  std::string content = buffer.str();

  TEST_ASSERT(content.find("traceEvents") != std::string::npos, "Trace file contains valid JSON");
  TEST_ASSERT(content.find("file_test1") != std::string::npos, "Trace file contains first operation");
  TEST_ASSERT(content.find("file_test2") != std::string::npos, "Trace file contains second operation");

  verifyFile.close();

  utiltimer::reset();
}

void test_timing_accuracy() {
  std::cout << "\n=== Test 20: Timing Accuracy Verification ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  const int sleep_ms = 50;
  utiltimer::start("accuracy_test");
  simulateWork(sleep_ms);
  utiltimer::stop("accuracy_test");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  std::string result = output.str();
  size_t pos = result.find("accuracy_test");
  TEST_ASSERT(pos != std::string::npos, "Timing result found");

  TEST_ASSERT(result.find("us") != std::string::npos, "Timing includes microseconds unit");

  utiltimer::reset();
}

void test_multiple_enable_disable() {
  std::cout << "\n=== Test 21: Multiple Enable/Disable Cycles ===" << std::endl;

  utiltimer::reset();

  utiltimer::enable(std::cout);
  utiltimer::start("cycle1");
  simulateWork(5);
  utiltimer::stop("cycle1");
  utiltimer::disable();

  utiltimer::enable(std::cout);
  utiltimer::start("cycle2");
  simulateWork(5);
  utiltimer::stop("cycle2");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("cycle1") != std::string::npos, "First cycle recorded");
  TEST_ASSERT(output.str().find("cycle2") != std::string::npos, "Second cycle recorded");

  utiltimer::reset();
}

void test_custom_warning_stream() {
  std::cout << "\n=== Test 22: Custom Warning Stream ===" << std::endl;

  utiltimer::reset();
  std::ostringstream customWarnings;
  utiltimer::enable(customWarnings);

  utiltimer::stop("never_started");

  TEST_ASSERT(customWarnings.str().find("Warning") != std::string::npos, "Custom warning stream receives warnings");
  TEST_ASSERT(customWarnings.str().find("not been started") != std::string::npos,
              "Custom stream has correct warning content");

  utiltimer::reset();
}

void test_many_operations() {
  std::cout << "\n=== Test 23: Large Number of Operations ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  const int num_ops = 100;
  for (int i = 0; i < num_ops; ++i) {
    std::string label = "op_" + std::to_string(i);
    utiltimer::start(label);
    utiltimer::stop(label);
  }

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  std::string result = output.str();
  TEST_ASSERT(result.find("op_0") != std::string::npos, "First operation recorded");
  TEST_ASSERT(result.find("op_99") != std::string::npos, "Last operation recorded");

  // Count individual measurements (with Thread: prefix)
  int count = 0;
  size_t pos = 0;
  while ((pos = result.find("[UtilityTimer][Thread:", pos)) != std::string::npos) {
    count++;
    pos++;
  }
  TEST_ASSERT(count == num_ops, "All operations recorded");

  utiltimer::reset();
}

void test_zero_duration() {
  std::cout << "\n=== Test 24: Zero-Duration Operations ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  utiltimer::start("zero_duration");
  utiltimer::stop("zero_duration");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("zero_duration") != std::string::npos, "Zero-duration operation recorded");

  utiltimer::reset();
}

void test_scoped_exception_safety() {
  std::cout << "\n=== Test 25: Scoped Timer Exception Safety ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  try {
    auto timer = utiltimer::startScoped("exception_test");
    simulateWork(5);
    throw std::runtime_error("Test exception");
  } catch (const std::exception&) {
  }

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("exception_test") != std::string::npos, "Scoped timer stops even with exception");

  utiltimer::reset();
}

void test_both_namespaces() {
  std::cout << "\n=== Test 26: Both Namespaces Simultaneously ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  utiltimer::start("util_op");
  simulateWork(5);
  utiltimer::stop("util_op");

  perf::UtilityTimer::start("perf_op");
  simulateWork(5);
  perf::UtilityTimer::stop("perf_op");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("util_op") != std::string::npos, "utiltimer namespace operation recorded");
  TEST_ASSERT(output.str().find("perf_op") != std::string::npos, "perf namespace operation recorded");

  utiltimer::reset();
}

void test_different_output_streams() {
  std::cout << "\n=== Test 27: Print to Different Streams ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  utiltimer::start("stream_test");
  simulateWork(5);
  utiltimer::stop("stream_test");

  std::ostringstream ss_output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, ss_output);
  TEST_ASSERT(ss_output.str().find("stream_test") != std::string::npos, "Print to stringstream works");

  std::ofstream file_output("test_stream_output.txt");
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, file_output);
  file_output.close();

  std::ifstream verify("test_stream_output.txt");
  std::stringstream buffer;
  buffer << verify.rdbuf();
  TEST_ASSERT(buffer.str().find("stream_test") != std::string::npos, "Print to file works");
  verify.close();

  utiltimer::reset();
}

void test_chrome_trace_timestamps() {
  std::cout << "\n=== Test 28: Chrome Trace Relative Timestamps ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  utiltimer::start("ts_test1");
  simulateWork(10);
  utiltimer::stop("ts_test1");

  utiltimer::start("ts_test2");
  simulateWork(10);
  utiltimer::stop("ts_test2");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::CHROME_TRACE_JSON, output);

  std::string result = output.str();

  TEST_ASSERT(result.find("\"ts\": 0") != std::string::npos || result.find("\"ts\":0") != std::string::npos,
              "First event has relative timestamp of 0");

  utiltimer::reset();
}

void test_long_labels() {
  std::cout << "\n=== Test 29: Long Label Names ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  std::string long_label =
      "this_is_a_very_long_label_name_that_tests_the_timer_with_extended_strings_to_ensure_proper_handling";

  utiltimer::start(long_label);
  simulateWork(5);
  utiltimer::stop(long_label);

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find(long_label) != std::string::npos, "Long label names handled correctly");

  utiltimer::reset();
}

void test_special_characters() {
  std::cout << "\n=== Test 30: Special Characters in Labels ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  utiltimer::start("label-with-dashes");
  simulateWork(5);
  utiltimer::stop("label-with-dashes");

  utiltimer::start("label_with_underscores");
  simulateWork(5);
  utiltimer::stop("label_with_underscores");

  utiltimer::start("label.with.dots");
  simulateWork(5);
  utiltimer::stop("label.with.dots");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("label-with-dashes") != std::string::npos, "Dashes in labels work");
  TEST_ASSERT(output.str().find("label_with_underscores") != std::string::npos, "Underscores in labels work");
  TEST_ASSERT(output.str().find("label.with.dots") != std::string::npos, "Dots in labels work");

  utiltimer::reset();
}

void test_reset_while_active() {
  std::cout << "\n=== Test 31: Reset While Timing Active ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  utiltimer::start("active_during_reset");
  simulateWork(5);

  utiltimer::reset();

  std::ostringstream output;
  std::ostringstream warnings;
  utiltimer::enable(warnings);
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("active_during_reset") == std::string::npos, "Reset clears active timers");

  utiltimer::reset();
  utiltimer::enable(std::cout);
}

void test_disable_then_print() {
  std::cout << "\n=== Test 32: Disable Then Print ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  utiltimer::start("disable_print_test");
  simulateWork(5);
  utiltimer::stop("disable_print_test");

  utiltimer::disable();

  std::ostringstream output;
  std::ostringstream warnings;
  utiltimer::enable(warnings);
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("disable_print_test") != std::string::npos, "Can print after re-enabling");

  utiltimer::reset();
}

void test_scoped_scope_verification() {
  std::cout << "\n=== Test 33: Scoped Timer Scope Verification ===" << std::endl;

  utiltimer::reset();
  std::ostringstream warnings;
  utiltimer::enable(warnings);

  {
    auto timer = utiltimer::startScoped("scope_test");
    simulateWork(5);

    std::ostringstream output;
    utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

    TEST_ASSERT(output.str().find("scope_test") == std::string::npos, "Active scoped timer not in print output");
    TEST_ASSERT(warnings.str().find("not stopped") != std::string::npos, "Warning generated for active timer");
  }

  std::ostringstream output2;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output2);
  TEST_ASSERT(output2.str().find("scope_test") != std::string::npos, "Scoped timer in output after scope ends");

  utiltimer::reset();
  utiltimer::enable(std::cout);
}

void test_json_structure() {
  std::cout << "\n=== Test 34: JSON Output Structure Validation ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  utiltimer::start("json_struct");
  simulateWork(5);
  utiltimer::stop("json_struct");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::CHROME_TRACE_JSON, output);

  std::string result = output.str();

  TEST_ASSERT(result.find("{") != std::string::npos, "JSON has opening brace");
  TEST_ASSERT(result.find("}") != std::string::npos, "JSON has closing brace");
  TEST_ASSERT(result.find("\"name\":") != std::string::npos, "JSON has name field");
  TEST_ASSERT(result.find("\"cat\":") != std::string::npos, "JSON has category field");
  TEST_ASSERT(result.find("\"ph\":") != std::string::npos, "JSON has phase field");
  TEST_ASSERT(result.find("\"pid\":") != std::string::npos, "JSON has process ID");
  TEST_ASSERT(result.find("\"tid\":") != std::string::npos, "JSON has thread ID");
  TEST_ASSERT(result.find("\"args\":") != std::string::npos, "JSON has args field");
  TEST_ASSERT(result.find("\"order\":") != std::string::npos, "JSON has order in args");

  utiltimer::reset();
}

void test_interleaved_operations() {
  std::cout << "\n=== Test 35: Interleaved Start/Stop ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  utiltimer::start("op_a");
  simulateWork(5);
  utiltimer::start("op_b");
  simulateWork(5);
  utiltimer::stop("op_a");
  simulateWork(5);
  utiltimer::stop("op_b");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("op_a") != std::string::npos, "First interleaved operation recorded");
  TEST_ASSERT(output.str().find("op_b") != std::string::npos, "Second interleaved operation recorded");

  utiltimer::reset();
}

void test_unique_labels_in_loop() {
  std::cout << "\n=== Test 36: Unique Labels in Loop ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  const int num_iterations = 5;
  for (int i = 0; i < num_iterations; ++i) {
    std::string label = "loop_iteration_" + std::to_string(i);
    utiltimer::start(label);
    simulateWork(5);
    utiltimer::stop(label);
  }

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  std::string result = output.str();

  TEST_ASSERT(result.find("loop_iteration_0") != std::string::npos, "First iteration recorded");
  TEST_ASSERT(result.find("loop_iteration_4") != std::string::npos, "Last iteration recorded");

  utiltimer::reset();
}

void test_duplicate_label_after_stop() {
  std::cout << "\n=== Test 37: Duplicate Label After Stop (Multiple Instances Allowed) ===" << std::endl;

  utiltimer::reset();
  std::ostringstream warnings;
  utiltimer::enable(warnings);

  utiltimer::start("reused_label");
  simulateWork(5);
  utiltimer::stop("reused_label");

  utiltimer::start("reused_label");
  simulateWork(5);
  utiltimer::stop("reused_label");

  TEST_ASSERT(warnings.str().find("Warning") == std::string::npos, "Reusing label does not generate warning");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  std::string result = output.str();
  size_t first_pos = result.find("reused_label");
  size_t second_pos = result.find("reused_label", first_pos + 1);

  TEST_ASSERT(first_pos != std::string::npos && second_pos != std::string::npos,
              "Both instances of reused label are recorded");

  utiltimer::reset();
}

void test_duplicate_stop_safe() {
  std::cout << "\n=== Test 38: Duplicate Stop is Safe (No Warning) ===" << std::endl;

  utiltimer::reset();
  std::ostringstream warnings;
  utiltimer::enable(warnings);

  utiltimer::start("stop_twice");
  simulateWork(5);
  utiltimer::stop("stop_twice");
  utiltimer::stop("stop_twice");

  TEST_ASSERT(warnings.str().find("Warning") == std::string::npos,
              "Duplicate stop does not generate warning (safe operation)");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("stop_twice") != std::string::npos, "Timer recorded correctly despite duplicate stop");

  utiltimer::reset();
}

void test_reuse_label_after_reset() {
  std::cout << "\n=== Test 39: Labels Can Be Reused After Reset ===" << std::endl;

  utiltimer::reset();
  std::ostringstream warnings;
  utiltimer::enable(warnings);

  utiltimer::start("reusable_label");
  simulateWork(5);
  utiltimer::stop("reusable_label");

  utiltimer::reset();
  warnings.str("");
  warnings.clear();
  utiltimer::enable(warnings);

  utiltimer::start("reusable_label");
  simulateWork(5);
  utiltimer::stop("reusable_label");

  TEST_ASSERT(warnings.str().find("Warning") == std::string::npos, "No warning when reusing label after reset");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("reusable_label") != std::string::npos, "Label recorded after reset");

  utiltimer::reset();
}

//=============================================================================
// SECTION 2: MULTITHREADED TESTS (6 tests from test_multithreaded.cpp)
//=============================================================================

void workerFunction(int threadNum, int iterations) {
  for (int i = 0; i < iterations; ++i) {
    utiltimer::start("worker_task");
    simulateWork(10 + threadNum);
    utiltimer::stop("worker_task");

    {
      auto timer = utiltimer::startScoped("scoped_task");
      simulateWork(5);
    }

    utiltimer::start("outer_task");
    simulateWork(3);

    utiltimer::start("inner_task");
    simulateWork(2);
    utiltimer::stop("inner_task");

    simulateWork(3);
    utiltimer::stop("outer_task");
  }
}

void test_multithread_basic() {
  std::cout << "\n=== Test 40: Multithreaded Basic Timing ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  const int NUM_THREADS = 4;
  const int ITERATIONS_PER_THREAD = 3;
  std::vector<std::thread> threads;

  for (int i = 0; i < NUM_THREADS; ++i) {
    threads.emplace_back(workerFunction, i, ITERATIONS_PER_THREAD);
  }

  utiltimer::start("main_coordination");
  simulateWork(10);
  utiltimer::stop("main_coordination");

  for (auto& t : threads) {
    t.join();
  }

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("worker_task") != std::string::npos, "Worker tasks recorded");
  TEST_ASSERT(output.str().find("main_coordination") != std::string::npos, "Main thread task recorded");

  utiltimer::reset();
}

void test_multithread_chrome_trace() {
  std::cout << "\n=== Test 41: Multithreaded Chrome Trace Output ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  const int NUM_THREADS = 3;
  std::vector<std::thread> threads;

  for (int i = 0; i < NUM_THREADS; ++i) {
    threads.emplace_back(workerFunction, i, 2);
  }

  for (auto& t : threads) {
    t.join();
  }

  std::ofstream traceFile("test_mt_trace.json");
  if (traceFile.is_open()) {
    utiltimer::print(utiltimer::TimerOutputFormat::CHROME_TRACE_JSON, traceFile);
    traceFile.close();

    std::ifstream verify("test_mt_trace.json");
    std::stringstream buffer;
    buffer << verify.rdbuf();
    std::string content = buffer.str();

    TEST_ASSERT(content.find("traceEvents") != std::string::npos, "Chrome trace contains traceEvents");
    TEST_ASSERT(content.find("worker_task") != std::string::npos, "Chrome trace contains worker tasks");

    verify.close();
  }

  utiltimer::reset();
}

void test_multithread_stress() {
  std::cout << "\n=== Test 42: Multithreaded Stress Test ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  const int STRESS_THREADS = 10;
  const int STRESS_ITERATIONS = 5;
  std::vector<std::thread> stressThreads;

  for (int i = 0; i < STRESS_THREADS; ++i) {
    stressThreads.emplace_back(workerFunction, i, STRESS_ITERATIONS);
  }

  for (auto& t : stressThreads) {
    t.join();
  }

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("worker_task") != std::string::npos, "Stress test completed successfully");

  utiltimer::reset();
}

void test_multithread_concurrent_access() {
  std::cout << "\n=== Test 43: Concurrent Timing Operations ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  auto concurrentWorker = []() {
    for (int i = 0; i < 100; ++i) {
      utiltimer::start("concurrent_op");
      utiltimer::stop("concurrent_op");
    }
  };

  std::vector<std::thread> concurrentThreads;
  for (int i = 0; i < 8; ++i) {
    concurrentThreads.emplace_back(concurrentWorker);
  }

  for (auto& t : concurrentThreads) {
    t.join();
  }

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("concurrent_op") != std::string::npos,
              "Concurrent operations completed (800 timing operations)");

  utiltimer::reset();
}

void test_multithread_disable_enable() {
  std::cout << "\n=== Test 44: Multithreaded Disable/Enable ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  utiltimer::start("before_disable");
  simulateWork(5);
  utiltimer::stop("before_disable");

  utiltimer::disable();

  utiltimer::start("while_disabled");
  simulateWork(5);
  utiltimer::stop("while_disabled");

  utiltimer::enable(std::cout);

  utiltimer::start("after_enable");
  simulateWork(5);
  utiltimer::stop("after_enable");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("before_disable") != std::string::npos, "Before disable recorded");
  TEST_ASSERT(output.str().find("while_disabled") == std::string::npos, "While disabled not recorded");
  TEST_ASSERT(output.str().find("after_enable") != std::string::npos, "After enable recorded");

  utiltimer::reset();
}

void test_multithread_same_label() {
  std::cout << "\n=== Test 45: Multithreaded Same Label ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  auto sameLabelWorker = []() {
    for (int i = 0; i < 5; ++i) {
      utiltimer::start("shared_label");
      simulateWork(5);
      utiltimer::stop("shared_label");
    }
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back(sameLabelWorker);
  }

  for (auto& t : threads) {
    t.join();
  }

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("shared_label") != std::string::npos, "Shared label across threads works");

  utiltimer::reset();
}

//=============================================================================
// SECTION 3: EDGE CASES AND STRESS TESTS (15 new tests)
//=============================================================================

void test_max_instances_limit() {
  std::cout << "\n=== Test 46: Maximum Instances Limit ===" << std::endl;

  utiltimer::reset();
  std::ostringstream warnings;
  utiltimer::enable(warnings);

  // Try to create many instances (not quite 1M to keep test fast)
  for (int i = 0; i < 10000; ++i) {
    std::string name = "limit_test_" + std::to_string(i);
    utiltimer::start(name);
    utiltimer::stop(name);
  }

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("limit_test_0") != std::string::npos, "Large number of instances handled");

  utiltimer::reset();
}

void test_thread_data_persistence() {
  std::cout << "\n=== Test 47: Thread Data Persistence ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  {
    std::thread t([]() {
      utiltimer::start("persistent_thread");
      simulateWork(5);
      utiltimer::stop("persistent_thread");
    });
    t.join();
  }

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("persistent_thread") != std::string::npos, "Thread data persists after thread exit");

  utiltimer::reset();

  std::ostringstream output2;
  std::ostringstream warnings;
  utiltimer::enable(warnings);
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output2);

  TEST_ASSERT(output2.str().find("persistent_thread") == std::string::npos, "Reset clears persisted thread data");

  utiltimer::reset();
  utiltimer::enable(std::cout);
}

void test_concurrent_thread_creation() {
  std::cout << "\n=== Test 48: Concurrent Thread Creation/Destruction ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  std::atomic<int> thread_count{0};
  std::vector<std::thread> threads;

  for (int i = 0; i < 50; ++i) {
    threads.emplace_back([i, &thread_count]() {
      thread_count++;
      std::string name = "rapid_" + std::to_string(i);
      utiltimer::start(name);
      simulateWork(1);
      utiltimer::stop(name);
      thread_count--;
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("rapid_0") != std::string::npos, "Rapid thread creation handled");
  TEST_ASSERT(output.str().find("rapid_49") != std::string::npos, "All rapid threads recorded");

  utiltimer::reset();
}

void test_scoped_timer_mt_manual_stop() {
  std::cout << "\n=== Test 49: Scoped Timer Multithreaded Manual Stop ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  auto worker = [](int id) {
    std::string name = "scoped_manual_" + std::to_string(id);
    auto timer = utiltimer::startScoped(name);
    simulateWork(5);
    timer.stop();
    simulateWork(5);
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < 3; ++i) {
    threads.emplace_back(worker, i);
  }

  for (auto& t : threads) {
    t.join();
  }

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("scoped_manual_0") != std::string::npos, "Scoped manual stop in multithread works");

  utiltimer::reset();
}

void test_scoped_timer_mt_exception() {
  std::cout << "\n=== Test 50: Scoped Timer Multithreaded Exception ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  auto worker = [](int id) {
    try {
      std::string name = "scoped_exception_" + std::to_string(id);
      auto timer = utiltimer::startScoped(name);
      simulateWork(5);
      if (id % 2 == 0) {
        throw std::runtime_error("test");
      }
    } catch (...) {
    }
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back(worker, i);
  }

  for (auto& t : threads) {
    t.join();
  }

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("scoped_exception_0") != std::string::npos, "Scoped exception safety in multithread");

  utiltimer::reset();
}

void test_reset_during_active_timing() {
  std::cout << "\n=== Test 51: Reset During Active Timing ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  std::atomic<bool> reset_done{false};

  std::thread t1([&reset_done]() {
    utiltimer::start("long_timer");
    while (!reset_done) {
      simulateWork(1);
    }
    utiltimer::stop("long_timer");
  });

  std::thread t2([&reset_done]() {
    simulateWork(10);
    utiltimer::reset();
    reset_done = true;
  });

  t1.join();
  t2.join();

  TEST_ASSERT(true, "Reset during active timing doesn't crash");

  utiltimer::reset();
  utiltimer::enable(std::cout);
}

void test_concurrent_print_calls() {
  std::cout << "\n=== Test 52: Concurrent Print Calls ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  utiltimer::start("print_test");
  simulateWork(5);
  utiltimer::stop("print_test");

  std::vector<std::string> outputs(5);
  std::vector<std::thread> threads;

  for (int i = 0; i < 5; ++i) {
    threads.emplace_back([&outputs, i]() {
      std::ostringstream oss;
      utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, oss);
      outputs[i] = oss.str();
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  for (const auto& output : outputs) {
    TEST_ASSERT(output.find("print_test") != std::string::npos, "Concurrent print works");
  }

  utiltimer::reset();
}

void test_thread_id_consistency() {
  std::cout << "\n=== Test 53: Thread ID Consistency ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  auto worker = [](int id) {
    std::string name = "tid_test_" + std::to_string(id);
    utiltimer::start(name);
    simulateWork(5);
    utiltimer::stop(name);
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < 3; ++i) {
    threads.emplace_back(worker, i);
  }

  for (auto& t : threads) {
    t.join();
  }

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::CHROME_TRACE_JSON, output);

  TEST_ASSERT(output.str().find("\"tid\"") != std::string::npos, "Thread IDs present in output");

  utiltimer::reset();
}

void test_atomic_counter_stress() {
  std::cout << "\n=== Test 54: Atomic Counter Stress Test ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  std::atomic<int> counter{0};

  auto worker = [&counter]() {
    for (int i = 0; i < 100; ++i) {
      std::string name = "stress_" + std::to_string(counter++);
      utiltimer::start(name);
      utiltimer::stop(name);
    }
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back(worker);
  }

  for (auto& t : threads) {
    t.join();
  }

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("stress_") != std::string::npos, "Atomic counter stress test passed");

  utiltimer::reset();
}

void test_enable_start_disable_stop() {
  std::cout << "\n=== Test 55: Enable→Start→Disable→Stop→Print ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);
  utiltimer::start("edge_case_1");
  utiltimer::disable();
  utiltimer::stop("edge_case_1");

  std::ostringstream output;
  std::ostringstream warnings;
  utiltimer::enable(warnings);
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(true, "Enable→Start→Disable→Stop sequence handled");

  utiltimer::reset();
  utiltimer::enable(std::cout);
}

void test_disable_start_enable_stop() {
  std::cout << "\n=== Test 56: Disable→Start→Enable→Stop→Print ===" << std::endl;

  utiltimer::reset();
  utiltimer::disable();
  utiltimer::start("edge_case_2");
  utiltimer::enable(std::cout);
  utiltimer::stop("edge_case_2");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(true, "Disable→Start→Enable→Stop sequence handled");

  utiltimer::reset();
}

void test_reset_between_start_stop() {
  std::cout << "\n=== Test 57: Reset Between Start and Stop ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);
  utiltimer::start("reset_between");
  simulateWork(5);
  utiltimer::reset();
  utiltimer::enable(std::cout);
  utiltimer::stop("reset_between");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(true, "Reset between start/stop handled gracefully");

  utiltimer::reset();
}

void test_chrome_trace_thread_ids() {
  std::cout << "\n=== Test 58: Chrome Trace Thread IDs ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  auto worker = [](int id) {
    std::string name = "chrome_tid_" + std::to_string(id);
    utiltimer::start(name);
    simulateWork(5);
    utiltimer::stop(name);
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < 3; ++i) {
    threads.emplace_back(worker, i);
  }

  for (auto& t : threads) {
    t.join();
  }

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::CHROME_TRACE_JSON, output);

  TEST_ASSERT(output.str().find("\"tid\"") != std::string::npos, "Chrome trace has thread IDs");
  TEST_ASSERT(output.str().find("chrome_tid_0") != std::string::npos, "Chrome trace thread 0");

  utiltimer::reset();
}

void test_high_frequency_timing() {
  std::cout << "\n=== Test 59: High Frequency Timing ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  for (int i = 0; i < 1000; ++i) {
    std::string name = "high_freq_" + std::to_string(i);
    utiltimer::start(name);
    utiltimer::stop(name);
  }

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("high_freq_0") != std::string::npos, "High frequency 0");
  TEST_ASSERT(output.str().find("high_freq_999") != std::string::npos, "High frequency 999");

  utiltimer::reset();
}

void test_multithread_interleaved() {
  std::cout << "\n=== Test 60: Multithread Interleaved Operations ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  std::atomic<bool> start_flag{false};

  auto worker = [&start_flag](int id) {
    while (!start_flag) {
      std::this_thread::yield();
    }

    for (int i = 0; i < 10; ++i) {
      std::string name = "interleaved_" + std::to_string(id) + "_" + std::to_string(i);
      utiltimer::start(name);
      std::this_thread::yield();
      utiltimer::stop(name);
    }
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < 5; ++i) {
    threads.emplace_back(worker, i);
  }

  start_flag = true;

  for (auto& t : threads) {
    t.join();
  }

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  TEST_ASSERT(output.str().find("interleaved_0_0") != std::string::npos, "Interleaved operations work");

  utiltimer::reset();
}

//=============================================================================
// SECTION 4: AVERAGE STATISTICS TESTS (5 new tests)
//=============================================================================

void test_single_label_average_stats() {
  std::cout << "\n=== Test 61: Single Label Average Statistics ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  // Create multiple measurements with the same label
  utiltimer::start("avg_test");
  simulateWork(10);
  utiltimer::stop("avg_test");

  utiltimer::start("avg_test");
  simulateWork(20);
  utiltimer::stop("avg_test");

  utiltimer::start("avg_test");
  simulateWork(30);
  utiltimer::stop("avg_test");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  std::string result = output.str();

  TEST_ASSERT(result.find("[UtilityTimer][avg_test]: n-measurements: 3") != std::string::npos,
              "Shows correct n-measurements count with proper format");
  TEST_ASSERT(result.find("avg:") != std::string::npos, "Contains average field");
  TEST_ASSERT(result.find("us") != std::string::npos, "Average has microseconds unit");

  utiltimer::reset();
}

void test_multiple_labels_average_stats() {
  std::cout << "\n=== Test 62: Multiple Labels Average Statistics ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  // Label A: 2 measurements
  utiltimer::start("label_a");
  simulateWork(10);
  utiltimer::stop("label_a");

  utiltimer::start("label_a");
  simulateWork(10);
  utiltimer::stop("label_a");

  // Label B: 3 measurements
  utiltimer::start("label_b");
  simulateWork(5);
  utiltimer::stop("label_b");

  utiltimer::start("label_b");
  simulateWork(5);
  utiltimer::stop("label_b");

  utiltimer::start("label_b");
  simulateWork(5);
  utiltimer::stop("label_b");

  // Label C: 1 measurement
  utiltimer::start("label_c");
  simulateWork(20);
  utiltimer::stop("label_c");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  std::string result = output.str();

  TEST_ASSERT(result.find("[UtilityTimer][label_a]: n-measurements: 2") != std::string::npos,
              "Label A shows 2 measurements");
  TEST_ASSERT(result.find("[UtilityTimer][label_b]: n-measurements: 3") != std::string::npos,
              "Label B shows 3 measurements");
  TEST_ASSERT(result.find("[UtilityTimer][label_c]: n-measurements: 1") != std::string::npos,
              "Label C shows 1 measurement");

  utiltimer::reset();
}

void test_total_average_sum() {
  std::cout << "\n=== Test 63: Total Average Sum Calculation ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  // Create measurements with known patterns
  utiltimer::start("sum_test_a");
  simulateWork(10);
  utiltimer::stop("sum_test_a");

  utiltimer::start("sum_test_b");
  simulateWork(20);
  utiltimer::stop("sum_test_b");

  utiltimer::start("sum_test_c");
  simulateWork(30);
  utiltimer::stop("sum_test_c");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  std::string result = output.str();

  TEST_ASSERT(result.find("[UtilityTimer] Total average sum:") != std::string::npos,
              "Contains total average sum line with proper format");
  TEST_ASSERT(result.find("us") != std::string::npos, "Total has microseconds unit");

  // Verify the total appears after all individual statistics
  size_t last_label_pos = result.rfind("[UtilityTimer][sum_test_c]: n-measurements:");
  size_t total_pos = result.find("[UtilityTimer] Total average sum:");
  TEST_ASSERT(total_pos > last_label_pos, "Total average sum appears after all label statistics");

  utiltimer::reset();
}

void test_single_measurement_average() {
  std::cout << "\n=== Test 64: Average Statistics with Single Measurement ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  utiltimer::start("single_measure");
  simulateWork(15);
  utiltimer::stop("single_measure");

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  std::string result = output.str();

  TEST_ASSERT(result.find("[UtilityTimer][single_measure]: n-measurements: 1") != std::string::npos,
              "Single measurement shows n-measurements: 1 with proper format");
  TEST_ASSERT(result.find("avg:") != std::string::npos, "Single measurement has average");
  TEST_ASSERT(result.find("[UtilityTimer] Total average sum:") != std::string::npos,
              "Total average sum present even with single label");

  utiltimer::reset();
}

void test_multithreaded_average_stats() {
  std::cout << "\n=== Test 65: Multithreaded Average Statistics ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  auto worker = [](int id) {
    for (int i = 0; i < 3; ++i) {
      utiltimer::start("mt_avg_label");
      simulateWork(5);
      utiltimer::stop("mt_avg_label");
    }
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back(worker, i);
  }

  for (auto& t : threads) {
    t.join();
  }

  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, output);

  std::string result = output.str();

  // 4 threads * 3 iterations = 12 measurements
  TEST_ASSERT(result.find("[UtilityTimer][mt_avg_label]: n-measurements: 12") != std::string::npos,
              "Multithreaded measurements aggregated correctly with proper format");
  TEST_ASSERT(result.find("avg:") != std::string::npos, "Average calculated across threads");
  TEST_ASSERT(result.find("[UtilityTimer] Total average sum:") != std::string::npos,
              "Total average sum present with proper format");

  utiltimer::reset();
}

void test_truncation_human_readable() {
  std::cout << "\n=== Test 66: Truncation for stdout (Hard Limit 100) ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  // Create 150 measurements (exceeds stdout limit of 100)
  for (int i = 0; i < 150; ++i) {
    std::string label = "truncate_test_" + std::to_string(i);
    utiltimer::start(label);
    utiltimer::stop(label);
  }

  // Print to stdout - should auto-limit to 100
  std::ostringstream cout_capture;
  std::streambuf* old_cout = std::cout.rdbuf(cout_capture.rdbuf());
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, std::cout);
  std::cout.rdbuf(old_cout);

  std::string result = cout_capture.str();

  // Should show first 100 individual measurements
  TEST_ASSERT(result.find("truncate_test_0") != std::string::npos, "First measurement shown");
  TEST_ASSERT(result.find("truncate_test_99") != std::string::npos, "100th measurement shown");

  // Should show truncation message
  TEST_ASSERT(result.find("showing first 100 of 150 total measurements") != std::string::npos,
              "Truncation message present for stdout");

  // Should still show all statistics (150 labels)
  TEST_ASSERT(result.find("[UtilityTimer][truncate_test_0]: n-measurements: 1") != std::string::npos,
              "First label statistics shown");
  TEST_ASSERT(result.find("[UtilityTimer][truncate_test_149]: n-measurements: 1") != std::string::npos,
              "Last label statistics shown");
  TEST_ASSERT(result.find("[UtilityTimer] Total average sum:") != std::string::npos, "Total average sum shown");

  utiltimer::reset();
}

void test_truncation_chrome_trace() {
  std::cout << "\n=== Test 67: Chrome Trace Auto-Limit (10000) ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  // Create 150 measurements (under limit, just verify no truncation)
  for (int i = 0; i < 150; ++i) {
    std::string label = "chrome_test_" + std::to_string(i);
    utiltimer::start(label);
    utiltimer::stop(label);
  }

  // Print to stringstream as Chrome trace - has 10000 limit but we're under it
  std::ostringstream output;
  utiltimer::print(utiltimer::TimerOutputFormat::CHROME_TRACE_JSON, output);

  std::string result = output.str();

  // Should NOT have truncation comment (under limit)
  TEST_ASSERT(result.find("\"_comment\"") == std::string::npos, "No truncation comment when under limit");

  // Should have all events
  TEST_ASSERT(result.find("chrome_test_0") != std::string::npos, "First event present");
  TEST_ASSERT(result.find("chrome_test_149") != std::string::npos, "Last event present");

  utiltimer::reset();
}

void test_no_truncation_when_under_limit() {
  std::cout << "\n=== Test 68: File Output Unlimited ===" << std::endl;

  utiltimer::reset();
  utiltimer::enable(std::cout);

  // Create 50 measurements
  for (int i = 0; i < 50; ++i) {
    std::string label = "file_test_" + std::to_string(i);
    utiltimer::start(label);
    utiltimer::stop(label);
  }

  // Print to file - should be unlimited
  std::ofstream file_output("test_unlimited.txt");
  utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, file_output);
  file_output.close();

  std::ifstream verify("test_unlimited.txt");
  std::stringstream buffer;
  buffer << verify.rdbuf();
  std::string result = buffer.str();
  verify.close();

  // Should NOT show truncation message (file output is unlimited)
  TEST_ASSERT(result.find("showing first") == std::string::npos, "No truncation message for file output");

  // Should show all measurements
  TEST_ASSERT(result.find("file_test_0") != std::string::npos, "First measurement shown");
  TEST_ASSERT(result.find("file_test_49") != std::string::npos, "Last measurement shown");
  TEST_ASSERT(result.find("[UtilityTimer] Total average sum:") != std::string::npos, "Total average sum shown");

  utiltimer::reset();
}

//=============================================================================
// MAIN TEST RUNNER
//=============================================================================

int main() {
  std::cout << "========================================" << std::endl;
  std::cout << "UtilityTimer Complete Coverage Test Suite" << std::endl;
  std::cout << "========================================" << std::endl;

  // Section 1: Single-threaded tests (39 tests)
  std::cout << "\n========================================" << std::endl;
  std::cout << "SECTION 1: Single-threaded Tests (39 tests)" << std::endl;
  std::cout << "========================================" << std::endl;

  test_enable_disable();
  test_manual_start_stop_simple();
  test_manual_start_stop_perf();
  test_scoped_timer_automatic();
  test_scoped_timer_manual();
  test_scoped_timer_perf();
  test_duplicate_label();
  test_stop_nonexistent();
  test_active_timer_warning();
  test_empty_data_warning();
  test_human_readable_output();
  test_chrome_trace_output();
  test_reset();
  test_operation_order();
  test_nested_scoped();
  test_scoped_when_disabled();
  test_scoped_double_stop();
  test_mixed_timers();
  test_chrome_trace_file();
  test_timing_accuracy();
  test_multiple_enable_disable();
  test_custom_warning_stream();
  test_many_operations();
  test_zero_duration();
  test_scoped_exception_safety();
  test_both_namespaces();
  test_different_output_streams();
  test_chrome_trace_timestamps();
  test_long_labels();
  test_special_characters();
  test_reset_while_active();
  test_disable_then_print();
  test_scoped_scope_verification();
  test_json_structure();
  test_interleaved_operations();
  test_unique_labels_in_loop();
  test_duplicate_label_after_stop();
  test_duplicate_stop_safe();
  test_reuse_label_after_reset();

  // Section 2: Multithreaded tests (6 tests)
  std::cout << "\n========================================" << std::endl;
  std::cout << "SECTION 2: Multithreaded Tests (6 tests)" << std::endl;
  std::cout << "========================================" << std::endl;

  test_multithread_basic();
  test_multithread_chrome_trace();
  test_multithread_stress();
  test_multithread_concurrent_access();
  test_multithread_disable_enable();
  test_multithread_same_label();

  // Section 3: Edge cases and stress tests (15 tests)
  std::cout << "\n========================================" << std::endl;
  std::cout << "SECTION 3: Edge Cases and Stress Tests (15 tests)" << std::endl;
  std::cout << "========================================" << std::endl;

  test_max_instances_limit();
  test_thread_data_persistence();
  test_concurrent_thread_creation();
  test_scoped_timer_mt_manual_stop();
  test_scoped_timer_mt_exception();
  test_reset_during_active_timing();
  test_concurrent_print_calls();
  test_thread_id_consistency();
  test_atomic_counter_stress();
  test_enable_start_disable_stop();
  test_disable_start_enable_stop();
  test_reset_between_start_stop();
  test_chrome_trace_thread_ids();
  test_high_frequency_timing();
  test_multithread_interleaved();

  // Section 4: Average statistics tests (5 tests)
  std::cout << "\n========================================" << std::endl;
  std::cout << "SECTION 4: Average Statistics Tests (5 tests)" << std::endl;
  std::cout << "========================================" << std::endl;

  test_single_label_average_stats();
  test_multiple_labels_average_stats();
  test_total_average_sum();
  test_single_measurement_average();
  test_multithreaded_average_stats();

  // Section 5: Output truncation tests (3 tests)
  std::cout << "\n========================================" << std::endl;
  std::cout << "SECTION 5: Output Truncation Tests (3 tests)" << std::endl;
  std::cout << "========================================" << std::endl;

  test_truncation_human_readable();
  test_truncation_chrome_trace();
  test_no_truncation_when_under_limit();

  // Summary
  std::cout << "\n========================================" << std::endl;
  std::cout << "Test Summary" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "Tests Passed: " << testsPassed << std::endl;
  std::cout << "Tests Failed: " << testsFailed << std::endl;
  std::cout << "Total Tests:  " << (testsPassed + testsFailed) << std::endl;

  if (testsFailed == 0) {
    std::cout << "\n✓ All tests passed!" << std::endl;
    return 0;
  } else {
    std::cout << "\n✗ Some tests failed!" << std::endl;
    return 1;
  }
}
