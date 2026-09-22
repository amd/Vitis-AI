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
# Shared install rules for cpp_examples apps (ml_ort, *_ml_*, etc.).
#
# Prerequisites: define TARGET (executable basename) in the parent Makefile
# before including this file. Optional: INSTALL_ROOT (default ../install).

ifndef TARGET
$(error TARGET must be set before including install-app.mk)
endif

INSTALL_ROOT ?= ../install
INSTALL_BIN = $(INSTALL_ROOT)/usr/bin
INSTALL_VAI_DEP_FILES = $(INSTALL_ROOT)/etc/vai/$(TARGET)

install: $(TARGET)
	@mkdir -p $(INSTALL_BIN) $(INSTALL_VAI_DEP_FILES)/json_configs
	@cp $(TARGET) $(INSTALL_BIN)/
	@if [ -d json_configs ]; then \
		for f in json_configs/*.json; do \
			[ -f "$$f" ] || continue; \
			cp "$$f" $(INSTALL_VAI_DEP_FILES)/json_configs/; \
		done; \
	fi
	@if [ -d labels ] && ls labels/*.txt 1> /dev/null 2>&1; then \
		mkdir -p $(INSTALL_VAI_DEP_FILES)/labels; \
		cp labels/*.txt $(INSTALL_VAI_DEP_FILES)/labels/; \
	fi
	@if [ -d data ] && ls data/*.bin 1> /dev/null 2>&1; then \
		mkdir -p $(INSTALL_VAI_DEP_FILES)/data; \
		cp data/*.bin $(INSTALL_VAI_DEP_FILES)/data/; \
	fi
