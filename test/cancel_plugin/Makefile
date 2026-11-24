include $(or $(PREFIX),$(shell smartbuildcfg --prefix))/share/smartmet/devel/makefile.inc

DEFINES = -DUNIX -D_REENTRANT

LIBS += $(PREFIX_LDFLAGS) -lsmartmet-spine -lsmartmet-macgyver

LIBFILE := cancel_plugin.so

all: $(LIBFILE)

clean:
	rm -f $(LIBFILE)

$(LIBFILE): Plugin.cpp
	$(CXX) $(CFLAGS) $(INCLUDES) -shared -rdynamic -o $(LIBFILE) Plugin.cpp $(LIBS)

.PHONY: all clean
