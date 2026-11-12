# © 2021 Intel Corporation
#
# This software and the related documents are Intel copyrighted materials, and
# your use of them is governed by the express license under which they were
# provided to you ("License"). Unless the License provides otherwise, you may
# not use, modify, copy, publish, distribute, disclose or transmit this software
# or the related documents without Intel's prior written permission.
#
# This software and the related documents are provided as is, with no express or
# implied warranties, other than those that are expressly stated in the License.

# NOTE: This file is a copy of `cmake/cmake-wrapper.mk` from the Simics Base
#       package. Re-run project-setup with `--force` flag to replace it with a
#       new copy from Base.

# To use this GNU Make wrapping of CMake; add the following line to
# config-user.mk:
#     -include cmake-wrapper.mk
#
# To run with Ninja (default) and default GCC:
#     make
# To run with GNU Make and custom GCC in debug build:
#     TARGET_BUILD_SYSTEM='Unix Makefiles' CC=/usr/itm/gcc/11.1.0/bin/gcc CXX=/usr/itm/gcc/11.1.0/bin/g++ make D=1
# To reconfigure (using default settings, else pass custom settings as above):
#     make reconf
#

# Host-specific setup here, change as needed (or pass on cmdline as shown above)
CC	?= gcc
CXX	?= g++
CMAKE   ?= cmake
NINJA   ?= ninja
TARGET_BUILD_SYSTEM ?= Ninja

# Generic setup below this line, no need to change
HOST_DIR = linux64
ifeq ('$(D)','1')
    CMAKE_BUILD_TYPE = Debug
    CMAKE_BUILD_DIR = $(HOST_DIR)/bt-debug
else
    CMAKE_BUILD_TYPE = RelWithDebInfo
    CMAKE_BUILD_DIR = $(HOST_DIR)/bt-release
endif

ifeq ('$(V)','1')
    BUILD_FLAGS := --verbose
endif

MAKEOVERRIDES := $(filter-out V=% D=%,$(MAKEOVERRIDES))
CMAKE_FLAGS := $(patsubst %,-D%,$(MAKEOVERRIDES))
CMAKE_CONFIG := CC=$(CC) CXX=$(CXX) $(CMAKE) -G '$(TARGET_BUILD_SYSTEM)' -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) $(CMAKE_FLAGS)

# Prevent GNU Make from running normal builds/tests/generators
override MODULES=
override _TSUITES=
export MAKE_INVOKED_FROM_CMAKE=1

# =========== Build
.DEFAULT:
	$(CMAKE) --build $(CMAKE_BUILD_DIR) $(BUILD_FLAGS) --target $@

.PHONY: all
all: configure
	$(CMAKE) --build $(CMAKE_BUILD_DIR) $(BUILD_FLAGS) --target $@

# =========== Configure/reconfigure build tree
.PHONY: configure reconf
configure: $(CMAKE_BUILD_DIR)/CMakeCache.txt
	@echo ""
	@echo "NOTE: to change debug/release build, you must explicitly"
	@echo "      re-run 'make configure D=[0,1]'"
	@echo ""

reconf:
	rm -rf $(CMAKE_BUILD_DIR)
	$(MAKE) configure

.PRECIOUS: CMakeCache.txt
$(CMAKE_BUILD_DIR)/CMakeCache.txt:
	echo "Configuring build tree: $(CMAKE_BUILD_DIR)"
	$(CMAKE_CONFIG) -S . -B $(CMAKE_BUILD_DIR)

# ============ Glue/workarounds
build-hook.mk:
	touch build-hook.mk

# ============ Testing
.PHONY: test
test: all
	./bin/test-runner

test-%:
	./bin/test-runner /$*/

# ============ Clean
.PHONY: clean clean-local distclean
clean: clean-local

clean-local:
	$(CMAKE) --build $(CMAKE_BUILD_DIR) $(BUILD_FLAGS) --target clean

distclean:
	rm -rf $(HOST_DIR)
