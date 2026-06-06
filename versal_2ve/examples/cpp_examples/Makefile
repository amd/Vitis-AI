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
.DEFAULT_GOAL:= default

default: install-pre-commit-hook all

# Source of the pre-commit hook
HOOK_SRC := ../../githooks/pre-commit.hook

# Destination in the repo
HOOK_DEST := ../../.git/hooks/pre-commit

# Install the pre-commit hook
install-pre-commit-hook:
	@echo "Installing pre-commit hook..."
	@if [ -f $(HOOK_SRC) ]; then \
	    cp $(HOOK_SRC) $(HOOK_DEST); \
	    chmod +x $(HOOK_DEST); \
	    echo "Pre-commit hook installed successfully."; \
	fi


# All leaf apps with a local Makefile and an `install` target are built and packaged
# into install/applications.tar.gz (see each subdir and common/install-app.mk where used).
# Placeholder directories (TBD, no sources yet; see each README): vart_infer_async, vart_ml_async.
SUBDIRS := ml_ort ml_vart x_plus_ml_ort x_plus_ml_vart spatial_mt_ml_ort vart_multimodel_seq vart_multi_tenancy vart_zerocopy vart_infer_async
LIBSUBDIRS := common/utility_timer
COMMON_UTILS_DIR := common/utils

.PHONY: all clean install package libs apps $(SUBDIRS) $(LIBSUBDIRS)

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

install: apps
	@for dir in $(LIBSUBDIRS); do \
		$(MAKE) -C $$dir install; \
	done
	@$(MAKE) -C $(COMMON_UTILS_DIR) install
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir install; \
	done

all: install
	@cd install && tar -czvf applications.tar.gz usr etc
