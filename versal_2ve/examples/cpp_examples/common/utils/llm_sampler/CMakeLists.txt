# Copyright (C) 2026 Advanced Micro Devices, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.15)
project(llm_sampler LANGUAGES CXX)

# C++17 — matches the tokenizer library
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_SHARED_LIBS "Build shared library" ON)
option(BUILD_EXAMPLES "Build example applications" OFF)
option(BUILD_TESTING "Build test suite" OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# Compiler warnings
if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Source files
set(SAMPLER_SOURCES
  src/i_sampler.cpp
  src/greedy_sampler.cpp
  src/temperature_sampler.cpp
  src/topk_sampler.cpp
  src/topp_sampler.cpp
  src/repetition_penalty_sampler.cpp
  src/sampler_factory.cpp
)

set(SAMPLER_PUBLIC_HEADERS
  include/llm_sampler.hpp
  include/i_sampler.hpp
  include/greedy_sampler.hpp
  include/temperature_sampler.hpp
  include/topk_sampler.hpp
  include/topp_sampler.hpp
  include/repetition_penalty_sampler.hpp
  include/sampler_factory.hpp
)

# Library target
if(BUILD_SHARED_LIBS)
  add_library(llm_sampler SHARED ${SAMPLER_SOURCES})
else()
  add_library(llm_sampler STATIC ${SAMPLER_SOURCES})
endif()

set_target_properties(llm_sampler PROPERTIES
  PUBLIC_HEADER "${SAMPLER_PUBLIC_HEADERS}"
)

target_compile_definitions(llm_sampler PRIVATE LLM_SAMPLER_BUILD)

target_include_directories(llm_sampler
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Ensure internal calls resolve within this library
if(NOT MSVC)
  target_link_options(llm_sampler PRIVATE "-Wl,-Bsymbolic-functions")
endif()

# Installation rules
install(TARGETS llm_sampler
  EXPORT llm_samplerTargets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  PUBLIC_HEADER DESTINATION include
)

install(EXPORT llm_samplerTargets
  FILE llm_samplerTargets.cmake
  NAMESPACE llm_sampler::
  DESTINATION lib/cmake/llm_sampler
)

# Examples (only when explicitly requested)
if(BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# Tests (only when explicitly requested)
if(BUILD_TESTING)
  add_subdirectory(tests/cpp)
endif()

message(STATUS "")
message(STATUS "===== Embedded LLM Sampler Configuration =====")
message(STATUS "Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "Build shared libs: ${BUILD_SHARED_LIBS}")
message(STATUS "===============================================")
message(STATUS "")
