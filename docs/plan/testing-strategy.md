# ProjectKeystone Testing Strategy

## Overview

ProjectKeystone adopts a comprehensive, multi-tiered testing approach to ensure correctness,
performance, and reliability of the hierarchical multi-agent system. The testing strategy emphasizes
test-driven development (TDD), continuous integration, and automated validation.

## Testing Philosophy

1. **Test Early, Test Often**: Write tests before or alongside implementation
2. **Comprehensive Coverage**: Target >95% coverage for core, >90% for agents
3. **Fast Feedback**: Unit tests complete in <1 second, full suite in <5 minutes
4. **Realistic Scenarios**: Integration tests mirror production workflows
5. **Performance Validation**: Benchmarks verify performance targets
6. **Chaos Engineering**: Stress tests validate resilience

## Testing Tiers

### Tier 1: Unit Tests

**Scope**: Individual functions, classes, and modules in isolation

**Framework**: Google Test (GTest)

**Coverage Target**: >95% for Keystone.Core, >90% for other modules

**Execution Time**: <1 second total

#### Example: Message Queue Unit Test

```cpp
// tests/unit/core/MessageQueueTest.cpp
#include <gtest/gtest.h>
#include "core/message_queue.hpp"

namespace Keystone::Core::Test {

TEST(MessageQueueTest, CanPushAndPopValues) {
    MessageQueue<int> queue;

    // Push and pop single value
    queue.push(42);

    int value;
    ASSERT_TRUE(queue.try_pop(value));
    EXPECT_EQ(value, 42);
}

TEST(MessageQueueTest, TryPopFromEmptyQueue) {
    MessageQueue<int> queue;
    int value;
    EXPECT_FALSE(queue.try_pop(value));
}

TEST(MessageQueueTest, HandlesConcurrentAccess) {
    MessageQueue<int> queue;
    constexpr int NUM_ITEMS = 10000;
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    // Start producers
    for (int i = 0; i < 4; ++i) {
        producers.emplace_back([&]() {
            for (int j = 0; j < NUM_ITEMS / 4; ++j) {
                queue.push(j);
                ++produced;
            }
        });
    }

    // Start consumers
    for (int i = 0; i < 4; ++i) {
        consumers.emplace_back([&]() {
            int value;
            while (consumed < NUM_ITEMS) {
                if (queue.try_pop(value)) {
                    ++consumed;
                }
            }
        });
    }

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    EXPECT_EQ(consumed, NUM_ITEMS);
}

} // namespace Keystone::Core::Test
```

#### Unit Test Organization

```
tests/unit/
├── core/
│   ├── MessageQueueTest.cpp
│   ├── ThreadPoolTest.cpp
│   ├── CoroutineTest.cpp
│   └── SynchronizationTest.cpp
├── protocol/
│   ├── KIMTest.cpp
│   ├── SerializationTest.cpp
│   └── GRPCTest.cpp
├── agents/
│   ├── AgentBaseTest.cpp
│   ├── StateMachineTest.cpp
│   ├── RootAgentTest.cpp
│   ├── BranchAgentTest.cpp
│   └── LeafAgentTest.cpp
└── integration/
    └── (AI integration tests)
```

### Tier 2: Integration Tests

**Scope**: Multi-component interactions and workflows

**Framework**: Google Test (GTest) with test fixtures

**Coverage Target**: All critical integration points

**Execution Time**: <30 seconds total

#### Example: Hierarchical Agent Integration Test

```cpp
// tests/integration/HierarchyTest.cpp
#include <gtest/gtest.h>
#include "agents/chief_architect_agent.hpp"
#include "core/thread_pool.hpp"

namespace Keystone::Agents::Test {

class AgentHierarchyTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize thread pool
        thread_pool_ = std::make_unique<Core::ThreadPool>(4);

        // Create agent hierarchy
        root_ = createRootAgent();
        root_->initialize();
    }

    void TearDown() override {
        root_->shutdown();
        thread_pool_->shutdown();
    }

    std::unique_ptr<Core::ThreadPool> thread_pool_;
    std::unique_ptr<RootAgent> root_;
};

TEST_F(AgentHierarchyTest, TaskDecompositionFlow) {
    // Create a complex task
    std::string goal = "Generate report from multiple data sources";

    // Root decomposes task
    auto future = root_->decomposeGoal(goal);

    // Wait for completion (with timeout)
    auto result = future.wait_for(std::chrono::seconds(5));

    ASSERT_EQ(result, std::future_status::ready);
    EXPECT_FALSE(future.get().empty());
}

TEST_F(AgentHierarchyTest, ResultAggregation) {
    // Simulate branch agents returning results
    // Verify root correctly aggregates results

    // Test implementation...
}

TEST_F(AgentHierarchyTest, FailureRecovery) {
    // Simulate leaf agent failure
    // Verify branch agent detects and recovers

    // Test implementation...
}

} // namespace Keystone::Agents::Test
```

#### Integration Test Categories

1. **Agent Communication**: Message passing, routing, session isolation
2. **Hierarchical Workflows**: Task decomposition, result aggregation
3. **Serialization**: Cista ↔ Protobuf conversion in leaf agents
4. **External Services**: gRPC client calls, ONNX inference
5. **Supervision**: Failure detection, recovery, escalation

### Tier 3: Performance Tests

**Scope**: Throughput, latency, scalability validation

**Framework**: Google Benchmark

**Targets**:

- Message throughput: >1M messages/second
- Internal latency: <1ms p99
- gRPC latency: <10ms p99
- Scalability: Linear to 1000+ agents

#### Example: Message Bus Benchmark

```cpp
// tests/performance/MessageBusBenchmark.cpp
import Keystone.Core.Messaging;
import <benchmark/benchmark.h>;

namespace Keystone::Core::Benchmark {

static void BM_MessageQueuePush(benchmark::State& state) {
    MessageQueue<int> queue;

    for (auto _ : state) {
        queue.push(42);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MessageQueuePush);

static void BM_MessageQueuePushPop(benchmark::State& state) {
    MessageQueue<int> queue;

    for (auto _ : state) {
        queue.push(42);
        int value;
        queue.try_pop(value);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MessageQueuePushPop);

static void BM_MessageThroughput(benchmark::State& state) {
    constexpr int NUM_THREADS = 8;
    MessageQueue<int> queue;

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    std::atomic<int64_t> total_processed{0};

    for (auto _ : state) {
        // Multi-producer multi-consumer scenario
        for (int i = 0; i < NUM_THREADS / 2; ++i) {
            producers.emplace_back([&]() {
                for (int j = 0; j < 10000; ++j) {
                    queue.push(j);
                }
            });
        }

        for (int i = 0; i < NUM_THREADS / 2; ++i) {
            consumers.emplace_back([&]() {
                int value;
                int count = 0;
                while (count < 10000) {
                    if (queue.try_pop(value)) {
                        ++count;
                    }
                }
                total_processed += count;
            });
        }

        for (auto& t : producers) t.join();
        for (auto& t : consumers) t.join();
        producers.clear();
        consumers.clear();
    }

    state.SetItemsProcessed(total_processed);
    state.SetBytesProcessed(total_processed * sizeof(int));
}
BENCHMARK(BM_MessageThroughput)->UseRealTime();

} // namespace Keystone::Core::Benchmark

BENCHMARK_MAIN();
```

#### Performance Test Categories

1. **Message Bus**: Queue operations, throughput, latency
2. **Serialization**: Cista serialization/deserialization speed
3. **Coroutines**: Suspend/resume overhead, context switching
4. **Agent Workflows**: End-to-end task processing time
5. **Scalability**: Performance with increasing agent count

### Tier 4: Stress & Chaos Tests

**Scope**: System resilience under extreme conditions

**Framework**: Custom chaos framework + Google Test (GTest)

**Targets**:

- Stability: 24+ hour continuous operation
- Recovery: <100ms failure detection and recovery
- No memory leaks (verified by AddressSanitizer)
- No data races (verified by ThreadSanitizer)
- No undefined behavior (verified by UBSanitizer)

#### Example: Chaos Test

```cpp
// tests/stress/ChaosTest.cpp
#include <gtest/gtest.h>
#include "agents/chief_architect_agent.hpp"
#include "core/thread_pool.hpp"

namespace Keystone::Stress::Test {

class ChaosTest : public ::testing::Test {
protected:
    // Randomly kill agents
    void injectAgentFailure() {
        // Implementation...
    }

    // Simulate network delays
    void injectNetworkLatency() {
        // Implementation...
    }

    // Exhaust resources
    void injectResourceExhaustion() {
        // Implementation...
    }
};

TEST_F(ChaosTest, SystemHandlesRandomAgentFailures) {
    // Create hierarchy
    auto root = createRootAgent();
    root->initialize();

    // Run workload while randomly killing agents
    std::thread chaos_thread([&]() {
        for (int i = 0; i < 100; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            injectAgentFailure();
        }
    });

    // Execute tasks
    for (int i = 0; i < 1000; ++i) {
        root->decomposeGoal("Task " + std::to_string(i));
    }

    chaos_thread.join();

    // Verify system still functional
    EXPECT_NE(root->getState(), AgentState::ERROR);
}

TEST(StressTest, LongDurationStability) {
    // Run for 24 hours (marked as long-running, exclude from regular test runs)
    auto start = std::chrono::steady_clock::now();
    auto end = start + std::chrono::hours(24);

    while (std::chrono::steady_clock::now() < end) {
        // Execute continuous workload
        // Monitor for failures, memory leaks, deadlocks
    }

    // Verify clean state
}

} // namespace Keystone::Stress::Test
```

#### Stress Test Categories

1. **Load Testing**: High message volume, many concurrent agents
2. **Failure Injection**: Random agent crashes, network partitions
3. **Resource Exhaustion**: Memory limits, CPU starvation
4. **Long Duration**: 24+ hour stability tests
5. **Memory Safety**: Valgrind, AddressSanitizer, LeakSanitizer

### Tier 5: Profiling Tests

**Scope**: Performance profiling instrumentation validation

**Framework**: Google Test (GTest) with KEYSTONE_PROFILE=1

**Execution**: Weekly CI runs (not per-commit due to overhead)

**Purpose**: Ensure profiling code doesn't regress without slowing regular development

**Overhead**: Profiling adds significant overhead (~1000µs per operation), making these tests too slow for per-commit CI
runs. Instead, they run weekly to validate the profiling infrastructure remains functional.

#### Running Locally

```bash
# Using justfile (recommended)
make test.profiling

# Or manually with CMake
cmake -S . -B build/profiling -G Ninja -DENABLE_PROFILING=ON
cmake --build build/profiling
./build/profiling/profiling_tests
```

#### CI Execution

Profiling tests run automatically:

- **Weekly**: Every Sunday at 2 AM UTC via scheduled workflow
- **Manual**: Can be triggered via GitHub Actions workflow_dispatch

See `.github/workflows/profiling-weekly.yml` for the weekly CI configuration.

#### Test Organization

Profiling tests are located in `tests/unit/test_profiling.cpp` and validate:

1. **RAII Auto-end**: ProfilingSession destructor properly ends profiling
2. **Multiple Samples**: Statistics correctly aggregate multiple measurements
3. **Percentile Calculation**: P50, P95, P99 percentiles computed accurately
4. **Report Generation**: Profiling reports contain all expected sections
5. **Thread Safety**: Concurrent profiling from multiple threads works correctly
6. **Move Semantics**: ProfilingSession move operations preserve timing data

#### Design Philosophy

- **Opt-in by default**: Tests skip automatically when `KEYSTONE_PROFILE=1` is not set
- **Separate build**: Profiling tests use dedicated CMake target and build directory
- **No regular CI impact**: Zero overhead for standard development workflows
- **Validation coverage**: Ensures profiling API remains functional and accurate

## Test-Driven Development (TDD) Workflow

### Red-Green-Refactor Cycle

```
1. RED: Write failing test
   └─> Test defines expected behavior

2. GREEN: Write minimal code to pass test
   └─> Implementation satisfies test

3. REFACTOR: Improve code quality
   └─> Maintain passing tests
```

### Example TDD Session

```cpp
// Step 1: RED - Write failing test
TEST(AgentBaseTest, CanSendMessages) {
    AgentBase sender, receiver;
    KeystoneMessage msg = MessageBuilder()
        .from(sender.getId())
        .to(receiver.getId())
        .action(ActionType::EXECUTE_TOOL)
        .build();

    sender.sendMessage(msg);

    ASSERT_EQ(receiver.mailbox_.size(), 1);  // FAILS - not implemented
}

// Step 2: GREEN - Implement minimal solution
Task<void> AgentBase::sendMessage(const KeystoneMessage& msg) {
    // Find recipient and push to their mailbox
    auto* recipient = findAgent(msg.recipient_id);
    recipient->mailbox_.push(msg);
    co_return;
}

// Step 3: REFACTOR - Improve implementation
Task<void> AgentBase::sendMessage(const KeystoneMessage& msg) {
    // Validate message
    if (!msg.isValid()) {
        throw std::invalid_argument("Invalid message");
    }

    // Route message
    auto* recipient = MessageRouter::instance().findAgent(msg.recipient_id);
    if (!recipient) {
        throw std::runtime_error("Recipient not found");
    }

    // Deliver
    recipient->mailbox_.push(msg);
    co_return;
}
```

## Code Coverage

### Coverage Targets

| Module | Target | Rationale |
|--------|--------|-----------|
| **Keystone.Core** | 95% | Critical foundation |
| **Keystone.Protocol** | 90% | Communication layer |
| **Keystone.Agents** | 90% | Core functionality |
| **Keystone.Integration** | 80% | External dependencies |

### Coverage Tools

- **gcov/lcov**: GCC coverage (Linux)
- **llvm-cov**: Clang coverage (macOS/Linux)
- **OpenCppCoverage**: Windows coverage

### Coverage Reporting

```bash
# Generate coverage report
cmake --preset debug -DKEYSTONE_ENABLE_COVERAGE=ON
cmake --build build/debug
ctest

# Generate HTML report
lcov --capture --directory build/debug --output-file coverage.info
genhtml coverage.info --output-directory coverage-report

# View report
open coverage-report/index.html
```

## Sanitizers

### Overview

Sanitizers are runtime error detection tools built into GCC/Clang that help catch bugs that tests alone might miss:

- **AddressSanitizer (ASan)**: Detects memory errors (use-after-free, buffer overflows, leaks)
- **UndefinedBehaviorSanitizer (UBSan)**: Detects undefined behavior (integer overflow, null deref)
- **ThreadSanitizer (TSan)**: Detects data races and threading issues
- **MemorySanitizer (MSan)**: Detects uninitialized memory reads

### Building with Sanitizers

```bash
# AddressSanitizer + UndefinedBehaviorSanitizer (compatible)
cmake -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
      ..

# ThreadSanitizer (incompatible with ASan, separate build required)
cmake -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" \
      ..

# MemorySanitizer (requires instrumented stdlib, advanced use case)
cmake -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=memory -fno-omit-frame-pointer -g" \
      ..
```

### Running Tests with Sanitizers

```bash
# Run tests with ASan
docker-compose up test-asan

# Run tests with TSan
docker-compose up test-tsan

# Or manually with Docker
docker build --build-arg SANITIZER=address -t projectkeystone:asan .
docker run --rm projectkeystone:asan
```

### Sanitizer Options

```bash
# ASan: Detect leaks
export ASAN_OPTIONS=detect_leaks=1:fast_unwind_on_malloc=0

# TSan: Report on exit
export TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1

# UBSan: Print stack traces
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
```

### CI Integration

All pull requests automatically run tests with sanitizers:

- **Fast checks**: ASan + UBSan (combined)
- **Concurrency checks**: TSan (separate job)
- **Optional**: MSan (on-demand for critical code)

### Suppression Files

For known false positives, use suppression files:

```bash
# asan.supp
leak:third_party/
# Suppress known leak in library

# tsan.supp
race:^std::__atomic_base
# Suppress race in standard library (false positive)
```

## Continuous Integration

### CI Pipeline Stages

```
┌──────────────┐
│ Code Commit  │
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Static Check │  clang-format, clang-tidy
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Build (All)  │  Linux, Windows, macOS
└──────┬───────┘
       │
       ├─────────────────────┬──────────────────┐
       ▼                     ▼                  ▼
┌──────────────┐    ┌──────────────┐   ┌──────────────┐
│  Unit Tests  │    │ Integration  │   │ Performance  │
└──────┬───────┘    └──────┬───────┘   └──────┬───────┘
       │                   │                  │
       └─────────┬─────────┴──────────────────┘
                 ▼
         ┌──────────────┐
         │   Coverage   │
         └──────┬───────┘
                ▼
         ┌──────────────┐
         │ Sanitizers   │  ASAN, TSAN, UBSAN
         └──────┬───────┘
                ▼
         ┌──────────────┐
         │   Deploy     │  Artifacts, Docs
         └──────────────┘
```

### CI Configuration (GitHub Actions)

```yaml
# .github/workflows/test.yml
name: Test Suite

on: [push, pull_request]

jobs:
  unit-tests:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4
      - name: Build and Test
        run: |
          cmake --preset debug
          cmake --build build/debug
          ctest --preset default --output-on-failure

      - name: Upload coverage
        uses: codecov/codecov-action@v3

  integration-tests:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4
      - name: Integration Tests
        run: |
          cmake --preset debug
          cmake --build build/debug
          ctest -R Integration --output-on-failure

  performance-tests:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4
      - name: Benchmarks
        run: |
          cmake --preset release
          cmake --build build/release
          ./build/release/benchmarks/keystone_benchmarks \
            --benchmark_format=json \
            --benchmark_out=benchmark_results.json

      - name: Compare to baseline
        run: |
          python scripts/compare_benchmarks.py \
            benchmark_results.json \
            baseline_benchmarks.json

  sanitizer-tests:
    strategy:
      matrix:
        sanitizer: [asan, tsan, ubsan]
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4
      - name: ${{ matrix.sanitizer }} Tests
        run: |
          cmake --preset ${{ matrix.sanitizer }}
          cmake --build build/${{ matrix.sanitizer }}
          ctest --preset ${{ matrix.sanitizer }} --output-on-failure
```

## Testing Best Practices

### 1. Test Naming Conventions

```cpp
// Format: Descriptive test names

TEST(MessageQueueTest, IncreasesSizeWhenPushingItems)
TEST(AgentBaseTest, DrainsMailboxWhenShuttingDownWithPendingMessages)
TEST(RootAgentTest, CreatesMultipleBranchesForComplexTaskDecomposition)
```

### 2. Test Independence

```cpp
// BAD: Tests depend on each other
static State global_state;  // ❌ Global state shared across tests

TEST(BadTestExample, InitializeGlobalState) {
    global_state = initialize();
}

TEST(BadTestExample, CheckStateIsReady) {
    EXPECT_TRUE(global_state.isReady());  // ❌ Depends on previous test
}

// GOOD: Each test is independent using fixtures
class GoodExampleTest : public ::testing::Test {
protected:
    void SetUp() override {
        state_ = initialize();  // ✅ Fresh state per test
    }
    State state_;
};

TEST_F(GoodExampleTest, StateIsProperlyInitialized) {
    EXPECT_TRUE(state_.isReady());
}
```

### 3. Deterministic Tests

```cpp
// BAD: Non-deterministic
TEST(DeterministicTest, WorkCompletesWithinTimeout) {
    auto start = now();
    doWork();
    auto elapsed = now() - start;
    EXPECT_LT(elapsed, std::chrono::milliseconds(100));  // ❌ Flaky on slow machines
}

// GOOD: Deterministic with mocking
TEST(DeterministicTest, WorkUsesExpectedClockTime) {
    MockClock clock;
    clock.setTime(0);
    doWorkWithClock(clock);
    EXPECT_EQ(clock.getTime(), 50);  // ✅ Deterministic
}
```

### 4. Comprehensive Assertions

```cpp
// Use specific assertions
EXPECT_EQ(value, 42);        // ✅ Specific
EXPECT_TRUE(value == 42);    // ❌ Generic

EXPECT_THROW(func(), std::runtime_error);  // ✅ Specific exception
EXPECT_ANY_THROW(func());                  // ❌ Too broad
```

## Test Maintenance

### Continuous Test Review

- **Weekly**: Review failing tests, update flaky tests
- **Monthly**: Analyze coverage reports, identify gaps
- **Quarterly**: Update performance baselines

### Test Debt Management

- Track test TODO items in issues
- Prioritize tests for critical paths
- Remove obsolete tests promptly

---

**Document Version**: 1.0
**Last Updated**: 2025-11-17
