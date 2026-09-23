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

io_DIR := io

# Source files, excluding pci.cpp
io_SRCS := $(filter-out $(io_DIR)/pci.cpp, $(wildcard $(io_DIR)/*.c*))

ifdef DISABLE_XRT
io_SRCS := $(filter-out $(io_DIR)/xrt.cpp,$(io_SRCS))
endif

# List of libraries to be created (remove .cpp extension)
io_TARGETS := $(patsubst $(io_DIR)/%.cpp,$(io_DIR)/lib%.so,$(io_SRCS))
io_TARGETS := $(patsubst $(io_DIR)/%.c,$(io_DIR)/lib%.so,$(io_TARGETS))

# Add the io libraries to the global TARGETS
TARGETS += $(io_TARGETS)

io_OBJS := $(patsubst %.cpp,%.o,$(patsubst %.c,%.o,$(io_SRCS)))

io_DEPS := $(io_OBJS:.o=.d)
-include $(io_DEPS)

ifneq ($(MAKECMDGOALS),install)
$(io_OBJS):    CXXFLAGS += -fPIC

$(io_TARGETS): CFLAGS   += -fPIC $(shell pkg-config --cflags libudev)
$(io_TARGETS): LDFLAGS  += -shared
$(io_TARGETS): LDLIBS    = -ldl $(shell pkg-config --libs libudev) -lutils

# If XRT is available and not disabled, enable it
ifndef DISABLE_XRT
ifeq ($(shell pkg-config --exists xrt && echo yes || echo no), yes)
$(io_OBJS):    CXXFLAGS += -DNPU_XRT_ENABLE $(shell pkg-config --cflags xrt) -isystem ${SDKTARGETSYSROOT}/usr/include/xrt
$(io_TARGETS): LDLIBS   += -lxrt_coreutil
endif
endif #DISABLE_XRT

$(io_TARGETS): $(utils_TARGET)
$(io_TARGETS): $(io_DIR)/lib%.so: $(io_DIR)/%.o
	$(LINK.c) $< $(LDLIBS) -o $@
endif

install: install-io

install-io:
	install -d $(DESTDIR)$(PREFIX)/lib/
	install $(io_TARGETS) $(DESTDIR)$(PREFIX)/lib/
	install -D $(io_DIR)/io.h $(DESTDIR)$(PREFIX)/include/$(io_DIR)/io.h
