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

check_dependencies:
	@pkg-config --atleast-version=0.1 vart-ml || (echo "Error: vart-ml version 0.1 or later is required."; exit 1)

CFLAGS+= -Wall -Wextra -Wpedantic -Wunused -Wformat -Wdeprecated -Wdisabled-optimization -Wuninitialized -std=c++17
CFLAGS+= `pkg-config --cflags vart-ml`
CFLAGS+= -I../common/include

LDFLAGS+= `pkg-config --libs vart-ml`
LDFLAGS+= -lboost_program_options

SRCS:=  main.cpp vart_multi_tenancy.cpp

OBJDIR = build

# Target executable
TARGET = vart_multi_tenancy

include ../common/install-app.mk

OBJS = $(addprefix $(OBJDIR)/, $(notdir $(SRCS:.cpp=.o)))

# Linking the executable from object files
$(TARGET): $(OBJS)
	$(CXX) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

all: check_dependencies $(TARGET)

# Ensure build directory exists
$(OBJDIR):
	mkdir -p $(OBJDIR)

# Pattern rule for sources in current directory
$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	$(CXX) $(CFLAGS) -c $< -o $@

# Clean target to remove generated files
clean:
	rm -rf $(OBJDIR)  $(TARGET)


distclean:
	rm -f $(SDKTARGETSYSROOT)/usr/bin/vart_multi_tenancy
	rm -f $(SDKTARGETSYSROOT)/usr/bin/.debug/vart_multi_tenancy

# Phony targets
.PHONY: clean install distclean

