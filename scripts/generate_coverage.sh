#!/bin/bash
# generate_coverage.sh
# Generates code coverage reports for ProjectKeystone
#
# Usage: ./scripts/generate_coverage.sh [--html-only]
#
# Requirements:
# - lcov installed (apt-get install lcov or brew install lcov)
# - Must be run from project root directory

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
PROJECT_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
# Resolve BUILD_DIR to absolute path so paths remain valid after 'cd "$BUILD_DIR"'
_raw_build="${BUILD_DIR:-$PROJECT_ROOT/build/coverage}"
if [[ "$_raw_build" = /* ]]; then
  BUILD_DIR="$_raw_build"
else
  BUILD_DIR="$PROJECT_ROOT/$_raw_build"
fi
unset _raw_build
COVERAGE_DIR="${COVERAGE_OUTPUT_DIR:-$BUILD_DIR/reports/coverage}"
HTML_OUTPUT_DIR="$COVERAGE_DIR/html"
COVERAGE_INFO="$COVERAGE_DIR/coverage.info"
COVERAGE_FILTERED="$COVERAGE_DIR/coverage_filtered.info"

HTML_ONLY=false

# Parse arguments
if [[ "$1" == "--html-only" ]]; then
    HTML_ONLY=true
fi

# Check if lcov is installed
if ! command -v lcov &> /dev/null; then
    echo -e "${RED}Error: lcov is not installed${NC}"
    echo "Install with:"
    echo "  Ubuntu/Debian: sudo apt-get install lcov"
    echo "  macOS: brew install lcov"
    exit 1
fi

# Check if genhtml is installed
if ! command -v genhtml &> /dev/null; then
    echo -e "${RED}Error: genhtml is not installed (should come with lcov)${NC}"
    exit 1
fi

echo -e "${BLUE}=== ProjectKeystone Code Coverage Generator ===${NC}"
echo ""

# Create coverage directory
mkdir -p "$COVERAGE_DIR"

if [[ "$HTML_ONLY" == "false" ]]; then
    # Clean previous build
    echo -e "${YELLOW}Cleaning previous build...${NC}"
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    # Configure with coverage enabled
    echo -e "${YELLOW}Configuring CMake with coverage enabled...${NC}"
    # Pass Conan toolchain if it exists
    TOOLCHAIN_ARG=""
    if [ -f "$PROJECT_ROOT/build/conan-deps/conan_toolchain.cmake" ]; then
        TOOLCHAIN_ARG="-DCMAKE_TOOLCHAIN_FILE=$PROJECT_ROOT/build/conan-deps/conan_toolchain.cmake"
    fi
    cmake -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug -G Ninja $TOOLCHAIN_ARG "$PROJECT_ROOT"

    if [[ $? -ne 0 ]]; then
        echo -e "${RED}CMake configuration failed${NC}"
        exit 1
    fi

    # Build
    echo -e "${YELLOW}Building project...${NC}"
    ninja

    if [[ $? -ne 0 ]]; then
        echo -e "${RED}Build failed${NC}"
        exit 1
    fi

    # Reset coverage counters
    echo -e "${YELLOW}Resetting coverage counters...${NC}"
    lcov --zerocounters --directory .

    # Run tests (continue even if some fail to get partial coverage)
    echo -e "${YELLOW}Running tests...${NC}"
    ctest --output-on-failure || echo -e "${YELLOW}Warning: Some tests failed, but continuing with coverage generation${NC}"

    # Capture coverage data (ignore errors from multi-threaded tests and gcov version skew)
    echo -e "${YELLOW}Capturing coverage data...${NC}"
    lcov --capture --directory . --output-file "$COVERAGE_INFO" --ignore-errors negative,mismatch,version,gcov

    if [[ $? -ne 0 ]]; then
        echo -e "${RED}Failed to capture coverage data${NC}"
        exit 1
    fi

    # Filter out system headers and third-party code
    echo -e "${YELLOW}Filtering coverage data...${NC}"
    lcov --remove "$COVERAGE_INFO" \
        '/usr/*' \
        '*/third_party/*' \
        '*/tests/*' \
        '*/_deps/*' \
        '*/googletest/*' \
        '*/benchmark/*' \
        '*/concurrentqueue/*' \
        '*/cista/*' \
        '*/spdlog/*' \
        --output-file "$COVERAGE_FILTERED"

    if [[ $? -ne 0 ]]; then
        echo -e "${RED}Failed to filter coverage data${NC}"
        exit 1
    fi
fi

# Generate HTML report
echo -e "${YELLOW}Generating HTML report...${NC}"
genhtml "$COVERAGE_FILTERED" --output-directory "$HTML_OUTPUT_DIR"

if [[ $? -ne 0 ]]; then
    echo -e "${RED}Failed to generate HTML report${NC}"
    exit 1
fi

# Print summary
echo ""
echo -e "${GREEN}=== Coverage Summary ===${NC}"
lcov --summary "$COVERAGE_FILTERED"

# Calculate coverage percentage
COVERAGE_PERCENT=$(lcov --summary "$COVERAGE_FILTERED" 2>&1 | grep -oP 'lines......: \K[0-9.]+')

echo ""
echo -e "${GREEN}Coverage report generated successfully!${NC}"
echo -e "  Report: ${BLUE}$HTML_OUTPUT_DIR/index.html${NC}"
echo -e "  Coverage: ${GREEN}$COVERAGE_PERCENT%${NC}"
echo ""

# Open report in browser (optional)
if command -v xdg-open &> /dev/null; then
    echo -e "${YELLOW}Opening report in browser...${NC}"
    xdg-open "$HTML_OUTPUT_DIR/index.html" 2>/dev/null &
elif command -v open &> /dev/null; then
    echo -e "${YELLOW}Opening report in browser...${NC}"
    open "$HTML_OUTPUT_DIR/index.html" 2>/dev/null &
fi

# Check if coverage meets threshold (85%)
THRESHOLD=85.0
if (( $(echo "$COVERAGE_PERCENT >= $THRESHOLD" | bc -l) )); then
    echo -e "${GREEN}✓ Coverage meets threshold (≥ 85%)${NC}"
    exit 0
else
    echo -e "${YELLOW}⚠ Coverage below threshold (< 85%): $COVERAGE_PERCENT%${NC}"
    echo -e "${YELLOW}  Target: $THRESHOLD%${NC}"
    exit 1
fi
