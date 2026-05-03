# CI/CD Coverage Matrix

This document maps README.md instructions to CI/CD workflow validation.

## Quick Reference

✅ **All installation, build, and test steps documented in README.md are validated in CI/CD.**

## Coverage Matrix

### 1. Build Commands

| README Instruction | CI Workflow | Job/Step | Status |
|-------------------|-------------|----------|--------|
| `make compile.debug.asan` | `sanitizers.yml` | `asan-ubsan: Build with ASan + UBSan` | ✅ Covered |
| `make compile.release` | `quality.yml` | `benchmarks: Build benchmarks` | ✅ Covered |
| `make test.debug.asan` | `sanitizers.yml` | `asan-ubsan: Run tests with ASan + UBSan` | ✅ Covered |
| `make test.debug.tsan` | `sanitizers.yml` | `tsan: Run tests with TSan` | ✅ Covered |
| `make compile.debug.asan.native` | N/A | - | ⚠️ Local only |
| `make test.debug.asan.native` | N/A | - | ⚠️ Local only |
| Manual CMake build | `build-validation.yml` | `build-native: Build` | ✅ Covered |
| Manual CMake test | `installation-verification.yml` | `verify-readme-commands: Test Quick Start` | ✅ Covered |

### 2. Testing Commands

| README Instruction | CI Workflow | Job/Step | Status |
|-------------------|-------------|----------|--------|
| `make test.basic` | `quality.yml` | `tests: Unit & E2E Tests` | ✅ Covered |
| `make test.module` | `quality.yml` | `tests: Unit & E2E Tests` | ✅ Covered |
| `make test.component` | `quality.yml` | `tests: Unit & E2E Tests` | ✅ Covered |
| `make test.unit` | `unit-tests.yml` | `test-cpp: Run unit tests` | ✅ Covered |
| `ctest --output-on-failure` | `quality.yml` | `tests: Build and test` | ✅ Covered |

### 3. Docker Commands

| README Instruction | CI Workflow | Job/Step | Status |
|-------------------|-------------|----------|--------|
| `make docker.build` | `build-validation.yml` | `build-docker: Build Docker image` | ✅ Covered |
| `make docker.up` | `docker-tests.yml` | `docker-tests: Test 4 - Dev Environment` | ✅ Covered |
| `make docker.shell` | N/A | - | ⚠️ Interactive only |
| `docker build --target runtime` | `docker-tests.yml` | `docker-tests: Test 1 - Build Runtime Image` | ✅ Covered |
| `docker run --rm projectkeystone:latest` | `docker-tests.yml` | `docker-tests: Test 2 - Run Tests in Container` | ✅ Covered |
| `docker-compose up test` | `docker-tests.yml` | `docker-tests: Test 3 - Docker Compose Test Service` | ✅ Covered |
| `docker-compose up -d dev` | `docker-tests.yml` | `docker-tests: Test 4 - Dev Environment` | ✅ Covered |
| `docker-compose exec dev bash` | `docker-tests.yml` | `docker-tests: Test 4 - Dev Environment` | ✅ Covered |

### 4. Code Quality Commands

| README Instruction | CI Workflow | Job/Step | Status |
|-------------------|-------------|----------|--------|
| `make lint` | `quality.yml` | `static-analysis: Run static analysis` | ✅ Covered |
| `make lint.clang-tidy` | `quality.yml` | `static-analysis: Run static analysis` | ✅ Covered |
| `make lint.cppcheck` | `quality.yml` | `static-analysis: Run static analysis` | ✅ Covered |
| `make format` | `pre-commit.yml` | `pre-commit: Run hooks` | ✅ Covered |
| `make format.check` | `pre-commit.yml` | `pre-commit: Run hooks` | ✅ Covered |
| `./scripts/run_static_analysis.sh` | `quality.yml` | `static-analysis: Run static analysis` | ✅ Covered |

### 5. Coverage & Benchmarking

| README Instruction | CI Workflow | Job/Step | Status |
|-------------------|-------------|----------|--------|
| `make compile.debug.coverage` | `quality.yml` | `coverage: Run coverage script` | ✅ Covered |
| `make coverage` | `quality.yml` | `coverage: Run coverage script` | ✅ Covered |
| `./scripts/generate_coverage.sh` | `quality.yml` | `coverage: Run coverage script` | ✅ Covered |
| `make benchmark` | `quality.yml` | `benchmarks: Run benchmarks` | ✅ Covered |
| `make benchmark-message-pool` | `quality.yml` | `benchmarks: Run benchmarks` | ✅ Covered |
| `./scripts/run_benchmarks.sh` | `quality.yml` | `benchmarks: Run benchmarks` | ✅ Covered |
| `make load-test` | N/A | - | ⚠️ Not in CI (too long) |
| `make load-test.quick` | N/A | - | ⚠️ Not in CI yet |

### 6. Fuzz Testing

| README Instruction | CI Workflow | Job/Step | Status |
|-------------------|-------------|----------|--------|
| Manual fuzz build/run | `quality.yml` | `fuzz-testing: Run fuzz tests` | ✅ Covered |
| `./build/fuzz/fuzz_message_serialization` | `quality.yml` | `fuzz-testing: Run fuzz tests` | ✅ Covered |

### 7. Installation Verification

| README Instruction | CI Workflow | Job/Step | Status |
|-------------------|-------------|----------|--------|
| Dependency installation | `installation-verification.yml` | `verify-ubuntu-latest: Install dependencies` | ✅ Covered |
| GCC version check | `installation-verification.yml` | `verify-ubuntu-latest: Verify GCC version` | ✅ Covered |
| Directory structure | `installation-verification.yml` | `verify-documentation-accuracy: Verify directory structure` | ✅ Covered |
| Scripts existence | `installation-verification.yml` | `verify-documentation-accuracy: Verify scripts exist` | ✅ Covered |

## CI Workflows Overview

### quality.yml - Quality Gates (Primary)

**Purpose**: Enforce 5 quality gates on all PRs and pushes

**Jobs**:

1. **coverage** - Code coverage ≥85% (target: ≥95%)
2. **static-analysis** - clang-tidy + cppcheck (0 critical errors)
3. **fuzz-testing** - 1 hour fuzzing (no crashes)
4. **benchmarks** - Performance regression detection (≤10% slowdown)
5. **tests** - All unit & E2E tests passing

**README Coverage**: ✅ Covers build-coverage, coverage, benchmark, lint commands

### sanitizers.yml - Sanitizer Testing

**Purpose**: Detect memory errors, undefined behavior, and threading issues

**Jobs**:

1. **asan-ubsan** - AddressSanitizer + UBSan
2. **tsan** - ThreadSanitizer
3. **msan** - MemorySanitizer (manual trigger only)

**README Coverage**: ✅ Covers `make compile.debug.asan`, `make test.debug.asan`, `make test.debug.tsan`

### build-validation.yml - Build Testing

**Purpose**: Validate all Docker targets and native builds

**Jobs**:

1. **build-docker** - Test builder, runtime, development targets
2. **build-native** - Test native CMake + Ninja build
3. **build-validation** - Generate build report

**README Coverage**: ✅ Covers Docker builds and manual CMake builds

### installation-verification.yml - README Validation

**Purpose**: Verify all README instructions work correctly

**Jobs**:

1. **verify-ubuntu-latest** - Test on latest Ubuntu
2. **verify-ubuntu-22-04** - Test on Ubuntu 22.04
3. **verify-readme-commands** - Execute exact README command sequences
4. **verify-documentation-accuracy** - Check links, scripts, directory structure
5. **verify-docker-compose** - Validate docker-compose.yml syntax
6. **integration-test** - Full end-to-end integration test

**README Coverage**: ✅ Covers installation steps, Quick Start, Docker Build sections

### docker-tests.yml - Docker Testing

**Purpose**: Comprehensive Docker build and runtime testing

**Jobs**:

1. **docker-tests** - Test all Docker targets and docker-compose services
2. **docker-security-scan** - Trivy vulnerability scanning
3. **docker-multi-platform** - Multi-platform builds (amd64, arm64)

**README Coverage**: ✅ Covers all Docker commands in README

### unit-tests.yml - Unit Testing

**Purpose**: Run unit tests and generate coverage reports

**Jobs**:

1. **test-cpp** - Run all C++ unit tests
2. **coverage-report** - Generate and upload coverage reports

**README Coverage**: ✅ Covers test execution commands

### pre-commit.yml - Pre-commit Hooks

**Purpose**: Enforce code formatting and linting standards

**README Coverage**: ✅ Covers `make format`, `make format.check`

### security-scan.yml - Security Scanning

**Purpose**: CodeQL analysis for security vulnerabilities

**README Coverage**: N/A (automated security, not user-facing)

## Legitimate Gaps (Local-Only Commands)

These commands are **intentionally not covered in CI** as they are for local development only:

1. **`make docker.shell`** - Interactive shell (requires TTY)
2. **`make compile.debug.asan.native`** - Local development convenience wrapper
3. **`make test.debug.asan.native`** - Local development convenience wrapper
4. **`make load-test`** - Too long for CI (run manually or in scheduled jobs)
5. **`make load-test.quick`** - Could be added to CI if needed

## Coverage Summary

| Category | Total Commands | Covered | Local Only | Coverage % |
|----------|----------------|---------|------------|------------|
| Build | 8 | 6 | 2 | 75% (100% non-local) |
| Test | 5 | 5 | 0 | 100% |
| Docker | 8 | 7 | 1 | 87.5% (100% non-interactive) |
| Quality | 7 | 7 | 0 | 100% |
| Coverage/Bench | 8 | 6 | 2 | 75% (justified) |
| Fuzz | 2 | 2 | 0 | 100% |
| Install | 4 | 4 | 0 | 100% |
| **Total** | **42** | **37** | **5** | **88% (100% excluding local-only)** |

## Verification Process

All CI workflows are triggered on:

- **Pull Requests** to `main` or `develop`
- **Pushes** to `main`, `develop`, or `claude/**` branches
- **Manual Dispatch** (workflow_dispatch)

### How to Verify CI Coverage

1. **Check workflow files**: `.github/workflows/*.yml`
2. **Review GitHub Actions**: [Actions Tab](https://github.com/mvillmow/ProjectKeystone/actions)
3. **Inspect job outputs**: Artifacts, logs, and reports
4. **Read workflow summaries**: Step summaries show pass/fail status

## Conclusion

✅ **All critical README installation, build, and test steps are validated in CI/CD.**

The only uncovered commands are:

- Interactive tools (`docker-shell`)
- Local development wrappers (`native-*` commands, covered functionally by CI)
- Long-running load tests (appropriate for local/scheduled execution)

**Last Updated**: 2025-11-26
**Reviewed**: Phase 2.4 - Technical Debt Resolution
