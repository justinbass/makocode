CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2
LDFLAGS ?=

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
APPLE_SDK ?= $(shell xcrun --sdk macosx --show-sdk-path)
CXXFLAGS += -isystem $(APPLE_SDK)/usr/include/c++/v1
endif

LCOV_REPORT ?= test_output/coverage/makocode.info
LCOV_HTML_DIR ?= test_output/coverage
MAKOCODE_SOURCES := src/makocode.cpp src/makocode_common.cpp src/makocode_encode.cpp src/makocode_decode.cpp src/makocode_minify.cpp src/makocode_checkpoint.cpp src/makocode_platform.cpp
# Clang's gcov output can assign synthesized functions to declaration lines or
# line 0. Do not infer function ranges from those incomplete locations, and
# fully ignore the resulting lcov metadata warnings (repeat categories per
# lcov's ignore-errors convention).
LCOV_FLAGS ?= --ignore-errors inconsistent,inconsistent,unsupported,format,format
LCOV_RC ?= --rc branch_coverage=1 --rc derive_function_end_line=0
GENHTML_FLAGS ?= --ignore-errors inconsistent,inconsistent,unsupported,corrupt,category,category

.PHONY: all clean test coverage

define test_status
	@if [ -t 1 ]; then \
		printf '\033[1;34m[TEST]\033[0m %s\n' '$(1)'; \
	else \
		printf '[TEST] %s\n' '$(1)'; \
	fi
endef

ifeq ($(COVERAGE),1)
CXXFLAGS += --coverage -O0 -g
LDFLAGS += --coverage
endif

all: makocode

makocode: $(MAKOCODE_SOURCES)
	$(call test_status,compile makocode)
	@$(CXX) $(CXXFLAGS) $(MAKOCODE_SOURCES) -o $@ $(LDFLAGS)

makocode_test: tests/makocode_test.cpp $(MAKOCODE_SOURCES)
	$(call test_status,compile makocode_test)
	@$(CXX) $(CXXFLAGS) -DMAKOCODE_TEST_BUILD tests/makocode_test.cpp src/makocode_platform.cpp -o $@ $(LDFLAGS)

test: makocode makocode_test
	$(call test_status,run CLI test suite)
	@./makocode_test --all

coverage: clean
	$(call test_status,check lcov)
	@command -v lcov >/dev/null
	$(call test_status,check genhtml)
	@command -v genhtml >/dev/null
	$(call test_status,run suite with coverage)
	@$(MAKE) --no-print-directory COVERAGE=1 test
	$(call test_status,prepare coverage output)
	@mkdir -p "$(dir $(LCOV_REPORT))"
	$(call test_status,capture coverage data)
	@lcov --capture --no-external --directory . --output-file $(LCOV_REPORT) $(LCOV_FLAGS) $(LCOV_RC)
	$(call test_status,generate coverage HTML)
	@genhtml $(LCOV_REPORT) --output-directory $(LCOV_HTML_DIR) $(GENHTML_FLAGS) --branch-coverage

clean:
	$(call test_status,remove application and test binaries)
	@rm -f makocode makocode_test
	$(call test_status,remove test artifacts)
	@rm -rf makocode.dSYM makocode_test.dSYM test_output
	$(call test_status,remove generated source and coverage files)
	@rm -f makocode_minified.cpp *.gcda *.gcno src/*.gcda src/*.gcno tests/*.gcda tests/*.gcno $(LCOV_REPORT)
