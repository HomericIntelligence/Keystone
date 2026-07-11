#!/usr/bin/env bash
# Installation Verification Script for Keystone HMAS
# Verifies all build/test/install steps from README.md

set -e  # Exit on error
set -u  # Exit on undefined variable

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test counter
TESTS_PASSED=0
TESTS_FAILED=0

# Helper functions
print_header() {
    echo -e "\n${YELLOW}===================================================${NC}"
    echo -e "${YELLOW}$1${NC}"
    echo -e "${YELLOW}===================================================${NC}\n"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

print_info() {
    echo -e "$1"
}

run_check() {
    local check_name="$1"
    shift

    if "$@"; then
        print_success "$check_name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        print_error "$check_name FAILED"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

# Check 1: Prerequisites
check_prerequisites() {
    print_header "Checking Prerequisites"

    local all_good=true

    # Check C++ compiler
    if command -v g++ &> /dev/null; then
        local gcc_version=$(g++ --version | head -1)
        print_success "GCC found: $gcc_version"

        # Check version >= 13
        local version_num=$(g++ -dumpversion | cut -d. -f1)
        if [[ $version_num -ge 13 ]]; then
            print_success "GCC version >= 13"
        else
            print_error "GCC version $version_num < 13 (required)"
            all_good=false
        fi
    elif command -v clang++ &> /dev/null; then
        local clang_version=$(clang++ --version | head -1)
        print_success "Clang found: $clang_version"

        # Check version >= 15
        local version_num=$(clang++ --version | grep -oP 'version \K\d+' | head -1)
        if [[ $version_num -ge 15 ]]; then
            print_success "Clang version >= 15"
        else
            print_error "Clang version $version_num < 15 (required)"
            all_good=false
        fi
    else
        print_error "No C++20 compiler found (GCC 13+ or Clang 15+ required)"
        all_good=false
    fi

    # Check CMake
    if command -v cmake &> /dev/null; then
        local cmake_version=$(cmake --version | head -1)
        print_success "CMake found: $cmake_version"

        local version_num=$(cmake --version | grep -oP '\d+\.\d+' | head -1)
        if [[ $(echo "$version_num >= 3.20" | bc -l) -eq 1 ]]; then
            print_success "CMake version >= 3.20"
        else
            print_error "CMake version $version_num < 3.20 (required)"
            all_good=false
        fi
    else
        print_error "CMake not found (version 3.20+ required)"
        all_good=false
    fi

    # Check Ninja
    if command -v ninja &> /dev/null; then
        local ninja_version=$(ninja --version)
        print_success "Ninja found: version $ninja_version"
    else
        print_error "Ninja not found (required for build)"
        all_good=false
    fi

    # Check Docker (optional)
    if command -v docker &> /dev/null; then
        local docker_version=$(docker --version | grep -oP '\d+\.\d+' | head -1)
        print_success "Docker found: version $docker_version (optional)"
    else
        print_info "Docker not found (optional for containerized builds)"
    fi

    $all_good
}

# Check 2: Build Configuration
check_build_configuration() {
    print_header "Verifying Build Configuration"

    # Create build directory if it doesn't exist
    if [[ ! -d "build" ]]; then
        print_info "Creating build directory..."
        mkdir build
    fi

    cd build

    # Run CMake configuration
    print_info "Running CMake configuration..."
    if cmake -G Ninja .. >/dev/null 2>&1; then
        print_success "CMake configuration successful"
        return 0
    else
        print_error "CMake configuration failed"
        cmake -G Ninja ..  # Run again with output for debugging
        return 1
    fi
}

# Check 3: Build Project
check_build_project() {
    print_header "Building Project"

    cd build

    print_info "Running Ninja build..."
    if ninja 2>&1 | tee build.log; then
        print_success "Build successful"
        return 0
    else
        print_error "Build failed - check build.log"
        return 1
    fi
}

# Check 4: Run Tests
check_run_tests() {
    print_header "Running Tests"

    cd build

    print_info "Running ctest..."
    if ctest --output-on-failure 2>&1 | tee test.log; then
        print_success "All tests passed"

        # Count tests
        local test_count=$(grep -oP '\d+/\d+ Test' test.log | head -1 | cut -d'/' -f2 | cut -d' ' -f1 || echo "unknown")
        print_info "Total tests run: $test_count"

        return 0
    else
        print_error "Some tests failed - check test.log"
        return 1
    fi
}

# Check 5: Docker Build (Optional)
check_docker_build() {
    print_header "Checking Docker Build (Optional)"

    if ! command -v docker &> /dev/null; then
        print_info "Docker not available - skipping Docker build check"
        return 0
    fi

    if ! command -v docker-compose &> /dev/null; then
        print_info "docker-compose not available - skipping Docker build check"
        return 0
    fi

    cd ..  # Return to project root

    print_info "Running docker-compose build..."
    if docker-compose build test >/dev/null 2>&1; then
        print_success "Docker build successful"

        print_info "Running docker-compose tests..."
        if docker-compose up --exit-code-from test test >/dev/null 2>&1; then
            print_success "Docker tests passed"
            return 0
        else
            print_error "Docker tests failed"
            return 1
        fi
    else
        print_error "Docker build failed"
        return 1
    fi
}

# Check 6: Coverage Script (Optional)
check_coverage_script() {
    print_header "Checking Coverage Generation (Optional)"

    if [[ ! -f "scripts/generate_coverage.sh" ]]; then
        print_info "Coverage script not found - skipping"
        return 0
    fi

    if ! command -v lcov &> /dev/null; then
        print_info "lcov not installed - skipping coverage check"
        return 0
    fi

    print_info "Verifying coverage script exists and is executable..."
    if [[ -x "scripts/generate_coverage.sh" ]]; then
        print_success "Coverage script is executable"
        return 0
    else
        print_error "Coverage script is not executable"
        return 1
    fi
}

# Check 7: Benchmark Scripts (Optional)
check_benchmark_scripts() {
    print_header "Checking Benchmark Scripts (Optional)"

    if [[ ! -f "scripts/run_benchmarks.sh" ]]; then
        print_info "Benchmark script not found - skipping"
        return 0
    fi

    print_info "Verifying benchmark script exists and is executable..."
    if [[ -x "scripts/run_benchmarks.sh" ]]; then
        print_success "Benchmark script is executable"
        return 0
    else
        print_error "Benchmark script is not executable"
        return 1
    fi
}

# Cleanup function
cleanup() {
    # PROJECT_ROOT is set by main(); if cleanup runs before that (impossible under
    # normal flow, but defensive), just no-op rather than aborting with `set -e`.
    if [[ -n "${PROJECT_ROOT:-}" && -d "$PROJECT_ROOT" ]]; then
        cd "$PROJECT_ROOT"
    fi
}

# Main execution
main() {
    # Save project root
    PROJECT_ROOT="$(pwd)"

    print_header "Keystone HMAS - Installation Verification"
    print_info "Verifying all installation and build steps from README.md..."

    # Trap cleanup on exit
    trap cleanup EXIT

    # Run checks. run_check already records pass/fail into TESTS_PASSED/TESTS_FAILED,
    # so a failing check must NOT abort the run — capture its rc into _rc explicitly
    # instead of suppressing with `|| true`.
    local _rc
    run_check "Prerequisites Check" check_prerequisites && _rc=0 || _rc=$?
    run_check "Build Configuration" check_build_configuration && _rc=0 || _rc=$?
    run_check "Project Build" check_build_project && _rc=0 || _rc=$?
    run_check "Test Execution" check_run_tests && _rc=0 || _rc=$?
    run_check "Docker Build (Optional)" check_docker_build && _rc=0 || _rc=$?
    run_check "Coverage Script (Optional)" check_coverage_script && _rc=0 || _rc=$?
    run_check "Benchmark Scripts (Optional)" check_benchmark_scripts && _rc=0 || _rc=$?
    : "${_rc:=0}"

    # Print summary
    print_header "Verification Summary"
    print_info "Checks Passed: ${GREEN}$TESTS_PASSED${NC}"
    print_info "Checks Failed: ${RED}$TESTS_FAILED${NC}"

    if [[ $TESTS_FAILED -eq 0 ]]; then
        print_success "All installation verification checks passed!"
        print_info "Keystone HMAS is ready to use."
        exit 0
    else
        print_error "Some verification checks failed."
        print_info "Please review the errors above and ensure all prerequisites are met."
        exit 1
    fi
}

# Run main function
main "$@"
