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

# in petalinux, OECORE_TARGET_ARCH will be set and in the native build
# it will not be set. We install only a single python version for petalinux.
ifdef OECORE_TARGET_ARCH

PY_VERSION := $(shell python3 -c 'import sys; print(f"{sys.version_info[0]}{sys.version_info[1]:02d}")')
py_LIB     := vart_ml.cpython-$(PY_VERSION)-$(OECORE_TARGET_ARCH)-linux-gnu.so
py_TARGET  := lib/python/$(py_LIB)

else

py_LIB     := vart_ml$(shell python3-config --extension-suffix)
py_TARGET  := lib/python/$(py_LIB)

endif #OECORE_TARGET_ARCH

ifneq ($(MAKECMDGOALS),install)

ifdef OECORE_TARGET_ARCH
PY_CPP_FLAGS := $(shell pkg-config --cflags python3)
else
PY_CPP_FLAGS := $(shell python3 -m pybind11 --includes)
endif

ifdef OECORE_TARGET_ARCH
TARGETS += $(py_TARGET)

$(py_TARGET): vart_ml_py_api/$(py_LIB)
	cp $< $@
endif

TARGETS += vart_ml_py_api/$(py_LIB)

py_OBJS := vart_ml_py_api/vart_ml_py_api.o

py_DEPS := $(py_OBJS:.o=.d)
-include $(py_DEPS)

$(py_OBJS): CXXFLAGS += -fPIC
$(py_OBJS): CPPFLAGS += $(PY_CPP_FLAGS)

vart_ml_py_api/$(py_LIB): LDFLAGS += -shared -Lvart_ml_runner -Lonnx_runner
vart_ml_py_api/$(py_LIB): LDLIBS  := -lio -lnpu_runner
vart_ml_py_api/$(py_LIB): LDLIBS  += -lvart_ml_runner -lonnx_runner
vart_ml_py_api/$(py_LIB): $(io_TARGETS) $(runner_TARGET) $(ONNX_TARGET) $(NPU_TARGET)
vart_ml_py_api/$(py_LIB): $(py_OBJS)
	$(CXX) $(LDFLAGS) $(py_OBJS) $(LDLIBS) -o $@

ifndef OECORE_TARGET_ARCH

# add installed python versions
ifeq ($(shell which python3.9-config &>/dev/null && echo OK), OK)
PY_VERS += 3.9
endif
ifeq ($(shell which python3.10-config &>/dev/null && echo OK), OK)
PY_VERS += 3.10
endif
ifeq ($(shell which python3.12-config &>/dev/null && echo OK), OK)
PY_VERS += 3.12
endif

define py_mod
py$(1)_OBJS := vart_ml_py_api/vart_ml_py_api.o.$(1)
py$(1)_LIB := vart_ml_py_api/vart_ml$(shell python$(1)-config --extension-suffix)
TARGETS += $$(py$(1)_LIB)

$$(py$(1)_OBJS): CXXFLAGS += -fPIC
$$(py$(1)_OBJS): CPPFLAGS += $(shell python$(1) -m pybind11 --includes)
$$(py$(1)_OBJS): vart_ml_py_api/%.o.$(1): vart_ml_py_api/%.cpp
	$(CXX) $$(CXXFLAGS) $$(CPPFLAGS) -c -o $$@ $$<

$$(py$(1)_LIB): LDFLAGS += -shared -Lvart_ml_runner -Lonnx_runner
$$(py$(1)_LIB): LDLIBS  := -lio -lnpu_runner
$$(py$(1)_LIB): LDLIBS  += -lvart_ml_runner -lonnx_runner
$$(py$(1)_LIB): $(io_TARGETS) $(runner_TARGET) $(ONNX_TARGET) $(NPU_TARGET)
$$(py$(1)_LIB): $$(py$(1)_OBJS)
	$(CXX) $$(LDFLAGS) $$(py$(1)_OBJS) $$(LDLIBS) -o $$@
endef
$(foreach ver,$(PY_VERS),$(eval $(call py_mod,$(ver))))

endif #OECORE_TARGET_ARCH
endif # install guard

INSTALL_DIR_PY := /etc/vai/lib/python
PREFIX_PY ?= $(INSTALL_DIR_PY)

install: install-py

install-py:
	install -D $(py_TARGET) $(DESTDIR)$(PREFIX_PY)/$(py_LIB)
	install -Dt $(DESTDIR)$(PREFIX_PY) lib/python/*.py
	install -D lib/python/vaisw_import/VaiswStub.py $(DESTDIR)$(PREFIX_PY)/vaisw_import/VaiswStub.py
	install -D lib/python/vaisw_import/mock_modules/wrappers.py $(DESTDIR)$(PREFIX_PY)/vaisw_import/mock_modules/wrappers.py
