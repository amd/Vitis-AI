#
# Copyright 2025 Advanced Micro Devices Inc.
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

ONNX_LIB    := libonnx_runner.so
ONNX_TARGET := onnx_runner/$(ONNX_LIB)
TARGETS     += $(ONNX_TARGET)

ifneq ($(MAKECMDGOALS),install)
# Check if onnxrt is installed
ONNXRT_INSTALLED := $(shell $(LD) -Lthird_party/onnxruntime/ -lonnxruntime -o /dev/null &>/dev/null \
&& echo '#include <onnxruntime/onnxruntime_c_api.h>' | $(CPP) -E -Ithird_party/ -o /dev/null &>/dev/null - && echo OK)

# If onnxrt is installed, compile onnx_runner.cpp, else compile a fallback
ifeq ($(ONNXRT_INSTALLED), OK)
ONNX_OBJS := onnx_runner/onnx_runner.o
else
$(info Could not find Onnxruntime locally. It will not be possible to run ONNX nodes.)
$(info To run ONNX nodes, please install the latest version of Onnxruntime from \
https://github.com/microsoft/onnxruntime/releases.)
ONNX_OBJS := onnx_runner/onnx_runner_fallback.o
endif

ONNX_DEPS := $(ONNX_OBJS:.o=.d)
-include $(ONNX_DEPS)

$(ONNX_OBJS): CXXFLAGS += -fPIC -Ithird_party/onnxruntime/

# If onnxrt is installed, link against it
ifeq ($(ONNXRT_INSTALLED), OK)
$(ONNX_TARGET): LDFLAGS += -Lthird_party/onnxruntime -Wl,-rpath,'$$ORIGIN/../third_party/onnxruntime'
$(ONNX_TARGET): LDLIBS  := -lonnxruntime
endif

$(ONNX_TARGET): LDFLAGS += -shared
$(ONNX_TARGET): $(utils_TARGET)
$(ONNX_TARGET): $(ONNX_OBJS)
	$(CXX) $(LDFLAGS) $(ONNX_OBJS) $(LDLIBS) -o $@
endif

install: install-onnx-lib

install-onnx-lib:
	install -D $(ONNX_TARGET) $(DESTDIR)$(PREFIX)/lib/$(ONNX_LIB)
	install -D onnx_runner/onnx_runner.h $(DESTDIR)$(PREFIX)/include/onnx_runner/onnx_runner.h
