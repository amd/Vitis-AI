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

utils_LIB := libutils.so

utils_TARGET := utils/$(utils_LIB)
TARGETS      += $(utils_TARGET)

utils_SRCS := $(wildcard utils/*.cpp)
utils_OBJS := $(patsubst %.cpp,%.o,$(utils_SRCS))

utils_DEPS := $(utils_OBJS:.o=.d)
-include $(utils_DEPS)

$(utils_TARGET): CXXFLAGS += -fPIC
$(utils_TARGET): LDFLAGS  += -shared
$(utils_TARGET): LDLIBS   := -lpthread -ldl
$(utils_TARGET): LDLIBS   += -Wl,--whole-archive -Wl,--no-whole-archive
$(utils_TARGET): $(utils_OBJS)
	$(LINK.cpp) $^ $(LDLIBS) -o $@

install: install-utils-lib

install-utils-lib:
	install -D $(utils_TARGET) $(DESTDIR)$(PREFIX)/lib/$(utils_LIB)
