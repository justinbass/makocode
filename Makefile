CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2

.PHONY: all clean test

all: makocode

makocode: makocode.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

test: makocode
	mkdir -p test
	./makocode test --summary
	./scripts/run_roundtrip.sh --label 3001 --size 8192 --ecc 0.5 --width 500 --height 500
	./scripts/run_roundtrip.sh --label 3002 --size 8192 --ecc 0 --width 500 --height 500
	./scripts/run_roundtrip.sh --label 3003 --size 131072 --ecc 1.0 --width 700 --height 700 --multi-page

clean:
	rm -f makocode
	rm -rf test
	rm -f makocode_minified.cpp
