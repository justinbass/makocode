CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2

.PHONY: all clean test

all: makocode

makocode: makocode.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

test: makocode
	./makocode test
	bash -eu -o pipefail -c '\
		makocode_bin="$$PWD/makocode"; \
		rm -f test/3001_random_payload.bin \
		      test/3001_random_payload_decoded.bin \
		      test/3001_random_payload_encoded.ppm \
		      test/3002_random_payload.bin \
		      test/3002_random_payload_decoded.bin \
		      test/3002_random_payload_encoded.ppm; \
		tmp_dir=$$(mktemp -d test/3001_tmp.XXXXXX); \
		trap '\''rm -rf "$$tmp_dir"'\'' EXIT; \
		head -c 8192 /dev/urandom > "$$tmp_dir/random.bin"; \
		( cd "$$tmp_dir" && "$$makocode_bin" encode --input=random.bin --ecc 0.5 --page-width=500 --page-height=500 ); \
		ppm_file=$$(cd "$$tmp_dir" && ls -1 -- *.ppm | head -n1); \
		( cd "$$tmp_dir" && "$$makocode_bin" decode "$$ppm_file" --output-dir decoded ); \
		mv "$$tmp_dir/random.bin" test/3001_random_payload.bin; \
		mv "$$tmp_dir/$$ppm_file" test/3001_random_payload_encoded.ppm; \
		mv "$$tmp_dir/decoded/random.bin" test/3001_random_payload_decoded.bin; \
		diff test/3001_random_payload.bin test/3001_random_payload_decoded.bin \
	'
	bash -eu -o pipefail -c '\
		makocode_bin="$$PWD/makocode"; \
		tmp_dir=$$(mktemp -d test/3002_tmp.XXXXXX); \
		trap '\''rm -rf "$$tmp_dir"'\'' EXIT; \
		head -c 8192 /dev/urandom > "$$tmp_dir/random.bin"; \
		( cd "$$tmp_dir" && "$$makocode_bin" encode --input=random.bin --ecc 0 --page-width=500 --page-height=500 ); \
		ppm_file=$$(cd "$$tmp_dir" && ls -1 -- *.ppm | head -n1); \
		( cd "$$tmp_dir" && "$$makocode_bin" decode "$$ppm_file" --output-dir decoded ); \
		mv "$$tmp_dir/random.bin" test/3002_random_payload.bin; \
		mv "$$tmp_dir/$$ppm_file" test/3002_random_payload_encoded.ppm; \
		mv "$$tmp_dir/decoded/random.bin" test/3002_random_payload_decoded.bin; \
		diff test/3002_random_payload.bin test/3002_random_payload_decoded.bin \
	'

clean:
	rm -f makocode
	rm -rf test
	rm -f makocode_minified.cpp
