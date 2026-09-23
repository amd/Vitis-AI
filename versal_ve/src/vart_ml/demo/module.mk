#
# Copyright 2024 Advanced Micro Devices Inc.
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
#

DEMO_DIR := demo

# Source files for the demos, excluding common.cpp
DEMO_SRCS := $(filter-out $(DEMO_DIR)/common.cpp, $(wildcard $(DEMO_DIR)/*.cpp))

# List of binaries to be created (remove .cpp extension)
DEMO_BINS := $(patsubst %.cpp,%,$(DEMO_SRCS))

# Add the demo binaries to the global TARGETS
TARGETS += $(DEMO_BINS)

DEMO_OBJS := $(patsubst %.cpp,%.o,$(DEMO_SRCS))
DEMO_OBJS += $(patsubst %.cpp,%.o,$(DEMO_DIR)/common.cpp)

DEMO_DEPS := $(DEMO_OBJS:.o=.d)
-include $(DEMO_DEPS)

ifneq ($(MAKECMDGOALS),install)
$(DEMO_OBJS): CXXFLAGS += -fPIC -I. -I$(DEMO_DIR)/. $(shell pkg-config --cflags opencv4) -pthread

$(DEMO_BINS): LDFLAGS += -Lvart_ml_runner -Lonnx_runner
$(DEMO_BINS): LDFLAGS += -Wl,-rpath,'$$ORIGIN/../third_party/opencv/lib'
$(DEMO_BINS): LDFLAGS += -Wl,-rpath,'$$ORIGIN/../vart_ml_runner' -Wl,--hash-style=gnu -pthread
$(DEMO_BINS): LDLIBS  := -lutils -lio -lnpu_runner -lvart_ml_runner -lonnx_runner
$(DEMO_BINS): LDLIBS  += $(shell pkg-config --libs opencv4)

# Rule to build each binary
$(DEMO_BINS): $(utils_TARGET) $(io_TARGETS) $(runner_TARGET) $(NPU_TARGET) $(ONNX_TARGET)
$(DEMO_BINS): $(DEMO_DIR)/%: $(DEMO_DIR)/%.o $(DEMO_DIR)/common.o
	$(CXX) $(LDFLAGS) $< $(DEMO_DIR)/common.o $(LDLIBS) -o $@
endif

install: install-demos

# Installation rule to place binaries in /usr/bin or $(PREFIX)/bin
install-demos:
	install -Dt $(DESTDIR)$(PREFIX)/bin/ $(DEMO_BINS) $(DEMO_DIR)/vart_ml_runner.py
