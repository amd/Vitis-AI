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

# Makefile for Embedded LLM Tokenizer
# Wrapper around CMake build system for convenience

.PHONY: all build clean tests help configure install distclean

# Default configuration
BUILD_DIR ?= build
BUILD_TYPE ?= Release
CMAKE_FLAGS ?= -Wall -Wextra -Wpedantic
NPROC := $(shell nproc 2>/dev/null || echo 4)

# CMake options (can be overridden from command line)
ENABLE_SPM_TOKENIZER ?= ON
ENABLE_GPT2_TOKENIZER ?= ON
ENABLE_WORDPIECE_TOKENIZER ?= ON
ENABLE_BERT_TOKENIZER ?= ON
ENABLE_TRIE_TOKENIZER ?= ON

# Detect Yocto OE CMake toolchain file when SDK environment is sourced.
# This properly configures cross-compilation sysroot, compiler, and include paths,
# preventing host /usr/include from leaking into the cross-build.
ifdef OECORE_NATIVE_SYSROOT
    _OE_TOOLCHAIN := $(wildcard $(OECORE_NATIVE_SYSROOT)/usr/share/cmake/OEToolchainConfig.cmake)
    ifneq ($(_OE_TOOLCHAIN),)
        _CMAKE_TOOLCHAIN_FLAG := -DCMAKE_TOOLCHAIN_FILE=$(_OE_TOOLCHAIN)
    endif
endif

# Collected CMake configuration
CMAKE_CONFIG_OPTS := \
	-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	-DCMAKE_CXX_FLAGS="$(CMAKE_FLAGS)" \
	-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
	-DENABLE_SPM_TOKENIZER=$(ENABLE_SPM_TOKENIZER) \
	-DENABLE_GPT2_TOKENIZER=$(ENABLE_GPT2_TOKENIZER) \
	-DENABLE_WORDPIECE_TOKENIZER=$(ENABLE_WORDPIECE_TOKENIZER) \
	-DENABLE_BERT_TOKENIZER=$(ENABLE_BERT_TOKENIZER) \
	-DENABLE_TRIE_TOKENIZER=$(ENABLE_TRIE_TOKENIZER) \
	-DBUILD_SHARED_LIBS=ON

# Default target: build library only
all: build

# Configure CMake
configure: $(BUILD_DIR)/Makefile

$(BUILD_DIR)/Makefile:
	@echo "==> Configuring CMake..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && LDFLAGS="" CPPFLAGS="" cmake .. $(CMAKE_CONFIG_OPTS) \
		$(_CMAKE_TOOLCHAIN_FLAG) \
		-DCMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES="" \
		-DCMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES="" \
		-DCMAKE_SHARED_LINKER_FLAGS="" \
		-DCMAKE_EXE_LINKER_FLAGS="" \
		-DCMAKE_MODULE_LINKER_FLAGS=""
	@echo "==> Configuration complete"

# Build the library
build: configure
	@echo "==> Building llm_tokenizer library..."
	@LDFLAGS="" CPPFLAGS="" $(MAKE) -C $(BUILD_DIR) -j$(NPROC) llm_tokenizer
	@if [ -f $(BUILD_DIR)/libllm_tokenizer.so ]; then \
		mkdir -p lib; \
		cp -f $(BUILD_DIR)/libllm_tokenizer.so lib/; \
	fi
	@echo "==> Build complete"
	@echo ""
	@echo "Library location:"
	@if [ -f lib/libllm_tokenizer.so ]; then \
		echo "  lib/libllm_tokenizer.so"; \
	fi

# Build everything (library + examples)
all-with-examples: configure
	@echo "==> Building library and examples..."
	@$(MAKE) -C $(BUILD_DIR) -j$(NPROC)
	@echo "==> Build complete"

# Clean build artifacts
clean:
	@echo "==> Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR) lib
	@echo "==> Clean complete"

# Deep clean - remove entire build directory
distclean:
	@echo "==> Removing build directory..."
	@rm -rf $(BUILD_DIR) lib
	@echo "==> Deep clean complete"

# Run tests
tests: build
	@echo "==> Running test suite..."
	@echo ""
	@./run_all_tests.sh

# Install to cpp_examples/install/usr/lib (staging dir next to top-level Makefile; override with INSTALL_ROOT)
INSTALL_ROOT ?= ../../../install
INSTALL_LIB = $(INSTALL_ROOT)/usr/lib

# Install the shared library for board packaging
install: build
	@echo "==> Installing llm_tokenizer library..."
	@mkdir -p $(INSTALL_LIB)
	@cp -v lib/libllm_tokenizer.so $(INSTALL_LIB)/
	@echo "Installed lib/libllm_tokenizer.so to $(INSTALL_LIB)"

# Build with strict warnings (treat warnings as errors)
strict: CMAKE_FLAGS := -Wall -Wextra -Wpedantic -Werror
strict: distclean build
	@echo "==> Strict build complete (warnings treated as errors)"

# Help target
help:
	@echo "Embedded LLM Tokenizer - Makefile Targets"
	@echo "=========================================="
	@echo ""
	@echo "Main targets:"
	@echo "  make              - Build the library (default)"
	@echo "  make build        - Build the library"
	@echo "  make clean        - Clean build artifacts"
	@echo "  make tests        - Build library and run test suite"
	@echo "  make install      - Install shared library to INSTALL_ROOT/usr/lib"
	@echo "  make distclean    - Remove entire build directory"
	@echo ""
	@echo "Additional targets:"
	@echo "  make all-with-examples - Build library and examples"
	@echo "  make strict       - Build with -Werror (warnings as errors)"
	@echo "  make configure    - Run CMake configuration only"
	@echo "  make help         - Show this help message"
	@echo ""
	@echo "Build configuration (override with VAR=value):"
	@echo "  BUILD_DIR=$(BUILD_DIR)       - Build directory name (simple name, not full path)"
	@echo "  BUILD_TYPE=$(BUILD_TYPE)"
	@echo "  CMAKE_FLAGS=$(CMAKE_FLAGS)"
	@echo ""
	@echo "Feature flags (override with VAR=ON|OFF):"
	@echo "  ENABLE_SPM_TOKENIZER=$(ENABLE_SPM_TOKENIZER)"
	@echo "  ENABLE_GPT2_TOKENIZER=$(ENABLE_GPT2_TOKENIZER)"
	@echo "  ENABLE_WORDPIECE_TOKENIZER=$(ENABLE_WORDPIECE_TOKENIZER)"
	@echo "  ENABLE_BERT_TOKENIZER=$(ENABLE_BERT_TOKENIZER)"
	@echo "  ENABLE_TRIE_TOKENIZER=$(ENABLE_TRIE_TOKENIZER)"
	@echo ""
	@echo "Installation variables:"
	@echo "  INSTALL_ROOT=$(INSTALL_ROOT) - Staging root (lib -> INSTALL_ROOT/usr/lib)"
	@echo ""
	@echo "Examples:"
	@echo "  make BUILD_TYPE=Debug              - Build in debug mode"
	@echo "  make BUILD_DIR=mybuild             - Use custom build directory"
	@echo "  make strict                        - Build with warnings as errors"
	@echo "  make install INSTALL_ROOT=../../../install"
	@echo ""
