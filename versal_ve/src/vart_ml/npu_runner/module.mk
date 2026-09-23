# Copyright 2026 Advanced Micro Devices Inc.
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

runner_LIB := libnpu_runner.so

runner_TARGET := npu_runner/$(runner_LIB)
TARGETS  += $(runner_TARGET)

runner_SRCS := $(wildcard npu_runner/*.cpp)
runner_OBJS := $(patsubst %.cpp,%.o,$(runner_SRCS))

runner_DEPS := $(runner_OBJS:.o=.d)
-include $(runner_DEPS)

$(runner_OBJS): CXXFLAGS += -fPIC

$(runner_TARGET): LDFLAGS += -shared
$(runner_TARGET): LDLIBS  := -lio -lutils
$(runner_TARGET): $(io_TARGETS) $(utils_TARGET)
$(runner_TARGET): $(runner_OBJS)
	$(CXX) $(LDFLAGS) $(runner_OBJS) $(LDLIBS) -o $@

install: install-c-lib

install-c-lib:
	install -D $(runner_TARGET) $(DESTDIR)$(PREFIX)/lib/$(runner_LIB)
	install -D npu_runner/npu_runner.h $(DESTDIR)$(PREFIX)/include/npu_runner/npu_runner.h
