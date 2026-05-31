# ProjectKeystone - Pure C++20 Message Transport Library

**C++20 message transport: lock-free local message bus + transparent NATS cross-host bridge**

> ℹ️ The 4-layer agent hierarchy (ChiefArchitect → ComponentLead → ModuleLead →
> TaskAgent) and the Python orchestration package were extracted to
> **ProjectAgamemnon** per ADR-015 / ADR-016. Keystone is now a pure transport
> library; the transport core depends only on `core::IMessageSink`.
>
> ✅ **All installation, build, and test steps are validated in CI/CD.**
> See [CI/CD Coverage Matrix](docs/CICD_COVERAGE.md) for complete documentation.

[![Required Checks](https://github.com/HomericIntelligence/ProjectKeystone/actions/workflows/_required.yml/badge.svg)](https://github.com/HomericIntelligence/ProjectKeystone/actions/workflows/_required.yml)
[![Extras](https://github.com/HomericIntelligence/ProjectKeystone/actions/workflows/extras.yml/badge.svg)](https://github.com/HomericIntelligence/ProjectKeystone/actions/workflows/extras.yml)
[![C++ Standard](https://img.shields.io/badge/C++-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

## Overview

ProjectKeystone is a high-performance message **transport** library built with
modern C++20. It moves bytes from publisher to subscriber reliably and
transparently: a lock-free intra-host message bus plus a transparent NATS
JetStream bridge for cross-host delivery. Agent runtime and orchestration logic
were extracted to ProjectAgamemnon (ADR-015 / ADR-016); the transport core
depends only on the one-method `core::IMessageSink` interface.

### Key Features

- **Lock-Free Message Bus**: `core::MessageBus` routing to any `IMessageSink`
- **Transparent NATS Bridge**: Automatic off-host forwarding via NATS JetStream
- **Async Work-Stealing Scheduler**: High-performance task distribution across worker threads
- **Resilience Components**: Retry policies, circuit breakers, heartbeat monitoring
- **Zero-Copy Serialization**: Efficient message passing with Cista
- **Distributed Simulation**: NUMA-aware distributed work-stealing
- **Comprehensive Testing**: Extensive C++ unit and integration coverage
- **Performance Benchmarks**: message-pool, distributed, scheduler, and allocation suites
- **Fuzz Testing**: libFuzzer targets for the untrusted-input surface
- **Static Analysis**: clang-tidy and cppcheck integration

## Build Requirements

### System Dependencies

| Requirement | Minimum | Recommended | Notes |
| :--- | :--- | :--- | :--- |
| **C++20 Compiler** | GCC 13 / Clang 15 | GCC 14 / Clang 18 | C++20 coroutines required |
| **CMake** | 3.20 | 3.28+ | FetchContent support |
| **Ninja** | 1.10 | 1.11+ | Fast parallel builds |
| **GNU Make** | - | - | Standard on Linux/macOS |
| **Python** | 3.11+ | 3.12+ | Optional — only to run/typecheck `conanfile.py` (the C++ Conan recipe). Keystone ships no Python runtime. |
| **Docker** | 20.10 | 24.0+ | Optional, for containerized builds |

### Auto-Fetched Dependencies

All libraries are **automatically downloaded** via CMake FetchContent - no manual installation required:

| Library | Version | Purpose |
| :--- | :--- | :--- |
| [Google Test](https://github.com/google/googletest) | 1.12.1 | Unit testing framework |
| [Google Benchmark](https://github.com/google/benchmark) | 1.8.3 | Performance benchmarking |
| [spdlog](https://github.com/gabime/spdlog) | 1.12.0 | Fast logging library |
| [Cista](https://github.com/felixguendling/cista) | 0.14 | Zero-copy serialization |
| [concurrentqueue](https://github.com/cameron314/concurrentqueue) | 1.0.4 | Lock-free MPMC queue |

### Optional Dependencies (Phase 8)

For distributed features (`-DENABLE_GRPC=ON`):

- **yaml-cpp** 0.7.0 - YAML task specification
- **gRPC** - Distributed communication
- **Protobuf** - Message serialization

## First-Time Setup

### Docker Setup (Recommended)

```bash
# Clone the repository
git clone https://github.com/HomericIntelligence/ProjectKeystone.git
cd ProjectKeystone

# Setup environment variables (required for Docker)
./scripts/setup-env.sh
# Writes ./.env with:
#   GIT_COMMIT  - short SHA of HEAD (or "latest" outside a git checkout),
#                 baked into image tags so docker compose can pull/build
#                 reproducible artifacts.
#   BUILD_UID   - your host UID, mapped into the dev container so files
#                 written from inside the container are owned by you.
#   BUILD_GID   - your host GID, same reason.
# Re-run this whenever you switch branches/commits or change UID/GID.
# The script writes only ./.env (gitignored); no other system state changes.

# Build and test
make docker.build
make test.debug.asan
```

### Native Setup

```bash
# Ensure you have GCC 13+/Clang 15+ and CMake 3.20+
make compile.debug.asan.native
make test.debug.asan.native
```

## Quick Start

### Using Makefile

The project uses `make` for unified build, test, and lint commands:

```bash
# Show all available commands
make help

# Build with AddressSanitizer (Docker)
make compile.debug.asan

# Build release mode
make compile.release

# Run all tests with ASan
make test.debug.asan

# Run specific test suite
make test.basic
make test.module
make test.unit

# Run linters
make lint
make format

# Native mode (run on host instead of Docker)
make compile.debug.asan.native
make test.debug.asan.native
```

### Manual Build (without Makefile)

```bash
# Build with ASan
cmake -S . -B build/asan -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"
cmake --build build/asan

# Run tests
cd build/asan && ctest --output-on-failure
```

### Docker Commands

```bash
# Build Docker images
make docker.build

# Start dev container
make docker.up

# Enter dev container shell
make docker.shell

# Or use docker-compose directly
docker-compose up -d dev
docker-compose exec dev bash
```

### Build Configuration

#### Build Directory Structure

Builds output to `build/x86.<mode>.<sanitizer>/` for parallel builds:

```
build/
├── x86.debug/           # Debug build
├── x86.release/         # Release build
├── x86.debug.asan/      # Debug + AddressSanitizer
├── x86.debug.tsan/      # Debug + ThreadSanitizer
├── x86.debug.ubsan/     # Debug + UBSan
├── x86.debug.coverage/  # Debug + Coverage
```

#### CMake Feature Flags

| Flag | Description | Default |
| :--- | :--- | :--- |
| `ENABLE_FUZZING` | Build fuzz test targets (requires Clang) | OFF |
| `ENABLE_GRPC` | Enable Phase 8 distributed features | OFF |
| `ENABLE_COVERAGE` | Enable code coverage instrumentation | OFF |
| `ENABLE_CLANG_TIDY` | Run clang-tidy during build | OFF |

Example:

```bash
cmake -S . -B build/fuzz -G Ninja \
    -DENABLE_FUZZING=ON \
    -DCMAKE_CXX_COMPILER=clang++
```

## Project Structure

```
ProjectKeystone/
├── include/            # Public headers
│   ├── core/           # Message bus, IMessageSink, serialization, resilience
│   ├── transport/      # NATS connection + transparent bridge
│   ├── network/        # NATS listener
│   └── concurrency/    # Work-stealing scheduler
├── src/                # Implementation
├── tests/              # C++ GoogleTest suites
│   ├── unit/           # Component unit tests
│   └── e2e/            # End-to-end / distributed tests
├── benchmarks/         # Performance benchmarks
├── fuzz/               # Fuzz test targets
├── scripts/            # Automation scripts
└── docs/               # Documentation

## Quality Metrics

| Metric | Status | Target |
|--------|--------|--------|
| **Line Coverage** | 86.2% | ≥95% |
| **Function Coverage** | 88.5% | ≥90% |
| **Tests Passing** | 481/481 (100%) | 100% |
| **Benchmark Suites** | 5 suites, 45+ tests | - |
| **Fuzz Targets** | 4 targets | - |
| **Static Analysis** | clang-tidy + cppcheck | 0 errors |

See [Coverage Baseline](docs/plan/PHASE_9_COVERAGE_BASELINE.md) for detailed metrics.

## Testing

### Unit and E2E Tests

```bash
# Run all tests (with ASan)
make test.debug.asan

# Run specific test suites
make test.basic          # Basic delegation tests
make test.module         # Module coordination tests
make test.component      # Component coordination tests
make test.async          # Async delegation tests
make test.unit           # Unit tests
make test.concurrency    # Concurrency unit tests

# Run with TSan (thread sanitizer)
make test.debug.tsan

# Run with all sanitizers
make test.debug.ubsan    # UndefinedBehaviorSanitizer
make test.debug.lsan     # LeakSanitizer
make test.debug.msan     # MemorySanitizer

# Native mode (faster iteration)
make test.debug.asan.native
```

### Code Coverage

```bash
# Build with coverage and generate report
make compile.debug.coverage
make coverage

# Or manually
./scripts/generate_coverage.sh

# View report
open build/coverage/html/index.html
```

### Fuzz Testing

```bash
# Note: Fuzzing requires Clang and ENABLE_FUZZING=ON
# Build with fuzzing support:
cmake -S . -B build/fuzz -G Ninja \
    -DENABLE_FUZZING=ON \
    -DCMAKE_CXX_COMPILER=clang++
cmake --build build/fuzz

# Run fuzz targets
./build/fuzz/fuzz_message_serialization -max_len=4096 -runs=1000000
./build/fuzz/fuzz_message_bus_routing -max_len=4096 -runs=1000000
```

See [Fuzz Testing README](fuzz/README.md) for details.

### Performance Benchmarks

```bash
# Run all benchmarks
make benchmark

# Run specific benchmark
make benchmark.message-pool
make benchmark.distributed
make benchmark.strings

# Manual benchmark execution
make compile.release
./scripts/run_benchmarks.sh

# Save baseline
./scripts/run_benchmarks.sh --baseline

# Detect regressions
./scripts/run_benchmarks.sh --compare benchmarks/results/baseline.json
```

### Load Testing

```bash
# Run all load test scenarios (full duration)
make load-test

# Quick load tests (for CI)
make load-test.quick
```

See [Benchmarks README](benchmarks/README.md) for details.

### Static Analysis

```bash
# Run all linters (clang-tidy + cppcheck)
make lint

# Run specific linter
make lint.clang-tidy
make lint.cppcheck

# Format code
make format

# Check formatting (CI)
make format.check

# Manual execution
./scripts/run_static_analysis.sh

# View reports
cat build/static_analysis/clang-tidy-report.txt
cat build/static_analysis/cppcheck-report.txt
```

## CI/CD Quality Gates

ProjectKeystone enforces 5 quality gates in CI/CD:

1. **Code Coverage** (≥95%)
2. **Static Analysis** (0 critical errors)
3. **Fuzz Testing** (1 hour, no crashes)
4. **Performance Benchmarks** (no >10% regressions)
5. **Unit & E2E Tests** (all passing)

See [CI/CD Quality Gates](docs/CICD_QUALITY_GATES.md) for complete documentation.

## Development Phases

ProjectKeystone follows a phased TDD approach:

- **Phase 1** ✅: Basic delegation (L0 → L3)
- **Phase 2** ✅: Module coordination (L0 → L2 → L3)
- **Phase 3** ✅: Full 4-layer hierarchy (L0 → L1 → L2 → L3)
- **Phase 4** ✅: Multi-component system
- **Phase 5** ✅: Chaos engineering and resilience
- **Phase D** ✅: Performance optimizations (profiling, pooling, distributed)
- **Phase 9** 🚧: Enhanced testing and quality (in progress)

Current status: **Phase 9** (Enhanced Testing & Quality)

See [TDD Roadmap](docs/plan/TDD_FOUR_LAYER_ROADMAP.md) for detailed phase descriptions.

## Architecture

Keystone moves bytes from publisher to subscriber, reliably and transparently.
A message is routed locally to any registered `core::IMessageSink`, or — when
the destination is off-host — promoted onto the appropriate NATS subject by the
transparent bridge. The former agent hierarchy (ChiefArchitect → ComponentLead
→ ModuleLead → TaskAgent) now lives in ProjectAgamemnon (ADR-015).

```
Publisher → MessageBus.routeMessage(msg)
                 ├─ local sink  → lock-free delivery to IMessageSink
                 └─ off-host    → TransparentBridge → NATS JetStream → remote MessageBus
```

### Core Components

- **MessageBus**: Central routing to registered `IMessageSink` targets
- **IMessageSink**: One-method delivery interface (transport ⟷ consumer boundary)
- **TransparentBridge**: Automatic off-host forwarding over NATS JetStream
- **WorkStealingScheduler**: Thread pool with task stealing
- **RetryPolicy**: Exponential backoff for transient failures
- **CircuitBreaker**: Fast failure for persistent errors
- **HeartbeatMonitor**: Liveness tracking
- **MessagePool**: Zero-allocation message pooling

## Documentation

- [CLAUDE.md](CLAUDE.md) - Project overview for Claude Code
- [Glossary](docs/GLOSSARY.md) - Terminology and definitions
- [Four-Layer Architecture](docs/plan/FOUR_LAYER_ARCHITECTURE.md)
- [TDD Roadmap](docs/plan/TDD_FOUR_LAYER_ROADMAP.md)
- [Testing Strategy](docs/plan/testing-strategy.md)
- [CI/CD Quality Gates](docs/CICD_QUALITY_GATES.md)
- [Coverage Baseline](docs/plan/PHASE_9_COVERAGE_BASELINE.md)
- [Phase 5 Complete](docs/plan/PHASE_5_COMPLETE.md)

## Performance Targets

| Component | Metric | Target | Current |
| :--- | :--- | :--- | :--- |
| MessageBus | Routing Latency | <500ns | TBD |
| MessageBus | Throughput | >2M msg/sec | TBD |
| Message Pool | Allocations | >10M/sec | TBD |
| Work-Stealing | Local Steal | <10us | TBD |
| Work-Stealing | Remote Steal | <100us | TBD |

Run benchmarks to establish baselines: `./scripts/run_benchmarks.sh --baseline`

## Contributing

1. Follow TDD approach (test first)
2. Maintain ≥95% code coverage
3. Pass all quality gates
4. Update documentation

See [CLAUDE.md](CLAUDE.md) for development guidelines.

## License

MIT License - See [LICENSE](LICENSE) for details.

## Acknowledgments

Built with:

- [Google Test](https://github.com/google/googletest) - Unit testing framework
- [Google Benchmark](https://github.com/google/benchmark) - Performance benchmarking
- [spdlog](https://github.com/gabime/spdlog) - Fast logging library
- [Cista](https://github.com/felixguendling/cista) - Zero-copy serialization
- [concurrentqueue](https://github.com/cameron314/concurrentqueue) - Lock-free queue

---

**Status**: Phase 9 (Enhanced Testing & Quality) - 86.2% coverage, 481 tests
**Last Updated**: 2025-12-02
