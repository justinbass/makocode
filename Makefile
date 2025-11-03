CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2

.PHONY: all clean check

all: makocode

makocode: makocode.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

test: test.cpp makocode
	$(CXX) $(CXXFLAGS) test.cpp -o $@

check: makocode test
	./test

clean:
	rm -f makocode test payload.bin encoded.ppm decoded.bin
