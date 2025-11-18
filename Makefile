CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2

.PHONY: all clean test

all: makocode

makocode: makocode.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

test: makocode
	mkdir -p test
	./scripts/run_test_suite.sh

clean:
	rm -f makocode
	rm -rf test
	rm -f makocode_minified.cpp
