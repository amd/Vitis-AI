/*
 * Copyright (C) 2025-2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software
 * is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
 * KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
 * EVENT SHALL "AMD" BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 * OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. Except as contained in this notice, the name of the AMD shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from AMD.
 */

/**
 * @file app_logger.hpp
 * @brief Thread-safe logging macro and log-level definitions.
 */

#pragma once

#include <cstdio>
#include <mutex>

/**
 * @enum AppLogLevel
 * @brief Defines the levels of application logging verbosity.
 *
 * This enumeration specifies various levels of logging granularity:
 * - NONE:    No logs will be displayed.
 * - ERROR:   Display error messages.
 * - WARNING: Display warning messages.
 * - RESULT:  Display inference results alongside warnings and errors.
 * - FIXME:   Display logs related to known features yet to be implemented
 * - INFO:    Display informational messages.
 * - DEBUG:   Display debug messages for troubleshooting.
 */
enum class AppLogLevel { NONE = 0, ERROR, WARNING, RESULT, FIXME, INFO, DEBUG };

// Global mutex for thread-safe logging (inline ensures a single instance across TUs)
inline std::mutex g_log_mutex;

/**
 * @def APP_LOG
 * @brief Application logging macro for consistent log output with level
 * filtering.
 *
 * Formats and displays log messages based on the specified log level, including
 * context information such as file and line number. The message is printed if
 * the current configured log level (`set_level`) is greater than or equal to
 * the level of the message.
 *
 * Supported log levels:
 * - AppLogLevel::ERROR
 * - AppLogLevel::WARNING
 * - AppLogLevel::RESULT
 * - AppLogLevel::FIXME
 * - AppLogLevel::INFO
 * - AppLogLevel::DEBUG
 *
 * @param level      The severity level of the log message (AppLogLevel enum).
 * @param set_level  The current log level for the application; messages below
 * this level are suppressed.
 * @param ...        The log message (printf-style format string and optional
 * arguments).
 *
 * @note Each log message includes a tag for the severity, the source file, and
 * line number for easier debugging.
 *
 * @example
 * APP_LOG(AppLogLevel::INFO, current_level, "Initialization completed with
 * status %d", status);
 */
#define APP_LOG(level, set_level, ...)                 \
  do {                                                 \
    const char* tag = nullptr;                         \
    switch (level) {                                   \
      case AppLogLevel::ERROR:                         \
        tag = "ERROR";                                 \
        break;                                         \
      case AppLogLevel::WARNING:                       \
        tag = "WARNING";                               \
        break;                                         \
      case AppLogLevel::RESULT:                        \
        tag = "RESULT";                                \
        break;                                         \
      case AppLogLevel::FIXME:                         \
        tag = "FIXME";                                 \
        break;                                         \
      case AppLogLevel::INFO:                          \
        tag = "INFO";                                  \
        break;                                         \
      case AppLogLevel::DEBUG:                         \
        tag = "DEBUG";                                 \
        break;                                         \
      default:                                         \
        tag = "UNKNOWN";                               \
        break;                                         \
    }                                                  \
    if (set_level >= level) {                          \
      std::lock_guard<std::mutex> lock(g_log_mutex);   \
      printf("[%s] %s:%d  ", tag, __FILE__, __LINE__); \
      printf(__VA_ARGS__);                             \
      printf("\n");                                    \
      fflush(stdout);                                  \
    }                                                  \
  } while (0)
