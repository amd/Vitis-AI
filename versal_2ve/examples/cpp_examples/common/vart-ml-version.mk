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
# Minimum vart-ml version required by the apps that link vart-ml.
#
# Include this file near the top of such an app's Makefile:
#
#     include ../common/vart-ml-version.mk
#
# and reference the variable in check_dependencies:
#
#     @pkg-config --atleast-version=$(VART_ML_MIN_VERSION) vart-ml || ...
#
# Only apps that link vart-ml include this file; apps built against other
# libraries declare their own minimums. Keeping one file per dependency
# avoids implying a dependency an app does not have.
#
# Apps are built both from the cpp_examples directory (make, which runs
# $(MAKE) -C <app>) and directly from an app folder. Make runs with the app
# directory as the working directory in both cases, so the relative include
# resolves the same way. The value uses ?= so an environment or command-line
# override wins:
#
#     make VART_ML_MIN_VERSION=0.5.0

# Bump this when the apps start depending on a newer VART-ML API or ABI.
VART_ML_MIN_VERSION ?= 0.4.0
