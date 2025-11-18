CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2
LDFLAGS ?=

LCOV_REPORT ?= test/makocode.info
LCOV_HTML_DIR ?= test/coverage
LCOV_FLAGS ?= --ignore-errors inconsistent,unsupported,format
GENHTML_FLAGS ?= --ignore-errors inconsistent,unsupported,corrupt,category

.PHONY: all clean test coverage

ifeq ($(COVERAGE),1)
CXXFLAGS += --coverage -O0 -g
LDFLAGS += --coverage
endif

all: makocode

makocode: makocode.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

test: makocode
	mkdir -p test
	./scripts/run_test_suite.sh

coverage: clean
	@if ! command -v lcov >/dev/null 2>&1; then \
		echo "coverage target requires 'lcov' (e.g. brew install lcov)" >&2; \
		exit 1; \
	fi
	@if ! command -v genhtml >/dev/null 2>&1; then \
		echo "coverage target requires 'genhtml' (usually shipped with lcov)" >&2; \
		exit 1; \
	fi
	$(MAKE) COVERAGE=1 test
	lcov --capture --no-external --directory . --output-file $(LCOV_REPORT) $(LCOV_FLAGS)
	genhtml $(LCOV_REPORT) --output-directory $(LCOV_HTML_DIR) $(GENHTML_FLAGS)

clean:
	rm -f makocode
	rm -rf makocode.dSYM
	rm -rf test
	rm -f makocode_minified.cpp
	rm -f *.gcda *.gcno $(LCOV_REPORT)
