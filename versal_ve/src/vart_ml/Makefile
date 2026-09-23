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

SHELL := /bin/bash

.DEFAULT_GOAL := all

CONFIG_FILE := .vart_ml_config.mk

# make install: apply same compile-time options stored in $(CONFIG_FILE)
ifeq ($(MAKECMDGOALS),install)
-include $(CONFIG_FILE)
endif

MODULES := utils
MODULES += io
MODULES += npu_runner
MODULES += vart_ml_tools
MODULES += onnx_runner
MODULES += vart_ml_runner

ifndef DISABLE_PYTHON
MODULES += vart_ml_py_api
endif

ifndef DISABLE_DEMO
MODULES += demo
endif

VPATH   := $(MODULES)
INCLUDE := $(MODULES) third_party

INSTALL_DIR = /usr
PREFIX ?= $(INSTALL_DIR)

CPPFLAGS += -I.
CPPFLAGS += $(foreach d,$(INCLUDE),-I$(d))

OPTLEVEL ?= -O3
CFLAGS   += -Wall -Wextra -Werror -MMD -MP $(OPTLEVEL) $(EXTRA_CFLAGS)
CXXFLAGS += -Wall -Wextra -Werror -std=c++20 -MMD -MP $(OPTLEVEL) $(EXTRA_CXXFLAGS)

# XXX: Earliest version of OpenCV on ubuntu22 (4.5.4) does not compile with c++20 without this flag.
CXXFLAGS+=-Wno-deprecated-enum-enum-conversion

# Add mcpu flag if missing
ifeq (,$(findstring mcpu, $(CC)))
CFLAGS   += -mcpu=native
CXXFLAGS += -mcpu=native
endif

LDFLAGS += -Lio -Lnpu_runner -Lutils
LDFLAGS += -Wl,-rpath,'$$ORIGIN/../npu_runner' -Wl,-rpath,'$$ORIGIN/../io'
LDFLAGS += -Wl,-rpath,'$$ORIGIN/../utils'

$(foreach module,$(MODULES),$(eval include $(module)/module.mk))

debug: OPTLEVEL := -Og
debug: CFLAGS   += -g
debug: CXXFLAGS += -g
debug: CPPFLAGS += -DDEBUG
debug: all

asan: CFLAGS   += -fsanitize=address
asan: CXXFLAGS += -fsanitize=address
asan: LDFLAGS  += -fsanitize=address
asan: debug

all: $(TARGETS) saveconfig

saveconfig:
	$(file >$(CONFIG_FILE),$(MAKEOVERRIDES))

clean-install:
	$(RM) -r vart_ml_install
	$(RM) vart_ml_install.tar.gz
	$(RM) $(CONFIG_FILE)

clean: clean-install
	$(RM) $(TARGETS) **/*.so **/*.o **/*.o.3.9 **/*.o.3.10 **/*.o.3.12 **/*.d

install:

install-sdk:
	DESTDIR=$(SDKTARGETSYSROOT) $(MAKE) install

install-tar: all clean-install
	DESTDIR=vart_ml_install $(MAKE) install
	install -D vai.sh vart_ml_install/etc/vai.sh
	./update_vai.sh vart_ml_install/etc/vai.sh
ifndef DISABLE_XRT
	install -D xrt.ini vart_ml_install/etc/vai/xrt.ini
endif
	tar -zcvf vart_ml_install.tar.gz -C vart_ml_install/ usr etc

.PHONY: all saveconfig clean-install clean install install-sdk install-tar
