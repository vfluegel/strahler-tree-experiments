CFLAGS=-O3 -std=c23 -Wall -Wextra -Wpedantic -DNDEBUG
CXXFLAGS=-O3 -Wall -Wextra -Wpedantic

all: genstree lenstree pms2dot

genstree: genstree.o prtstree.o
	$(CC) $(CFLAGS) $^ -o $@

pms2dot: pms2dot.o prtstree.o
	$(CC) $(CFLAGS) $^ -o $@

lenstree: lenstree.o
	$(CXX) $(CXXFLAGS) $^ -o $@

%.o: %.c prtstree.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f genstree lenstree pms2dot *.o *.gcda *.gcno *.gcov

GCOV?=gcov
coverage: CFLAGS += --coverage
coverage: CXXFLAGS += --coverage
coverage: clean all regtest
	$(GCOV) genstree.c pms2dot.c prtstree.c lenstree.cpp

regtest:
	@echo "Running regression tests..."
	./tests/run-regression.sh
