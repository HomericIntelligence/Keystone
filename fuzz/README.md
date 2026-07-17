# Fuzz Testing for Keystone

This directory contains fuzz tests for Keystone HMAS using libFuzzer.

## Overview

Fuzz testing uses automated random input generation to discover bugs, crashes,
and undefined behavior. Keystone includes 4 fuzz test targets:

1. **fuzz_message_serialization** - Tests KeystoneMessage parsing and serialization
2. **fuzz_message_bus_routing** - Tests MessageBus routing logic
3. **fuzz_work_stealing** - Tests work-stealing scheduler robustness
4. **fuzz_retry_policy** - Tests retry policy calculations

## Building with Fuzz Testing

Fuzz testing requires **Clang** compiler (libFuzzer is built into Clang).

### Build Steps

```bash
# Clean previous build
rm -rf build
mkdir build && cd build

# Configure with fuzzing enabled (requires clang++)
cmake -DENABLE_FUZZING=ON -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -G Ninja ..

# Build
ninja

# Fuzz targets are now available:
# - fuzz_message_serialization
# - fuzz_message_bus_routing
# - fuzz_work_stealing
# - fuzz_retry_policy
```

## Running Fuzz Tests

### Basic Usage

```bash
# Run with default settings (1 million iterations, max 4KB input)
./fuzz_message_serialization -max_len=4096 -runs=1000000

# Run with corpus directory (for better coverage)
./fuzz_message_serialization -max_len=4096 -runs=1000000 ../fuzz/corpus/message_serialization/

# Run until crash found
./fuzz_message_serialization -max_len=4096
```

### Advanced Options

```bash
# Run with custom timeout (default 1200 seconds = 20 minutes)
./fuzz_message_serialization -max_total_time=3600

# Run with custom memory limit (default 2GB)
./fuzz_message_serialization -rss_limit_mb=4096

# Run with verbose output
./fuzz_message_serialization -verbosity=2

# Run with specific seed
./fuzz_message_serialization -seed=12345

# Generate coverage report
./fuzz_message_serialization -runs=100000 -print_coverage=1
```

### Parallel Fuzzing

For faster results, run multiple fuzz instances in parallel:

```bash
# Terminal 1
./fuzz_message_serialization -jobs=4 -workers=4 ../fuzz/corpus/message_serialization/

# This runs 4 parallel workers sharing the same corpus
```

## Interpreting Results

### Successful Run

If fuzzing completes without finding crashes:

```
#1000000    DONE   cov: 245 ft: 678 corp: 42/1234b exec/s: 50000 rss: 128Mb
```

- **cov**: Coverage (edges covered)
- **ft**: Features (unique code paths)
- **corp**: Corpus size (number of interesting inputs / total bytes)
- **exec/s**: Executions per second
- **rss**: Memory usage

### Crash Found

If a crash is found, libFuzzer will:

1. Print the stack trace
2. Save the crashing input to `crash-<hash>`
3. Exit with non-zero status

Example:

```
==12345==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x...
...
artifact_prefix='./'; Test unit written to ./crash-abc123def456
```

### Reproducing Crashes

```bash
# Reproduce the crash
./fuzz_message_serialization crash-abc123def456

# Run with debugger
gdb --args ./fuzz_message_serialization crash-abc123def456
```

## Corpus Management

### Seed Corpus

Each fuzz target has a seed corpus in `fuzz/corpus/<target>/`:

- `message_serialization/` - Valid and edge-case JSON messages
- `message_bus_routing/` - Agent registration sequences
- `work_stealing/` - Task submission patterns
- `retry_policy/` - Retry configuration bytes

### Minimizing Corpus

After a long fuzz run, minimize the corpus to remove redundant inputs:

```bash
# Create output directory
mkdir minimized_corpus

# Minimize
./fuzz_message_serialization -merge=1 minimized_corpus/ ../fuzz/corpus/message_serialization/

# Replace original corpus
rm -rf ../fuzz/corpus/message_serialization/*
mv minimized_corpus/* ../fuzz/corpus/message_serialization/
```

## Sanitizer Integration

Fuzz tests automatically include:

- **AddressSanitizer (ASan)** - Detects memory errors
- **UndefinedBehaviorSanitizer (UBSan)** - Detects undefined behavior

These are enabled via the `-fsanitize=fuzzer,address,undefined` flags.

## CI/CD Integration

Fuzz tests can be integrated into CI/CD pipelines:

```bash
# Run each fuzz target for 1 hour in CI
for target in fuzz_*; do
    ./$target -max_total_time=3600 || exit 1
done
```

## Continuous Fuzzing

For production systems, consider continuous fuzzing with:

- **OSS-Fuzz** - Google's continuous fuzzing service (free for open source)
- **ClusterFuzz** - Google's scalable fuzzing infrastructure
- **OneFuzz** - Microsoft's self-hosted fuzzing platform

## Troubleshooting

### Out of Memory

If fuzzing runs out of memory:

```bash
# Reduce memory limit
./fuzz_target -rss_limit_mb=1024

# Reduce max input length
./fuzz_target -max_len=1024
```

### Slow Execution

If fuzzing is too slow:

```bash
# Check exec/s in output
# Should be >1000 exec/s for good performance

# Profile the fuzz target
perf record ./fuzz_target -runs=10000
perf report
```

### No New Coverage

If fuzzing plateaus (no new coverage):

```bash
# Try different strategies
./fuzz_target -focus_function=processMessage
./fuzz_target -use_value_profile=1
```

## References

- [libFuzzer Documentation](https://llvm.org/docs/LibFuzzer.html)
- [Efficient Fuzzing Guide](https://github.com/google/fuzzing/blob/master/docs/good-fuzz-target.md)
- [AddressSanitizer](https://github.com/google/sanitizers/wiki/AddressSanitizer)

## Phase 9.2 Checklist

- [x] Create fuzz test infrastructure
- [x] Implement fuzz_message_serialization
- [x] Implement fuzz_message_bus_routing
- [x] Implement fuzz_work_stealing
- [x] Implement fuzz_retry_policy
- [x] Add CMake integration
- [x] Create seed corpus
- [x] Document fuzzing workflow

**Next**: Phase 9.4 - Performance Benchmarking
