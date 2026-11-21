CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2
LDFLAGS ?=
INCLUDES ?= 1
FREESTANDING ?= 0
BUILD_DIR ?= build
BIN_DIR ?= $(BUILD_DIR)/bin
COVERAGE_DATA_DIR ?= $(BIN_DIR)
LCOV_REPORT ?= $(BUILD_DIR)/coverage/report/makocode.info
LCOV_HTML_DIR ?= $(BUILD_DIR)/coverage/report/html

ifeq ($(FREESTANDING),1)
CXXFLAGS += -ffreestanding -DINCLUDES=0
else
CXXFLAGS += -DINCLUDES=$(INCLUDES)
endif

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
APPLE_SDK ?= $(shell xcrun --sdk macosx --show-sdk-path)
CXXFLAGS += -isystem $(APPLE_SDK)/usr/include/c++/v1
endif

PLATFORM_SOURCE ?= src/makocode_platform.cpp
MAKOCODE_SOURCES := src/makocode.cpp src/makocode_common.cpp src/makocode_encode.cpp src/makocode_decode.cpp src/makocode_minify.cpp src/makocode_checkpoint.cpp $(PLATFORM_SOURCE)
MAKOCODE_HEADERS := $(wildcard src/*.hpp)
CLI_OBJECT_DIR := $(BIN_DIR)/obj
MAKOCODE_OBJECTS := $(patsubst %.cpp,$(CLI_OBJECT_DIR)/%.o,$(MAKOCODE_SOURCES))
MAKOCODE_BIN := $(BIN_DIR)/makocode
MAKOCODE_TEST_BIN := $(BIN_DIR)/makocode_test
# Clang's gcov output can assign synthesized functions to declaration lines or
# line 0. Do not infer function ranges from those incomplete locations, and
# fully ignore the resulting lcov metadata warnings (repeat categories per
# lcov's ignore-errors convention).
LCOV_FLAGS ?= --ignore-errors inconsistent,inconsistent,unsupported,format,format
LCOV_RC ?= --rc branch_coverage=1 --rc derive_function_end_line=0
GENHTML_FLAGS ?= --ignore-errors inconsistent,inconsistent,unsupported,corrupt,category,category

.PHONY: all clean test coverage freestanding makocode makocode_test

define test_status
	@if [ -t 1 ]; then \
		printf '\033[1;34m[TEST]\033[0m %s\n' '$(1)'; \
	else \
		printf '[TEST] %s\n' '$(1)'; \
	fi
endef

ifeq ($(COVERAGE),1)
CXXFLAGS += --coverage -O2 -g
LDFLAGS += --coverage
COMPILE_DIR := $(abspath $(COVERAGE_DATA_DIR))
else
COMPILE_DIR := $(CURDIR)
endif

all: $(MAKOCODE_BIN)

makocode: $(MAKOCODE_BIN)

$(CLI_OBJECT_DIR)/%.o: %.cpp $(MAKOCODE_HEADERS)
	$(call test_status,compile $<)
	@mkdir -p "$(dir $@)" "$(COMPILE_DIR)"
	@cd "$(COMPILE_DIR)" && $(CXX) $(CXXFLAGS) -I"$(CURDIR)/src" -c "$(abspath $<)" -o "$(abspath $@)"

$(MAKOCODE_BIN): $(MAKOCODE_OBJECTS)
	$(call test_status,compile makocode)
	@mkdir -p "$(BIN_DIR)" "$(COMPILE_DIR)"
	@cd "$(COMPILE_DIR)" && $(CXX) $(CXXFLAGS) $(abspath $(MAKOCODE_OBJECTS)) -o "$(abspath $@)" $(LDFLAGS)

makocode_test: $(MAKOCODE_TEST_BIN)

$(MAKOCODE_TEST_BIN): tests/makocode_test.cpp $(MAKOCODE_SOURCES) $(MAKOCODE_HEADERS)
	$(call test_status,compile makocode_test)
	@mkdir -p "$(BIN_DIR)" "$(COMPILE_DIR)"
	@cd "$(COMPILE_DIR)" && $(CXX) $(CXXFLAGS) -DMAKOCODE_TEST_BUILD $(abspath tests/makocode_test.cpp src/makocode_platform.cpp) -o "$(abspath $@)" $(LDFLAGS)

test: $(MAKOCODE_BIN) $(MAKOCODE_TEST_BIN)
	$(call test_status,run CLI test suite)
	@"$(MAKOCODE_TEST_BIN)" --all

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
	@lcov --capture --no-external --directory "$(COVERAGE_DATA_DIR)" --base-directory "$(CURDIR)" --output-file "$(LCOV_REPORT)" $(LCOV_FLAGS) $(LCOV_RC)
	$(call test_status,generate coverage HTML)
	@genhtml $(LCOV_REPORT) --output-directory $(LCOV_HTML_DIR) $(GENHTML_FLAGS) --branch-coverage

# Compile every translation unit without the host platform implementation.
# A target integration supplies the makocode_platform_* definitions at link time.
freestanding:
	@mkdir -p "$(BUILD_DIR)/freestanding"
	@set -e; for source in $(MAKOCODE_SOURCES); do \
		object="$(BUILD_DIR)/freestanding/$$(basename "$$source" .cpp).o"; \
		$(CXX) $(filter-out -DINCLUDES=%,$(CXXFLAGS)) -ffreestanding -DINCLUDES=0 -Isrc -c "$$source" -o "$$object"; \
	done

clean:
	$(call test_status,remove build and test output directories)
	@rm -rf "$(BUILD_DIR)" test_output makocode.dSYM makocode_test.dSYM
	$(call test_status,remove generated source output)
	@rm -f makocode makocode_test makocode_minified.cpp *.gcda *.gcno src/*.gcda src/*.gcno tests/*.gcda tests/*.gcno
