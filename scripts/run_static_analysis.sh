#!/bin/bash
# run_static_analysis.sh
# Runs static analysis on Keystone codebase
#
# Usage: ./scripts/run_static_analysis.sh [--clang-tidy-only|--cppcheck-only]
#
# Requirements:
# - clang-tidy installed
# - cppcheck installed
# - compile_commands.json generated (CMake with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
PROJECT_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build/x86.debug}"
ANALYSIS_DIR="${ANALYSIS_OUTPUT_DIR:-$PROJECT_ROOT/build/reports/analysis}"
mkdir -p "$ANALYSIS_DIR"

CLANG_TIDY_REPORT="$ANALYSIS_DIR/clang-tidy-report.txt"
CPPCHECK_REPORT="$ANALYSIS_DIR/cppcheck-report.xml"

RUN_CLANG_TIDY=true
RUN_CPPCHECK=true

# Parse arguments
for arg in "$@"; do
    case $arg in
        --clang-tidy-only)
            RUN_CPPCHECK=false
            ;;
        --cppcheck-only)
            RUN_CLANG_TIDY=false
            ;;
    esac
done

echo -e "${BLUE}=== Keystone Static Analysis ===${NC}"
echo ""

# Create analysis directory
mkdir -p "$ANALYSIS_DIR"

# Check if build directory exists
if [[ ! -d "$BUILD_DIR" ]]; then
    echo -e "${YELLOW}Build directory not found. Creating and configuring...${NC}"
    mkdir -p "$BUILD_DIR"
    cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -G Ninja
fi

# Generate compile_commands.json if not exists
if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
    echo -e "${YELLOW}Generating compile_commands.json...${NC}"
    cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -G Ninja
fi

# Run clang-tidy
if [[ "$RUN_CLANG_TIDY" == "true" ]]; then
    if ! command -v clang-tidy &> /dev/null; then
        echo -e "${RED}Error: clang-tidy is not installed${NC}"
        echo "Install with: sudo apt-get install clang-tidy"
        exit 1
    fi

    echo -e "${YELLOW}Running clang-tidy...${NC}"
    echo "This may take several minutes..."
    echo ""

    # Find all C++ source files
    SOURCE_FILES=$(find "$PROJECT_ROOT/src" "$PROJECT_ROOT/include" -name "*.cpp" -o -name "*.hpp" 2>/dev/null)

    # Run clang-tidy on each file
    CLANG_TIDY_ISSUES=0
    > "$CLANG_TIDY_REPORT"  # Clear report file

    for file in $SOURCE_FILES; do
        echo -e "${BLUE}Analyzing: $(basename "$file")${NC}"
        # clang-tidy exits non-zero when it finds warnings/errors — that is the
        # expected case for an aggregator. We want every file analysed and the
        # results captured; the final exit code is decided from CLANG_TIDY_ERRORS.
        rc=0
        clang-tidy "$file" -p "$BUILD_DIR" >> "$CLANG_TIDY_REPORT" 2>&1 || rc=$?
        if [[ $rc -ne 0 && $rc -ne 1 ]]; then
            echo "warn: clang-tidy on $file exited rc=$rc (continuing)" >&2
        fi
    done

    # Count issues (exclude third-party dependency parsing errors).
    # awk always exits 0, so the count is reliable even when nothing matches.
    CLANG_TIDY_WARNINGS=$(awk '/warning:/ && !/_deps\//{n++} END{print n+0}' "$CLANG_TIDY_REPORT")
    CLANG_TIDY_ERRORS=$(awk '/error:/ && !/Error parsing.*_deps\//{n++} END{print n+0}' "$CLANG_TIDY_REPORT")

    echo ""
    echo -e "${GREEN}clang-tidy analysis complete${NC}"
    echo -e "  Report: ${BLUE}$CLANG_TIDY_REPORT${NC}"
    echo -e "  Warnings: ${YELLOW}$CLANG_TIDY_WARNINGS${NC}"
    echo -e "  Errors: ${RED}$CLANG_TIDY_ERRORS${NC}"
    echo ""
fi

# Run cppcheck
if [[ "$RUN_CPPCHECK" == "true" ]]; then
    if ! command -v cppcheck &> /dev/null; then
        echo -e "${RED}Error: cppcheck is not installed${NC}"
        echo "Install with: sudo apt-get install cppcheck"
        exit 1
    fi

    echo -e "${YELLOW}Running cppcheck...${NC}"
    echo "This may take several minutes..."
    echo ""

    cppcheck \
        --enable=all \
        --inconclusive \
        --xml \
        --xml-version=2 \
        --suppress=missingIncludeSystem \
        --suppress=unmatchedSuppression \
        --suppress=unusedFunction \
        -I "$PROJECT_ROOT/include" \
        "$PROJECT_ROOT/src" \
        "$PROJECT_ROOT/include" \
        2> "$CPPCHECK_REPORT"

    # Count issues
    CPPCHECK_ERRORS=$(grep -c 'severity="error"' "$CPPCHECK_REPORT" 2>/dev/null || echo "0")
    CPPCHECK_WARNINGS=$(grep -c 'severity="warning"' "$CPPCHECK_REPORT" 2>/dev/null || echo "0")
    CPPCHECK_STYLE=$(grep -c 'severity="style"' "$CPPCHECK_REPORT" 2>/dev/null || echo "0")
    CPPCHECK_PERFORMANCE=$(grep -c 'severity="performance"' "$CPPCHECK_REPORT" 2>/dev/null || echo "0")
    CPPCHECK_PORTABILITY=$(grep -c 'severity="portability"' "$CPPCHECK_REPORT" 2>/dev/null || echo "0")

    echo ""
    echo -e "${GREEN}cppcheck analysis complete${NC}"
    echo -e "  Report: ${BLUE}$CPPCHECK_REPORT${NC}"
    echo -e "  Errors: ${RED}$CPPCHECK_ERRORS${NC}"
    echo -e "  Warnings: ${YELLOW}$CPPCHECK_WARNINGS${NC}"
    echo -e "  Style: $CPPCHECK_STYLE"
    echo -e "  Performance: $CPPCHECK_PERFORMANCE"
    echo -e "  Portability: $CPPCHECK_PORTABILITY"
    echo ""
fi

# Summary
echo -e "${BLUE}=== Analysis Summary ===${NC}"
echo ""

if [[ "$RUN_CLANG_TIDY" == "true" ]]; then
    echo -e "clang-tidy:"
    echo -e "  ${RED}$CLANG_TIDY_ERRORS${NC} errors"
    echo -e "  ${YELLOW}$CLANG_TIDY_WARNINGS${NC} warnings"
    echo ""
fi

if [[ "$RUN_CPPCHECK" == "true" ]]; then
    echo -e "cppcheck:"
    echo -e "  ${RED}$CPPCHECK_ERRORS${NC} errors"
    echo -e "  ${YELLOW}$CPPCHECK_WARNINGS${NC} warnings"
    echo -e "  $CPPCHECK_STYLE style issues"
    echo -e "  $CPPCHECK_PERFORMANCE performance issues"
    echo -e "  $CPPCHECK_PORTABILITY portability issues"
    echo ""
fi

# Check if we should fail based on errors
TOTAL_ERRORS=0
if [[ "$RUN_CLANG_TIDY" == "true" ]]; then
    TOTAL_ERRORS=$((TOTAL_ERRORS + CLANG_TIDY_ERRORS))
fi
if [[ "$RUN_CPPCHECK" == "true" ]]; then
    TOTAL_ERRORS=$((TOTAL_ERRORS + CPPCHECK_ERRORS))
fi

if [[ $TOTAL_ERRORS -gt 0 ]]; then
    echo -e "${RED}✗ Static analysis found $TOTAL_ERRORS critical errors${NC}"
    exit 1
else
    echo -e "${GREEN}✓ No critical errors found${NC}"
    exit 0
fi
