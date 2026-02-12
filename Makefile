CFLAGS:=$(CFLAGS) -O3 -std=c23 -Wall -Wextra -Wpedantic
CXXFLAGS:=$(CXXFLAGS) -O3 -Wall -Wextra -Wpedantic

all: genstree lenstree pms2dot unit_tests

genstree: genstree.o prtstree.o
	$(CC) $(CFLAGS) $^ -o $@

pms2dot: pms2dot.o prtstree.o
	$(CC) $(CFLAGS) $^ -o $@

lenstree: lenstree.o
	$(CXX) $(CXXFLAGS) $^ -o $@

%.o: %.c prtstree.h utrees.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

unit_tests: tests/unit_tests.c genstree.c prtstree.c
	$(CC) $(CFLAGS) -DUNIT_TEST tests/unit_tests.c prtstree.c -o unit_tests

clean:
	rm -f genstree lenstree pms2dot unit_tests *.o *.gcda *.gcno *.gcov

GCOV?=gcov
coverage: CFLAGS += --coverage
coverage: CXXFLAGS += --coverage
coverage: clean all regtest
	$(GCOV) genstree.c pms2dot.c prtstree.c lenstree.cpp

regtest:
	@echo "Running regression tests..."
	./tests/run-regression.sh

unittest: unit_tests
	@echo "Running unit tests..."
	./unit_tests

test: unittest regtest
