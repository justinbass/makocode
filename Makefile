CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2

.PHONY: all clean test

all: makocode

makocode: makocode.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

test: makocode
	./makocode test
	bash -eu -o pipefail -c '\
		rm -rf test/e2e; \
		mkdir -p test/e2e/decoded; \
		head -c 8192 /dev/urandom > test/e2e/random.bin; \
		makocode_bin="$$PWD/makocode"; \
		( cd test/e2e && "$$makocode_bin" encode --input=random.bin --ecc 0.5 --page-width=500 --page-height=500 ); \
		ppm_file=$$(cd test/e2e && ls -1 -- *.ppm | head -n1); \
		mv "test/e2e/$$ppm_file" test/e2e/encoded.ppm; \
		( cd test/e2e && "$$makocode_bin" decode encoded.ppm --output-dir decoded ); \
		diff test/e2e/random.bin test/e2e/decoded/random.bin \
	'

clean:
	rm -f makocode
	rm -rf test
	rm -f makocode_minified.cpp
