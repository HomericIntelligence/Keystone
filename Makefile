# Keystone Makefile
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

# Container runtime (Podman)
# Use podman-compose instead of docker compose CLI plugin (which delegates to snap)
CONTAINER_CHECK := DOCKER_HOST="$(DOCKER_HOST)" podman-compose up -d dev >/dev/null 2>&1 || true;
CONTAINER_PREFIX := DOCKER_HOST="$(DOCKER_HOST)" podman-compose exec -T dev

# Sanitizer runtime options that must be set INSIDE the dev container.
# `podman-compose exec` does NOT forward the host's TSAN_OPTIONS into the
# container, so a TSAN_OPTIONS set on the CI runner (pointing at a host path
# like $GITHUB_WORKSPACE/tsan.supp) never reaches ctest, leaving the
# moodycamel::ConcurrentQueue suppressions in tsan.supp unloaded and failing
# tests on benign internal queue races (e.g. SimulationCornerCaseTest.MessageFlood).
# The repo is mounted at /workspace (see docker-compose.yml), so reference the
# suppression file by its in-container path. second_deadlock_stack=1 mirrors the
# CI default. Set unconditionally — harmless for non-TSan builds, which ignore it.
CONTAINER_TSAN_OPTIONS := suppressions=/workspace/tsan.supp:second_deadlock_stack=1

# Compiler flags
BUILD_FLAGS_debug := -O0 -g -D_DEBUG
BUILD_FLAGS_release := -O3 -DNDEBUG
# Issue #586: KEYSTONE_SANITIZER_BUILD gates a GTEST_SKIP() in two ThreadPool
# construction/destruction tests whose runtime exceeds CTEST_TIMEOUT under
# thread-instrumentation. The non-sanitizer unit-tests CI job runs them
# unfiltered.
BUILD_FLAGS_asan := -fsanitize=address -fno-omit-frame-pointer -DKEYSTONE_SANITIZER_BUILD=1
BUILD_FLAGS_ubsan := -fsanitize=undefined -fno-omit-frame-pointer -DKEYSTONE_SANITIZER_BUILD=1
BUILD_FLAGS_lsan := -fsanitize=leak -fno-omit-frame-pointer -DKEYSTONE_SANITIZER_BUILD=1
BUILD_FLAGS_tsan := -fsanitize=thread -fno-omit-frame-pointer -DKEYSTONE_SANITIZER_BUILD=1
BUILD_FLAGS_msan := -fsanitize=memory -fno-omit-frame-pointer -DKEYSTONE_SANITIZER_BUILD=1

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

# Feature options (ENABLE_COVERAGE/PROFILING/FUZZING) are CMake option() cache
# variables — they must reach cmake as -D<opt>=ON args, NOT inside
# -DCMAKE_CXX_FLAGS (where they would become inert clang preprocessor defines).
# The feature-flag pattern rules below append to this variable; `compile`
# forwards it on the cmake configure line. Kept separate from CMAKE_EXTRA_FLAGS
# so the recursive pattern-rule make does not have to override the Conan
# toolchain append above.
CMAKE_FEATURE_FLAGS ?=

# ============================================================================
# Dependency Management (Conan)
# ============================================================================

.PHONY: deps
deps:
	@echo "Installing Conan dependencies (Debug + Release)..."
	$(CONTAINER_CHECK)
	$(CONTAINER_PREFIX) conan profile detect --exist-ok
	$(CONTAINER_PREFIX) conan install . --output-folder=$(CONAN_OUTPUT_DIR) --build=missing -s build_type=Debug -s compiler.cppstd=20
	$(CONTAINER_PREFIX) conan install . --output-folder=$(CONAN_OUTPUT_DIR) --build=missing -s build_type=Release -s compiler.cppstd=20

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
	$(CONTAINER_CHECK)
	$(CONTAINER_PREFIX) bash -c "cmake -S . -B $(BUILD_DIR)/$(BUILD_SUBDIR) -G Ninja -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) -DCMAKE_CXX_FLAGS=\"$(BUILD_FLAGS)\" $(CMAKE_EXTRA_FLAGS) $(CMAKE_FEATURE_FLAGS)"
	$(CONTAINER_PREFIX) bash -c "cmake --build $(BUILD_DIR)/$(BUILD_SUBDIR) -j$(NPROC)"

# ============================================================================
# Test Recipes
# ============================================================================

.PHONY: test test.unit test.basic test.module test.component test.async test.distributed test.concurrency test.simulation test.profiling

# Test executables
TEST_BASIC := basic_delegation_tests
TEST_MODULE := module_coordination_tests
TEST_COMPONENT := component_coordination_tests
TEST_ASYNC := async_delegation_tests
TEST_DISTRIBUTED := distributed_hierarchy_tests
TEST_UNIT := unit_tests
TEST_CONCURRENCY := concurrency_unit_tests
TEST_SCHEDULER_BACKOFF := scheduler_backoff_tests
TEST_SIMULATION := simulation_unit_tests
TEST_PROFILING := profiling_tests

# Run all tests with ctest
test: compile
	@echo "Running all tests..."
	$(CONTAINER_CHECK)
	$(CONTAINER_PREFIX) bash -c "cd $(BUILD_DIR)/$(BUILD_SUBDIR) && KEYSTONE_PROFILE=1 TSAN_OPTIONS=\"$(CONTAINER_TSAN_OPTIONS)\" ctest --output-on-failure -j$(NPROC) --timeout $(CTEST_TIMEOUT)"

# Individual test suites (run specific executable)
test.unit: compile
	@echo "Running unit tests..."
	$(CONTAINER_CHECK)
	$(CONTAINER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/$(TEST_UNIT)

test.basic: compile
	@echo "Running basic delegation tests..."
	$(CONTAINER_CHECK)
	$(CONTAINER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/$(TEST_BASIC)

test.module: compile
	@echo "Running module coordination tests..."
	$(CONTAINER_CHECK)
	$(CONTAINER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/$(TEST_MODULE)

test.component: compile
	@echo "Running component coordination tests..."
	$(CONTAINER_CHECK)
	$(CONTAINER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/$(TEST_COMPONENT)

test.async: compile
	@echo "Running async delegation tests..."
	$(CONTAINER_CHECK)
	$(CONTAINER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/$(TEST_ASYNC)

test.distributed: compile
	@echo "Running distributed hierarchy tests..."
	$(CONTAINER_CHECK)
	$(CONTAINER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/$(TEST_DISTRIBUTED)

test.concurrency: compile
	@echo "Running concurrency unit tests..."
	$(CONTAINER_CHECK)
	$(CONTAINER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/$(TEST_CONCURRENCY)
	$(CONTAINER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/$(TEST_SCHEDULER_BACKOFF)

test.simulation: compile
	@echo "Running simulation unit tests..."
	$(CONTAINER_CHECK)
	$(CONTAINER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/$(TEST_SIMULATION)

test.profiling: compile.profile
	@echo "Running profiling tests..."
	$(CONTAINER_CHECK)
	$(CONTAINER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/$(TEST_PROFILING)

# ============================================================================
# Benchmark Recipes
# ============================================================================

.PHONY: benchmark benchmark.message-pool benchmark.distributed benchmark.strings

# Run all benchmarks
benchmark: compile.release
	@echo "Running benchmarks..."
	$(CONTAINER_CHECK)
	$(CONTAINER_PREFIX) ./scripts/run_benchmarks.sh

# Individual benchmark targets
benchmark.message-pool: compile.release
	@echo "Running message pool benchmarks..."
	$(CONTAINER_CHECK)
	$(CONTAINER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/message_pool_benchmarks

benchmark.distributed: compile.release
	@echo "Running distributed benchmarks..."
	$(CONTAINER_CHECK)
	$(CONTAINER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/distributed_benchmarks

benchmark.strings: compile.release
	@echo "Running string allocation benchmarks..."
	$(CONTAINER_CHECK)
	$(CONTAINER_PREFIX) ./$(BUILD_DIR)/$(BUILD_SUBDIR)/string_allocation_benchmarks

# ============================================================================
# Load Testing — removed: the hmas_load_test binary exercised the agent
# hierarchy extracted to ProjectAgamemnon per ADR-015.
# ============================================================================

# ============================================================================
# Coverage
# ============================================================================

.PHONY: coverage

coverage: compile.coverage
	@echo "Generating coverage report..."
	$(CONTAINER_CHECK)
	$(CONTAINER_PREFIX) ./scripts/generate_coverage.sh

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
	$(CONTAINER_CHECK)
	$(CONTAINER_PREFIX) ./scripts/run_static_analysis.sh $(LINT_FLAGS);

%.clang-tidy:
	@echo "Running clang-tidy..."
	@$(MAKE) $* LINT_FLAGS=--clang-tidy-only

%.cppcheck:
	@echo "Running cppcheck..."
	$(CONTAINER_CHECK)
	@$(MAKE) $* LINT_FLAGS=--cppcheck-only

# ============================================================================
# Code Formatting
# ============================================================================

.PHONY: format format.check

format:
	@echo "Formatting C++ code with clang-format..."
	$(CONTAINER_CHECK)
	$(CONTAINER_PREFIX) bash -c \
			"find src include tests benchmarks -type f \( -name '*.cpp' -o -name '*.hpp' \) \
			-not -path '*/build/*' -not -path '*/_deps/*' \
			| xargs clang-format -i --Werror";
	@echo "✓ Formatting complete"

format.check:
	@echo "Checking C++ formatting..."
	$(CONTAINER_CHECK)
	$(CONTAINER_PREFIX) bash -c \
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
# Container Management (Podman)
# ============================================================================

.PHONY: container.build container.up container.clean container.down container.shell

container.build:
	@echo "Building container image: dev..."
	DOCKER_HOST="$(DOCKER_HOST)" podman-compose build dev

container.build.%:
	@echo "Building container image: $*..."
	DOCKER_HOST="$(DOCKER_HOST)" podman-compose build $*

container.up:
	@echo "Starting dev container..."
	DOCKER_HOST="$(DOCKER_HOST)" podman-compose up -d dev
	sleep 2

container.clean:
	@echo "Cleaning container resources..."
	DOCKER_HOST="$(DOCKER_HOST)" podman-compose down -v
	podman rmi -f keystone-dev:latest keystone:latest || true

container.down:
	@echo "Stopping containers..."
	DOCKER_HOST="$(DOCKER_HOST)" podman-compose down

container.shell: container.up
	$(CONTAINER_PREFIX) /bin/bash

# ============================================================================
# Build Variants
# ============================================================================

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
# ENABLE_COVERAGE/PROFILING/FUZZING are CMake option() cache variables (see
# CMakeLists.txt), NOT compiler flags. They are forwarded via
# CMAKE_FEATURE_FLAGS (-D<opt>=ON cmake args) rather than BUILD_FLAGS, which is
# passed inside -DCMAKE_CXX_FLAGS="..." and would land as an inert clang
# preprocessor define (-DENABLE_COVERAGE=ON) that never toggles the option.
# With the option left OFF the build is not instrumented, so coverage produced
# zero .gcda files and the coverage job's generate_coverage.sh failed with
# "no .gcda files found".
%.coverage:
	@$(MAKE) $* CMAKE_FEATURE_FLAGS="$(CMAKE_FEATURE_FLAGS) -DENABLE_COVERAGE=ON" BUILD_SUBDIR="$(BUILD_SUBDIR)$(suffix $@)" CMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)

%.profile:
	@$(MAKE) $* CMAKE_FEATURE_FLAGS="$(CMAKE_FEATURE_FLAGS) -DENABLE_PROFILING=ON" BUILD_SUBDIR="$(BUILD_SUBDIR)$(suffix $@)" CMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)

%.fuzz:
	@$(MAKE) $* CMAKE_FEATURE_FLAGS="$(CMAKE_FEATURE_FLAGS) -DENABLE_FUZZING=ON" BUILD_SUBDIR="$(BUILD_SUBDIR)$(suffix $@)" CMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)

%.debug:
	@$(MAKE) $* BUILD_FLAGS="$(BUILD_FLAGS) $(BUILD_FLAGS_debug)" BUILD_SUBDIR="$(BUILD_SUBDIR)$(suffix $@)" CMAKE_BUILD_TYPE=Debug

%.release:
	@$(MAKE) $* BUILD_FLAGS="$(BUILD_FLAGS) $(BUILD_FLAGS_release)" BUILD_SUBDIR="$(BUILD_SUBDIR)$(suffix $@)" CMAKE_BUILD_TYPE=Release

# ============================================================================
# Help & Info
# ============================================================================

.PHONY: help

help:
	@echo "Keystone Makefile"
	@echo "Unified build system with debug, release, sanitizer modes, and testing"
	@echo ""
	@echo "Usage: make <target>[.modifier]"
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
	@echo "  make test.profiling     Run profiling tests"
	@echo ""
	@echo "Benchmarks & Load Testing:"
	@echo "  make benchmark          Run all benchmarks (release build)"
	@echo "  make benchmark.message-pool  Run message pool benchmarks"
	@echo "  make benchmark.distributed   Run distributed benchmarks"
	@echo "  make benchmark.strings       Run string allocation benchmarks"
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
	@echo "Container (Podman):"
	@echo "  make container.build    Build container image"
	@echo "  make container.up       Start dev container"
	@echo "  make container.down     Stop containers"
	@echo "  make container.shell    Enter dev container"
	@echo ""
	@echo "Clean:"
	@echo "  make clean              Clean current build directory"
	@echo "  make clean.debug        Clean debug build"
	@echo "  make clean.release.tsan Clean release TSan build"
	@echo ""
	@echo "Examples:"
	@echo "  make compile.debug.asan           # Build debug with ASan (in container)"
	@echo "  make test.debug.asan              # Run tests with ASan (in container)"
	@echo "  make test.debug.tsan              # Run TSan tests (in container)"
	@echo "  make benchmark                    # Run benchmarks (in container)"
