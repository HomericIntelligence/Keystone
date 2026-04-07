# ProjectKeystone Makefile
# Simple build system with debug, release, and asan modes
# Artifacts stored in build/debug, build/release, build/debug.asan, build/release.asan
#
# Usage:
#   make                    # Build debug mode (build/debug)
#   make release            # Build release mode (build/release)
#   make debug.asan         # Build debug with ASan (build/debug.asan)
#   make release.asan       # Build release with ASan (build/release.asan)
#   make test               # Run tests (uses debug build)
#   make test.asan          # Run tests with ASan (uses debug.asan build)

# ============================================================================
# Configuration Variables
# ============================================================================

# Number of processors for parallel builds
NPROC ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Docker mode detection
ifeq ($(NATIVE),1)
    DOCKER_CHECK :=
    DOCKER_PREFIX :=
else
    DOCKER_CHECK := docker-compose up -d dev >/dev/null 2>&1 || true;
    DOCKER_PREFIX := docker-compose exec -T dev 
endif

# Compiler flags
BUILD_FLAGS_debug := -O0 -g -D_DEBUG
BUILD_FLAGS_release := -O3 -DNDEBUG
BUILD_FLAGS_asan := -fsanitize=address -fno-omit-frame-pointer
BUILD_FLAGS_ubsan := -fsanitize=undefined -fno-omit-frame-pointer
BUILD_FLAGS_lsan := -fsanitize=leak -fno-omit-frame-pointer
BUILD_FLAGS_tsan := -fsanitize=thread -fno-omit-frame-pointer
BUILD_FLAGS_msan := -fsanitize=memory -fno-omit-frame-pointer

BUILD_DIR ?= build
EMPTY :=
SPACE := $(EMPTY) $(EMPTY)
BUILD_SUBDIR ?= x86
BUILD_SUBDIR := $(subst $(SPACE),.,$(strip $(sort $(subst .,$(SPACE),$(BUILD_SUBDIR)))))
CMAKE_BUILD_TYPE ?= Debug
# TSan slows thread ops 5-20x; give it more time per test
CTEST_TIMEOUT ?= 120

# Conan dependency management
CONAN_OUTPUT_DIR ?= build/conan-deps
CONAN_TOOLCHAIN := $(wildcard $(CONAN_OUTPUT_DIR)/conan_toolchain.cmake)
CMAKE_EXTRA_FLAGS ?=
ifneq ($(CONAN_TOOLCHAIN),)
    CMAKE_EXTRA_FLAGS += -DCMAKE_TOOLCHAIN_FILE=$(CONAN_TOOLCHAIN)
endif

# ============================================================================
# Dependency Management (Conan)
# ============================================================================

.PHONY: deps
deps:
	@echo "Installing Conan dependencies (Debug + Release)..."
	conan install . --output-folder=$(CONAN_OUTPUT_DIR) --build=missing -s build_type=Debug -s compiler.cppstd=20
	conan install . --output-folder=$(CONAN_OUTPUT_DIR) --build=missing -s build_type=Release -s compiler.cppstd=20

# ============================================================================
# Default target
# ============================================================================

.PHONY: default
default: compile

# Directory creation rule - only runs if directory doesn't exist
.PHONY: $(BUILD_DIR)

$(BUILD_DIR)/$(BUILD_SUBDIR):
	@echo "Creating build directory: $@"
	@mkdir -p $(BUILD_DIR)/$(BUILD_SUBDIR)

# Generic build rule for any mode
compile: $(BUILD_DIR)/$(BUILD_SUBDIR)
	@echo "Building $* mode..."
	$(DOCKER_CHECK)
	$(DOCKER_PREFIX) bash -c "cmake -S . -B $(BUILD_DIR)/$(BUILD_SUBDIR) -G Ninja -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) -DCMAKE_CXX_FLAGS=\"$(BUILD_FLAGS)\" $(CMAKE_EXTRA_FLAGS)"
	$(DOCKER_PREFIX) bash -c "cmake --build $(BUILD_DIR)/$(BUILD_SUBDIR) -j$(NPROC)"

# ============================================================================
# Test Recipes
# ============================================================================

.PHONY: test test.unit test.basic test.module test.component test.async test.distributed test.concurrency test.simulation test.grpc test.profiling

# Test executables
TEST_BASIC := basic_delegation_tests
TEST_MODULE := module_coordination_tests
TEST_COMPONENT := component_coordination_tests
TEST_ASYNC := async_delegation_tests
TEST_DISTRIBUTED := distributed_hierarchy_tests
TEST_UNIT := unit_tests
TEST_CONCURRENCY := concurrency_unit_tests
TEST_SIMULATION := simulation_unit_tests
TEST_GRPC := distributed_grpc_tests
TEST_PROFILING := profiling_tests

# Run all tests with ctest
test: compile
	@echo "Running all tests..."
	$(DOCKER_CHECK)
	$(DOCKER_PREFIX) bash -c "cd $(BUILD_DIR)/$(BUILD_SUBDIR) && ctest --output-on-failure -j$(NPROC) --timeout $(CTEST_TIMEOUT)"

# Individual test suites (run specific executable)
test.unit: compile
	@echo "Running unit tests..."
	$(DOCKER_CHECK)
	$(DOCKER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/$(TEST_UNIT)

test.basic: compile
	@echo "Running basic delegation tests..."
	$(DOCKER_CHECK)
	$(DOCKER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/$(TEST_BASIC)

test.module: compile
	@echo "Running module coordination tests..."
	$(DOCKER_CHECK)
	$(DOCKER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/$(TEST_MODULE)

test.component: compile
	@echo "Running component coordination tests..."
	$(DOCKER_CHECK)
	$(DOCKER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/$(TEST_COMPONENT)

test.async: compile
	@echo "Running async delegation tests..."
	$(DOCKER_CHECK)
	$(DOCKER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/$(TEST_ASYNC)

test.distributed: compile
	@echo "Running distributed hierarchy tests..."
	$(DOCKER_CHECK)
	$(DOCKER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/$(TEST_DISTRIBUTED)

test.concurrency: compile
	@echo "Running concurrency unit tests..."
	$(DOCKER_CHECK)
	$(DOCKER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/$(TEST_CONCURRENCY)

test.simulation: compile
	@echo "Running simulation unit tests..."
	$(DOCKER_CHECK)
	$(DOCKER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/$(TEST_SIMULATION)

test.grpc: compile.grpc
	@echo "Running gRPC distributed tests..."
	$(DOCKER_CHECK)
	$(DOCKER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/$(TEST_GRPC)

test.profiling: compile.profile
	@echo "Running profiling tests..."
	$(DOCKER_CHECK)
	$(DOCKER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/$(TEST_PROFILING)

# ============================================================================
# Benchmark Recipes
# ============================================================================

.PHONY: benchmark benchmark.message-pool benchmark.distributed benchmark.strings

# Run all benchmarks
benchmark: compile.release
	@echo "Running benchmarks..."
	$(DOCKER_CHECK)
	$(DOCKER_PREFIX) ./scripts/run_benchmarks.sh

# Individual benchmark targets
benchmark.message-pool: compile.release
	@echo "Running message pool benchmarks..."
	$(DOCKER_CHECK)
	$(DOCKER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/message_pool_benchmarks

benchmark.distributed: compile.release
	@echo "Running distributed benchmarks..."
	$(DOCKER_CHECK)
	$(DOCKER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/distributed_benchmarks

benchmark.strings: compile.release
	@echo "Running string allocation benchmarks..."
	$(DOCKER_CHECK)
	$(DOCKER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/string_allocation_benchmarks

# ============================================================================
# Load Testing
# ============================================================================

.PHONY: load-test load-test.quick

# Run all load test scenarios
load-test: compile.release
	@echo "Running load tests (full duration)..."
	$(DOCKER_CHECK)
	$(DOCKER_PREFIX) ./tests/load/run_all_scenarios.sh

# Run load tests in quick mode (for CI)
load-test.quick: compile.release
	@echo "Running load tests (quick mode)..."
	$(DOCKER_CHECK)
	$(DOCKER_PREFIX) ./tests/load/run_all_scenarios.sh --quick

# ============================================================================
# Coverage
# ============================================================================

.PHONY: coverage

coverage: compile.coverage
	@echo "Generating coverage report..."
	$(DOCKER_CHECK)
	$(DOCKER_PREFIX) ./scripts/generate_coverage.sh

# ============================================================================
# CI/CD Helper Recipes
# ============================================================================

.PHONY: ci ci.quick pre-commit

# Full CI pipeline
ci: compile.debug.asan test.debug.asan lint format.check
	@echo "✓ CI pipeline complete"

# Quick CI (for pull requests)
ci.quick: compile.debug.asan test.basic test.module test.component format.check
	@echo "✓ Quick CI complete"

# Pre-commit checks
pre-commit: format.check lint.clang-tidy test.basic
	@echo "✓ Pre-commit checks passed"

# ============================================================================
# Linting & Static Analysis
# ============================================================================

.PHONY: lint lint-clang-tidy lint-cppcheck

lint:
	@echo "Running static analysis..."
	$(DOCKER_CHECK)
	$(DOCKER_PREFIX) ./scripts/run_static_analysis.sh $(LINT_FLAGS);

%.clang-tidy:
	@echo "Running clang-tidy..."
	@$(MAKE) $* LINT_FLAGS=--clang-tidy-only

%.cppcheck:
	@echo "Running cppcheck..."
	$(DOCKER_CHECK)
	@$(MAKE) $* LINT_FLAGS=--cppcheck-only

# ============================================================================
# Code Formatting
# ============================================================================

.PHONY: format format.check

format:
	@echo "Formatting C++ code with clang-format..."
	$(DOCKER_CHECK)
	$(DOCKER_PREFIX) bash -c \
			"find src include tests benchmarks -type f \( -name '*.cpp' -o -name '*.hpp' \) \
			-not -path '*/build/*' -not -path '*/_deps/*' \
			| xargs clang-format -i --Werror";
	@echo "✓ Formatting complete"

format.check:
	@echo "Checking C++ formatting..."
	$(DOCKER_CHECK)
	$(DOCKER_PREFIX) bash -c \
			"find src include tests benchmarks -type f \( -name '*.cpp' -o -name '*.hpp' \) \
			-not -path '*/build/*' -not -path '*/_deps/*' \
			| xargs clang-format --dry-run --Werror"
	@echo "✓ Formatting check passed"

# ============================================================================
# Clean Recipes
# ============================================================================

.PHONY: clean clean-all clean-everything

# Clean specific build directory
clean:
	@echo "Cleaning directory $(BUILD_DIR)..."
	rm -rf $(BUILD_DIR)/$(BUILD_SUBDIR)

# ============================================================================
# Docker Management
# ============================================================================

.PHONY: docker.build docker.up docker.clean docker.down docker.shell

docker.build:
	@echo "Building Docker image: dev (commit $(GIT_COMMIT))..."
	docker-compose build dev

docker.build.%:
	@echo "Building Docker image: $* (commit $(GIT_COMMIT))..."
	docker-compose build $*

docker.up:
	@echo "Starting dev container for commit $(GIT_COMMIT)..."
	docker-compose up -d dev
	sleep 2

docker.clean:
	@echo "Cleaning Docker resources..."
	docker-compose down -v
	docker rmi -f projectkeystone-dev:$(GIT_COMMIT) projectkeystone:$(GIT_COMMIT) || true

docker.down:
	@echo "Stopping containers..."
	docker-compose down

docker.shell: docker-up
	$(DOCKER_PREFIX) /bin/bash

# ============================================================================
# Native Variants (run on host system instead of Docker)
# ============================================================================

# Pattern rule for native variants - matches any target with .native suffix
%.native:
	@$(MAKE) $* NATIVE=1

# Sanitizer pattern rules - append sanitizer flags to existing targets
%.asan:
	@$(MAKE) $* BUILD_FLAGS="$(BUILD_FLAGS) $(BUILD_FLAGS_asan)" BUILD_SUBDIR="$(BUILD_SUBDIR)$(suffix $@)" CMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)

%.ubsan:
	@$(MAKE) $* BUILD_FLAGS="$(BUILD_FLAGS) $(BUILD_FLAGS_ubsan)" BUILD_SUBDIR="$(BUILD_SUBDIR)$(suffix $@)" CMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)

%.lsan:
	@$(MAKE) $* BUILD_FLAGS="$(BUILD_FLAGS) $(BUILD_FLAGS_lsan)" BUILD_SUBDIR="$(BUILD_SUBDIR)$(suffix $@)" CMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)

%.tsan:
	@$(MAKE) $* BUILD_FLAGS="$(BUILD_FLAGS) $(BUILD_FLAGS_tsan)" BUILD_SUBDIR="$(BUILD_SUBDIR)$(suffix $@)" CMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) CTEST_TIMEOUT=600

%.msan:
	@$(MAKE) $* BUILD_FLAGS="$(BUILD_FLAGS) $(BUILD_FLAGS_msan)" BUILD_SUBDIR="$(BUILD_SUBDIR)$(suffix $@)" CMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)

# Feature flag patterns
%.grpc:
	@$(MAKE) $* BUILD_FLAGS="$(BUILD_FLAGS) -DENABLE_GRPC=ON" BUILD_SUBDIR="$(BUILD_SUBDIR)$(suffix $@)" CMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)

%.coverage:
	@$(MAKE) $* BUILD_FLAGS="$(BUILD_FLAGS) -DENABLE_COVERAGE=ON" BUILD_SUBDIR="$(BUILD_SUBDIR)$(suffix $@)" CMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)

%.profile:
	@$(MAKE) $* BUILD_FLAGS="$(BUILD_FLAGS) -DENABLE_PROFILING=ON" BUILD_SUBDIR="$(BUILD_SUBDIR)$(suffix $@)" CMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)

%.fuzz:
	@$(MAKE) $* BUILD_FLAGS="$(BUILD_FLAGS) -DENABLE_FUZZING=ON" BUILD_SUBDIR="$(BUILD_SUBDIR)$(suffix $@)" CMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)

%.debug:
	@$(MAKE) $* BUILD_FLAGS="$(BUILD_FLAGS) $(BUILD_FLAGS_debug)" BUILD_SUBDIR="$(BUILD_SUBDIR)$(suffix $@)" CMAKE_BUILD_TYPE=Debug

%.release:
	@$(MAKE) $* BUILD_FLAGS="$(BUILD_FLAGS) $(BUILD_FLAGS_release)" BUILD_SUBDIR="$(BUILD_SUBDIR)$(suffix $@)" CMAKE_BUILD_TYPE=Release


# ============================================================================
# Help & Info
# ============================================================================

.PHONY: help

help:
	@echo "ProjectKeystone Makefile"
	@echo "Unified build system with debug, release, sanitizer modes, and testing"
	@echo ""
	@echo "Usage: make <target>[.modifier][.native]"
	@echo ""
	@echo "Build Commands:"
	@echo "  make                    Build debug mode (build/x86.debug)"
	@echo "  make compile.release    Build release mode (build/x86.release)"
	@echo "  make compile.debug.asan Build debug with ASan (build/x86.debug.asan)"
	@echo ""
	@echo "Sanitizer Modifiers (append to any build/test target):"
	@echo "  .asan                   AddressSanitizer + UBSan"
	@echo "  .ubsan                  UndefinedBehaviorSanitizer"
	@echo "  .tsan                   ThreadSanitizer"
	@echo "  .lsan                   LeakSanitizer"
	@echo "  .msan                   MemorySanitizer"
	@echo ""
	@echo "Feature Modifiers:"
	@echo "  .grpc                   Enable gRPC support"
	@echo "  .coverage               Enable coverage instrumentation"
	@echo "  .profile                Enable profiling"
	@echo "  .fuzz                   Enable fuzzing"
	@echo ""
	@echo "Test Commands:"
	@echo "  make test               Run all tests (ctest)"
	@echo "  make test.debug.asan    Run all tests with ASan"
	@echo "  make test.debug.tsan    Run all tests with TSan"
	@echo "  make test.unit          Run unit tests"
	@echo "  make test.basic         Run basic delegation tests"
	@echo "  make test.module        Run module coordination tests"
	@echo "  make test.component     Run component coordination tests"
	@echo "  make test.async         Run async delegation tests"
	@echo "  make test.distributed   Run distributed hierarchy tests"
	@echo "  make test.concurrency   Run concurrency unit tests"
	@echo "  make test.simulation    Run simulation unit tests"
	@echo "  make test.grpc          Run gRPC tests (requires .grpc build)"
	@echo "  make test.profiling     Run profiling tests"
	@echo ""
	@echo "Benchmarks & Load Testing:"
	@echo "  make benchmark          Run all benchmarks (release build)"
	@echo "  make benchmark.message-pool  Run message pool benchmarks"
	@echo "  make benchmark.distributed   Run distributed benchmarks"
	@echo "  make benchmark.strings       Run string allocation benchmarks"
	@echo "  make load-test          Run all load tests (full)"
	@echo "  make load-test.quick    Run load tests (quick, for CI)"
	@echo ""
	@echo "Coverage:"
	@echo "  make coverage           Generate coverage report"
	@echo ""
	@echo "Lint & Format:"
	@echo "  make lint               Run all linters"
	@echo "  make lint.clang-tidy    Run clang-tidy only"
	@echo "  make lint.cppcheck      Run cppcheck only"
	@echo "  make format             Format all C++ files"
	@echo "  make format.check       Check formatting (CI)"
	@echo ""
	@echo "CI/CD:"
	@echo "  make ci                 Full CI pipeline (build, test, lint, format)"
	@echo "  make ci.quick           Quick CI for PRs"
	@echo "  make pre-commit         Pre-commit checks"
	@echo ""
	@echo "Docker:"
	@echo "  make docker.build       Build Docker image"
	@echo "  make docker.up          Start dev container"
	@echo "  make docker.down        Stop containers"
	@echo "  make docker.shell       Enter dev container"
	@echo ""
	@echo "Native Mode (run on host instead of Docker):"
	@echo "  Append .native to any target:"
	@echo "    make compile.debug.asan.native   Build with ASan on host"
	@echo "    make test.debug.asan.native      Run ASan tests on host"
	@echo "    make benchmark.native            Run benchmarks on host"
	@echo "    make lint.native                 Run linters on host"
	@echo ""
	@echo "Clean:"
	@echo "  make clean              Clean current build directory"
	@echo "  make clean.debug        Clean debug build"
	@echo "  make clean.release.tsan Clean release TSan build"
	@echo ""
	@echo "Examples:"
	@echo "  make compile.debug.asan           # Build debug with ASan in Docker"
	@echo "  make test.debug.asan              # Run tests with ASan in Docker"
	@echo "  make compile.debug.asan.native    # Build debug with ASan on host"
	@echo "  make test.debug.tsan.native       # Run TSan tests on host"
	@echo "  make benchmark.native             # Run benchmarks on host"
