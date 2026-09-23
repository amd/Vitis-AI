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

NPU_LIB    := libvart_ml_runner.so
NPU_TARGET := vart_ml_runner/$(NPU_LIB)
TARGETS    += $(NPU_TARGET)

NPU_SRCS := $(wildcard vart_ml_runner/*.cpp)
NPU_OBJS := $(patsubst %.cpp,%.o,$(NPU_SRCS))

NPU_DEPS := $(NPU_OBJS:.o=.d)
-include $(NPU_DEPS)

$(NPU_OBJS): CXXFLAGS += -fPIC

$(NPU_TARGET): LDFLAGS += -shared -Lonnx_runner
$(NPU_TARGET): LDLIBS  := -lonnx_runner -lnpu_runner
$(NPU_TARGET): $(runner_TARGET) $(ONNX_TARGET)
$(NPU_TARGET): $(NPU_OBJS)
	$(CXX) $(LDFLAGS) $(NPU_OBJS) $(LDLIBS) -o $@

install: install-vart-ml-lib

install-vart-ml-lib:
	install -D $(NPU_TARGET) $(DESTDIR)$(PREFIX)/lib/$(NPU_LIB)
	install -D vart_ml_runner/vart_runner_factory.hpp $(DESTDIR)$(PREFIX)/include/vart_ml_runner/vart_runner_factory.hpp
