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
.DEFAULT_GOAL:= all

# All leaf apps with a local Makefile and an `install` target are built and packaged
# into install/applications.tar.gz (see each subdir and common/install-app.mk where used).
SUBDIRS := ml_ort ml_vart vart_llm x_plus_ml_ort x_plus_ml_vart spatial_mt_ml_ort vart_multimodel_seq vart_multi_tenancy vart_zerocopy vart_infer_async
LIBSUBDIRS := common/utility_timer common/utils/llm_sampler common/utils/llm_tokenizer

COMMON_UTILS_DIR := common/utils

# Python example scripts installed to install/etc/vai/python/
PYTHON_EXAMPLES_DIR := ../python_examples
INSTALL_VAI_PYTHON := install/etc/vai/python

.PHONY: all clean install package libs apps $(SUBDIRS) $(LIBSUBDIRS) install-python-examples

libs: $(LIBSUBDIRS)

$(LIBSUBDIRS):
	$(MAKE) -C $@

apps: libs $(SUBDIRS)

$(SUBDIRS): libs
	$(MAKE) -C $@

clean:
	for dir in $(LIBSUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done
	rm -rf install applications.tar.gz

install-python-examples:
	@if [ -d $(PYTHON_EXAMPLES_DIR) ]; then \
		mkdir -p $(INSTALL_VAI_PYTHON); \
		for f in $(PYTHON_EXAMPLES_DIR)/*.py; do \
			[ -f "$$f" ] || continue; \
			cp "$$f" $(INSTALL_VAI_PYTHON)/; \
		done; \
		echo "Installed python example scripts to $(INSTALL_VAI_PYTHON)"; \
	else \
		echo "$(PYTHON_EXAMPLES_DIR) not found, skipping python example install"; \
	fi

install: apps
	@for dir in $(LIBSUBDIRS); do \
		$(MAKE) -C $$dir install; \
	done
	@$(MAKE) -C $(COMMON_UTILS_DIR) install
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir install; \
	done
	@$(MAKE) install-python-examples

all: install
	@cd install && tar -czvf applications.tar.gz usr etc
