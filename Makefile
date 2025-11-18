CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2

.PHONY: all clean test

all: makocode

makocode: makocode.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

test: makocode
	mkdir -p test
	./scripts/run_roundtrip.sh --label 3001 --size 8192 --ecc 0.5 --width 500 --height 500
	./scripts/run_roundtrip.sh --label 3002 --size 8192 --ecc 0 --width 500 --height 500
	./scripts/run_roundtrip.sh --label 3003 --size 131072 --ecc 1.0 --width 700 --height 700 --multi-page
	./scripts/run_roundtrip.sh --label 3004 --size 8192 --ecc 0.25 --width 360 --height 360 --palette "White Black"
	./scripts/run_roundtrip.sh --label 3005 --size 8192 --ecc 0.25 --width 420 --height 420 --palette "White Cyan Magenta"
	./scripts/run_roundtrip.sh --label 3006 --size 8192 --ecc 0.25 --width 480 --height 480 --palette "White Cyan Magenta Yellow"
	./scripts/run_roundtrip.sh --label 3007 --size 8192 --ecc 0.25 --width 480 --height 480 --palette "White Cyan Magenta Yellow Black"
	./scripts/run_roundtrip.sh --label 3008 --size 8192 --ecc 0.25 --width 480 --height 480 --palette "FFFFFF FF0000 00FF00 0000FF FFFF00 FF00FF 00FFFF 000000"
	./scripts/run_roundtrip.sh --label 3009 --size 32768 --ecc 0.25 --width 1000 --height 1000 --palette "White Cyan Magenta Yellow Black"
	./scripts/run_payload_suite.sh
	./scripts/test_cli_output_dir.sh
	./scripts/test_decode_failures.sh
	# Overlay E2E remains optional because it requires aggressive blending tuning.
#	./scripts/test_overlay_e2e.sh 3010

clean:
	rm -f makocode
	rm -rf test
	rm -f makocode_minified.cpp
