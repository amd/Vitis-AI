# UtilityTimer - Performance Profiling Library

<!--
## Copyright and license statement

Copyright (C) 2026 Advanced Micro Devices, Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
-->


## Overview

`UtilityTimer` is a singleton-based performance timing utility class designed for measuring and profiling code execution times in C++ applications. It provides microsecond-precision timing with support for multiple labeled timing operations, making it ideal for performance analysis and optimization.

## Key Features

- **Lock-Free Multithreading**: Thread-local storage enables zero-contention concurrent timing
- **Singleton Pattern**: Single global instance ensures consistent timing across your application
- **Multiple Timing Labels**: Track multiple operations simultaneously with unique string labels
- **Microsecond Precision**: High-resolution timing using `std::chrono::high_resolution_clock`
- **Global Enable/Disable**: Toggle timing on/off without removing timing code
- **Zero Overhead When Disabled**: No performance impact when timing is disabled
- **Multiple Output Formats**: Human-readable text and Chrome trace JSON for visualization
- **Automatic Thread Detection**: Thread ID captured automatically - no manual management
- **Smart Memory Management**: Uses `std::shared_ptr` for automatic cleanup

## Directory Structure

```
utility_timer/
├── include/           # Public headers
│   ├── SimpleUtilityTimer.hpp
│   └── UtilityTimer.hpp
├── src/              # Implementation files
│   ├── UtilityTimer.cpp
│   └── SimpleUtilityTimer.cpp
├── lib/              # Built libraries (generated)
│   ├── libutility_timer.a   # Static library
│   └── libutility_timer.so  # Shared library
├── obj/              # Object files (generated)
├── Makefile          # Build system
└── README.md         # This file
```

## Quick Start

### Basic Usage

```cpp
#include "SimpleUtilityTimer.hpp"

int main() {
    utiltimer::enable(std::cout);
    
    utiltimer::start("my_operation");
    // ... your code here ...
    utiltimer::stop("my_operation");
    
    utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, std::cout);
    return 0;
}
```

### Scoped Timer (RAII)

```cpp
#include "SimpleUtilityTimer.hpp"

int main() {
    utiltimer::enable(std::cout);
    
    {
        auto timer = utiltimer::startScoped("my_operation");
        // ... your code here ...
    } // Timer stops automatically
    
    utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, std::cout);
    return 0;
}
```

### Multithreaded Usage

```cpp
#include "SimpleUtilityTimer.hpp"
#include <thread>

void workerFunction() {
    utiltimer::start("worker_task");  // Thread ID captured automatically
    // ... do work ...
    utiltimer::stop("worker_task");
}

int main() {
    utiltimer::enable(std::cout);
    
    std::thread t1(workerFunction);
    std::thread t2(workerFunction);
    
    t1.join();
    t2.join();
    
    utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, std::cout);
    return 0;
}
```

## Building

```bash
make        # Build both static and shared libraries
make clean  # Clean build artifacts
```

This creates:
- `lib/libutility_timer.a` (static library)
- `lib/libutility_timer.so` (shared library)

## Integration

### Makefile Integration

#### Using Static Library

```makefile
UTILITY_TIMER_DIR = ../common/utility_timer
CFLAGS += -I$(UTILITY_TIMER_DIR)/include

$(UTILITY_TIMER_DIR)/lib/libutility_timer.a:
	+$(MAKE) -C $(UTILITY_TIMER_DIR) CXX="$(CXX)" CXXFLAGS="$(CXXFLAGS)" AR="$(AR)"

$(TARGET): $(OBJS) $(UTILITY_TIMER_DIR)/lib/libutility_timer.a
	$(CXX) $(CFLAGS) -o $(TARGET) $(OBJS) $(UTILITY_TIMER_DIR)/lib/libutility_timer.a $(LDFLAGS)
```

#### Using Shared Library

```makefile
UTILITY_TIMER_DIR = ../common/utility_timer
CFLAGS += -I$(UTILITY_TIMER_DIR)/include
LDFLAGS += -L$(UTILITY_TIMER_DIR)/lib -lutility_timer -Wl,-rpath,$(UTILITY_TIMER_DIR)/lib

$(UTILITY_TIMER_DIR)/lib/libutility_timer.so:
	+$(MAKE) -C $(UTILITY_TIMER_DIR) CXX="$(CXX)" CXXFLAGS="$(CXXFLAGS)"

$(TARGET): $(OBJS) $(UTILITY_TIMER_DIR)/lib/libutility_timer.so
	$(CXX) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)
```

### Meson Integration

#### Using Static Library

```meson
utility_timer_inc = include_directories('../../../examples/cpp_examples/common/utility_timer/include')
utility_timer_sources = files(
  '../../../examples/cpp_examples/common/utility_timer/src/UtilityTimer.cpp',
  '../../../examples/cpp_examples/common/utility_timer/src/SimpleUtilityTimer.cpp'
)

vart_utility_timer = static_library('vart_utility_timer',
  utility_timer_sources,
  cpp_args : ['-std=c++17'],
  include_directories : [utility_timer_inc],
  install : false,
)

vart_utility_timer_dep = declare_dependency(
  link_with : [vart_utility_timer],
  include_directories : [utility_timer_inc]
)
```

#### Using Shared Library

```meson
utility_timer_inc = include_directories('../../../examples/cpp_examples/common/utility_timer/include')
utility_timer_sources = files(
  '../../../examples/cpp_examples/common/utility_timer/src/UtilityTimer.cpp',
  '../../../examples/cpp_examples/common/utility_timer/src/SimpleUtilityTimer.cpp'
)

vart_utility_timer = shared_library('vart_utility_timer',
  utility_timer_sources,
  cpp_args : ['-std=c++17'],
  include_directories : [utility_timer_inc],
  install : false,
)

vart_utility_timer_dep = declare_dependency(
  link_with : [vart_utility_timer],
  include_directories : [utility_timer_inc]
)
```

## API Reference

### Core Functions

#### `enable(std::ostream& warningOutStream)`
Enables performance timing and sets warning output stream.

```cpp
utiltimer::enable(std::cout);
```

#### `disable()`
Disables performance timing.

```cpp
utiltimer::disable();
```

#### `start(const std::string& label)`
Starts timing for a labeled operation. Thread ID captured automatically.

```cpp
utiltimer::start("preprocessing");
```

#### `stop(const std::string& label)`
Stops timing for a labeled operation.

```cpp
utiltimer::stop("preprocessing");
```

#### `print(TimerOutputFormat format, std::ostream& outputStream)`
Prints timing results. Formats: `HUMAN_READABLE_TEXT` or `CHROME_TRACE_JSON`.

```cpp
utiltimer::print(utiltimer::TimerOutputFormat::HUMAN_READABLE_TEXT, std::cout);
```

#### `reset()`
Clears all timing data and resets counters.

```cpp
utiltimer::reset();
```

#### `startScoped(const std::string& label)`
Creates RAII timer that stops automatically when out of scope.

```cpp
auto timer = utiltimer::startScoped("my_operation");
```

## Output Limiting

UtilityTimer automatically limits output to prevent overwhelming terminal displays while preserving complete statistical data.

### Automatic Limits

| Output Stream | Individual Measurements Limit | Notes |
|--------------|------------------------------|-------|
| `std::cout` / `std::cerr` | **100** | Prevents terminal overflow |
| Chrome Trace JSON (any stream) | **10,000** | Keeps trace files manageable |
| File streams (human-readable) | **Unlimited** | Full detail for analysis |

### How It Works

When you call `print()`:
1. **Individual measurements** are limited based on the output stream type
2. **Complete statistics** for all labels are always shown (n-measurements, averages)
3. **Truncation message** indicates when output is limited

### Example Output (stdout with 3508 measurements)

```
[UtilityTimer][Thread:12345][preprocess]: 2087 us
[UtilityTimer][Thread:12345][inference]: 5088 us
... (first 100 measurements) ...
[UtilityTimer][Thread:67890][display]: 1092 us
[UtilityTimer] ... (showing first 100 of 3508 total measurements) ...
[UtilityTimer][camera_capture]: n-measurements: 250, avg: 3105 us
[UtilityTimer][display]: n-measurements: 752, avg: 1090 us
[UtilityTimer][frame_decode]: n-measurements: 250, avg: 2100 us
[UtilityTimer][inference]: n-measurements: 752, avg: 5099 us
[UtilityTimer][postprocess]: n-measurements: 752, avg: 3094 us
[UtilityTimer][preprocess]: n-measurements: 752, avg: 2090 us
[UtilityTimer] Total average sum: 16578 us
```

### Benefits

- **Terminal-Friendly**: 100 measurements is enough to see patterns without scrolling
- **Complete Statistics**: Always get full statistical summary regardless of truncation
- **Detailed Analysis**: Write to file for unlimited detail when needed
- **Flexible**: Choose the right output method for your needs

### Recommendations

- **Quick Check**: Print to `std::cout` for immediate feedback (limited to 100)
- **Detailed Analysis**: Print to file for complete data
- **Visualization**: Use Chrome trace JSON for timeline analysis

## Output Format Details

### Human-Readable Format Structure

The human-readable output consists of three sections:

#### 1. Individual Measurements
Each timing instance is printed with thread ID and duration:
```
[UtilityTimer][Thread:140234567890][operation_name]: 1234 us
```

- **Thread ID**: Unique identifier for the thread that performed the operation
- **Label**: The operation name you provided to `start()`
- **Duration**: Time in microseconds (us)

#### 2. Per-Label Statistics
After individual measurements, statistics are shown for each unique label:
```
[UtilityTimer][operation_name]: n-measurements: 10, avg: 1250 us
```

- **n-measurements**: Total count of times this label was used
- **avg**: Average duration across all measurements for this label

#### 3. Total Average Sum
Final line shows the sum of all label averages:
```
[UtilityTimer] Total average sum: 12500 us
```

This represents the total time if you ran one instance of each unique operation.

## Best Practices

1. **Enable Early**: Call `enable()` at application start
2. **Use Descriptive Labels**: `"model_inference"` not `"op1"`
3. **Always Pair Start/Stop**: Every `start()` needs a `stop()`
4. **Reset Between Runs**: Use `reset()` to clear data between test runs
5. **Use Scoped Timers**: Prefer RAII for exception safety
6. **Print After Threads Complete**: Wait for all threads to join before calling `print()`

## Common Issues

### Forgetting to Stop
```cpp
utiltimer::start("operation");
utiltimer::print();  // WARNING: operation was not stopped!
```
**Solution**: Always call `stop()` before `print()`.

### Timing When Disabled
```cpp
utiltimer::start("operation");  // No effect if not enabled
```
**Solution**: Call `enable()` before using the timer.

### Printing Before Scoped Timer Ends
```cpp
void myFunction() {
    auto timer = utiltimer::startScoped("myFunction");
    utiltimer::print();  // WARNING: timer still active!
}
```
**Solution**: Call `timer.stop()` before `print()`, or ensure timer goes out of scope first.

## Scoped Timer (RAII)

Scoped timers automatically stop when they go out of scope:

```cpp
{
    auto timer = utiltimer::startScoped("data_processing");
    // ... processing code ...
} // Timer stops here automatically
```

**Manual Stop (Optional):**
```cpp
auto timer = utiltimer::startScoped("processing");
if (someCondition()) {
    timer.stop();  // Stop early
}
```

**Exception Safety:**
Scoped timers automatically stop even if exceptions are thrown.

**Restrictions:**
- No copying or moving allowed
- Ensures single, well-defined lifetime

## Chrome Trace Visualization

Output timing data in Chrome Trace format for visual analysis:

```cpp
std::ofstream traceFile("trace.json");
utiltimer::print(utiltimer::TimerOutputFormat::CHROME_TRACE_JSON, traceFile);
```

**Viewing:**
1. Open Chrome browser
2. Navigate to `chrome://tracing`
3. Click "Load" and select your trace.json file

**Benefits**: Visual timeline, zoom/pan, see exact durations, analyze bottlenecks

## Multithreading Support

UtilityTimer provides **lock-free multithreaded timing** using thread-local storage.

### Key Features

- ✅ 100% lock-free `start()` and `stop()` operations
- ✅ Automatic thread ID capture
- ✅ Zero API changes - existing code works identically
- ✅ Thread-aware output (threads shown separately in Chrome trace)
- ✅ Global ordering across all threads

### How It Works

Each thread gets its own independent timing data structure. Thread ID is automatically captured when `start()` or `stop()` is called. A global atomic counter maintains chronological order across all threads.

### Output Format

**Human-Readable:**
```
[UtilityTimer][Thread:140234567890][worker_task]: 1234 us
[UtilityTimer][Thread:140234567891][worker_task]: 1245 us
```

**Chrome Trace:**
Each thread appears on a separate row in chrome://tracing for easy visualization of concurrent operations.

### Performance

- **Lock-Free**: Zero contention during timing operations
- **Scalable**: Linear performance scaling with thread count
- **Overhead**: ~100ns per operation regardless of thread count

### Memory Management

Uses `std::shared_ptr` for automatic memory management:
1. Thread creates `shared_ptr<ThreadLocalData>` (ref count = 1)
2. Registry stores copy (ref count = 2)
3. Thread exits, thread's copy destroyed (ref count = 1)
4. Data remains available for `print()` via registry
5. `reset()` or program exit releases references (ref count = 0)
6. Memory automatically freed

**Benefits**: Data persists after thread exit, automatic cleanup when no longer needed.

### Thread Safety

✅ **Safe**: Multiple threads calling `start()`/`stop()` concurrently
⚠️ **Note**: Call `print()` after all threads complete to avoid warnings

### Implementation Details

**Architecture**: Thread-local storage (TLS) with global registry

**Key Components**:
- Thread-local `shared_ptr<ThreadLocalData>` per thread
- Global registry of all thread instances
- Atomic counter for global ordering
- Mutex only for registry access (not timing operations)

**Alternative Design Considered**: 16-shard map with per-shard mutexes. TLS chosen for superior lock-free performance.

**Design Decisions**:
1. `std::shared_ptr` for automatic memory management and data persistence
2. Automatic thread ID capture for zero overhead when disabled
3. Global atomic counter for chronological ordering
4. Const correctness with mutable mutex for const functions

## Performance Considerations

- **When Disabled**: Single boolean check - minimal overhead
- **When Enabled**: ~100ns overhead per timing operation
- **Multithreaded**: Scales linearly - no contention between threads
- **Memory**: ~100-200 bytes per thread + timing data + 16 bytes for shared_ptr control block

## Timing Accuracy

Timing accuracy depends on OS clock resolution, system load, and context switching. For best results:
- Use for operations > 1ms (shorter operations benefit from loop averaging)
- Expect some variability between runs
- Focus on relative comparisons rather than absolute values

**Loop Averaging for Short Operations:**
```cpp
const int NUM_ITERATIONS = 10000;
utiltimer::start("short_operation");
for (int i = 0; i < NUM_ITERATIONS; ++i) {
    // Your short operation here
}
utiltimer::stop("short_operation");
// Divide reported time by NUM_ITERATIONS for average
```

## Testing

```bash
cd examples/cpp_examples/common/utility_timer
make test_multithreaded
./test_multithreaded

make test_comprehensive
./test_comprehensive
```

## Cross-Compilation

For Makefile projects, pass compiler variables:
```makefile
$(UTILITY_TIMER_DIR)/lib/libutility_timer.a:
	+$(MAKE) -C $(UTILITY_TIMER_DIR) CXX="$(CXX)" CXXFLAGS="$(CXXFLAGS)" AR="$(AR)"
```

For Meson projects, cross-compiler is automatically used.
