PROG = $(patsubst %.test,%,$(wildcard *.test))
TEST = $(PROG:%=%.test)

all: tester

test: tester

tester: Tester.cpp
	 g++ -o tester -std=c++0x -O2 -I /usr/include/smartmet/ Tester.cpp -lboost_system -lboost_filesystem -lboost_thread -lrt -lboost_program_options -DNDEBUG

clean:
	rm tester
