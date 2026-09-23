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

tools_BIN    := vart_ml_tools
tools_TARGET := vart_ml_tools/$(tools_BIN)
TARGETS      += $(tools_TARGET)

tools_OBJS := vart_ml_tools/tester.o
tools_OBJS += vart_ml_tools/ddr_poke.o
tools_OBJS += vart_ml_tools/ctrlbus_poke.o
tools_OBJS += vart_ml_tools/uploader.o
tools_OBJS += vart_ml_tools/downloader.o
tools_OBJS += vart_ml_tools/clocks.o
tools_OBJS += vart_ml_tools/vart_ml_tools.o
tools_OBJS += vart_ml_tools/ddr_debug.o

tools_DEPS := $(tools_OBJS:.o=.d)
-include $(tools_DEPS)

$(tools_TARGET): LDLIBS := -lnpu_runner -lio -lutils
$(tools_TARGET): $(utils_TARGET) $(io_TARGETS) $(runner_TARGET)
$(tools_TARGET): $(tools_OBJS)
	$(CXX) $(LDFLAGS) $(tools_OBJS) $(LDLIBS) -o $@

install: install-tools

install-tools:
	install -D $(tools_TARGET) $(DESTDIR)$(PREFIX)/bin/vart_ml_tools
