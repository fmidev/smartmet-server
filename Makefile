MODULE = smartmet-server
SPEC = smartmet-server

-include $(HOME)/.smartmet.mk
GCC_DIAG_COLOR ?= always

FLAGS = -MD -Wall -W -Wno-unused-parameter -std=c++11 -fdiagnostics-color=$(GCC_DIAG_COLOR)

# mdsplib does not declare things correctly

FLAGS += -fpermissive


EXTRAFLAGS = \
	-Werror \
	-Winline \
	-Wpointer-arith \
	-Wcast-qual \
	-Wcast-align \
	-Wwrite-strings \
	-Wnon-virtual-dtor \
	-Wno-pmf-conversions \
	-Wsign-promo \
	-Wchar-subscripts \
	-Wredundant-decls \
	-Woverloaded-virtual

DIFFICULTFLAGS = \
	-Wunreachable-code \
	-Wconversion \
	-Wctor-dtor-privacy \
	-Weffc++ \
	-Wold-style-cast \
	-pedantic \
	-Wshadow

ifeq ($(TSAN), yes)
  FLAGS += -fsanitize=thread
endif
ifeq ($(ASAN), yes)
  FLAGS += -fsanitize=address -fsanitize=pointer-compare -fsanitize=pointer-subtract -fsanitize=undefined -fsanitize-address-use-after-scope
endif

CC = g++

# Default compiler flags

DEFINES = -DUNIX

CFLAGS = $(DEFINES) -O2 -g -DNDEBUG $(FLAGS)
override LDFLAGS += -rdynamic

# Special modes

CFLAGS_DEBUG = $(DEFINES) -O0 -g $(FLAGS) $(EXTRAFLAGS) -Werror
CFLAGS_PROFILE = $(DEFINES) -O2 -g -pg -DNDEBUG $(FLAGS)

override LDFLAGS_DEBUG += -rdynamic
override LDFLAGS_PROFILE += -rdynamic

INCLUDES = -I$(includedir) \
	-I$(includedir)/smartmet \
	`pkg-config --cflags libconfig++`

LIBS = -L$(libdir) \
	-lsmartmet-spine \
	-lsmartmet-macgyver \
	`pkg-config --libs libconfig++` \
	-ldl \
	-lboost_filesystem \
	-lboost_regex \
	-lboost_date_time \
	-lboost_iostreams \
	-lboost_program_options \
	-lboost_thread \
	-lboost_system \
	-lfmt \
	-lz -lpthread \
	-ldw

ifneq (,$(findstring sanitize=address,$(CFLAGS)))
else
ifneq (,$(findstring sanitize=thread,$(CFLAGS)))
else
  LIBS += -ljemalloc-debug
endif
endif


# Common library compiling template

# Installation directories

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

objdir = obj
includedir = $(PREFIX)/include

ifeq ($(origin SBINDIR), undefined)
  sbindir = $(PREFIX)/sbin
else
  sbindir = $(SBINDIR)
endif

ifeq ($(origin DATADIR), undefined)
  datadir = $(PREFIX)/share
else
  datadir = $(DATADIR)
endif

# Special modes

ifneq (,$(findstring debug,$(MAKECMDGOALS)))
  CFLAGS = $(CFLAGS_DEBUG)
  LDFLAGS = $(LDFLAGS_DEBUG)
endif

ifneq (,$(findstring profile,$(MAKECMDGOALS)))
  CFLAGS = $(CFLAGS_PROFILE)
  LDFLAGS = $(LDFLAGS_PROFILE)
endif

# Compilation directories

vpath %.cpp source main
vpath %.h include
vpath %.o $(objdir)
vpath %.d $(objdir)

# How to install

INSTALL_PROG = install -m 775
INSTALL_DATA = install -m 664

# The files to be compiled

HDRS = $(patsubst include/%,%,$(wildcard *.h include/*.h))

MAINSRCS     = $(patsubst main/%,%,$(wildcard main/*.cpp))
MAINPROGS    = $(MAINSRCS:%.cpp=%)
MAINOBJS     = $(MAINSRCS:%.cpp=%.o)
MAINOBJFILES = $(MAINOBJS:%.o=obj/%.o)

SRCS     = $(patsubst source/%,%,$(wildcard source/*.cpp))
OBJS     = $(SRCS:%.cpp=%.o)
OBJFILES = $(OBJS:%.o=obj/%.o)

INCLUDES := -Iinclude $(INCLUDES)

# For make depend:

ALLSRCS = $(wildcard main/*.cpp source/*.cpp)

.PHONY: test rpm

# The rules

all: objdir $(MAINPROGS)
debug: objdir $(MAINPROGS)
release: objdir $(MAINPROGS)
profile: objdir $(MAINPROGS)

.SECONDEXPANSION:
$(MAINPROGS): % : obj/%.o $(OBJFILES)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ obj/$@.o $(OBJFILES) $(LIBS)

clean:
	rm -f $(MAINPROGS) source/*~ include/*~
	rm -rf obj
	rm -f test/suites/*

format:
	clang-format -i -style=file include/*.h source/*.cpp main/*.cpp

install:
	@mkdir -p $(sbindir)
	@list='$(MAINPROGS)'; \
	for prog in $$list; do \
	  echo $(INSTALL_PROG) $$prog $(sbindir)/$$prog; \
	  $(INSTALL_PROG) $$prog $(sbindir)/$$prog; \
	done
	@mkdir -p $(sysconfdir)/smartmet
	@mkdir -p $(sysconfdir)/logrotate.d
	$(INSTALL_DATA) etc/smartmet-server-access-log-rotate $(sysconfdir)/logrotate.d/smartmet-server
	$(INSTALL_DATA) etc/smartmetd.env $(sysconfdir)/smartmet/smartmetd.env
	@mkdir -p $(libdir)/../lib/systemd/system
	$(INSTALL_DATA) systemd/smartmet-server.service $(libdir)/../lib/systemd/system/


test:
	cd test && make test

objdir:
	@mkdir -p $(objdir)

rpm: clean $(SPEC).spec
	rm -f $(SPEC).tar.gz # Clean a possible leftover from previous attempt
	tar -czvf $(SPEC).tar.gz --exclude test --exclude-vcs --transform "s,^,$(SPEC)/," *
	rpmbuild -ta $(SPEC).tar.gz
	rm -f $(SPEC).tar.gz

.SUFFIXES: $(SUFFIXES) .cpp

obj/%.o : %.cpp
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

-include obj/*.d
