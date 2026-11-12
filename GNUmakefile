# -*- Makefile ; coding: utf-8 -*-

# © 2015 Intel Corporation
#
# This software and the related documents are Intel copyrighted materials, and
# your use of them is governed by the express license under which they were
# provided to you ("License"). Unless the License provides otherwise, you may
# not use, modify, copy, publish, distribute, disclose or transmit this software
# or the related documents without Intel's prior written permission.
#
# This software and the related documents are provided as is, with no express or
# implied warranties, other than those that are expressly stated in the License.

# Do not edit. This file will be overwritten by the project setup script.

# V=1 selects verbose build output (the actual commands).
# For compatibility, we accept VERBOSE=yes as well.
ifeq ($(VERBOSE),yes)
    $(warning VERBOSE=yes is deprecated, use V=1 instead)
    V:=1
endif

.PHONY: default
default: all

.SUFFIXES:

# Check for MinGW Make 4.x or newer before trying to invoke cmd.exe
make_banner:=$(MAKE_HOST)
err_message=$(warning You need MinGW make 4.1 or newer in order to build modules.)    \
	    $(warning Try compiling with bin\make.bat or)		      \
            $(warning refer to the "Simics Model Builder User's Guide") \
            $(warning for more information.)				      \
            $(error Unsupported make version)

# Check for old unsupported version that may still be in use
ifeq ($(make_banner),)
    $(warning Your version of make is older than 4.0.)
    $(err_message)
endif

# We must use mingw make (msys or cygwin will not work)
ifneq ($(findstring mingw,$(make_banner)),mingw)
    $(warning make binary does not appear to be a MingW make.)
    $(err_message)
endif

export SHELL=cmd.exe

ifneq ($(V),1)
    MAKEFLAGS += -s
endif

# change all '/' to '\' to allow the path to be used in a pure Windows way
W_PATH = $(subst /,\,$(1))
# change all '\' to '/' to allow the path to be used with make file matching
S_PATH = $(subst \,/,$(1))

include config.mk

# Convert a path to something make can understand in include directives etc
makequote = $(subst $() ,\ ,$(1))
# Convert a path to something CMD.EXE can understand.
# We use a funny representation of the strings ( and ), because make's
# parser requires parentheses to be balanced within $(...)
_shellquote = $(subst %,%%,$(subst /,\,$(subst "",,$(subst $(firstword ( )),"$(firstword ( ))",$(subst $(lastword ( )),"$(lastword ( ))",$(subst $() ," ",$(1)))))))

# Function definitions are exported to module.mk.
export _MAKEQUOTE := $(value makequote)
export _SHELLQUOTE := $(value _shellquote)

SIMICS_BASE:=$(call _shellquote,$(RAW_SIMICS_BASE))
SIMICS_MODEL_BUILDER:=$(SIMICS_BASE)
_M_DODOC_PKG:=$(call makequote,$(RAW_DODOC_PKG))
DODOC_PKG:=$(call _shellquote,$(RAW_DODOC_PKG))
PYTHON_PKG:=$(call _shellquote,$(RAW_PYTHON_PKG))
export PYTHON_PKG

# For eager references in config-user.mk. config/project/config.mk
# will redefine the variable to something else, which will be used by
# lazy references in config-user.mk. Lazy references will not work if
# the project path contains spaces.
SIMICS_PROJECT:=.
_M_SIMICS_BASE := $(call makequote,$(RAW_SIMICS_BASE))
_M_PYTHON_PKG := $(call makequote,$(RAW_PYTHON_PKG))

ifeq ($(SIMICS_PYTHON),)
MINIPYTHONPATH := $(RAW_SIMICS_BASE)/win64;$(RAW_PYTHON_PKG)/win64/bin/py3
export MINIPYTHONPATH
_RAW_PYTHON := $(wildcard $(_M_PYTHON_PKG)/win64/bin/py3/mini-python.exe)
$(if $(_RAW_PYTHON),,$(error mini-python not found))
PYTHON := $(call _shellquote,$(_RAW_PYTHON))
else
PYTHONPATH := $(RAW_SIMICS_BASE)/win64;$(RAW_SIMICS_BASE)/win64/bin/py3
export PYTHONPATH
PYTHON := $(SIMICS_PYTHON)
endif
PYTHON3 := $(PYTHON)

_SYSTEMC_DML_PACKAGE:=$(call _shellquote,$(RAW_SYSTEMC_DML_PACKAGE))
export _SYSTEMC_DML_PACKAGE

DMLC_DIR ?= $(SIMICS_BASE)/$(HOST_TYPE)/bin
DMLC ?= $(PYTHON) $(DMLC_DIR)\dml\python

# Put user definitions in config-user.mk
-include config-user.mk

include compiler.mk

include $(_M_SIMICS_BASE)/config/project/config.mk.win32

ifeq ($(ENVCHECK),disable)
    ENVCHECK_FLAG=
else
    include $(_M_SIMICS_BASE)/config/project/envcheck.mk.win32
    ENVCHECK_FLAG:=$(HOST_TYPE)/.environment-check/all
endif

_TEST_RUNNER := bin\test-runner.bat

# Remove a file
_rm = $(if $(wildcard $(1)),$(PYTHON) $(SIMICS_BASE)\scripts\build\rm_or_move.py $(1))
# Remove a directory, expressed as a relative path
_rm_r = $(if $(wildcard $(1)),rmdir /s /q $(subst /,\,$(wildcard $(1))))

include $(_M_SIMICS_BASE)/config/project/toplevel-rules.mk
