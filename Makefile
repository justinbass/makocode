CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2
LDFLAGS ?=

LCOV_REPORT ?= test_output/makocode.info
LCOV_HTML_DIR ?= test_output/coverage
LCOV_FLAGS ?= --ignore-errors inconsistent,unsupported,format
LCOV_RC ?= --rc branch_coverage=1 --rc derive_function_end_line=1
GENHTML_FLAGS ?= --ignore-errors inconsistent,inconsistent,unsupported,corrupt,category

STATUS ?= ./test/make_step.sh

TEST_CASES := $(shell bash ./test/run_test_case.sh --list)
TEST_CASE_TARGETS := $(addprefix test-case-,$(TEST_CASES))

.PHONY: all clean test test-setup test-finalize coverage $(TEST_CASE_TARGETS)

ifeq ($(COVERAGE),1)
CXXFLAGS += --coverage -O0 -g
LDFLAGS += --coverage
endif

all: makocode

makocode: makocode.cpp
	@$(STATUS) build "compile makocode" $(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

test/ppm_transform: test/ppm_transform.cpp
	@$(STATUS) build "compile ppm_transform" $(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

test: test-finalize

test-setup:
	@$(STATUS) test "prepare test suite" bash ./test/run_test_suite.sh --prepare

$(TEST_CASE_TARGETS): test-case-%: test-setup makocode test/ppm_transform
	@bash ./test/run_test_case.sh --run "$*"

test-finalize: $(TEST_CASE_TARGETS)
	@$(STATUS) test "collect visual artifacts" bash ./test/run_test_suite.sh --finalize

coverage: clean
	@$(STATUS) coverage "check lcov" sh -c 'command -v lcov >/dev/null 2>&1'
	@$(STATUS) coverage "check genhtml" sh -c 'command -v genhtml >/dev/null 2>&1'
	@$(STATUS) coverage "run tests (COVERAGE=1)" env MAKOCODE_COVERAGE_PROBES=1 $(MAKE) COVERAGE=1 test
	@$(STATUS) coverage "capture lcov" lcov --capture --no-external --directory . --output-file $(LCOV_REPORT) $(LCOV_FLAGS) $(LCOV_RC)
	@$(STATUS) coverage "generate html" genhtml $(LCOV_REPORT) --output-directory $(LCOV_HTML_DIR) $(GENHTML_FLAGS) --branch-coverage

clean:
	@$(STATUS) --note clean "cleanup start"
	@$(STATUS) clean "binary" rm -f makocode
	@$(STATUS) clean "symbol bundle" rm -rf makocode.dSYM
	@$(STATUS) clean "ppm_transform helper" rm -f test/ppm_transform
	@$(STATUS) clean "test artifacts" rm -rf test_output
	@$(STATUS) clean "minified stub" rm -f makocode_minified.cpp
	@$(STATUS) clean "coverage data" rm -f *.gcda *.gcno test/*.gcda test/*.gcno $(LCOV_REPORT)
	@$(STATUS) --note clean "cleanup complete"
