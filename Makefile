PROG = smartmetd
SPEC = smartmet-server

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

sharedir = $(PREFIX)/share

objdir = obj
includedir = $(PREFIX)/include

ifeq ($(origin SBINDIR), undefined)
  sbindir = $(PREFIX)/sbin
else
  sbindir = $(SBINDIR)
endif

# Compiler options

DEFINES = -DUNIX -D_REENTRANT

ifeq ($(CXX), clang++)

 FLAGS = -std=c++11 -fPIC -MD \
	-Weverything \
	-Wno-c++98-compat-pedantic \
	-Wno-padded \
	-Wno-missing-prototypes \
	-Wno-documentation-unknown-command

 INCLUDES = \
	-isystem $(includedir) \
	-I$(includedir)/smartmet \
	`pkg-config --cflags libconfig++`

else

 FLAGS = -std=c++11 -fPIC -MD -Wall -W \
	-Wno-unused-parameter -fno-omit-frame-pointer -fdiagnostics-color=always

 FLAGS_DEBUG = \
	-Werror \
	-Wpointer-arith \
	-Wcast-qual \
	-Wcast-align \
	-Wwrite-strings \
	-Winline \
	-Wctor-dtor-privacy \
	-Wnon-virtual-dtor \
	-Wno-pmf-conversions \
	-Wsign-promo \
	-Wchar-subscripts \
	-Wredundant-decls \
	-Woverloaded-virtual

 INCLUDES = \
	-I$(includedir) \
	-I$(includedir)/smartmet \
	`pkg-config --cflags libconfig++`

endif

# Compile options in detault, debug and profile modes

CFLAGS_RELEASE = $(DEFINES) $(FLAGS) $(FLAGS_RELEASE) -DNDEBUG -O2 -g
CFLAGS_DEBUG   = $(DEFINES) $(FLAGS) $(FLAGS_DEBUG)   -Werror  -O0 -g

ifneq (,$(findstring debug,$(MAKECMDGOALS)))
  override CFLAGS += $(CFLAGS_DEBUG)
else
  override CFLAGS += $(CFLAGS_RELEASE)
endif

override LDFLAGS += -rdynamic

LIBS = -L$(libdir) \
	-lsmartmet-spine \
	-lsmartmet-macgyver \
	`pkg-config --libs libconfig++` \
	-ldl \
	-lboost_filesystem \
	-lboost_date_time \
	-lboost_iostreams \
	-lboost_program_options \
	-lboost_regex \
	-lboost_thread \
	-lboost_system \
	-lfmt \
	-lz -lpthread \
	-ldw

ifneq (,$(findstring sanitize=address,$(CFLAGS)))
  LIBS += -lasan
else
ifneq (,$(findstring sanitize=thread,$(CFLAGS)))
  LIBS += -ltsan
else
  LIBS += -ljemalloc
endif
endif

# Common library compiling template

# rpm variables

CWP = $(shell pwd)
BIN = $(shell basename $(CWP))

# Compilation directories

vpath %.cpp source
vpath %.h include
vpath %.o $(objdir)

# How to install

INSTALL_PROG = install -p -m 755
INSTALL_DATA = install -p -m 664

# The files to be compiled

SRCS = $(patsubst source/%,%,$(wildcard source/*.cpp))
HDRS = $(patsubst include/%,%,$(wildcard include/*.h))
OBJS = $(SRCS:%.cpp=%.o)

OBJFILES = $(OBJS:%.o=obj/%.o)

MAINSRCS = $(PROG:%=%.cpp)
SUBSRCS = $(filter-out $(MAINSRCS),$(SRCS))
SUBOBJS = $(SUBSRCS:%.cpp=%.o)
SUBOBJFILES = $(SUBOBJS:%.o=obj/%.o)

INCLUDES := -Iinclude $(INCLUDES)

# For makedepend:

ALLSRCS = $(wildcard *.cpp source/*.cpp)

.PHONY: test rpm

# The rules

all: objdir $(PROG)
debug: all
release: all
profile: all

$(PROG): % : $(SUBOBJS) %.o Makefile
	$(CXX) $(LDFLAGS) -o $@ obj/$@.o $(SUBOBJFILES) $(LIBS)

clean:
	rm -f $(PROG) *~ source/*~ include/*~
	rm -rf obj
	rm -f test/suites/*

format:
	clang-format -i -style=file include/*.h source/*.cpp test/*.cpp

install:
	mkdir -p $(sbindir)
	@list='$(PROG)'; \
	for prog in $$list; do \
	  echo $(INSTALL_PROG) $$prog $(sbindir)/$$prog; \
	  $(INSTALL_PROG) $$prog $(sbindir)/$$prog; \
	done

test:
	cd test && make test

objdir:
	@mkdir -p $(objdir)

rpm: clean
	@if [ -a $(SPEC).spec ]; \
	then \
	  tar -czvf $(SPEC).tar.gz --transform "s,^,$(SPEC)/," * ; \
	  rpmbuild -ta $(SPEC).tar.gz ; \
	  rm -f $(SPEC).tar.gz ; \
	else \
	  echo $(SPEC).spec missing; \
	fi;


.SUFFIXES: $(SUFFIXES) .cpp

.cpp.o : %.cpp %.h Makefile
	$(CXX) $(CFLAGS) $(INCLUDES) -c -o $(objdir)/$@ $<

ifneq ($(wildcard obj/*.d),)
-include $(wildcard obj/*.d)
endif
