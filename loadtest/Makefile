include $(shell echo $${PREFIX-/usr})/share/smartmet/devel/makefile.inc

PROG = $(patsubst %.test,%,$(wildcard *.test))
TEST = $(PROG:%=%.test)

LIBS := \
	-lsmartmet-macgyver \
	-lboost_thread \
	-lrt \
	-lboost_program_options \
	-lpthread

all: tester

test: tester

tester: Tester.cpp
	 g++ -o tester $(CFLAGS) $(INCLUDES) -DNDEBUG Tester.cpp $(LIBS)

clean:
	rm -f tester
