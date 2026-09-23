.DEFAULT_GOAL:=all

check_dependencies:
	@pkg-config --atleast-version=4.2.0 opencv4 || (echo "Error: opencv4 version 4.2.0 or later is required."; exit 1)
	@pkg-config --atleast-version=2.6 xrt || (echo "Error: xrt version 2.6 or later is required."; exit 1)
	@pkg-config --atleast-version=0.3 vart-x || (echo "Error: vart-x version 0.3 or later is required."; exit 1)
	@pkg-config --exists vvas-core || (echo "Error: vvas-core pkg-config not found."; exit 1)

CFLAGS+= -Wall -Wextra -Wpedantic -Wunused -Wformat -Wdeprecated -Wdisabled-optimization -Wuninitialized -std=c++17
CFLAGS+= `pkg-config --cflags vart-x`
CFLAGS+= `pkg-config --cflags opencv4`
CFLAGS+= `pkg-config --cflags xrt`
CFLAGS+= `pkg-config --cflags vvas-core`
CFLAGS+= -DVVAS_GLIB_UTILS

LDFLAGS+= `pkg-config --libs vart-x`
LDFLAGS+= `pkg-config --libs opencv4`
LDFLAGS+= `pkg-config --libs xrt`
LDFLAGS+= `pkg-config --libs vartml-sw`
LDFLAGS+= -lvvascore_logger-1.0
LDFLAGS+= `pkg-config --libs vvas-core`

SRCS:= x_plus_ml_app.cpp video_frame_pool.cpp video_input_output.cpp vart_context.cpp pre_process.cpp inference.cpp post_process.cpp postprocess/vvas_postprocess_bridge.cpp

# Target executable
TARGET = x_plus_ml_app

OBJS = $(SRCS:.cpp=.o)

# Linking the executable from object files
$(TARGET): $(OBJS)
	$(CXX) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

# Build the postprocess shared library plugin
postprocess:
	$(MAKE) -C postprocess

all: check_dependencies $(TARGET) postprocess

install:
	@echo "Installing $(TARGET) to $(SDKTARGETSYSROOT)"
	cp $(TARGET) $(SDKTARGETSYSROOT)/usr/bin/
	$(MAKE) -C postprocess install

# Compiling source files into object files
%.o: %.cpp
	$(CXX) $(CFLAGS) -c $< -o $@

# Clean target to remove generated files
clean:
	rm -f $(OBJS) $(TARGET)
	$(MAKE) -C postprocess clean

distclean:
	rm -f $(SDKTARGETSYSROOT)/usr/bin/$(TARGET)
	rm -f $(SDKTARGETSYSROOT)/usr/bin/.debug/$(TARGET)
	$(MAKE) -C postprocess distclean

# Phony targets
.PHONY: clean install distclean postprocess
