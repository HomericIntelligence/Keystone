#!/usr/bin/env bash
# Docker Testing Script for ProjectKeystone HMAS
# Automated version of DOCKER_TESTING.md test scenarios

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

run_test() {
    local test_name="$1"
    shift

    print_header "Test: $test_name"

    if "$@"; then
        print_success "$test_name PASSED"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        print_error "$test_name FAILED"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

# Test 1: Build Runtime Image
test_build_runtime() {
    print_info "Building runtime image..."

    docker build --target runtime -t projectkeystone:latest .

    # Check image size
    local image_size=$(docker images projectkeystone:latest --format "{{.Size}}")
    print_info "Image size: $image_size"

    # Verify image exists
    if docker images | grep -q "projectkeystone.*latest"; then
        print_success "Runtime image built successfully"
        return 0
    else
        print_error "Runtime image not found"
        return 1
    fi
}

# Test 2: Run Tests in Container
test_run_tests() {
    print_info "Running tests in container..."

    # Run tests and capture output
    if docker run --rm projectkeystone:latest; then
        print_success "Tests passed in container"
        return 0
    else
        print_error "Tests failed in container"
        return 1
    fi
}

# Test 3: Docker Compose - Test Service
test_docker_compose_test() {
    print_info "Running docker-compose test service..."

    # Build and run tests
    if docker-compose up --exit-code-from test test; then
        print_success "Docker Compose test service passed"
        return 0
    else
        print_error "Docker Compose test service failed"
        return 1
    fi
}

# Test 4: Docker Compose - Development Environment
test_docker_compose_dev() {
    print_info "Testing docker-compose development environment..."

    # Start dev container
    docker-compose up -d dev

    # Wait for container to be ready
    sleep 2

    # Run build and test inside container
    if docker-compose exec -T dev bash -c "cd build && cmake -G Ninja .. >/dev/null 2>&1 && ninja && ctest --output-on-failure"; then
        print_success "Development environment test passed"
        docker-compose down
        return 0
    else
        print_error "Development environment test failed"
        docker-compose down
        return 1
    fi
}

# Test 5: Build from Scratch (No Cache)
test_clean_build() {
    print_info "Building from scratch (no cache)..."

    # Clean build
    docker-compose build --no-cache test

    # Run tests
    if docker-compose up --exit-code-from test test; then
        print_success "Clean build and test passed"
        return 0
    else
        print_error "Clean build and test failed"
        return 1
    fi
}

# Cleanup function
cleanup() {
    print_header "Cleanup"
    print_info "Stopping containers..."
    if ! docker-compose down 2>/dev/null; then
        echo "warn: docker-compose down encountered errors (idempotent)" >&2
    fi
    print_success "Cleanup complete"
}

# Main execution
main() {
    print_header "ProjectKeystone HMAS - Docker Testing Suite"
    print_info "Running automated Docker tests..."

    # Check prerequisites
    print_header "Prerequisites Check"

    if ! command -v docker &> /dev/null; then
        print_error "Docker not found. Please install Docker 20.10+"
        exit 1
    fi

    if ! command -v docker-compose &> /dev/null; then
        print_error "docker-compose not found. Please install docker-compose 1.29+"
        exit 1
    fi

    local docker_version=$(docker --version | grep -oP '\d+\.\d+' | head -1)
    local compose_version=$(docker-compose --version | grep -oP '\d+\.\d+' | head -1)

    print_success "Docker version: $docker_version"
    print_success "docker-compose version: $compose_version"

    # Trap cleanup on exit
    trap cleanup EXIT

    # Run tests. run_test already records pass/fail into TESTS_PASSED/TESTS_FAILED,
    # so a failing test must NOT abort the run — capture its rc into _rc explicitly
    # instead of suppressing with `|| true`.
    local _rc
    run_test "Test 1: Build Runtime Image" test_build_runtime && _rc=0 || _rc=$?
    run_test "Test 2: Run Tests in Container" test_run_tests && _rc=0 || _rc=$?
    run_test "Test 3: Docker Compose Test Service" test_docker_compose_test && _rc=0 || _rc=$?
    run_test "Test 4: Docker Compose Development Environment" test_docker_compose_dev && _rc=0 || _rc=$?

    # Test 5 is optional (takes longest time)
    if [[ "${SKIP_CLEAN_BUILD:-0}" != "1" ]]; then
        run_test "Test 5: Clean Build (No Cache)" test_clean_build && _rc=0 || _rc=$?
    else
        print_info "Skipping Test 5 (Clean Build) - Set SKIP_CLEAN_BUILD=0 to enable"
    fi
    : "${_rc:=0}"  # silence shellcheck unused-var; _rc is intentionally discarded

    # Print summary
    print_header "Test Summary"
    print_info "Tests Passed: ${GREEN}$TESTS_PASSED${NC}"
    print_info "Tests Failed: ${RED}$TESTS_FAILED${NC}"

    if [[ $TESTS_FAILED -eq 0 ]]; then
        print_success "All tests passed!"
        exit 0
    else
        print_error "Some tests failed. Please check the output above."
        exit 1
    fi
}

# Run main function
main "$@"
