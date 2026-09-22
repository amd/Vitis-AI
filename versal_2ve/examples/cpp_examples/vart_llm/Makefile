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

.DEFAULT_GOAL:=all

SAMPLER_DIR := ../common/utils/llm_sampler
TOKENIZER_DIR := ../common/utils/llm_tokenizer
SAMPLER_LIB := $(SAMPLER_DIR)/lib/libllm_sampler.so
TOKENIZER_LIB := $(TOKENIZER_DIR)/lib/libllm_tokenizer.so

check_dependencies:
	@pkg-config --atleast-version=2.6 xrt || (echo "Error: xrt version 2.6 or later is required."; exit 1)
	@pkg-config --atleast-version=0.3 vart-ml || (echo "Error: vart-ml version 0.3 or later is required."; exit 1)

CFLAGS+= -Wall -Wextra -Wpedantic -Wunused -Wformat -Wdeprecated -Wdisabled-optimization -Wuninitialized -std=c++20
CFLAGS+= -I.
CFLAGS+= -I../common/include
CFLAGS+= -I$(TOKENIZER_DIR)/include
CFLAGS+= -I$(SAMPLER_DIR)/include
CFLAGS+= `pkg-config --cflags vart-ml`
CFLAGS+= `pkg-config --cflags xrt`

LDFLAGS+= -L$(TOKENIZER_DIR)/lib -lllm_tokenizer
LDFLAGS+= -L$(SAMPLER_DIR)/lib -lllm_sampler
LDFLAGS+= `pkg-config --libs vart-ml`
LDFLAGS+= `pkg-config --libs xrt`
LDFLAGS+= -lboost_program_options

SRCS:= main.cpp vart_llm.cpp

OBJDIR = build

TARGET = vart_llm

include ../common/install-app.mk

OBJS = $(addprefix $(OBJDIR)/, $(notdir $(SRCS:.cpp=.o)))

$(SAMPLER_LIB):
	$(MAKE) -C $(SAMPLER_DIR) LDFLAGS=

$(TOKENIZER_LIB):
	$(MAKE) -C $(TOKENIZER_DIR) LDFLAGS=

$(TARGET): check_dependencies $(SAMPLER_LIB) $(TOKENIZER_LIB) $(OBJS)
	$(CXX) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

all: $(TARGET)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	$(CXX) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(TARGET)

distclean:
	rm -f $(SDKTARGETSYSROOT)/usr/bin/vart_llm
	rm -f $(SDKTARGETSYSROOT)/usr/bin/.debug/vart_llm

.PHONY: all clean install distclean check_dependencies
