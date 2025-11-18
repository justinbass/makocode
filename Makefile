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
	@{ \
		if [ -n "$${NO_COLOR:-}" ] || [ ! -t 1 ]; then \
			tag='[clean]'; \
			green=''; \
			red=''; \
			reset=''; \
		else \
			tag='\033[1;34m[clean]\033[0m'; \
			green='\033[32m'; \
			red='\033[31m'; \
			reset='\033[0m'; \
		fi; \
		step() { \
			desc="$$1"; shift; \
			printf '%b %-24s ... ' "$$tag" "$$desc"; \
			if "$$@"; then \
				printf '%bPASS%b\n' "$$green" "$$reset"; \
			else \
				status="$$?"; \
				printf '%bFAIL (exit %s)%b\n' "$$red" "$$status" "$$reset"; \
				exit "$$status"; \
			fi; \
		}; \
		printf '%b cleanup start%b\n' "$$tag" "$$reset"; \
		step "binary" rm -f makocode; \
		step "debug symbols" rm -rf makocode.dSYM; \
		step "test artifacts" rm -rf test; \
		step "minified stub" rm -f makocode_minified.cpp; \
		step "coverage data" rm -f *.gcda *.gcno $(LCOV_REPORT); \
		printf '%b cleanup complete%b\n' "$$tag" "$$reset"; \
	}
