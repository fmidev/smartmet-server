MODULE = smartmet-server
SPEC = smartmet-server

REQUIRES = configpp

include $(shell echo $${PREFIX-/usr})/share/smartmet/devel/makefile.inc

# Default compiler flags

DEFINES = -DUNIX

CFLAGS = $(DEFINES) -O2 -g -DNDEBUG $(FLAGS)

override LDFLAGS += -rdynamic
override LDFLAGS_DEBUG += -rdynamic
override LDFLAGS_PROFILE += -rdynamic

LIBS += -L$(libdir) \
	-lsmartmet-spine \
	-lsmartmet-macgyver \
	$(CONFIGPP_LIBS) \
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
  LIBS += -ljemalloc
endif
endif

ifeq ($(origin SBINDIR), undefined)
  sbindir = $(PREFIX)/sbin
else
  sbindir = $(SBINDIR)
endif

# Compilation directories

vpath %.cpp source main
vpath %.h include
vpath %.o $(objdir)
vpath %.d $(objdir)

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
	$(CXX) $(LDFLAGS) $(CFLAGS) -o $@ obj/$@.o $(OBJFILES) $(LIBS)

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
	tar -czvf $(SPEC).tar.gz --exclude test --exclude loadtest --exclude-vcs --transform "s,^,$(SPEC)/," *
	rpmbuild -tb $(SPEC).tar.gz
	rm -f $(SPEC).tar.gz

.SUFFIXES: $(SUFFIXES) .cpp

obj/%.o : %.cpp
	$(CXX) $(CFLAGS) $(INCLUDES) -c -o $@ $<

-include obj/*.d
