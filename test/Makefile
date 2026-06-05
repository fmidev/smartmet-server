PROG = $(patsubst %.cpp,%,$(wildcard *Test.cpp))

REQUIRES = configpp

include $(shell smartbuildcfg --prefix)/share/smartmet/devel/makefile.inc

SMARTMETD=$(shell test -e ../smartmetd && echo `pwd`/../smartmetd || echo /usr/sbin/smartmetd)

CFLAGS = -DUNIX -O0 -g $(FLAGS)

INCLUDES += -I../include

LIBS += -lsmartmet-spine -lsmartmet-macgyver -lfmt -lboost_iostreams -lz

test: startuptest unittest

startuptest: $(SMARTMETD) startup-test.sh
	bash ./startup-test.sh $(SMARTMETD)
	@echo "Startup test OK"

unittest: $(PROG)
	@echo Running unit tests:
	@ok=true; \
	for prog in $(PROG); do \
	./$$prog || ok=false; \
	done; \
	$$ok

clean:
	rm -f $(PROG) test.out test.status
	rm -rf obj

UtilityTest: obj/UtilityTest.o ../obj/Utility.o
	$(CXX) $(CFLAGS) -o $@ $^ $(LIBS)

obj/%.o: %.cpp
	@mkdir -p obj
	$(CXX) $(CFLAGS) $(INCLUDES) -c -MD -MF $(patsubst obj/%.o, obj/%.d, $@) -MT $@ -o $@ $<
