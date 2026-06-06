##########################################################################
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
###########################################################################

# Makefile for UtilityTimer shared library
# Builds libutility_timer.so for use in examples and other projects

CXX ?= g++
CXXFLAGS += -std=c++17 -O2 -Wall -fPIC
LDFLAGS_SO = -shared

# Directories
SRC_DIR = src
INC_DIR = include
LIB_DIR = lib
OBJ_DIR = obj

# Source files
SOURCES = $(SRC_DIR)/UtilityTimer.cpp $(SRC_DIR)/SimpleUtilityTimer.cpp
OBJECTS = $(OBJ_DIR)/UtilityTimer.o $(OBJ_DIR)/SimpleUtilityTimer.o

# Output library (shared only)
LIBRARY_SO = $(LIB_DIR)/libutility_timer.so

# Test executable
TEST_COMPLETE_COVERAGE = test_complete_coverage

# Default target
all: $(LIBRARY_SO)

# Create directories if they don't exist
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(LIB_DIR):
	mkdir -p $(LIB_DIR)

# Compile source files to object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -c $< -o $@

# Create shared library from object files
$(LIBRARY_SO): $(OBJECTS) | $(LIB_DIR)
	$(CXX) $(LDFLAGS) $(LDFLAGS_SO) -o $@ $(OBJECTS)
	@echo "Shared library created: $(LIBRARY_SO)"

# Install to ../../install/usr/lib (install dir parallel to top-level Makefile; override with INSTALL_ROOT)
INSTALL_ROOT ?= ../../install
INSTALL_LIB = $(INSTALL_ROOT)/usr/lib

install: $(LIBRARY_SO)
	@mkdir -p $(INSTALL_LIB)
	@cp $(LIBRARY_SO) $(INSTALL_LIB)/
	@echo "Installed $(LIBRARY_SO) to $(INSTALL_LIB)"



# Test target
$(TEST_COMPLETE_COVERAGE): test_complete_coverage.cpp $(OBJECTS)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -pthread -o $@ $^
	@echo "Built: $(TEST_COMPLETE_COVERAGE)"

# Build test
tests: $(TEST_COMPLETE_COVERAGE)

# Run the complete coverage test
test: $(TEST_COMPLETE_COVERAGE)
	@echo "Running complete coverage test..."
	./$(TEST_COMPLETE_COVERAGE)

# Clean build artifacts
clean:
	rm -rf $(OBJ_DIR) $(LIB_DIR) $(TEST_COMPLETE_COVERAGE)
	rm -f test_complete_coverage_trace.json test_stream_output.txt test_mt_trace.json test_unlimited.txt
	@echo "Cleaned build artifacts"

# Phony targets
.PHONY: all clean install tests test
