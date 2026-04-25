# Phase D: Performance Optimization Plan

**Status**: 📝 Planning Phase
**Date**: 2025-11-18
**Branch**: `claude/work-stealing-migration-01D8QY8wG6fDLMjVeu3H8UCC`

## Overview

Phase D focuses on **practical performance optimizations** that provide measurable benefits for the current single-node, multi-threaded HMAS architecture. This document analyzes proposed optimizations and categorizes them as:

- **✅ IMPLEMENT NOW**: Clear benefit, low complexity, addresses current needs
- **🔄 DEFER**: Premature optimization, needs more data, or requires infrastructure first
- **❌ NOT NEEDED**: Not applicable to current architecture

## Current System Status

### What We Have (Phases A-C Complete)

- ✅ Work-stealing scheduler with configurable workers
- ✅ **Lock-free priority queues** (HIGH/NORMAL/LOW) - Already implemented in Phase C!
- ✅ Lock-free work-stealing queues (moodycamel::ConcurrentQueue)
- ✅ Weighted round-robin anti-starvation scheduling
- ✅ Comprehensive metrics collection (message counts, latency, queue depth)
- ✅ Deadline scheduling with miss detection
- ✅ Thread-local logging with distributed tracing
- ✅ Async coroutine-based agents

### Performance Baseline (from partial benchmarks)

- **Sync 4-Layer**: 41.2 µs/op, **24.3k items/sec**
- **Async 4 Workers**: 10.1 ms/op, 20k items/sec
- **Async 8 Workers**: 10.1 ms/op, 14.3k items/sec

**Observation**: Async mode is **245x slower** than sync mode (10.1ms vs 41µs). This suggests significant overhead from:

- Scheduler startup/shutdown in benchmark loop
- 10ms sleep for async completion
- Context switching overhead

## Proposed Optimizations Analysis

### 1. Lock-Free Agent Inboxes ✅ **ALREADY DONE**

**Status**: ❌ NOT NEEDED - Already implemented in Phase C!

**Details**:

- Phase C implemented lock-free priority inboxes using `moodycamel::ConcurrentQueue`
- Each agent has 3 lock-free queues: `high_priority_inbox_`, `normal_priority_inbox_`, `low_priority_inbox_`
- No mutex contention in message delivery path
- See `include/agents/agent_base.hpp:76-78`

**Conclusion**: This optimization is complete. Move on.

---

### 2. NUMA-Aware Scheduling 🔄 **DEFER**

**Status**: 🔄 DEFER - Premature optimization for current needs

**Rationale**:

- Requires multi-socket hardware with NUMA topology
- Current testing environment: 16-core single CPU (no NUMA zones)
- Complexity: High (requires libnuma, topology detection, worker pinning)
- Benefit: Only measurable on multi-socket systems (not our target)

**When to Reconsider**:

- When deploying on multi-socket servers (2+ CPUs)
- When profiling shows cross-NUMA memory access latency
- When scaling to 32+ cores across multiple sockets

**Alternative NOW**: Simple CPU affinity (see #7 below)

---

### 3. Zero-Copy Message Passing 🔄 **DEFER**

**Status**: 🔄 DEFER - Requires network transport first

**Current State**:

- Messages passed by value: `KeystoneMessage msg` copied into queues
- Cista serialization ready but not used (no network transport)
- Copy cost: Small (KeystoneMessage is ~200 bytes with small payloads)

**Why Defer**:

- **No network layer yet**: Zero-copy is for inter-process/inter-node communication
- **Intra-process**: Shared memory is already "zero-copy" conceptually
- **Complexity**: Requires shared_ptr or custom refcounting, lifetime management
- **Current bottleneck**: Not message copying (see benchmark - async overhead dominates)

**When to Implement**:

- When adding distributed work-stealing (Phase D-Distributed)
- When network transport layer is added
- When profiling shows message copy overhead >10% of total time

**Conclusion**: Not the current bottleneck. Defer until network layer exists.

---

### 4. Custom Allocators for Work Items 🔄 **DEFER**

**Status**: 🔄 DEFER - Need allocation profiling first

**Proposed Optimization**:

- Pool allocator for WorkItem objects
- Arena allocator for message payloads
- Thread-local allocators to reduce contention

**Why Defer**:

- **No evidence of allocator bottleneck**: Benchmarks don't show malloc as hotspot
- **Complexity**: High (custom allocators, thread-safety, fragmentation)
- **Modern allocators**: jemalloc/tcmalloc already very fast for small objects
- **Premature**: Should profile first with `perf` or `heaptrack`

**How to Measure First**:

```bash
# Profile allocations
heaptrack ./hierarchy_benchmarks
heaptrack_print heaptrack.hierarchy_benchmarks.*.gz

# Or use perf
perf record -e cycles ./hierarchy_benchmarks
perf report
```

**When to Implement**:

- After profiling shows >15% time in malloc/free
- When processing >1M messages/sec (high allocation rate)
- When memory fragmentation becomes measurable issue

**Conclusion**: Measure first, optimize later.

---

### 5. Message Pooling / Object Reuse ✅ **IMPLEMENT NOW**

**Status**: ✅ IMPLEMENT NOW - Low complexity, clear benefit

**Rationale**:

- Reduce allocation pressure for high-frequency message creation
- Simple object pool pattern (lock-free if per-thread)
- Immediate benefit for high-throughput scenarios
- Low implementation complexity

**Design**:

```cpp
class MessagePool {
public:
    static KeystoneMessage acquire() {
        // Try to get from thread-local pool
        // If empty, create new message
    }

    static void release(KeystoneMessage&& msg) {
        // Reset and return to pool
        // If pool full, let it destruct
    }

private:
    static thread_local std::vector<KeystoneMessage> pool_;
    static constexpr size_t MAX_POOL_SIZE = 1000;
};
```

**Benefits**:

- Reduces allocation count under load
- Amortizes allocation cost over message lifetime
- Thread-local pool avoids contention

**Implementation Effort**: Low (~100 LOC)

**Priority**: HIGH - Implement in Phase D

---

### 6. Queue Depth Alerting & Monitoring ✅ **IMPLEMENT NOW**

**Status**: ✅ IMPLEMENT NOW - Builds on existing metrics

**Current State**:

- Phase C has `recordQueueDepth()` collecting data
- No alerting or threshold monitoring
- No dashboard or real-time visibility

**What to Add**:

1. **Queue depth thresholds**:
   - Warning: depth > 1000 messages
   - Critical: depth > 10000 messages
2. **Metrics export**:
   - Prometheus metrics endpoint (or simple JSON)
   - Per-agent queue depth histograms
3. **Backpressure detection**:
   - Identify agents with consistently full queues
   - Flag potential message processing bottlenecks

**Design**:

```cpp
class Metrics {
public:
    void recordQueueDepth(const std::string& agent_id, size_t depth) {
        // Existing implementation...

        // NEW: Check thresholds
        if (depth > CRITICAL_DEPTH) {
            Logger::critical("Agent {} queue critical: {}", agent_id, depth);
        } else if (depth > WARNING_DEPTH) {
            Logger::warn("Agent {} queue high: {}", agent_id, depth);
        }
    }

    std::string exportPrometheusMetrics() const;

private:
    static constexpr size_t WARNING_DEPTH = 1000;
    static constexpr size_t CRITICAL_DEPTH = 10000;
};
```

**Implementation Effort**: Low (~200 LOC)

**Priority**: HIGH - Operational visibility is critical

---

### 7. Worker Thread CPU Affinity ✅ **IMPLEMENT NOW**

**Status**: ✅ IMPLEMENT NOW - Simple, measurable benefit

**Rationale**:

- Pin worker threads to specific CPU cores
- Reduces context switching and cache thrashing
- Much simpler than full NUMA-aware scheduling
- Works on single-socket systems (our target)

**Design**:

```cpp
class WorkStealingScheduler {
public:
    void start(bool enable_affinity = true) {
        for (size_t i = 0; i < workers_.size(); ++i) {
            workers_[i] = std::thread([this, i, enable_affinity]() {
                if (enable_affinity) {
                    setCPUAffinity(i);
                }
                workerLoop(i);
            });
        }
    }

private:
    void setCPUAffinity(size_t worker_index) {
        #ifdef __linux__
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(worker_index % std::thread::hardware_concurrency(), &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        #endif
    }
};
```

**Benefits**:

- Better cache locality (L1/L2 cache per core)
- Reduced scheduler overhead (less migration)
- Predictable performance (no surprise migrations)

**Implementation Effort**: Low (~50 LOC)

**Priority**: MEDIUM-HIGH - Easy win for cache performance

---

### 8. Adaptive Worker Pool Sizing 🔄 **DEFER**

**Status**: 🔄 DEFER - Needs load monitoring infrastructure first

**Proposed**:

- Dynamically add/remove workers based on queue depth
- Scale up when queues full, scale down when idle
- Auto-tuning for varying workloads

**Why Defer**:

- **Complexity**: High (thread lifecycle management, hysteresis, thresholds)
- **Risk**: Thread creation overhead can make performance worse
- **Alternative**: Start with static optimal worker count (hardware_concurrency)
- **Prerequisites**: Need queue depth monitoring first (item #6)

**When to Implement**:

- After queue depth monitoring is operational
- When workload varies significantly over time
- When static worker count proves suboptimal

**Conclusion**: Get monitoring first, then decide if adaptive sizing is needed.

---

### 9. Performance Profiling Infrastructure ✅ **IMPLEMENT NOW**

**Status**: ✅ IMPLEMENT NOW - Essential for data-driven optimization

**What to Add**:

1. **Built-in profiling mode**:
   - `KEYSTONE_PROFILE=1 ./app` enables detailed tracing
   - Per-agent processing time histograms
   - Work-stealing statistics (steals per worker)
2. **Flame graph support**:
   - Integrate with `perf` or `gperftools`
   - Auto-generate flame graphs from benchmarks
3. **Benchmark improvements**:
   - Remove artificial `sleep(10ms)` in async benchmarks
   - Add realistic workload simulations
   - Measure actual async message flow (not setup/teardown)

**Design**:

```cpp
class ProfilingSession {
public:
    static void start(const std::string& name) {
        if (profiling_enabled_) {
            auto start = std::chrono::high_resolution_clock::now();
            sessions_[name] = start;
        }
    }

    static void end(const std::string& name) {
        if (profiling_enabled_) {
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = end - sessions_[name];
            recordDuration(name, duration);
        }
    }

private:
    static bool profiling_enabled_;
    static thread_local std::unordered_map<std::string, TimePoint> sessions_;
};

// Usage in code
void processMessage(const KeystoneMessage& msg) {
    ProfilingSession::start("processMessage");
    // ... actual work ...
    ProfilingSession::end("processMessage");
}
```

**Implementation Effort**: Medium (~300 LOC)

**Priority**: HIGH - Foundational for all other optimizations

---

### 10. Fix Async Benchmark Overhead ✅ **IMPLEMENT NOW**

**Status**: ✅ IMPLEMENT NOW - Current benchmarks misleading

**Problem**:

- Async benchmarks include scheduler startup/shutdown in timing
- Artificial 10ms sleep makes results meaningless
- Not measuring actual message flow performance

**Fix**:

```cpp
// BEFORE (misleading)
for (auto _ : state) {
    chief->sendCommandAsync("command", "agent");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));  // BAD!
}

// AFTER (accurate)
static void BM_Async4LayerHierarchy_4Workers(benchmark::State& state) {
    // Setup ONCE (outside benchmark loop)
    WorkStealingScheduler scheduler(4);
    scheduler.start();
    // ... setup agents ...

    for (auto _ : state) {
        // Just measure message send + processing
        auto start = std::chrono::high_resolution_clock::now();
        chief->sendCommandAsync("command", "agent");

        // Wait for ACTUAL completion (not arbitrary 10ms)
        while (!chief->hasResponse()) {
            std::this_thread::yield();
        }
        auto end = std::chrono::high_resolution_clock::now();

        state.SetIterationTime(
            std::chrono::duration<double>(end - start).count());
    }

    // Teardown ONCE (outside benchmark loop)
    scheduler.shutdown();
}
```

**Implementation Effort**: Low (~100 LOC)

**Priority**: CRITICAL - Need accurate performance data

---

## Phase D Implementation Priority

### Phase D.1: Foundational (Week 1) ✅ **DO NOW**

1. **Fix async benchmarks** (CRITICAL)
   - Remove scheduler startup/shutdown from loop
   - Remove artificial sleep
   - Add proper async completion detection
   - **Estimated**: 2 hours

2. **Performance profiling infrastructure** (HIGH)
   - Add ProfilingSession helper class
   - Integrate with existing Logger
   - Environment variable control (`KEYSTONE_PROFILE=1`)
   - **Estimated**: 4 hours

3. **Queue depth alerting** (HIGH)
   - Add threshold checking to recordQueueDepth()
   - Log warnings/criticals via Logger
   - **Estimated**: 2 hours

**Total Week 1**: 8 hours, delivers accurate benchmarks + operational monitoring

---

### Phase D.2: Performance Wins (Week 2) ✅ **DO NOW**

4. **Worker CPU affinity** (MEDIUM-HIGH)
   - Linux pthread_setaffinity_np()
   - Optional enable/disable flag
   - Test with/without affinity
   - **Estimated**: 3 hours

5. **Message pooling** (HIGH)
   - Thread-local MessagePool class
   - acquire() / release() API
   - Integration with KeystoneMessage::create()
   - **Estimated**: 5 hours

**Total Week 2**: 8 hours, delivers measurable performance gains

---

### Phase D.3: Advanced Monitoring (Week 3) 🔄 **OPTIONAL**

6. **Metrics export endpoint** (MEDIUM)
   - Prometheus-compatible format
   - HTTP endpoint or file export
   - Per-agent histograms
   - **Estimated**: 6 hours

7. **Work-stealing statistics** (MEDIUM)
   - Track steals per worker
   - Queue utilization metrics
   - Load balancing visualization
   - **Estimated**: 4 hours

**Total Week 3**: 10 hours, nice-to-have operational tools

---

### Phase D.3: Simulation Architecture ✅ **IMPLEMENT** (NEW)

**UPDATE**: User requested simulation-based implementation to test NUMA and networking optimizations without requiring actual hardware.

9. **NUMA Simulation Framework** (HIGH PRIORITY - NEW)
   - Multiple WorkStealingSchedulers simulating different NUMA nodes
   - Artificial latency injection for cross-"node" access
   - Thread pool per simulated node
   - Configurable node count (e.g., 2-4 nodes)
   - Agent-to-node affinity policies

10. **Network Simulation Layer** (HIGH PRIORITY - NEW)
    - Local sockets or pipes simulating network communication
    - Configurable latency (e.g., 100µs-1ms between "nodes")
    - Bandwidth limiting simulation
    - Message serialization via Cista
    - Packet loss simulation (optional)

11. **Distributed Work-Stealing Simulation** (MEDIUM PRIORITY - NEW)
    - Cross-"node" work stealing with simulated network cost
    - Steal thresholds based on local queue depth
    - Network-aware stealing policies (prefer local, steal remote when desperate)
    - Statistics: local vs remote steals, network message count

**Benefits of Simulation**:

- Test distributed features on single machine
- Adjustable parameters (latency, bandwidth, node count)
- Reproducible experiments
- No hardware dependencies
- Easy to test edge cases (high latency, packet loss)

**Implementation Approach**:

```cpp
class SimulatedNUMANode {
    WorkStealingScheduler scheduler;
    size_t node_id;
    std::vector<SimulatedNUMANode*> remote_nodes;  // Other nodes

    // Simulate cross-node latency
    void crossNodeDelay() {
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }

    bool stealFromRemote() {
        crossNodeDelay();  // Network latency
        // Try to steal from remote nodes...
    }
};
```

**Estimated Effort**: 12-16 hours for full simulation framework

### NOT Implementing in Phase D ❌

- ❌ **Custom allocators** - No evidence of allocator bottleneck yet
- ❌ **Adaptive worker pool** - Too complex without monitoring first

**Note**: NUMA-aware and distributed work-stealing ARE being implemented via simulation!

---

## Success Criteria for Phase D

### Must Have ✅

- [x] Async benchmarks accurately measure message flow (not setup/teardown)
- [x] Performance profiling can identify performance bottlenecks
- [x] Queue depth alerts log warnings when agents overloaded
- [x] All existing tests still passing ✅ **255/255 tests passing**

### Should Have 🎯

- [x] Worker CPU affinity implemented (Phase D.2) ✅
- [x] Message pooling reduces allocation count (Phase D.2) ✅
- [x] Benchmark suite runs in <2 minutes ✅ ACHIEVED (now ~30 sec vs 10+ min before)

### Nice to Have 💡

- [ ] Prometheus metrics export for Grafana dashboards (deferred)
- [ ] Work-stealing efficiency >80% (successful steals / steal attempts)
- [ ] Flame graphs generated automatically from benchmarks (deferred)

### Phase D.3 Simulation Criteria ✅

- [x] NUMA simulation with 2-4 simulated nodes ✅ **SimulatedCluster**
- [x] Cross-node latency injection (configurable 100µs-1ms) ✅ **SimulatedNetwork**
- [x] Network-aware work stealing (local-first policy) ✅ **WorkStealingScheduler**
- [x] Statistics: local steals vs remote steals ratio ✅ **SimulatedNUMANode tracks**
- [x] Benchmarks comparing local-only vs distributed work-stealing ✅ **distributed_work_stealing.cpp**
- [x] **76 simulation tests** (6 integration + 70 unit tests)

---

## Rationale Summary

**What Makes Sense NOW**:

1. **Fix benchmarks** - Can't optimize what we can't measure accurately
2. **Profiling infra** - Data-driven decisions beat guessing
3. **Queue alerting** - Operational visibility prevents production issues
4. **CPU affinity** - Simple, low-risk, measurable cache benefit
5. **Message pooling** - Reduces allocation pressure, common optimization

**What Doesn't Make Sense YET**:

1. **NUMA** - No multi-socket hardware to target
2. **Zero-copy** - No network layer to benefit from it
3. **Custom allocators** - No evidence malloc is the bottleneck
4. **Adaptive sizing** - Need monitoring data first
5. **Distributed** - Single-node architecture for now

**Philosophy**:
> "Premature optimization is the root of all evil" - Donald Knuth

Measure first, optimize what matters, defer complexity until it's justified by data.

---

**Next Steps**:

1. ✅ Phase D.1 COMPLETE: Queue depth alerting + profiling infrastructure
2. ✅ Phase D.2 COMPLETE: CPU affinity + message pooling
3. ✅ Phase D.3 COMPLETE: NUMA/Network Simulation Architecture
   - ✅ SimulatedNUMANode class (wrapper around WorkStealingScheduler)
   - ✅ Cross-node latency injection (SimulatedNetwork)
   - ✅ Network-aware work stealing (built into WorkStealingScheduler)
   - ✅ Distributed benchmarks (distributed_work_stealing.cpp)
   - ✅ 76 simulation tests (6 integration E2E + 70 unit tests)
4. **Phase D COMPLETE** - All criteria met ✅

**Status**: ✅ **PHASE D COMPLETE** - All sub-phases complete (D.1, D.2, D.3)
**Last Updated**: 2025-11-19 (Phase D completed with full simulation framework and 255 total tests passing)
