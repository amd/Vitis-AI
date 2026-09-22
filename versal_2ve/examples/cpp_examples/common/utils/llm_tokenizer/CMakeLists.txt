# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License.

cmake_minimum_required(VERSION 3.15)
project(llm_tokenizer LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)

# Build options
option(ENABLE_GPT2_TOKENIZER "Enable GPT2/BPE tokenizer" ON)
option(ENABLE_SPM_TOKENIZER "Enable SentencePiece tokenizer" ON)
option(ENABLE_WORDPIECE_TOKENIZER "Enable WordPiece tokenizer" ON)
option(ENABLE_BERT_TOKENIZER "Enable BERT tokenizer" ON)
option(ENABLE_TRIE_TOKENIZER "Enable Trie tokenizer" ON)
option(BUILD_SHARED_LIBS "Build shared library" ON)

# Compiler flags
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# Symbol visibility - hide all symbols by default, export only the public API
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)
set(CMAKE_C_VISIBILITY_PRESET hidden)

# Add compiler warnings
if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Source files
set(TOKENIZER_SOURCES
  # Core tokenizer files
  src/llm_tokenizer.cc
  src/tokenizer_impl.cc
  src/unicode.cc
  src/case_encoder.cc
  src/string_utils.cc
  src/base64.cc
  src/bpe_kernels.cc
  src/wordpiece_json_tokenizer.cc
)

# Conditional sources based on options
if(ENABLE_BERT_TOKENIZER)
  list(APPEND TOKENIZER_SOURCES
    src/bert_tokenizer.cc
    src/basic_tokenizer.cc
    src/bert_tokenizer_decoder.cc
  )
  add_compile_definitions(ENABLE_BERT_TOKENIZER)
endif()

if(ENABLE_WORDPIECE_TOKENIZER)
  list(APPEND TOKENIZER_SOURCES
    src/wordpiece_tokenizer.cc
  )
  add_compile_definitions(ENABLE_WORDPIECE_TOKENIZER)
endif()

if(ENABLE_GPT2_TOKENIZER)
  list(APPEND TOKENIZER_SOURCES
    src/bpe_kernels.cc
  )
  add_compile_definitions(ENABLE_GPT2_TOKENIZER)
endif()

if(ENABLE_SPM_TOKENIZER)
  # Note: sentencepiece_tokenizer.cc contains ONNX operator code and is not needed
  # The actual SentencePiece tokenizer support is in ugm_kernels.hpp
  add_compile_definitions(ENABLE_SPM_TOKENIZER)
endif()

if(ENABLE_TRIE_TOKENIZER)
  add_compile_definitions(ENABLE_TRIE_TOKENIZER)
endif()

# Public header files (C++ API)
set(TOKENIZER_PUBLIC_HEADERS
  include/llm_tokenizer.hpp
)

# Internal header files
set(TOKENIZER_INTERNAL_HEADERS
  include/tokenizer_impl.h
  include/tokenizer_common.h
  include/unicode.h
  include/case_encoder.h
  include/basic_tokenizer.hpp
  include/bert_tokenizer.hpp
  include/bert_tokenizer_decoder.hpp
  include/wordpiece_tokenizer.h
  include/wordpiece_json_tokenizer.hpp
  include/bpe_kernels.h
  include/bpe_tokenizer_model.hpp
  include/bpe_decoder.hpp
  include/bpe_streaming.hpp
  include/bpe_utils.hpp
  include/sentencepiece_tokenizer.h
  include/sentencepiece_decoder.hpp
  include/tokenizer_jsconfig.hpp
  include/trie_tokenizer.hpp
  include/trietree.hpp
  include/ugm_kernels.hpp
  include/unescape.hpp
  include/string_utils.h
  include/ext_status.h
  include/ortx_types.h
  include/narrow.h
  include/base64.h
  include/minja.hpp
)

set(TOKENIZER_HEADERS
  ${TOKENIZER_PUBLIC_HEADERS}
  ${TOKENIZER_INTERNAL_HEADERS}
)

# Create library target
if(BUILD_SHARED_LIBS)
  add_library(llm_tokenizer SHARED ${TOKENIZER_SOURCES})
else()
  add_library(llm_tokenizer STATIC ${TOKENIZER_SOURCES})
endif()

set_target_properties(llm_tokenizer PROPERTIES
  PUBLIC_HEADER "${TOKENIZER_PUBLIC_HEADERS}"
)

# Define build macro so export.h marks public symbols for export
target_compile_definitions(llm_tokenizer PRIVATE LLM_TOKENIZER_BUILD)

# Ensure internal function calls resolve within this library, not the dynamic linker search order
target_link_options(llm_tokenizer PRIVATE
  "-Wl,-Bsymbolic-functions"
)

# Third-party library setup
# SentencePiece - NOT NEEDED
# The standalone UGM implementation in ugm_kernels.hpp provides SentencePiece
# support without requiring the third-party SentencePiece library.
# if(ENABLE_SPM_TOKENIZER)
#   add_subdirectory(third_party/sentencepiece EXCLUDE_FROM_ALL)
#   target_link_libraries(llm_tokenizer PRIVATE sentencepiece-static)
#   target_include_directories(llm_tokenizer PRIVATE
#     ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sentencepiece/src
#     ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sentencepiece/src/builtin_pb
#   )
# endif()

# Include directories
target_include_directories(llm_tokenizer
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# All dependencies from onnxruntime-extensions have been resolved!
# This is now a fully standalone library with no ONNX Runtime dependencies.
#
# Included dependencies (copied/integrated):
# - ext_status.h, ortx_types.h (error handling)
# - string_utils.h, narrow.h (utilities)
# - base64.h (encoding utilities)
# - minja.hpp (Jinja2 template engine for chat templates)
#
# Third-party dependencies:
# - nlohmann/json (JSON parsing) - in ../nlohmann/ (accessed via parent dir include)

message(STATUS "")
message(STATUS "===== Embedded LLM Tokenizer Configuration =====")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Build shared libs: ${BUILD_SHARED_LIBS}")
message(STATUS "C++ API only: ON (C API removed)")
message(STATUS "GPT2 tokenizer: ${ENABLE_GPT2_TOKENIZER}")
message(STATUS "SentencePiece tokenizer: ${ENABLE_SPM_TOKENIZER}")
message(STATUS "WordPiece tokenizer: ${ENABLE_WORDPIECE_TOKENIZER}")
message(STATUS "BERT tokenizer: ${ENABLE_BERT_TOKENIZER}")
message(STATUS "Trie tokenizer: ${ENABLE_TRIE_TOKENIZER}")
message(STATUS "================================================")
message(STATUS "Standalone library: All dependencies resolved!")
message(STATUS "")

# Installation rules
install(TARGETS llm_tokenizer
  EXPORT llm_tokenizerTargets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  PUBLIC_HEADER DESTINATION include
)

# Internal headers are not installed (implementation details)

# Export package configuration
install(EXPORT llm_tokenizerTargets
  FILE llm_tokenizerTargets.cmake
  NAMESPACE llm_tokenizer::
  DESTINATION lib/cmake/llm_tokenizer
)
