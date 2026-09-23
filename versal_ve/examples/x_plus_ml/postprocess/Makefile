.DEFAULT_GOAL:=all

# Library versioning — matches vvas-core project version (1.0)
CORE_VERSION = 1.0

# Shared library output names
RESNET50_LIB = libpostprocess_resnet50-$(CORE_VERSION).so
YOLO_LIB     = libpostprocess_yolo-$(CORE_VERSION).so

# Sources
RESNET50_SRC = postprocess_resnet50.cpp
RESNET50_OBJ = $(RESNET50_SRC:.cpp=.o)

# yolo (generic YOLO/YOLOX) postprocess; self-contained single source
YOLO_SRC = yolox/postprocess_yolox.cpp
YOLO_OBJ = $(YOLO_SRC:.cpp=.o)

check_dependencies:
	@pkg-config --exists vvas-core || (echo "Error: vvas-core pkg-config not found."; exit 1)
	@pkg-config --exists glib-2.0 || (echo "Error: glib-2.0 pkg-config not found."; exit 1)

# Flags common to every postprocess library
COMMON_CFLAGS = -fPIC -DVVAS_GLIB_UTILS -std=c++23
COMMON_CFLAGS += `pkg-config --cflags vvas-core`
COMMON_CFLAGS += `pkg-config --cflags glib-2.0`

LDFLAGS += -shared
LDFLAGS += `pkg-config --libs vvas-core`
LDFLAGS += `pkg-config --libs glib-2.0`

# resnet50: original strict flag set
RESNET50_CFLAGS = $(COMMON_CFLAGS) -Wall -Wextra -Wpedantic -Wunused -Wformat \
                  -Wdeprecated -Wdisabled-optimization -Wuninitialized

# yolo: -Wno-psabi for the float16/bfloat16 codepaths
YOLO_CFLAGS = $(COMMON_CFLAGS) -Wall -Wextra -Wno-psabi

$(RESNET50_OBJ): %.o: %.cpp
	$(CXX) $(RESNET50_CFLAGS) -c $< -o $@

$(RESNET50_LIB): $(RESNET50_OBJ)
	$(CXX) $(RESNET50_CFLAGS) -o $@ $(RESNET50_OBJ) $(LDFLAGS)

$(YOLO_OBJ): %.o: %.cpp
	$(CXX) $(YOLO_CFLAGS) -c $< -o $@

$(YOLO_LIB): $(YOLO_OBJ)
	$(CXX) $(YOLO_CFLAGS) -o $@ $(YOLO_OBJ) $(LDFLAGS)

all: check_dependencies $(RESNET50_LIB) $(YOLO_LIB)

install:
	@echo "Installing $(RESNET50_LIB) to $(SDKTARGETSYSROOT)/usr/lib/"
	cp $(RESNET50_LIB) $(SDKTARGETSYSROOT)/usr/lib/
	@echo "Installing $(YOLO_LIB) to $(SDKTARGETSYSROOT)/usr/lib/"
	cp $(YOLO_LIB) $(SDKTARGETSYSROOT)/usr/lib/

clean:
	rm -f $(RESNET50_OBJ) $(RESNET50_LIB) $(YOLO_OBJ) $(YOLO_LIB)

distclean:
	rm -f $(SDKTARGETSYSROOT)/usr/lib/$(RESNET50_LIB)
	rm -f $(SDKTARGETSYSROOT)/usr/lib/$(YOLO_LIB)

.PHONY: all check_dependencies clean install distclean
