CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2
LDFLAGS ?=

LCOV_REPORT ?= test/makocode.info
LCOV_HTML_DIR ?= test/coverage
LCOV_FLAGS ?= --ignore-errors inconsistent,unsupported,format
LCOV_RC ?= --rc branch_coverage=1 --rc derive_function_end_line=1
GENHTML_FLAGS ?= --ignore-errors inconsistent,unsupported,corrupt,category

STATUS ?= ./scripts/make_step.sh

.PHONY: all clean test coverage

ifeq ($(COVERAGE),1)
CXXFLAGS += --coverage -O0 -g
LDFLAGS += --coverage
endif

all: makocode

makocode: makocode.cpp
	@$(STATUS) build "compile makocode" $(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

test: makocode
	@$(STATUS) test "ensure test dir" mkdir -p test
	@$(STATUS) test "run test suite" ./scripts/run_test_suite.sh

coverage: clean
	@$(STATUS) coverage "check lcov" sh -c 'command -v lcov >/dev/null 2>&1'
	@$(STATUS) coverage "check genhtml" sh -c 'command -v genhtml >/dev/null 2>&1'
	@$(STATUS) coverage "run tests (COVERAGE=1)" env MAKOCODE_COVERAGE_PROBES=1 $(MAKE) COVERAGE=1 test
	@$(STATUS) coverage "capture lcov" lcov --capture --no-external --directory . --output-file $(LCOV_REPORT) $(LCOV_FLAGS) $(LCOV_RC)
	@$(STATUS) coverage "generate html" genhtml $(LCOV_REPORT) --output-directory $(LCOV_HTML_DIR) $(GENHTML_FLAGS) --branch-coverage

clean:
	@$(STATUS) --note clean "cleanup start"
	@$(STATUS) clean "binary" rm -f makocode
	@$(STATUS) clean "debug symbols" rm -rf makocode.dSYM
	@$(STATUS) clean "test artifacts" rm -rf test
	@$(STATUS) clean "minified stub" rm -f makocode_minified.cpp
	@$(STATUS) clean "coverage data" rm -f *.gcda *.gcno $(LCOV_REPORT)
	@$(STATUS) --note clean "cleanup complete"
