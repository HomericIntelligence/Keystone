#!/bin/bash
# run_benchmarks.sh
# Run all ProjectKeystone benchmarks and detect performance regressions
#
# Usage:
#   ./scripts/run_benchmarks.sh [--baseline] [--compare <baseline.json>]
#
# Options:
#   --baseline            Save results as new baseline
#   --compare <file>      Compare against baseline and detect regressions
#   --filter <pattern>    Only run benchmarks matching pattern
#   --format <fmt>        Output format: console|json|csv (default: console)
#
# Requirements:
#   - Build with: cmake -DCMAKE_BUILD_TYPE=Release -G Ninja ..
#   - Install: pip3 install --user google-benchmark-plot (optional, for visualization)

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
PROJECT_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
# BUILD_DIR is the CMake build directory; binaries land in $BUILD_DIR/bin
# because CMakeLists.txt sets CMAKE_RUNTIME_OUTPUT_DIRECTORY to
# "${CMAKE_BINARY_DIR}/bin". The Makefile uses $(BUILD_DIR)/$(BUILD_SUBDIR)
# as its CMake binary dir, which defaults to build/x86.release
# (BUILD_DIR=build, BUILD_SUBDIR=x86, CMAKE_BUILD_TYPE=Release -> .release).
# Callers may override BUILD_DIR (full path to CMake binary dir),
# BUILD_SUBDIR (suffix only), or BENCH_BIN_DIR (full path to binaries).
: "${BUILD_SUBDIR:=x86.release}"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build/$BUILD_SUBDIR}"
BENCH_BIN_DIR="${BENCH_BIN_DIR:-$BUILD_DIR/bin}"
BENCHMARK_DIR="${BENCHMARK_OUTPUT_DIR:-$PROJECT_ROOT/build/reports/benchmarks}"
RESULTS_DIR="$BENCHMARK_DIR/results"
mkdir -p "$RESULTS_DIR"
BASELINE_FILE="$RESULTS_DIR/baseline.json"

# Options
SAVE_BASELINE=false
COMPARE_BASELINE=""
FILTER=""
FORMAT="console"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --baseline)
            SAVE_BASELINE=true
            shift
            ;;
        --compare)
            COMPARE_BASELINE="$2"
            shift 2
            ;;
        --filter)
            FILTER="$2"
            shift 2
            ;;
        --format)
            FORMAT="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Create results directory
mkdir -p "$RESULTS_DIR"

echo -e "${BLUE}=== ProjectKeystone Performance Benchmarks ===${NC}"
echo ""

# Check if build exists
if [[ ! -d "$BUILD_DIR" ]]; then
    echo -e "${RED}Error: Build directory not found${NC}"
    echo "Run: mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Release -G Ninja .. && ninja"
    exit 1
fi

# List of benchmark executables
BENCHMARKS=(
    "hierarchy_benchmarks"
    "message_pool_benchmarks"
    "distributed_benchmarks"
)

# Check that benchmarks exist
missing=0
for bench in "${BENCHMARKS[@]}"; do
    if [[ ! -f "$BENCH_BIN_DIR/$bench" ]]; then
        echo -e "${YELLOW}Warning: $bench not found, skipping${NC}"
        missing=$((missing + 1))
    fi
done

if [[ $missing -eq ${#BENCHMARKS[@]} ]]; then
    echo -e "${RED}Error: No benchmark executables found${NC}"
    echo "Build benchmarks with: cd build && ninja"
    exit 1
fi

# Timestamp for results.
# Exported so the embedded `python3 << EOF` heredocs below can read them via
# os.environ.get(). Heredocs run as a subprocess of bash and only inherit
# *exported* variables; without `export` the python sees None and emits
# "No result files found matching None/*_None.json".
export TIMESTAMP
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
export RESULTS_DIR
export RESULTS_FILE
RESULTS_FILE="$RESULTS_DIR/results_$TIMESTAMP.json"
export COMPARE_BASELINE

echo -e "${YELLOW}Running benchmarks...${NC}"
echo ""

# Run each benchmark suite
for bench in "${BENCHMARKS[@]}"; do
    if [[ ! -f "$BENCH_BIN_DIR/$bench" ]]; then
        continue
    fi

    echo -e "${BLUE}Running $bench...${NC}"

    # Build benchmark command
    BENCH_CMD="$BENCH_BIN_DIR/$bench"
    BENCH_ARGS="--benchmark_out_format=json --benchmark_out=$RESULTS_DIR/${bench}_$TIMESTAMP.json"

    # Add filter if specified
    if [[ -n "$FILTER" ]]; then
        BENCH_ARGS="$BENCH_ARGS --benchmark_filter=$FILTER"
    fi

    # Run benchmark
    if [[ "$FORMAT" == "console" ]]; then
        $BENCH_CMD $BENCH_ARGS
    else
        $BENCH_CMD $BENCH_ARGS --benchmark_format=$FORMAT
    fi

    echo ""
done

# Merge all JSON results
echo -e "${YELLOW}Merging results...${NC}"

python3 << 'EOF'
import json
import glob
import sys
import os

results_dir = os.environ.get('RESULTS_DIR')
timestamp = os.environ.get('TIMESTAMP')

# Find all result files for this run
pattern = f"{results_dir}/*_{timestamp}.json"
files = glob.glob(pattern)

if not files:
    print(f"No result files found matching {pattern}", file=sys.stderr)
    sys.exit(1)

# Merge benchmarks
merged = {
    "context": {},
    "benchmarks": []
}

for file in files:
    with open(file, 'r') as f:
        data = json.load(f)
        if not merged["context"]:
            merged["context"] = data.get("context", {})
        merged["benchmarks"].extend(data.get("benchmarks", []))

# Write merged results
output_file = os.environ.get('RESULTS_FILE')
with open(output_file, 'w') as f:
    json.dump(merged, f, indent=2)

print(f"Merged {len(files)} benchmark files into {output_file}")
EOF

if [[ $? -ne 0 ]]; then
    echo -e "${RED}Error merging results${NC}"
    exit 1
fi

# Save as baseline if requested
if [[ "$SAVE_BASELINE" == "true" ]]; then
    echo -e "${YELLOW}Saving baseline...${NC}"
    cp "$RESULTS_FILE" "$BASELINE_FILE"
    echo -e "${GREEN}Baseline saved to $BASELINE_FILE${NC}"
fi

# Compare against baseline if requested
if [[ -n "$COMPARE_BASELINE" ]]; then
    echo ""
    echo -e "${BLUE}=== Regression Detection ===${NC}"
    echo ""

    if [[ ! -f "$COMPARE_BASELINE" ]]; then
        echo -e "${RED}Error: Baseline file not found: $COMPARE_BASELINE${NC}"
        exit 1
    fi

    python3 << 'EOF'
import json
import sys
import os

def load_benchmarks(filename):
    with open(filename, 'r') as f:
        data = json.load(f)
    return {b['name']: b for b in data['benchmarks']}

def format_time(ns):
    """Format time in human-readable format"""
    if ns < 1000:
        return f"{ns:.2f} ns"
    elif ns < 1000000:
        return f"{ns/1000:.2f} us"
    elif ns < 1000000000:
        return f"{ns/1000000:.2f} ms"
    else:
        return f"{ns/1000000000:.2f} s"

baseline_file = os.environ.get('COMPARE_BASELINE')
current_file = os.environ.get('RESULTS_FILE')
threshold = 1.10  # 10% regression threshold

baseline = load_benchmarks(baseline_file)
current = load_benchmarks(current_file)

regressions = []
improvements = []
unchanged = []

for name, curr_bench in current.items():
    if name not in baseline:
        print(f"NEW: {name}")
        continue

    base_bench = baseline[name]

    # Compare real_time (primary metric)
    base_time = base_bench.get('real_time', 0)
    curr_time = curr_bench.get('real_time', 0)

    if base_time == 0:
        continue

    ratio = curr_time / base_time
    pct_change = (ratio - 1.0) * 100

    result = {
        'name': name,
        'baseline': base_time,
        'current': curr_time,
        'ratio': ratio,
        'change_pct': pct_change
    }

    if ratio > threshold:
        regressions.append(result)
    elif ratio < 0.9:  # 10% improvement
        improvements.append(result)
    else:
        unchanged.append(result)

# Print results
print(f"\033[32m✓ Passing: {len(unchanged)}\033[0m")
print(f"\033[33m↑ Improvements: {len(improvements)}\033[0m")
print(f"\033[31m↓ Regressions: {len(regressions)}\033[0m")
print()

if improvements:
    print("\033[33m=== Improvements (>10% faster) ===\033[0m")
    for r in sorted(improvements, key=lambda x: x['ratio']):
        print(f"  {r['name']}")
        print(f"    Baseline: {format_time(r['baseline'])}")
        print(f"    Current:  {format_time(r['current'])}")
        print(f"    Change:   \033[32m{r['change_pct']:+.1f}%\033[0m (x{r['ratio']:.2f})")
        print()

if regressions:
    print("\033[31m=== REGRESSIONS (>10% slower) ===\033[0m")
    for r in sorted(regressions, key=lambda x: x['ratio'], reverse=True):
        print(f"  {r['name']}")
        print(f"    Baseline: {format_time(r['baseline'])}")
        print(f"    Current:  {format_time(r['current'])}")
        print(f"    Change:   \033[31m{r['change_pct']:+.1f}%\033[0m (x{r['ratio']:.2f})")
        print()

    sys.exit(1)  # Fail if regressions detected
else:
    print("\033[32m✓ No performance regressions detected\033[0m")
    sys.exit(0)
EOF

    COMPARE_EXIT=$?
    echo ""

    if [[ $COMPARE_EXIT -eq 0 ]]; then
        echo -e "${GREEN}✓ Benchmark suite passed${NC}"
        exit 0
    else
        echo -e "${RED}✗ Performance regressions detected${NC}"
        exit 1
    fi
fi

# Summary
echo -e "${GREEN}Benchmarks completed successfully${NC}"
echo -e "  Results: ${BLUE}$RESULTS_FILE${NC}"
echo ""
echo "View results:"
echo "  cat $RESULTS_FILE | jq '.benchmarks[] | {name, real_time, cpu_time}'"
echo ""
echo "To save as baseline:"
echo "  ./scripts/run_benchmarks.sh --baseline"
echo ""
echo "To detect regressions:"
echo "  ./scripts/run_benchmarks.sh --compare $BASELINE_FILE"
echo ""

exit 0
