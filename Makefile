CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2

.PHONY: all clean test

all: makocode

makocode: makocode.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

test: makocode
	./makocode test

clean:
	rm -f makocode \
		payload.bin encoded.ppm decoded.bin \
		payload_c*_s*.bin encoded_c*_s*.ppm decoded_c*_s*.bin
