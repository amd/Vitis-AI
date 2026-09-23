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

ifeq ($(shell uname -m),aarch64)
	BASEDIR=$(VAISW_INSTALL_DIR)/vart_ml
	OPENCV=opencv4
	LDFLAGS=-L$(BASEDIR)/npu_runner -L$(BASEDIR)/io -L$(BASEDIR)/utils -L$(BASEDIR)/vart_ml_runner -L$(BASEDIR)/onnx_runner
else
	OPENCV=opencv
	LDFLAGS=-L$(VAISW_INSTALL_DIR)/lib/vaisw
endif

CPPFLAGS=-I.
CPPFLAGS+=-I..
CXXFILES=common.cpp
CXXFLAGS=$(shell pkg-config --cflags $(OPENCV)) $(CXXFILES) -Wall -Wextra -Werror -std=c++20 -O3 -mcpu=native -pthread
LDLIBS=$(shell pkg-config --libs $(OPENCV))
LDLIBS+=-lnpu_runner -lio -lutils -lvart_ml_runner -lonnx_runner

TARGETS:= connect simple_demo async_demo multi_models_demo multi_threads_demo vart_ml_demo hot_swap_demo

all: $(TARGETS)

clean:
	@$(RM) $(TARGETS) *.o

.PHONY: clean
