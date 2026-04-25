# Async Work-Stealing Architecture

**Phase A (Weeks 1-3)**: Foundation for scalable async agent processing

## Overview

ProjectKeystone now supports asynchronous message routing through a work-stealing scheduler, enabling parallel agent execution across multiple worker threads. This architecture maintains 100% backward compatibility with synchronous mode while providing a high-performance async execution model.

## Architecture Components

### 1. WorkStealingScheduler

**Location**: `include/concurrency/work_stealing_scheduler.hpp`

The scheduler manages a pool of worker threads, each with its own lock-free work queue. Workers execute a cooperative loop:

1. Try to pop work from own queue (LIFO - cache-friendly)
2. If empty, steal from other workers' queues (FIFO - load balancing)
3. Execute work items (functions or coroutines)
4. Repeat until shutdown

**Key Features**:

- **Lock-free queues**: Uses `moodycamel::ConcurrentQueue` for MPMC operations
- **Work-stealing**: Idle workers steal from busy workers for load balancing
- **Round-robin submission**: `submit()` distributes work evenly across workers
- **Targeted submission**: `submitTo(index)` allows affinity-based scheduling
- **Graceful shutdown**: Drains all pending work before terminating workers

**Usage**:

```cpp
// Create scheduler with 4 workers
WorkStealingScheduler scheduler(4);
scheduler.start();

// Submit function work
scheduler.submit([]() {
    // Execute on any available worker
});

// Submit coroutine work
Task<void> myTask = doAsyncWork();
scheduler.submit(myTask.get_handle());

// Submit to specific worker (affinity)
scheduler.submitTo(2, []() {
    // Execute on worker 2
});

// Graceful shutdown (waits for pending work)
scheduler.shutdown();
```

### 2. MessageBus Integration

**Location**: `include/core/message_bus.hpp`, `src/core/message_bus.cpp`

The MessageBus now supports two routing modes:

#### Synchronous Mode (Default)

When no scheduler is set, MessageBus routes messages synchronously:

```cpp
MessageBus bus;
// No scheduler - sync routing
bus.routeMessage(msg);  // Delivers immediately via agent->receiveMessage()
```

#### Asynchronous Mode

When a scheduler is set, MessageBus routes messages asynchronously:

```cpp
WorkStealingScheduler scheduler(4);
scheduler.start();

MessageBus bus;
bus.setScheduler(&scheduler);  // Enable async routing

// Messages now delivered via work-stealing
bus.routeMessage(msg);  // Submits to scheduler, returns immediately
```

**Implementation Details**:

```cpp
bool MessageBus::routeMessage(const KeystoneMessage& msg) {
    std::lock_guard<std::mutex> lock(registry_mutex_);

    auto it = agents_.find(msg.receiver_id);
    if (it == agents_.end()) {
        return false;
    }

    agents::BaseAgent* agent = it->second;

    // Async routing if scheduler present
    if (scheduler_) {
        scheduler_->submit([agent, msg]() {
            agent->receiveMessage(msg);
        });
        return true;
    }

    // Synchronous delivery (backward compatible)
    agent->receiveMessage(msg);
    return true;
}
```

### 3. PullOrSteal Awaitable

**Location**: `include/concurrency/pull_or_steal.hpp`

A C++20 coroutine awaitable that implements the work-stealing algorithm:

```cpp
Task<std::optional<WorkItem>> workerLoop(size_t worker_index) {
    while (!shutdown_) {
        // co_await suspends until work is available
        auto work = co_await PullOrSteal(
            my_queue,
            all_queues,
            worker_index,
            shutdown_flag
        );

        if (work) {
            work->execute();  // Run function or resume coroutine
        }
    }
}
```

**Algorithm**:

1. Try `my_queue.pop()` (LIFO - own work first)
2. If empty, iterate through other queues in round-robin order
3. Try `victim_queue.steal()` (FIFO - oldest work first)
4. Return first work item found, or `std::nullopt` if all queues empty

### 4. Logger with Thread-Local Context

**Location**: `include/concurrency/logger.hpp`

Thread-local logging context for distributed tracing:

```cpp
// Initialize logger (once, main thread)
Logger::init(spdlog::level::info);

// Worker thread sets context
void workerLoop(size_t worker_index) {
    LogContext::set("worker", worker_index);

    // All logs in this thread include worker_index
    Logger::info("Processing message");
    // Output: [worker=2] Processing message
}
```

## Message Flow

### Synchronous Flow (Phase 1-3)

```
ChiefArchitect → MessageBus → TaskAgent
                      ↓ (synchronous)
               agent->receiveMessage(msg)
                      ↓ (blocks until delivered)
               TaskAgent inbox
```

### Asynchronous Flow (Phase A)

```
ChiefArchitect → MessageBus → WorkStealingScheduler
                      ↓              ↓
               scheduler->submit()   Work queues (round-robin)
                      ↓              ↓
                   (returns)     Worker threads
                                    ↓
                            agent->receiveMessage(msg)
                                    ↓
                            TaskAgent inbox
```

**Key Differences**:

- **Synchronous**: Caller blocks until message delivered to agent's inbox
- **Asynchronous**: Caller returns immediately, delivery happens on worker thread
- **Ordering**: Async mode does NOT guarantee FIFO delivery (work-stealing reorders)
- **Correlation**: Use `msg.msg_id` to match responses (UUID-based)

## Performance Characteristics

### Work-Stealing Benefits

1. **Load Balancing**: Idle workers steal from busy workers automatically
2. **Cache Locality**: Workers prefer own queue (LIFO) for hot cache lines
3. **Scalability**: Lock-free queues enable parallel submission and stealing
4. **Fairness**: Round-robin submission and stealing prevents starvation

### Benchmarks (Phase A)

| Test | Configuration | Result |
|------|---------------|--------|
| High Load | 1000 messages, 4 workers | 100% success, <500ms |
| Load Balancing | 100 messages, 10 agents | All agents receive work |
| Parallel Execution | 20 tasks, 4 workers | ≥2 concurrent (verified) |
| Shutdown | 20 pending, shutdown | All work completed |

### Scalability

- **Tested**: Up to 4 workers, 10 agents, 1000 messages
- **Expected**: Scales linearly with worker count (lock-free design)
- **Bottleneck**: Agent inbox (per-agent mutex), not scheduler

## Usage Patterns

### Pattern 1: Enable Async for Existing System

```cpp
// Existing Phase 1-3 code (sync mode)
auto bus = std::make_unique<MessageBus>();
auto chief = std::make_unique<ChiefArchitectAgent>("chief");
auto task = std::make_unique<TaskAgent>("task");

bus->registerAgent(chief->getAgentId(), chief.get());
bus->registerAgent(task->getAgentId(), task.get());

chief->setMessageBus(bus.get());
task->setMessageBus(bus.get());

// Enable async mode (Phase A)
WorkStealingScheduler scheduler(4);
scheduler.start();
bus->setScheduler(&scheduler);  // ← Single line change

// All existing code works unchanged
auto msg = KeystoneMessage::create("chief", "task", "echo hello");
chief->sendMessage(msg);  // Now async!

// Cleanup
scheduler.shutdown();  // Wait for pending work
```

### Pattern 2: Runtime Mode Switching

```cpp
MessageBus bus;
WorkStealingScheduler scheduler(2);

// Start in sync mode
auto msg1 = KeystoneMessage::create("chief", "task", "sync command");
chief->sendMessage(msg1);  // Synchronous delivery

auto result1 = task->getMessage();  // Immediate

// Switch to async mode
scheduler.start();
bus.setScheduler(&scheduler);

auto msg2 = KeystoneMessage::create("chief", "task", "async command");
chief->sendMessage(msg2);  // Asynchronous delivery

std::this_thread::sleep_for(std::chrono::milliseconds(50));
auto result2 = task->getMessage();  // Delayed

// Switch back to sync mode
bus.setScheduler(nullptr);

auto msg3 = KeystoneMessage::create("chief", "task", "sync again");
chief->sendMessage(msg3);  // Synchronous again

scheduler.shutdown();
```

### Pattern 3: Affinity-Based Agent Scheduling

```cpp
// Assign specific agents to specific workers for cache locality
WorkStealingScheduler scheduler(4);
scheduler.start();

// Agent 0 → Worker 0
// Agent 1 → Worker 1
// Agent 2 → Worker 2
// Agent 3 → Worker 3

// Custom MessageBus subclass for affinity
class AffinityMessageBus : public MessageBus {
public:
    bool routeMessage(const KeystoneMessage& msg) override {
        auto agent = findAgent(msg.receiver_id);
        if (!agent) return false;

        size_t worker_index = hashAgentId(msg.receiver_id) % num_workers_;
        scheduler_->submitTo(worker_index, [agent, msg]() {
            agent->receiveMessage(msg);
        });
        return true;
    }
};
```

## Migration Guide

### From Sync to Async

**Step 1**: Ensure all tests pass in sync mode

```bash
cmake --build build && ctest --test-dir build
```

**Step 2**: Add scheduler to MessageBus

```cpp
WorkStealingScheduler scheduler(std::thread::hardware_concurrency());
scheduler.start();
bus->setScheduler(&scheduler);
```

**Step 3**: Adjust timing-sensitive code

```cpp
// BEFORE (sync mode)
agent->sendMessage(msg);
auto response = sender->getMessage();  // Immediate

// AFTER (async mode)
agent->sendMessage(msg);
std::this_thread::sleep_for(std::chrono::milliseconds(50));  // Allow delivery
auto response = sender->getMessage();
```

**Step 4**: Use msg_id for correlation

```cpp
// BEFORE (ordered, sync)
std::vector<int> expected_results = {42, 17, 99};
for (size_t i = 0; i < 3; ++i) {
    auto response = chief->getMessage();
    EXPECT_EQ(parseResult(response), expected_results[i]);  // ✗ May fail in async
}

// AFTER (unordered, async)
std::map<std::string, int> expected;  // msg_id → result
for (auto msg_id : sent_message_ids) {
    expected[msg_id] = compute_expected(msg_id);
}

for (size_t i = 0; i < 3; ++i) {
    auto response = chief->getMessage();
    EXPECT_EQ(parseResult(response), expected[response->msg_id]);  // ✓ Correct
}
```

### Backward Compatibility Guarantees

1. **Default behavior unchanged**: No scheduler = sync mode
2. **All existing tests pass**: 106 Phase 1-3 tests pass without modification
3. **Opt-in async**: Requires explicit `setScheduler()` call
4. **Runtime switching**: Can enable/disable async at any time
5. **Same API**: No changes to agent or message interfaces

## Testing

### Test Coverage

**Unit Tests** (`tests/unit/test_message_bus_async.cpp`):

- Synchronous routing without scheduler
- Asynchronous routing with scheduler
- High load (1000 messages)
- Runtime mode switching
- Graceful shutdown with pending work
- Scheduler lifecycle

**E2E Tests** (`tests/e2e/phase_a_async_delegation.cpp`):

- Full async workflow: Chief → 3 TaskAgents via work-stealing
- Load balancing: 100 messages across 10 agents
- Mixed mode: Runtime switching between sync and async

**Total**: 115 tests, 100% passing

### Running Tests

```bash
# All tests
cmake --build build && ctest --test-dir build

# Phase A tests only
./build/phase_a_e2e_tests
./build/unit_tests --gtest_filter="MessageBusAsync*"

# With verbose output
./build/phase_a_e2e_tests --gtest_color=yes

# Specific test
./build/concurrency_unit_tests --gtest_filter="WorkStealingSchedulerTest.HeavyLoad"
```

## Implementation Timeline

### Week 1: Core Concurrency Components

- ✅ Task<T> coroutine type
- ✅ ThreadPool with work queues
- ✅ WorkStealingQueue (lock-free)

### Week 2: Work-Stealing Infrastructure

- ✅ Enhanced KeystoneMessage with action types
- ✅ Cista serialization with cista::offset::hash_map
- ✅ Logger with thread-local LogContext
- ✅ PullOrSteal awaitable
- ✅ WorkStealingScheduler

### Week 3: MessageBus Integration

- ✅ MessageBus async routing support
- ✅ Backward-compatible dual-mode routing
- ✅ Async MessageBus tests (6 tests)
- ✅ Phase A E2E tests (3 tests)
- ✅ 100% backward compatibility (106 existing tests pass)

## Future Work (Phase B+)

### Phase B: Agent Coroutine Migration

- Migrate agent `processMessage()` to coroutines
- Enable `co_await` for async operations within agents
- Agent-level work-stealing (agents as work items)

### Phase C: Advanced Features

- Message priority queues
- Deadline scheduling
- Agent affinity policies
- Distributed work-stealing (multi-node)

### Phase D: Performance Optimization

- NUMA-aware scheduling
- Lock-free agent inboxes
- Zero-copy message passing
- Custom allocators for work items

## References

- [TDD_FOUR_LAYER_ROADMAP.md](TDD_FOUR_LAYER_ROADMAP.md) - Original development plan
- [ADR-001-message-bus-architecture.md](adr/ADR-001-message-bus-architecture.md) - MessageBus design
- [testing-strategy.md](testing-strategy.md) - Test organization

## Glossary

- **Work-stealing**: Algorithm where idle workers steal work from busy workers
- **LIFO**: Last-In-First-Out (stack), used for own queue (cache-friendly)
- **FIFO**: First-In-First-Out (queue), used for stealing (fairness)
- **Lock-free**: Data structure that doesn't use mutexes (scalable)
- **Coroutine**: Function that can suspend and resume execution
- **Awaitable**: C++20 object that can be used with `co_await`
- **msg_id**: UUID-based message identifier for async correlation

---

**Phase A Status**: ✅ Complete (100% tests passing)
**Last Updated**: 2025-11-18
**Version**: 1.0 (Phase A final)
