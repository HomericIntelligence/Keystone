# Performance Benchmarks for Keystone

This directory contains comprehensive performance benchmarks for the Keystone HMAS using Google Benchmark.

## Overview

Keystone includes **45+ benchmark tests** across 5 benchmark suites:

1. **hierarchy_benchmarks** (5 benchmarks) - Sync vs async agent hierarchies
2. **message_pool_benchmarks** (10 benchmarks) - Message pooling performance
3. **distributed_benchmarks** (8 benchmarks) - Distributed work-stealing
4. **message_bus_benchmarks** (11 benchmarks) - MessageBus routing and delivery
5. **resilience_benchmarks** (20 benchmarks) - Retry policy, circuit breaker, heartbeat

## Building Benchmarks

Benchmarks should be built in **Release mode** for accurate performance measurements:

```bash
# Clean build
rm -rf build
mkdir build && cd build

# Configure with Release mode
cmake -DCMAKE_BUILD_TYPE=Release -G Ninja ..

# Build all benchmarks
ninja

# Benchmark executables are now available:
# - hierarchy_benchmarks
# - message_pool_benchmarks
# - distributed_benchmarks
# - message_bus_benchmarks
# - resilience_benchmarks
```

## Running Benchmarks

### Quick Start

```bash
# Run all benchmarks with automated regression detection
./scripts/run_benchmarks.sh

# Run a specific benchmark suite
./build/message_bus_benchmarks

# Run with filter
./build/message_bus_benchmarks --benchmark_filter=BM_MessageRouting
```

### Automated Benchmark Suite

The `scripts/run_benchmarks.sh` script provides:

- **Automated execution** of all benchmark suites
- **Result aggregation** in JSON format
- **Baseline management** for regression detection
- **Performance regression detection** (>10% slowdown triggers failure)

```bash
# Basic usage
./scripts/run_benchmarks.sh

# Save current run as baseline
./scripts/run_benchmarks.sh --baseline

# Compare against baseline (CI/CD)
./scripts/run_benchmarks.sh --compare benchmarks/results/baseline.json

# Run specific benchmarks
./scripts/run_benchmarks.sh --filter BM_MessageRouting

# Output formats
./scripts/run_benchmarks.sh --format json
./scripts/run_benchmarks.sh --format csv
```

## Benchmark Suites

### 1. Hierarchy Performance (hierarchy_benchmarks)

Measures performance of sync vs async agent hierarchies:

```bash
./build/hierarchy_benchmarks
```

**Benchmarks:**

- `BM_Sync4LayerHierarchy` - Synchronous 4-layer message flow
- `BM_Async4LayerHierarchy_4Workers` - Async with 4 worker threads
- `BM_Async4LayerHierarchy_8Workers` - Async with 8 worker threads
- `BM_AsyncTaskAgentThroughput` - Task agent message throughput
- `BM_SchedulerSubmissionRate` - Work-stealing scheduler submission rate

**Key Metrics:**

- Messages/second throughput
- Latency per message
- Scalability with worker count

### 2. Message Pool Performance (message_pool_benchmarks)

Measures message pooling effectiveness:

```bash
./build/message_pool_benchmarks
```

**Benchmarks:**

- `BM_MessageCreation_NoPooling` - Baseline allocation overhead
- `BM_MessageCreation_WithPooling` - Pooled allocation
- `BM_MessageBurst_*` - Burst traffic patterns
- `BM_SteadyState_*` - Steady-state traffic
- `BM_PoolStatistics` - Pool statistics overhead
- `BM_PoolHitRate` - Cache hit rate measurement
- `BM_ThreadLocalPooling` - Thread-local pool performance

**Key Metrics:**

- Allocation speed (allocations/sec)
- Pool hit rate (%)
- Memory reuse efficiency

### 3. Distributed Work-Stealing (distributed_benchmarks)

Measures distributed work-stealing performance across simulated NUMA nodes:

```bash
./build/distributed_benchmarks
```

**Benchmarks:**

- `BM_WorkStealing_LocalOnly` - Single-node baseline
- `BM_WorkStealing_TwoNodes_*` - Cross-node stealing with varying latencies
- `BM_LoadBalancing_Imbalanced` - Load balancing effectiveness
- `BM_NetworkOverhead_MessageOnly` - Network overhead measurement
- `BM_AgentAffinity_Registered` - CPU affinity impact
- `BM_PacketLoss_Impact` - Packet loss resilience

**Key Metrics:**

- Work stealing latency
- Load balance ratio
- Network overhead (%)
- Packet loss impact

### 4. Message Bus Performance (message_bus_benchmarks)

Measures MessageBus routing and delivery performance:

```bash
./build/message_bus_benchmarks
```

**Benchmarks:**

- `BM_MessageRouting_SingleAgent` - Single agent routing latency
- `BM_MessageRouting_FanOut` - Fan-out to N agents (8-512)
- `BM_AgentRegistration` - Agent registration overhead
- `BM_AgentUnregistration` - Unregistration overhead
- `BM_AgentLookup` - hasAgent() lookup speed (8-1024 agents)
- `BM_ListAgents` - List all agents overhead
- `BM_ConcurrentRouting` - Multi-threaded routing (1-8 threads)
- `BM_MessageRouting_WithPayload` - Payload size impact (64B-64KB)
- `BM_MessageRoundTrip` - Round-trip latency
- `BM_MessageBroadcast` - Broadcast to N agents (8-256)

**Key Metrics:**

- Routing latency (ns/message)
- Throughput (messages/sec)
- Scalability with agent count
- Payload size impact

### 5. Resilience Performance (resilience_benchmarks)

Measures retry policy, circuit breaker, and heartbeat monitor performance:

```bash
./build/resilience_benchmarks
```

**Benchmarks:**

**Retry Policy (8 benchmarks):**

- `BM_RetryPolicy_Creation` - Policy creation overhead
- `BM_RetryPolicy_ShouldRetry` - shouldRetry() latency
- `BM_RetryPolicy_BackoffCalculation` - Backoff delay calculation
- `BM_RetryPolicy_FullSequence` - Full retry sequence (1-64 retries)
- `BM_RetryPolicy_VaryingMultiplier` - Multiplier impact (1.0-5.0)

**Circuit Breaker (8 benchmarks):**

- `BM_CircuitBreaker_Creation` - CB creation overhead
- `BM_CircuitBreaker_AllowRequest_Closed` - Request check latency
- `BM_CircuitBreaker_RecordSuccess` - Success recording
- `BM_CircuitBreaker_RecordFailure` - Failure recording
- `BM_CircuitBreaker_StateTransition` - State transition latency
- `BM_CircuitBreaker_GetState` - State query
- `BM_CircuitBreaker_Concurrent` - Multi-threaded access (1-8 threads)

**Heartbeat Monitor (6 benchmarks):**

- `BM_HeartbeatMonitor_Creation` - Monitor creation
- `BM_HeartbeatMonitor_RegisterAgent` - Agent registration
- `BM_HeartbeatMonitor_RecordHeartbeat` - Heartbeat recording
- `BM_HeartbeatMonitor_IsAgentAlive` - Liveness check
- `BM_HeartbeatMonitor_GetDeadAgents` - Dead agent detection (8-512 agents)
- `BM_HeartbeatMonitor_ConcurrentHeartbeat` - Concurrent heartbeat (1-8 threads)

**Key Metrics:**

- Retry policy overhead (ns)
- Circuit breaker latency (ns)
- Heartbeat recording speed
- Concurrent access performance

## Performance Baselines

### Expected Performance Targets

Based on initial benchmarks, Keystone targets:

| Component | Metric | Target | Notes |
|-----------|--------|--------|-------|
| MessageBus Routing | Latency | < 500 ns | Single agent, no payload |
| MessageBus Routing | Throughput | > 2M msg/sec | Single thread |
| Message Pool | Allocation | > 10M alloc/sec | With pooling enabled |
| Work-Stealing | Steal Latency | < 10 us | Same node |
| Work-Stealing | Remote Steal | < 100 us | Cross-node (100us RTT) |
| Retry Policy | shouldRetry | < 20 ns | Check only |
| Circuit Breaker | allowRequest | < 50 ns | Closed state |
| Heartbeat | Record | < 100 ns | Single heartbeat |

### Latency Percentiles

Google Benchmark supports latency percentiles using `SetStatistics`:

```cpp
BENCHMARK(BM_MessageRouting)
    ->Repetitions(1000)
    ->ComputeStatisticsWithPercentiles({50, 95, 99, 99.9});
```

This reports:

- **p50** (median) - Typical latency
- **p95** - 95th percentile
- **p99** - 99th percentile (tail latency)
- **p99.9** - 99.9th percentile (worst-case)

## Regression Detection

The `run_benchmarks.sh` script implements automated regression detection:

### Workflow

```bash
# 1. Establish baseline (after implementing a feature)
./scripts/run_benchmarks.sh --baseline

# 2. Make changes to code
# ... edit code ...

# 3. Run regression check
./scripts/run_benchmarks.sh --compare benchmarks/results/baseline.json

# If regressions detected (>10% slowdown), script exits with error code 1
```

### Regression Threshold

- **Regression**: >10% slower than baseline (ratio > 1.10)
- **Improvement**: >10% faster than baseline (ratio < 0.90)
- **Unchanged**: Within ±10% of baseline

### Example Output

```
✓ Passing: 38
↑ Improvements: 3
↓ Regressions: 2

=== REGRESSIONS (>10% slower) ===
  BM_MessageRouting_FanOut/512
    Baseline: 245.23 us
    Current:  285.67 us
    Change:   +16.5% (x1.16)
```

## CI/CD Integration

In CI/CD pipelines (GitHub Actions):

```yaml
# .github/workflows/benchmarks.yml
- name: Run Benchmarks
  run: |
    mkdir build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release -G Ninja ..
    ninja
    cd ..
    ./scripts/run_benchmarks.sh --compare benchmarks/results/baseline.json
```

This ensures no performance regressions are merged to main.

## Profiling

For detailed performance analysis:

```bash
# Run with profiler
perf record -g ./build/message_bus_benchmarks --benchmark_filter=BM_MessageRouting_SingleAgent
perf report

# Flamegraph visualization
perf script | FlameGraph/stackcollapse-perf.pl | FlameGraph/flamegraph.pl > flamegraph.svg
```

## Troubleshooting

### Noisy Benchmarks

If benchmarks show high variance:

```bash
# Increase minimum time
./build/message_bus_benchmarks --benchmark_min_time=5.0

# Increase repetitions
./build/message_bus_benchmarks --benchmark_repetitions=10

# Disable CPU frequency scaling
sudo cpupower frequency-set --governor performance
```

### Low Throughput

If throughput is unexpectedly low:

1. Verify Release build: `cmake -DCMAKE_BUILD_TYPE=Release`
2. Check CPU governor: `cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor`
3. Disable thermal throttling monitoring
4. Run on isolated CPU core: `taskset -c 0 ./build/message_bus_benchmarks`

## References

- [Google Benchmark User Guide](https://github.com/google/benchmark/blob/main/docs/user_guide.md)
- [Benchmark Optimization Tips](https://github.com/google/benchmark/blob/main/docs/reducing_variance.md)
- [Perf Tutorial](https://perf.wiki.kernel.org/index.php/Tutorial)

## Phase 9.4 Checklist

- [x] Create 5 comprehensive benchmark suites (45+ benchmarks total)
- [x] Implement message bus performance benchmarks (11 tests)
- [x] Implement resilience performance benchmarks (20 tests)
- [x] Add CMake integration for new benchmarks
- [x] Create automated benchmark runner script
- [x] Implement regression detection (>10% threshold)
- [x] Establish performance baselines and targets
- [x] Document benchmark usage and CI/CD integration
- [ ] Run initial baseline and commit results (optional)

**Next**: Phase 9.5 - CI/CD Quality Gates
