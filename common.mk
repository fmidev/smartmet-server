-include $(HOME)/.smartmet.mk

bindir = $(PREFIX)/bin
includedir = $(PREFIX)/include
datadir = $(PREFIX)/share
objdir = obj

GCC_DIAG_COLOR ?= always

ifneq ($(findstring clang++,$(CXX)),)
USE_CLANG=yes
CXX_STD ?= c++17
else
USE_CLANG=no
CXX_STD ?= c++11
endif

processor := $(shell uname -p)

ifeq ($(origin PREFIX), undefined)
  PREFIX = /usr
else
  PREFIX = $(PREFIX)
endif

ifeq ($(processor), x86_64)
  libdir = $(PREFIX)/lib64
else
  libdir = $(PREFIX)/lib
endif

bindir = $(PREFIX)/bin
includedir = $(PREFIX)/include
datadir = $(PREFIX)/share
objdir = obj

ifeq ($(PREFIX),/usr)
  SYSTEM_INCLUDES =
else
  SYSTEM_INCLUDES = -isystem $(includedir)
endif

ifneq "$(wildcard /usr/include/boost169)" ""
  INCLUDES += -isystem /usr/include/boost169
  LIBS += -L/usr/lib64/boost169
endif
