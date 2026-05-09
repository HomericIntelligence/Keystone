# ADR-005: Message Pool Design

**Status**: Accepted
**Date**: 2025-11-21
**Deciders**: ProjectKeystone Development Team
**Tags**: architecture, performance, memory, phase-d

## Context

ProjectKeystone's hierarchical agent system generates thousands of `KeystoneMessage` objects per second under load.
Each message allocation/deallocation incurs overhead from the system allocator (malloc/free), causing performance
degradation and memory fragmentation at scale.

### Performance Problem

**Benchmark Results (Before Message Pool)**:

- 100,000 messages/sec → ~15% CPU time in malloc/free
- Memory fragmentation → 20-30% unused heap space after 1 hour
- Allocation latency → p99: 500ns (vs 50ns for pooled)

### Requirements

1. **Reduce Allocations**: Reuse message objects to minimize malloc/free
2. **Low Overhead**: Minimal synchronization and bookkeeping
3. **Bounded Memory**: Prevent unbounded pool growth
4. **Thread-Safe**: Support concurrent agent operations
5. **Opt-In**: Backward compatible with existing `KeystoneMessage::create()`

## Decision

Implement **thread-local object pooling** with fixed maximum size and RAII-based lifecycle management.

### Design Choices

#### 1. Thread-Local Storage (No Synchronization)

```cpp
class MessagePool {
private:
  struct ThreadLocalData {
    std::vector<KeystoneMessage> pool;
    PoolStats stats;
  };

  static ThreadLocalData& getThreadLocal() {
    thread_local ThreadLocalData data;  // One pool per thread
    return data;
  }
};
```

**Rationale**:

- **Zero Synchronization**: Each thread has its own pool (no mutex/atomics)
- **Cache Locality**: Thread's messages stay in local cache
- **Scalability**: No contention between worker threads
- **Simple**: Standard `thread_local` storage

**Alternative (Rejected): Global Pool with Mutex**:

```cpp
std::mutex pool_mutex_;
std::vector<KeystoneMessage> global_pool_;  // Shared across threads
```

- **Contention**: All threads compete for single lock
- **Bottleneck**: Serializes all acquire/release operations
- **Cache Thrashing**: Messages ping-pong between thread caches

**Benchmarks**: Thread-local 20x faster than global mutex pool.

#### 2. Fixed Maximum Pool Size

```cpp
static constexpr size_t MAX_POOL_SIZE = 1000;  // Per thread

void release(KeystoneMessage&& msg) {
  auto& tld = getThreadLocal();

  if (tld.pool.size() >= MAX_POOL_SIZE) {
    // Pool full - let message be destroyed (RAII)
    return;
  }

  // Reset and return to pool
  msg.msg_id.clear();
  msg.sender_id.clear();
  // ... reset all fields ...

  tld.pool.push_back(std::move(msg));
}
```

**Rationale**:

- **Bounded Memory**: Each worker limited to 1000 messages (~1-10MB depending on payload)
- **Total Memory**: 256 workers *1000 msgs* ~10KB = ~2.5GB max (acceptable)
- **Prevents Leaks**: No unbounded growth from forgotten releases
- **Graceful Degradation**: Falls back to normal allocation when full

**Why 1000?**:

- **Sufficient for Bursts**: Typical agent processes 10-100 msgs/sec
- **Reasonable Memory**: ~10MB per thread worst case
- **Rare Overflow**: Only under extreme sustained load (>1000 msgs/sec)

**Alternative (Rejected): Unbounded Pool**:

- **Memory Leak Risk**: Pool grows indefinitely if releases exceed acquires
- **Production Risk**: Cannot predict max memory usage

**Alternative (Rejected): Smaller Limit (100)**:

- **Frequent Fallback**: Pool exhausted under moderate load
- **Reduced Benefit**: 50-70% reuse vs 80-90% with 1000

#### 3. RAII and Automatic Cleanup

```cpp
KeystoneMessage acquire() {
  auto& tld = getThreadLocal();

  if (!tld.pool.empty()) {
    KeystoneMessage msg = std::move(tld.pool.back());
    tld.pool.pop_back();
    return msg;  // Reused from pool
  }

  // Pool empty - create new
  KeystoneMessage msg{};
  msg.priority = Priority::NORMAL;
  return msg;
}

void release(KeystoneMessage&& msg) {
  // ... reset msg ...
  tld.pool.push_back(std::move(msg));
}

// Optional: Explicit release
MessagePool::release(std::move(msg));

// Automatic: Message destructor (RAII)
// No explicit release needed - message destroyed normally
```

**Opt-In Pattern**:

```cpp
// Using pool (opt-in)
auto msg = MessagePool::acquire();
msg.sender_id = "agent1";
// ... use message ...
MessagePool::release(std::move(msg));  // Return to pool

// Not using pool (existing code, still works)
auto msg = KeystoneMessage::create("agent1", "agent2", "cmd");
// ... use message ...
// Destructor runs normally, no pool interaction
```

**Rationale**:

- **Backward Compatible**: Existing code unchanged
- **Explicit Control**: Developer chooses when to use pool
- **RAII Safety**: If release() forgotten, message still destroyed safely

#### 4. Message Reset Before Reuse

```cpp
void release(KeystoneMessage&& msg) {
  // Reset to default state
  msg.msg_id.clear();
  msg.sender_id.clear();
  msg.receiver_id.clear();
  msg.command.clear();
  msg.payload.reset();
  msg.priority = Priority::NORMAL;
  msg.deadline.reset();
  msg.metadata.clear();
  // timestamp will be overwritten on next use

  tld.pool.push_back(std::move(msg));
}
```

**Rationale**:

- **No Data Leakage**: Previous message content cleared
- **Predictable State**: Next acquire() gets clean message
- **Security**: Prevent sensitive data from leaking between messages

#### 5. Pool Statistics for Monitoring

```cpp
struct PoolStats {
  size_t total_acquires;    // Total acquire() calls
  size_t total_releases;    // Total release() calls
  size_t pool_hits;         // Acquires from pool (reuse)
  size_t pool_misses;       // Acquires that needed new allocation
  size_t current_size;      // Current pool size
  size_t max_size_reached;  // Maximum pool size seen
};

PoolStats getStats();  // Thread-local stats
```

**Rationale**:

- **Observability**: Verify pool effectiveness in production
- **Debugging**: Detect pool exhaustion or imbalance
- **Tuning**: Adjust MAX_POOL_SIZE based on metrics

**Example Metrics**:

- Hit rate: 85% → Pool working well
- Hit rate: 30% → Pool too small or high churn
- Max size: 950 → Approaching limit, consider increasing

## Consequences

### Positive

1. **Performance**:
   - 50-90% reduction in allocation count under load
   - Allocation latency: 50ns (pooled) vs 500ns (malloc)
   - 10-15% overall throughput improvement
2. **Memory Efficiency**:
   - Reduced fragmentation (fewer alloc/free cycles)
   - Predictable memory usage (bounded by MAX_POOL_SIZE)
3. **Scalability**:
   - Zero contention (thread-local)
   - Linear scaling to 256+ worker threads
4. **Backward Compatibility**:
   - Existing `KeystoneMessage::create()` still works
   - Opt-in adoption (no breaking changes)
5. **Observability**:
   - Statistics for monitoring and tuning
   - Easy to verify effectiveness

### Negative

1. **Memory Overhead**:
   - Up to 1000 messages per thread (even if unused)
   - Worst case: 256 workers * 10MB = 2.5GB
   - Acceptable for server deployments
2. **Thread Affinity**:
   - Message released on different thread → not returned to pool
   - Rare in practice (messages processed on submitting worker)
3. **Manual Release**:
   - Developer must call `MessagePool::release()` explicitly
   - Forgetting is safe (RAII) but loses pool benefit
4. **No Sharing**:
   - Thread A cannot use Thread B's pooled messages
   - Acceptable tradeoff for zero-contention design

### Migration Path

**No Breaking Changes**: Opt-in feature, existing code unaffected.

**Adoption Strategy**:

1. ✅ Implement MessagePool as opt-in feature
2. ✅ Add benchmarks showing benefit
3. Phase D.2: Convert high-throughput paths to use pool
4. Phase E: Adopt in all agent message processing
5. Future: Consider automatic pooling (RAII wrapper)

## Alternatives Considered

### Alternative 1: Global Mutex-Protected Pool

```cpp
class MessagePool {
  std::mutex mutex_;
  std::vector<KeystoneMessage> pool_;

  KeystoneMessage acquire() {
    std::lock_guard lock(mutex_);
    // ... pop from pool ...
  }
};
```

**Rejected**:

- **Contention**: Serializes all acquire/release operations
- **Bottleneck**: 256 workers competing for single mutex
- **Slow**: 20x slower than thread-local in benchmarks

### Alternative 2: Lock-Free Global Pool

```cpp
moodycamel::ConcurrentQueue<KeystoneMessage> pool_;
```

**Rejected**:

- **Cache Thrashing**: Messages bounce between thread caches
- **Overhead**: ConcurrentQueue has per-operation cost (~50ns)
- **Complexity**: More complex than thread-local for marginal gain

**When Acceptable**: If cross-thread message transfer becomes common pattern.

### Alternative 3: Custom Allocator

```cpp
template <typename T>
class PoolAllocator {
  // Custom std::allocator for KeystoneMessage
};

using PooledMessage = std::basic_string<char, std::char_traits<char>, PoolAllocator<char>>;
```

**Rejected**:

- **Invasive**: Requires changing KeystoneMessage definition
- **Complexity**: Custom allocator is error-prone
- **Inflexible**: Hard to reset message state

**Prefer**: Explicit pooling for control and simplicity.

### Alternative 4: Automatic Pooling via Wrapper

```cpp
class PooledMessage {
  KeystoneMessage msg;
  ~PooledMessage() { MessagePool::release(std::move(msg)); }
};
```

**Deferred to Future**:

- **Good Idea**: RAII automatic release
- **Not Urgent**: Manual release acceptable for Phase D
- **Future**: Phase E could add RAII wrapper

### Alternative 5: Per-Agent Pools

```cpp
class AgentBase {
  std::vector<KeystoneMessage> my_pool_;
};
```

**Rejected**:

- **Fragmentation**: Each agent has separate pool
- **Imbalance**: Busy agents exhaust pool, idle agents waste memory
- **Complexity**: Need pool per agent instance

**Thread-Local Better**: Natural load balancing across workers.

## Implementation Details

### Thread Safety

- **Thread-Local**: No synchronization needed
- **Move Semantics**: Messages moved, not copied
- **Atomic-Free**: No atomic operations (pure thread-local)

### Performance Characteristics

- **Acquire (Hit)**: ~50ns (vector pop_back)
- **Acquire (Miss)**: ~500ns (malloc + constructor)
- **Release**: ~200ns (reset + vector push_back)
- **Memory**: O(MAX_POOL_SIZE) = 1000 messages per thread

### Pool Sizing Strategy

```cpp
MAX_POOL_SIZE = 1000;  // Per thread

// Rationale:
// - Typical agent: 10-100 msgs/sec
// - Burst capacity: 1000 msgs covers 10 seconds of backlog
// - Memory: ~10MB per worker (acceptable)
```

**Tuning**:

- High-throughput deployment: 2000
- Memory-constrained: 500
- Low-load: 100

## Validation

### Unit Tests

- ✅ Acquire from empty pool (allocates new)
- ✅ Acquire from non-empty pool (reuses)
- ✅ Release when pool not full (returns to pool)
- ✅ Release when pool full (destroys)
- ✅ Message reset on release
- ✅ Thread-local independence (separate pools)
- ✅ Statistics tracking

### Benchmarks

- ✅ Allocation throughput: 2M msgs/sec (vs 500K without pool)
- ✅ Hit rate under load: 85-90%
- ✅ Memory usage: Bounded to MAX_POOL_SIZE

### E2E Tests

- ✅ High-throughput message processing (10K msgs/sec)
- ✅ Pool effectiveness in multi-agent scenarios

## Future Evolution

### Phase E: RAII Wrapper

```cpp
class PooledMessage {
  KeystoneMessage msg_;

  PooledMessage() : msg_(MessagePool::acquire()) {}
  ~PooledMessage() { MessagePool::release(std::move(msg_)); }

  KeystoneMessage& operator*() { return msg_; }
};

// Usage
auto msg = PooledMessage();
msg->sender_id = "agent1";  // Automatic release on scope exit
```

### Adaptive Pool Sizing

```cpp
// Grow/shrink pool based on hit rate
if (hit_rate < 0.5 && pool.size() < 2000) {
  // Pool too small, allow growth
}
```

### Cross-Thread Pool Sharing

```cpp
// Work stealing can steal pooled messages
MessagePool::release_to_thread(std::move(msg), target_thread_id);
```

## References

- [Object Pool Pattern](https://en.wikipedia.org/wiki/Object_pool_pattern) - Design pattern
- [Memory Allocators](https://www.gingerbill.org/article/2019/02/01/memory-allocation-strategies-001/) - Allocation
strategies
- [Thread-Local Storage](https://en.cppreference.com/w/cpp/keyword/thread_local) - C++ reference
- [ADR-002: Work-Stealing Scheduler](./ADR-002-work-stealing-scheduler-architecture.md) - Thread architecture
- [benchmarks/message_pool_performance.cpp](../../../benchmarks/message_pool_performance.cpp) - Performance tests

## Acceptance Criteria

- ✅ All code compiles without warnings
- ✅ All unit tests pass (message pool tests)
- ✅ Benchmarks show 2x allocation throughput
- ✅ Memory bounded to MAX_POOL_SIZE per thread
- ✅ No memory leaks (valgrind clean)
- ✅ Thread-safe (TSan clean)
- ✅ Backward compatible (existing code works)

---

**Last Updated**: 2025-11-21
**Version**: 1.0
**Project**: ProjectKeystone HMAS (C++20)
