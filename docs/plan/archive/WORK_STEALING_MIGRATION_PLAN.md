# ProjectKeystone: Work-Stealing Architecture Migration Plan

## Executive Summary

**Migration Goal**: Transform ProjectKeystone from synchronous MessageBus to high-performance Work-Stealing concurrent architecture while maintaining the 4-layer hierarchy and all existing test coverage.

**Key Architectural Decisions** (Confirmed):

- ✅ **Queue**: `concurrentqueue` library (lock-free, header-only)
- ✅ **Coroutines**: Custom `Task<T>` using C++20 coroutines
- ✅ **Serialization**: Cista (zero-copy, header-only)
- ✅ **Modules**: C++20 Modules structure
- ✅ **External Model Communication**: Process execution + filesystem (NO gRPC, NO ONNX Runtime)
- ✅ **Logging**: `spdlog` (structured, async, thread-safe, header-only)
- ✅ **Checkpoint/Resume**: Full hierarchy state serialization for fault tolerance
- ✅ **NO Facebook libraries** (no folly)
- ✅ **NO GPU dependencies** (no CUDA/cuDNN)

---

## 1. Current State Analysis

### Current Architecture (Phase 1-3 Complete)

```
┌─────────────────────────────────────────────────────┐
│          Synchronous MessageBus (mutex)             │
│  - Central routing hub                              │
│  - Mutex-protected agent registry                   │
│  - Synchronous routeMessage()                       │
└─────────────────────────────────────────────────────┘
                         │
        ┌────────────────┼────────────────┐
        ▼                ▼                ▼
┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│ ChiefArchitect│ │ComponentLead │ │ ModuleLead   │
│   (L0)       │ │   (L1)       │ │   (L2)       │
└──────────────┘ └──────────────┘ └──────────────┘
        │                │                │
        └────────────────┼────────────────┘
                         ▼
                  ┌──────────────┐
                  │  TaskAgent   │
                  │   (L3)       │
                  └──────────────┘
                         │
                         ▼
                  popen() + pclose()
                  (bash execution)
```

**Execution Model**: Single-threaded, synchronous message processing

**Agent Inbox**: `std::queue<KeystoneMessage>` + `std::mutex`

**Message Format**: Simple struct (no serialization)

```cpp
struct KeystoneMessage {
    std::string msg_id;
    std::string sender_id;
    std::string receiver_id;
    std::string command;
    std::optional<std::string> payload;
    std::chrono::system_clock::time_point timestamp;
};
```

**Test Coverage**: 17/17 tests passing

- Phase 1 E2E: 3 tests
- Phase 2 E2E: 2 tests
- Phase 3 E2E: 1 test
- Unit tests: 11 tests

---

## 2. Target Architecture

### Work-Stealing Concurrent Design

```
┌─────────────────────────────────────────────────────────────┐
│              Work-Stealing Scheduler                        │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐         │
│  │ Worker      │  │ Worker      │  │ Worker      │         │
│  │ Thread 1    │  │ Thread 2    │  │ Thread N    │         │
│  │             │  │             │  │             │         │
│  │ [Local Q]───┼──┼──> STEAL    │  │             │         │
│  │             │  │  [Local Q]  │  │  [Local Q]  │         │
│  └─────────────┘  └─────────────┘  └─────────────┘         │
│         │                 │                 │               │
│         └─────────────────┴─────────────────┘               │
│                           │                                 │
│                  concurrentqueue                            │
│                  (lock-free MPMC)                           │
└─────────────────────────────────────────────────────────────┘
                            │
        ┌───────────────────┼───────────────────┐
        ▼                   ▼                   ▼
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ Coroutine    │    │ Coroutine    │    │ Coroutine    │
│ ChiefArchitect│   │ComponentLead │    │ ModuleLead   │
│              │    │              │    │              │
│ co_await     │    │ co_await     │    │ co_await     │
│ pullOrSteal()│    │ pullOrSteal()│    │ pullOrSteal()│
└──────────────┘    └──────────────┘    └──────────────┘
        │                   │                   │
        └───────────────────┼───────────────────┘
                            ▼
                    ┌──────────────┐
                    │  TaskAgent   │
                    │  (Coroutine) │
                    │              │
                    │ co_await     │
                    │ executeProc()│
                    └──────────────┘
                            │
                            ▼
                    Process Execution
                    (popen + filesystem I/O)
```

**Key Components**:

1. **Thread Pool**: Fixed-size worker threads (`std::thread::hardware_concurrency()`)
2. **Work-Stealing Queues**: Thread-local `concurrentqueue` instances
3. **Coroutine Execution**: All agents run as `Task<void> run()` coroutines
4. **Message Serialization**: Cista for zero-copy internal messages
5. **C++20 Modules**: Modular code organization

---

## 3. Component-by-Component Migration Plan

### 3.1. Core Infrastructure (NEW)

#### 3.1.1. Custom Task<T> Coroutine Type

**Purpose**: Awaitable coroutine return type for async agent execution

**File**: `include/concurrency/task.hpp` (Module: `Keystone.Concurrency`)

**Interface**:

```cpp
export module Keystone.Concurrency:Task;

import <coroutine>;
import <exception>;
import <utility>;

namespace keystone::concurrency {

template<typename T>
class Task {
public:
    struct promise_type {
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void unhandled_exception() { exception_ = std::current_exception(); }

        void return_value(T value) { value_ = std::move(value); }

        T value_;
        std::exception_ptr exception_;
    };

    Task(std::coroutine_handle<promise_type> handle) : handle_(handle) {}

    ~Task() {
        if (handle_) handle_.destroy();
    }

    // Non-copyable, movable
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    T get() {
        if (!handle_ || !handle_.done()) {
            throw std::runtime_error("Task not completed");
        }
        if (handle_.promise().exception_) {
            std::rethrow_exception(handle_.promise().exception_);
        }
        return std::move(handle_.promise().value_);
    }

    bool done() const { return handle_ && handle_.done(); }
    void resume() { if (handle_ && !handle_.done()) handle_.resume(); }

private:
    std::coroutine_handle<promise_type> handle_;
};

// Specialization for void
template<>
class Task<void> {
    // Similar implementation without return_value/value_
};

} // namespace keystone::concurrency
```

**Testing**:

- Unit test: Create Task, suspend, resume, get result
- Unit test: Exception propagation
- Unit test: Move semantics

---

#### 3.1.2. Thread Pool with Coroutine Support

**Purpose**: Fixed-size worker thread pool that executes coroutine continuations

**File**: `include/concurrency/thread_pool.hpp` (Module: `Keystone.Concurrency`)

**Interface**:

```cpp
export module Keystone.Concurrency:ThreadPool;

import <thread>;
import <vector>;
import <atomic>;
import <functional>;
import <coroutine>;

namespace keystone::concurrency {

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency());
    ~ThreadPool();

    // Non-copyable, non-movable
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Submit a coroutine handle for execution
    void submit(std::coroutine_handle<> handle);

    // Submit a function
    void submit(std::function<void()> func);

    // Graceful shutdown
    void shutdown();

    // Check if shutdown requested
    bool isShuttingDown() const { return shutdown_requested_.load(); }

    size_t numThreads() const { return workers_.size(); }

private:
    void workerLoop(size_t thread_id);

    std::vector<std::thread> workers_;
    std::atomic<bool> shutdown_requested_{false};

    // Each thread has its own work queue (defined in work_stealing_queue.hpp)
    std::vector<std::unique_ptr<WorkStealingQueue>> thread_queues_;

    thread_local static size_t thread_local_id_;
};

} // namespace keystone::concurrency
```

**Key Features**:

- One worker thread per CPU core
- Each thread has a thread-local queue
- Worker loop: `while (!shutdown_requested_) { pullOrSteal(); execute(); }`

**Testing**:

- Unit test: ThreadPool creation, num threads matches `hardware_concurrency()`
- Unit test: Submit function, verify execution
- Unit test: Submit coroutine handle, verify resumption
- Unit test: Graceful shutdown, all threads joined

---

#### 3.1.3. Work-Stealing Queue

**Purpose**: Thread-local lock-free queue with work-stealing support

**File**: `include/concurrency/work_stealing_queue.hpp` (Module: `Keystone.Concurrency`)

**Interface**:

```cpp
export module Keystone.Concurrency:WorkStealingQueue;

import <concurrentqueue.h>;  // Third-party header-only library
import <optional>;
import <coroutine>;
import <variant>;

namespace keystone::concurrency {

// Work item can be either a coroutine handle or a function
using WorkItem = std::variant<std::coroutine_handle<>, std::function<void()>>;

class WorkStealingQueue {
public:
    WorkStealingQueue() = default;

    // Push work to local queue (owner thread only)
    void push(WorkItem item);

    // Pop work from local queue (owner thread only)
    std::optional<WorkItem> pop();

    // Steal work from this queue (any thread)
    std::optional<WorkItem> steal();

    // Check if queue is empty
    bool empty() const;

    // Get approximate queue size (for metrics)
    size_t size() const;

private:
    moodycamel::ConcurrentQueue<WorkItem> queue_;
};

} // namespace keystone::concurrency
```

**Implementation Notes**:

- `concurrentqueue` is MPMC (multi-producer, multi-consumer) - perfect for work-stealing
- `push()` and `pop()` are typically called by the owning thread
- `steal()` is called by other threads when their local queue is empty

**Testing**:

- Unit test: Push/pop from same thread
- Unit test: Steal from different thread
- Unit test: Concurrent push/steal (ThreadSanitizer)
- Unit test: Queue empty check

---

#### 3.1.4. PullOrSteal Awaitable

**Purpose**: Custom awaitable for coroutine-based message retrieval with work-stealing

**File**: `include/concurrency/pull_or_steal.hpp` (Module: `Keystone.Concurrency`)

**Interface**:

```cpp
export module Keystone.Concurrency:PullOrSteal;

import Keystone.Core:Message;
import Keystone.Concurrency:WorkStealingQueue;
import <coroutine>;
import <optional>;

namespace keystone::concurrency {

class PullOrSteal {
public:
    PullOrSteal(WorkStealingScheduler* scheduler);

    // Awaitable interface
    bool await_ready();
    void await_suspend(std::coroutine_handle<> handle);
    KeystoneMessage await_resume();

private:
    WorkStealingScheduler* scheduler_;
    std::optional<KeystoneMessage> cached_message_;
};

} // namespace keystone::concurrency
```

**Behavior**:

1. **await_ready()**: Try non-blocking pop from thread-local queue
   - If message available: return `true` (no suspension)
   - If queue empty: return `false` (will suspend)

2. **await_suspend(handle)**: Attempt work-stealing
   - Try to steal from other threads' queues (random victim selection)
   - If stolen work is a message: cache it, return `false` (resume immediately)
   - If stolen work is a coroutine: submit to thread pool, try again
   - If all queues empty: suspend coroutine, store handle for later resumption

3. **await_resume()**: Return the message
   - Return cached message from `await_ready()` or `await_suspend()`

**Testing**:

- Unit test: Local queue has message → no suspension
- Unit test: Local queue empty, steal succeeds → no suspension
- Unit test: All queues empty → suspend, resume when message arrives
- Integration test: Multiple coroutines stealing from each other

---

#### 3.1.5. Work-Stealing Scheduler

**Purpose**: Orchestrates thread pool, queues, and coroutine execution

**File**: `include/concurrency/work_stealing_scheduler.hpp` (Module: `Keystone.Concurrency`)

**Interface**:

```cpp
export module Keystone.Concurrency:WorkStealingScheduler;

import Keystone.Concurrency:ThreadPool;
import Keystone.Concurrency:WorkStealingQueue;
import Keystone.Core:Message;
import <memory>;
import <vector>;

namespace keystone::concurrency {

class WorkStealingScheduler {
public:
    explicit WorkStealingScheduler(size_t num_threads = std::thread::hardware_concurrency());
    ~WorkStealingScheduler();

    // Send a message (enqueues to appropriate thread's queue)
    void sendMessage(const KeystoneMessage& msg);

    // Register an agent's coroutine (starts execution)
    void registerAgent(std::coroutine_handle<> agent_coroutine);

    // Attempt to pull message from local queue
    std::optional<KeystoneMessage> tryPullLocal();

    // Attempt to steal message from other queues
    std::optional<KeystoneMessage> trySteal();

    // Get reference to thread-local queue (for metrics)
    WorkStealingQueue* getLocalQueue();

    // Graceful shutdown
    void shutdown();

private:
    std::unique_ptr<ThreadPool> thread_pool_;
    std::vector<std::unique_ptr<WorkStealingQueue>> queues_;

    // Map agent_id -> queue index (for routing)
    std::unordered_map<std::string, size_t> agent_to_queue_;
    std::mutex routing_mutex_;
};

} // namespace keystone::concurrency
```

**Key Responsibilities**:

- Manage thread pool lifecycle
- Route messages to appropriate thread-local queue
- Provide pull/steal interface for coroutines
- Coordinate graceful shutdown

**Testing**:

- Integration test: Scheduler + ThreadPool + WorkStealingQueue
- Integration test: Multiple agents pulling/stealing messages
- Integration test: Graceful shutdown with in-flight coroutines

---

### 3.2. Message Protocol Evolution

#### 3.2.1. Enhanced KeystoneMessage (KIM)

**Purpose**: Extend message format to support new architecture

**File**: `include/core/message.hpp` (Module: `Keystone.Core`)

**Updated Structure**:

```cpp
export module Keystone.Core:Message;

import <string>;
import <chrono>;
import <optional>;
import <map>;

namespace keystone::core {

enum class ActionType {
    DECOMPOSE,          // Request task decomposition
    EXECUTE,            // Execute a task
    RETURN_RESULT,      // Return execution result
    SHUTDOWN,           // Graceful shutdown signal
    HEARTBEAT           // Health check
};

enum class ContentType {
    TEXT_PLAIN,         // Simple string payload
    BINARY_CISTA        // Cista-serialized binary
};

struct KeystoneMessage {
    // Core fields (existing)
    std::string msg_id;
    std::string sender_id;
    std::string receiver_id;

    // Enhanced fields (NEW)
    ActionType action_type;
    ContentType content_type;
    std::string session_id;     // For context isolation

    // Payload (enhanced)
    std::optional<std::string> payload;  // Text or Cista-serialized binary

    // Metadata
    std::map<std::string, std::string> metadata;  // Priority, tags, etc.
    std::chrono::system_clock::time_point timestamp;

    // Factory methods
    static KeystoneMessage create(
        const std::string& sender,
        const std::string& receiver,
        ActionType action,
        const std::string& session_id = "",
        const std::optional<std::string>& data = std::nullopt
    );
};

} // namespace keystone::core
```

**Backward Compatibility**:

- Keep existing `create()` method, map to new fields
- Default `action_type = ActionType::EXECUTE`
- Default `content_type = ContentType::TEXT_PLAIN`
- Existing tests continue to work

**Testing**:

- Unit test: Create message with all fields
- Unit test: Backward compatibility (old `create()` signature)
- Unit test: Session ID propagation

---

#### 3.2.2. Cista Serialization Layer

**Purpose**: Zero-copy serialization for high-performance message passing

**File**: `include/core/message_serializer.hpp` (Module: `Keystone.Core`)

**Interface**:

```cpp
export module Keystone.Core:MessageSerializer;

import Keystone.Core:Message;
import <cista.h>;  // Third-party header-only library
import <vector>;
import <string>;

namespace keystone::core {

class MessageSerializer {
public:
    // Serialize message to binary buffer (zero-copy write)
    static std::vector<uint8_t> serialize(const KeystoneMessage& msg);

    // Deserialize message from binary buffer (zero-copy read)
    static KeystoneMessage deserialize(const std::vector<uint8_t>& buffer);

    // In-place deserialization (no copy, direct buffer access)
    static const KeystoneMessage* deserializeInPlace(const uint8_t* buffer, size_t size);
};

} // namespace keystone::core
```

**Cista Usage**:

```cpp
// Cista requires defining serializable structs
namespace cista_data = cista::offset;

struct CistaKeystoneMessage {
    cista_data::string msg_id;
    cista_data::string sender_id;
    cista_data::string receiver_id;
    uint8_t action_type;  // Enum as uint8_t
    uint8_t content_type;
    cista_data::string session_id;
    cista_data::optional<cista_data::string> payload;
    // ... metadata
    int64_t timestamp_ns;  // Chrono converted to nanoseconds
};

// Serialize
auto buffer = cista::serialize(cista_msg);

// Deserialize (zero-copy)
auto* msg = cista::deserialize<CistaKeystoneMessage>(buffer);
```

**Performance Target**:

- Serialization: < 0.1 ms
- Deserialization: < 0.002 ms (zero-copy benefit)

**Testing**:

- Unit test: Serialize/deserialize round-trip
- Unit test: Binary compatibility (serialize on one thread, deserialize on another)
- Performance test: Benchmark vs. direct struct passing
- Unit test: Ensure no memory leaks (ASan)

---

### 3.3. Agent Migration to Coroutines

#### 3.3.1. BaseAgent Coroutine Refactor

**Current BaseAgent**:

```cpp
class BaseAgent {
public:
    virtual Response processMessage(const KeystoneMessage& msg) = 0;
    void sendMessage(const KeystoneMessage& msg);  // Via MessageBus
    std::optional<KeystoneMessage> getMessage();   // From local queue

private:
    std::queue<KeystoneMessage> inbox_;
    std::mutex inbox_mutex_;
    MessageBus* message_bus_;
};
```

**Target BaseAgent (Coroutine-Based)**:

```cpp
export module Keystone.Agents:BaseAgent;

import Keystone.Core:Message;
import Keystone.Concurrency:Task;
import Keystone.Concurrency:WorkStealingScheduler;

namespace keystone::agents {

class BaseAgent {
public:
    explicit BaseAgent(const std::string& agent_id);
    virtual ~BaseAgent() = default;

    // Main coroutine execution loop (pure virtual)
    virtual Task<void> run() = 0;

    // Send message via scheduler
    void sendMessage(const KeystoneMessage& msg);

    // Async message retrieval (co_await this)
    Task<KeystoneMessage> pullOrSteal();

    // Agent ID
    const std::string& getAgentId() const { return agent_id_; }

    // Set scheduler (replaces setMessageBus)
    void setScheduler(WorkStealingScheduler* scheduler);

protected:
    std::string agent_id_;
    WorkStealingScheduler* scheduler_{nullptr};

    // State machine (preserved from current implementation)
    enum class State {
        IDLE,
        PLANNING,
        WAITING,
        EXECUTING,
        SHUTTING_DOWN
    };
    State state_{State::IDLE};
};

} // namespace keystone::agents
```

**Key Changes**:

1. **Removed**: Local `inbox_` queue (now managed by WorkStealingScheduler)
2. **Removed**: `processMessage()` synchronous method
3. **Added**: `run()` coroutine (agent's main execution loop)
4. **Added**: `pullOrSteal()` async message retrieval
5. **Replaced**: `MessageBus*` → `WorkStealingScheduler*`

**Migration Strategy**:

- Keep `processMessage()` logic, move into `run()` loop
- Replace `getMessage()` calls with `co_await pullOrSteal()`

**Testing**:

- Unit test: BaseAgent creation, scheduler assignment
- Unit test: sendMessage enqueues to scheduler
- Integration test: Agent coroutine pull/steal

---

#### 3.3.2. TaskAgent (Leaf) Coroutine Implementation

**Current TaskAgent** (synchronous):

```cpp
Response TaskAgent::processMessage(const KeystoneMessage& msg) {
    if (msg.command.starts_with("echo ")) {
        // Execute bash command
        auto result = executeCommand(msg.command);
        return Response::createSuccess(msg, agent_id_, result);
    }
    // ...
}
```

**Target TaskAgent** (coroutine):

```cpp
export module Keystone.Agents.Leaf:TaskAgent;

import Keystone.Agents:BaseAgent;
import Keystone.Concurrency:Task;

namespace keystone::agents {

class TaskAgent : public BaseAgent {
public:
    explicit TaskAgent(const std::string& agent_id);

    // Main coroutine execution loop
    Task<void> run() override;

private:
    // Async process execution (co_await this)
    Task<std::string> executeProcess(const std::string& command);

    // RAII helper for popen/pclose (preserved from current)
    struct PipeDeleter {
        void operator()(FILE* pipe) const {
            if (pipe) pclose(pipe);
        }
    };
    using PipeHandle = std::unique_ptr<FILE, PipeDeleter>;
};

// Implementation
Task<void> TaskAgent::run() {
    while (true) {
        // Pull or steal message
        auto msg = co_await pullOrSteal();

        // Check for shutdown
        if (msg.action_type == ActionType::SHUTDOWN) {
            state_ = State::SHUTTING_DOWN;
            co_return;
        }

        // Process message
        state_ = State::EXECUTING;
        auto result = co_await executeProcess(msg.payload.value_or(""));

        // Send result back
        auto response = KeystoneMessage::create(
            agent_id_,
            msg.sender_id,
            ActionType::RETURN_RESULT,
            msg.session_id,
            result
        );
        sendMessage(response);

        state_ = State::IDLE;
    }
}

Task<std::string> TaskAgent::executeProcess(const std::string& command) {
    // For now, keep synchronous popen (can optimize later with async I/O)
    PipeHandle pipe(popen(command.c_str(), "r"));
    if (!pipe) {
        co_return "ERROR: Failed to execute command";
    }

    std::string result;
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe.get())) {
        result += buffer;
    }

    co_return result;
}

} // namespace keystone::agents
```

**Key Changes**:

- `run()` is infinite loop: pull message → process → send result
- `executeProcess()` is `Task<std::string>` (can be made truly async later with `io_uring`)
- Shutdown handled via `ActionType::SHUTDOWN` message

**Future Optimization** (Phase 2+):

- Replace `popen` with async process execution using `io_uring` or similar
- This would make `executeProcess()` truly non-blocking

**Testing**:

- E2E test: TaskAgent receives message, executes, returns result
- E2E test: TaskAgent handles shutdown gracefully
- Unit test: executeProcess with various commands

---

#### 3.3.3. ModuleLead (Branch) Coroutine Implementation

**Current ModuleLead** (synchronous):

```cpp
Response ModuleLeadAgent::processMessage(const KeystoneMessage& msg) {
    if (state_ == State::IDLE) {
        // Decompose tasks
        auto tasks = decomposeTasks(msg.command);
        // Delegate to TaskAgents
        delegateTasks(tasks);
        state_ = State::WAITING_FOR_TASKS;
    }
    // Wait for results...
    // Synthesize...
}
```

**Target ModuleLead** (coroutine with std::barrier):

```cpp
export module Keystone.Agents.Branch:ModuleLeadAgent;

import Keystone.Agents:BaseAgent;
import Keystone.Concurrency:Task;
import <barrier>;
import <vector>;

namespace keystone::agents {

class ModuleLeadAgent : public BaseAgent {
public:
    explicit ModuleLeadAgent(const std::string& agent_id);

    Task<void> run() override;

    void setAvailableTaskAgents(const std::vector<std::string>& task_agent_ids);

private:
    std::vector<std::string> decomposeTasks(const std::string& module_goal);
    void delegateTasks(const std::vector<std::string>& tasks, const std::string& session_id);
    std::string synthesizeResults(const std::vector<std::string>& results);

    std::vector<std::string> available_task_agents_;
    std::vector<std::string> task_results_;

    // Barrier for coordinating task completion
    std::unique_ptr<std::barrier<>> task_completion_barrier_;
};

// Implementation
Task<void> ModuleLeadAgent::run() {
    while (true) {
        // Pull module-level command
        auto msg = co_await pullOrSteal();

        if (msg.action_type == ActionType::SHUTDOWN) {
            // Forward shutdown to all TaskAgents
            for (const auto& task_id : available_task_agents_) {
                auto shutdown_msg = KeystoneMessage::create(
                    agent_id_, task_id, ActionType::SHUTDOWN, msg.session_id
                );
                sendMessage(shutdown_msg);
            }
            co_return;
        }

        // PLANNING: Decompose into tasks
        state_ = State::PLANNING;
        auto tasks = decomposeTasks(msg.payload.value_or(""));

        // Create barrier for this round (num tasks + 1 for self)
        task_completion_barrier_ = std::make_unique<std::barrier<>>(tasks.size() + 1);

        // Delegate to TaskAgents
        delegateTasks(tasks, msg.session_id);
        state_ = State::WAITING;

        // Wait for all task results
        task_results_.clear();
        task_results_.resize(tasks.size());

        // Collect results
        for (size_t i = 0; i < tasks.size(); ++i) {
            auto result_msg = co_await pullOrSteal();
            // Store result
            task_results_[i] = result_msg.payload.value_or("");
        }

        // Barrier sync (wait for all tasks to complete)
        task_completion_barrier_->arrive_and_wait();

        // SYNTHESIZING: Aggregate results
        state_ = State::SYNTHESIZING;
        auto synthesized = synthesizeResults(task_results_);

        // Send result back to parent
        auto response = KeystoneMessage::create(
            agent_id_,
            msg.sender_id,
            ActionType::RETURN_RESULT,
            msg.session_id,
            synthesized
        );
        sendMessage(response);

        state_ = State::IDLE;
    }
}

} // namespace keystone::agents
```

**Key Changes**:

- Use `std::barrier` for phase synchronization (PLANNING → WAITING → SYNTHESIZING)
- Multiple `co_await pullOrSteal()` calls to collect all task results
- Barrier ensures all tasks complete before synthesis

**Testing**:

- E2E test: ModuleLead coordinates 3 TaskAgents with barrier
- E2E test: Shutdown propagation to subordinates
- Integration test: Barrier synchronization

---

#### 3.3.4. ComponentLead (Branch) Coroutine Implementation

**Similar to ModuleLead**, but coordinates ModuleLeadAgents instead of TaskAgents:

```cpp
Task<void> ComponentLeadAgent::run() {
    while (true) {
        auto msg = co_await pullOrSteal();

        // Decompose into modules
        auto modules = decomposeModules(msg.payload.value_or(""));

        // Create barrier
        module_completion_barrier_ = std::make_unique<std::barrier<>>(modules.size() + 1);

        // Delegate to ModuleLeads
        delegateModules(modules, msg.session_id);

        // Collect module results
        std::vector<std::string> module_results;
        for (size_t i = 0; i < modules.size(); ++i) {
            auto result_msg = co_await pullOrSteal();
            module_results.push_back(result_msg.payload.value_or(""));
        }

        // Barrier sync
        module_completion_barrier_->arrive_and_wait();

        // Aggregate module results
        auto aggregated = aggregateModules(module_results);

        // Send to parent
        auto response = KeystoneMessage::create(
            agent_id_, msg.sender_id, ActionType::RETURN_RESULT,
            msg.session_id, aggregated
        );
        sendMessage(response);
    }
}
```

**Testing**:

- E2E test: ComponentLead coordinates 2 ModuleLeads, each with 3 TaskAgents

---

#### 3.3.5. ChiefArchitect (Root) Coroutine Implementation

**Simplest coordinator**, just delegates to ComponentLead or ModuleLead:

```cpp
Task<void> ChiefArchitectAgent::run() {
    while (true) {
        // Pull external user command
        auto msg = co_await pullOrSteal();

        if (msg.action_type == ActionType::SHUTDOWN) {
            // Forward to all subordinates
            co_return;
        }

        // Delegate to appropriate component
        auto component_msg = KeystoneMessage::create(
            agent_id_,
            delegate_id_,
            ActionType::EXECUTE,
            msg.session_id,
            msg.payload
        );
        sendMessage(component_msg);

        // Wait for result
        auto result_msg = co_await pullOrSteal();

        // Return to external caller (how? need interface)
        // For now, store result for testing
    }
}
```

**Testing**:

- E2E test: Full 4-layer hierarchy with coroutines

---

### 3.4. C++20 Modules Structure

**Module Hierarchy**:

```
Keystone.Core
├── Message          (message.hpp, message_serializer.hpp)
└── (no dependencies)

Keystone.Concurrency
├── Task             (task.hpp)
├── ThreadPool       (thread_pool.hpp)
├── WorkStealingQueue (work_stealing_queue.hpp)
├── PullOrSteal      (pull_or_steal.hpp)
└── WorkStealingScheduler (work_stealing_scheduler.hpp)
    └── depends on: Keystone.Core

Keystone.Observability
├── Logger           (logger.hpp)
├── LogContext       (log_context.hpp)
├── Checkpoint       (checkpoint.hpp)
├── CheckpointResume (checkpoint_resume.hpp)
└── SafePointDetector (safe_point_detector.hpp)
    └── depends on: Keystone.Core, Keystone.Concurrency

Keystone.Agents
├── BaseAgent        (base_agent.hpp)
│   └── depends on: Keystone.Core, Keystone.Concurrency, Keystone.Observability
├── Root
│   └── ChiefArchitectAgent
├── Branch
│   ├── ComponentLeadAgent
│   └── ModuleLeadAgent
└── Leaf
    └── TaskAgent
```

**CMakeLists.txt Updates**:

```cmake
# Enable C++20 Modules
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_DYNDEP 1)

# Fetch concurrentqueue
FetchContent_Declare(
    concurrentqueue
    GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
    GIT_TAG v1.0.4
)
FetchContent_MakeAvailable(concurrentqueue)

# Fetch Cista
FetchContent_Declare(
    cista
    GIT_REPOSITORY https://github.com/felixguendling/cista.git
    GIT_TAG v0.14
)
FetchContent_MakeAvailable(cista)

# Fetch spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.12.0
)
FetchContent_MakeAvailable(spdlog)

# Core module
add_library(keystone_core)
target_sources(keystone_core
    PUBLIC FILE_SET CXX_MODULES FILES
        src/core/message.cpp
        src/core/message_serializer.cpp
)
target_link_libraries(keystone_core PUBLIC cista::cista)

# Concurrency module
add_library(keystone_concurrency)
target_sources(keystone_concurrency
    PUBLIC FILE_SET CXX_MODULES FILES
        src/concurrency/task.cpp
        src/concurrency/thread_pool.cpp
        src/concurrency/work_stealing_queue.cpp
        src/concurrency/pull_or_steal.cpp
        src/concurrency/work_stealing_scheduler.cpp
)
target_link_libraries(keystone_concurrency
    PUBLIC keystone_core
    PUBLIC concurrentqueue
)

# Observability module
add_library(keystone_observability)
target_sources(keystone_observability
    PUBLIC FILE_SET CXX_MODULES FILES
        src/observability/logger.cpp
        src/observability/log_context.cpp
        src/observability/checkpoint.cpp
        src/observability/checkpoint_resume.cpp
        src/observability/safe_point_detector.cpp
)
target_link_libraries(keystone_observability
    PUBLIC keystone_core
    PUBLIC keystone_concurrency
    PUBLIC spdlog::spdlog
    PUBLIC cista::cista
)

# Agents module
add_library(keystone_agents)
target_sources(keystone_agents
    PUBLIC FILE_SET CXX_MODULES FILES
        src/agents/base_agent.cpp
        src/agents/chief_architect_agent.cpp
        src/agents/component_lead_agent.cpp
        src/agents/module_lead_agent.cpp
        src/agents/task_agent.cpp
)
target_link_libraries(keystone_agents
    PUBLIC keystone_core
    PUBLIC keystone_concurrency
    PUBLIC keystone_observability
)
```

**Module Interface Examples**:

`src/core/message.cpp`:

```cpp
export module Keystone.Core:Message;

export import <string>;
export import <chrono>;
export import <optional>;

export namespace keystone::core {
    enum class ActionType { /* ... */ };
    struct KeystoneMessage { /* ... */ };
}
```

---

### 3.5. Graceful Shutdown

**Signal Handler Thread**:

```cpp
export module Keystone.Concurrency:SignalHandler;

import <csignal>;
import <thread>;
import <atomic>;

namespace keystone::concurrency {

class SignalHandler {
public:
    static void initialize() {
        // Mask SIGTERM/SIGINT in all threads
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGTERM);
        sigaddset(&mask, SIGINT);
        pthread_sigmask(SIG_BLOCK, &mask, nullptr);

        // Start dedicated handler thread
        handler_thread_ = std::thread([]() {
            sigset_t wait_mask;
            sigemptyset(&wait_mask);
            sigaddset(&wait_mask, SIGTERM);
            sigaddset(&wait_mask, SIGINT);

            int sig;
            sigwait(&wait_mask, &sig);  // Blocks until signal received

            // Set shutdown flag
            shutdown_requested_.store(true);
        });
    }

    static bool isShutdownRequested() {
        return shutdown_requested_.load();
    }

    static void join() {
        if (handler_thread_.joinable()) {
            handler_thread_.join();
        }
    }

private:
    static std::atomic<bool> shutdown_requested_;
    static std::thread handler_thread_;
};

} // namespace keystone::concurrency
```

**Shutdown Protocol**:

1. Signal received → `shutdown_requested_ = true`
2. ThreadPool stops accepting new work
3. WorkStealingScheduler sends `SHUTDOWN` messages to all agents
4. Agents drain their work, send shutdown to subordinates
5. Leaf agents finish in-flight process executions
6. All coroutines reach `co_return`
7. ThreadPool joins all worker threads

**Testing**:

- E2E test: Send SIGTERM during execution, verify graceful shutdown
- E2E test: In-flight tasks complete before shutdown

---

### 3.6. Structured Logging Infrastructure

**Purpose**: Comprehensive, thread-safe, async logging for debugging, monitoring, and production operations

**Library Choice**: **spdlog** (header-only, fast, async-capable, widely adopted)

**File**: `include/observability/logger.hpp` (Module: `Keystone.Observability`)

#### 3.6.1. Logger Interface

```cpp
export module Keystone.Observability:Logger;

import <spdlog/spdlog.h>;
import <spdlog/async.h>;
import <spdlog/sinks/rotating_file_sink.h>;
import <spdlog/sinks/stdout_color_sinks.h>;
import <string>;
import <memory>;
import <map>;

namespace keystone::observability {

enum class LogLevel {
    TRACE,    // Fine-grained debug (e.g., "Attempting to steal from queue 3")
    DEBUG,    // Debug info (e.g., "Agent state transition: IDLE -> PLANNING")
    INFO,     // Important milestones (e.g., "ModuleLead delegated 3 tasks")
    WARN,     // Warnings (e.g., "Queue depth exceeds 80% capacity")
    ERROR,    // Errors (e.g., "Process execution failed")
    CRITICAL  // Critical failures (e.g., "Work-stealing deadlock detected")
};

class Logger {
public:
    // Initialize logging system (call once at startup)
    static void initialize(
        const std::string& log_file_path = "keystone.log",
        LogLevel level = LogLevel::INFO,
        bool async = true  // Async logging for performance
    );

    // Get logger instance
    static std::shared_ptr<spdlog::logger> get();

    // Structured logging with context
    static void log(
        LogLevel level,
        const std::string& message,
        const std::map<std::string, std::string>& context = {}
    );

    // Convenience methods with automatic context propagation
    static void trace(const std::string& msg, const std::map<std::string, std::string>& ctx = {});
    static void debug(const std::string& msg, const std::map<std::string, std::string>& ctx = {});
    static void info(const std::string& msg, const std::map<std::string, std::string>& ctx = {});
    static void warn(const std::string& msg, const std::map<std::string, std::string>& ctx = {});
    static void error(const std::string& msg, const std::map<std::string, std::string>& ctx = {});
    static void critical(const std::string& msg, const std::map<std::string, std::string>& ctx = {});

    // Shutdown (flush all async logs)
    static void shutdown();

private:
    static std::shared_ptr<spdlog::logger> logger_;
};

} // namespace keystone::observability
```

#### 3.6.2. Contextual Logging

**Context Propagation**: Automatically include critical metadata in all logs

```cpp
export module Keystone.Observability:LogContext;

import <string>;
import <map>;

namespace keystone::observability {

// Thread-local log context (automatically included in all logs)
class LogContext {
public:
    // Set context for current thread
    static void set(const std::string& key, const std::string& value);
    static void setSessionId(const std::string& session_id);
    static void setAgentId(const std::string& agent_id);
    static void setMessageId(const std::string& msg_id);
    static void setThreadId(size_t thread_id);

    // Get current context
    static std::map<std::string, std::string> get();

    // Clear context (e.g., at end of task)
    static void clear();

private:
    thread_local static std::map<std::string, std::string> context_;
};

} // namespace keystone::observability
```

**Usage Example**:

```cpp
// In TaskAgent::run()
Task<void> TaskAgent::run() {
    while (true) {
        auto msg = co_await pullOrSteal();

        // Set log context
        LogContext::setSessionId(msg.session_id);
        LogContext::setAgentId(agent_id_);
        LogContext::setMessageId(msg.msg_id);

        // All subsequent logs automatically include this context
        Logger::info("Received message", {
            {"action_type", actionTypeToString(msg.action_type)},
            {"sender", msg.sender_id}
        });
        // Log output:
        // [2025-01-15 10:23:45.123] [info] [session=abc123] [agent=task-1] [msg=msg-456]
        // Received message | action_type=EXECUTE | sender=module-lead-1

        state_ = State::EXECUTING;
        Logger::debug("State transition", {{"from", "IDLE"}, {"to", "EXECUTING"}});

        auto result = co_await executeProcess(msg.payload.value_or(""));

        if (result.starts_with("ERROR")) {
            Logger::error("Process execution failed", {{"command", msg.payload.value_or("")}});
        } else {
            Logger::info("Process completed successfully", {{"result_length", std::to_string(result.size())}});
        }

        // Clear context at end
        LogContext::clear();
    }
}
```

#### 3.6.3. Performance Considerations

**Async Logging**: Use spdlog's async mode to avoid blocking worker threads

- Logs are written to a lock-free queue
- Dedicated background thread flushes to disk
- Worker threads never block on I/O

**Log Levels**: Configurable at runtime

- Production: INFO and above
- Development: DEBUG and above
- Performance testing: WARN and above (minimal overhead)

**Structured Format**: JSON-compatible for log aggregation

```json
{
  "timestamp": "2025-01-15T10:23:45.123Z",
  "level": "INFO",
  "session_id": "abc123",
  "agent_id": "task-1",
  "msg_id": "msg-456",
  "thread_id": 3,
  "message": "Received message",
  "action_type": "EXECUTE",
  "sender": "module-lead-1"
}
```

#### 3.6.4. Key Logging Points

**Agent Lifecycle**:

- Agent creation/destruction
- State machine transitions (IDLE → PLANNING → EXECUTING → IDLE)
- Coroutine suspension/resumption

**Message Flow**:

- Message sent (from, to, action_type)
- Message received
- Message processing started/completed

**Work-Stealing**:

- Local queue pull success/failure
- Steal attempt (victim thread, success/failure)
- Queue depth changes

**Process Execution** (TaskAgent):

- Process start (command, PID)
- Process completion (exit code, duration)
- Process errors (stderr output)

**Coordination** (Branch Agents):

- Task decomposition (num tasks created)
- Barrier wait (num subordinates)
- Result synthesis (num results aggregated)

**Performance Metrics**:

- Message latency (time from send to receive)
- Agent processing time (time in EXECUTING state)
- Queue depth warnings (>80% capacity)

**Testing**:

- Unit test: Logger initialization, async mode
- Unit test: Log context propagation
- Integration test: Logs from multiple threads (no interleaving)
- Performance test: Logging overhead (<1% impact on throughput)

---

### 3.7. Checkpoint/Resume System

**Purpose**: Serialize entire hierarchy state to disk for fault tolerance, debugging, and resume after interruption

**Challenge**: C++20 coroutines do not have built-in serialization - we need a **logical checkpoint** approach

#### 3.7.1. Checkpoint Strategy

**Snapshot at Safe Points**: Only checkpoint when the entire hierarchy is in a consistent, quiescent state

**Safe Point Definition**:

1. All agents are in `IDLE` state (no in-flight work)
2. All message queues are empty (no pending messages)
3. No coroutines are suspended mid-execution
4. All external process executions have completed

**Checkpoint Trigger**:

- Manual trigger (external signal, e.g., `SIGUSR1`)
- Periodic checkpointing (e.g., every 5 minutes if idle)
- Pre-shutdown checkpoint (before graceful shutdown)

**Checkpoint Scope**:

- Agent metadata (agent_id, type, state)
- Hierarchy topology (parent-child relationships)
- Session states (active sessions, their progress)
- Execution history (completed tasks, results)
- Configuration (available task agents, routing tables)

**NOT Checkpointed** (ephemeral state):

- Coroutine suspension points (not serializable in C++20)
- Thread-local queues (transient, reconstructed on resume)
- In-flight messages (not allowed at safe points)

#### 3.7.2. Checkpoint Data Model

**File**: `include/observability/checkpoint.hpp` (Module: `Keystone.Observability`)

```cpp
export module Keystone.Observability:Checkpoint;

import Keystone.Core:Message;
import <cista.h>;
import <string>;
import <vector>;
import <map>;
import <chrono>;

namespace keystone::observability {

// Checkpoint format version (for compatibility)
constexpr uint32_t CHECKPOINT_VERSION = 1;

// Agent state snapshot
struct AgentSnapshot {
    cista::offset::string agent_id;
    cista::offset::string agent_type;  // "ChiefArchitect", "ModuleLead", etc.
    uint8_t state;  // State enum as uint8_t
    cista::offset::vector<cista::offset::string> subordinate_ids;  // Child agents
    cista::offset::map<cista::offset::string, cista::offset::string> metadata;  // Custom state
};

// Session state snapshot
struct SessionSnapshot {
    cista::offset::string session_id;
    cista::offset::string root_goal;  // Original user request
    int64_t start_time_ns;  // Timestamp
    cista::offset::vector<cista::offset::string> completed_tasks;  // Task IDs
    cista::offset::map<cista::offset::string, cista::offset::string> partial_results;  // Intermediate results
};

// Full hierarchy checkpoint
struct HierarchyCheckpoint {
    uint32_t version{CHECKPOINT_VERSION};
    int64_t checkpoint_time_ns;  // When checkpoint was taken

    // Hierarchy topology
    cista::offset::string root_agent_id;
    cista::offset::vector<AgentSnapshot> agents;

    // Active sessions
    cista::offset::vector<SessionSnapshot> sessions;

    // Global configuration
    cista::offset::map<cista::offset::string, cista::offset::string> config;
};

class CheckpointManager {
public:
    // Take a checkpoint (must be at safe point)
    static bool takeCheckpoint(
        const std::string& checkpoint_path,
        const HierarchyCheckpoint& checkpoint
    );

    // Load checkpoint from disk
    static std::optional<HierarchyCheckpoint> loadCheckpoint(
        const std::string& checkpoint_path
    );

    // Verify checkpoint integrity (version, consistency)
    static bool verifyCheckpoint(const HierarchyCheckpoint& checkpoint);

    // Get checkpoint metadata without loading full state
    static std::optional<CheckpointMetadata> getCheckpointMetadata(
        const std::string& checkpoint_path
    );
};

struct CheckpointMetadata {
    uint32_t version;
    int64_t checkpoint_time_ns;
    size_t num_agents;
    size_t num_sessions;
};

} // namespace keystone::observability
```

#### 3.7.3. Checkpointing Protocol

**Safe Point Detection**:

```cpp
export module Keystone.Concurrency:SafePointDetector;

import Keystone.Agents:BaseAgent;
import <atomic>;
import <vector>;
import <barrier>;

namespace keystone::concurrency {

class SafePointDetector {
public:
    // Check if all agents are at safe point (IDLE, queues empty)
    static bool atSafePoint(const std::vector<BaseAgent*>& all_agents);

    // Request all agents to reach safe point
    static void requestSafePoint();

    // Wait until safe point is reached (with timeout)
    static bool waitForSafePoint(std::chrono::milliseconds timeout);

private:
    static std::atomic<bool> safe_point_requested_;
    static std::unique_ptr<std::barrier<>> safe_point_barrier_;
};

} // namespace keystone::concurrency
```

**Checkpoint Flow**:

1. **Trigger**: External signal (SIGUSR1) or periodic timer
2. **Request Safe Point**: Set global flag, agents stop pulling new work
3. **Drain Queues**: All agents finish current tasks, enter IDLE
4. **Barrier Sync**: All agents reach safe point barrier
5. **Collect State**: Traverse hierarchy, serialize agent states
6. **Write to Disk**: Serialize HierarchyCheckpoint using Cista
7. **Resume**: Agents resume normal operation

**Integration with Agents**:

```cpp
// In BaseAgent::run()
Task<void> BaseAgent::run() {
    while (true) {
        // Check for checkpoint request
        if (SafePointDetector::isSafePointRequested() && state_ == State::IDLE) {
            // Participate in safe point barrier
            co_await SafePointDetector::reachSafePoint(this);
            // Checkpoint taken by coordinator, now resume
        }

        auto msg = co_await pullOrSteal();
        // ... process message
    }
}
```

#### 3.7.4. Resume from Checkpoint

**Resume Flow**:

1. **Load Checkpoint**: Deserialize HierarchyCheckpoint from disk
2. **Validate**: Check version compatibility, integrity
3. **Reconstruct Hierarchy**:
   - Create agents (ChiefArchitect, ComponentLeads, ModuleLeads, TaskAgents)
   - Restore parent-child relationships
   - Restore agent state (set state machine to checkpointed state)
4. **Restore Sessions**:
   - Recreate session contexts
   - Restore partial results
5. **Resume Execution**:
   - Start all agent coroutines
   - ChiefArchitect resumes from last checkpoint
   - May need to replay or re-delegate incomplete tasks

**Limitations**:

- **No Mid-Execution Resume**: Cannot resume from arbitrary coroutine suspension points
- **Replay Required**: If tasks were partially completed, may need to re-execute from last safe point
- **External State**: File system state (process outputs) must be managed separately

**Resume Interface**:

```cpp
export module Keystone.Observability:CheckpointResume;

import Keystone.Observability:Checkpoint;
import Keystone.Concurrency:WorkStealingScheduler;

namespace keystone::observability {

class CheckpointResume {
public:
    // Resume system from checkpoint
    static bool resumeFromCheckpoint(
        const std::string& checkpoint_path,
        WorkStealingScheduler* scheduler
    );

private:
    // Reconstruct agent hierarchy from snapshot
    static void reconstructHierarchy(
        const HierarchyCheckpoint& checkpoint,
        WorkStealingScheduler* scheduler
    );

    // Restore session states
    static void restoreSessions(const HierarchyCheckpoint& checkpoint);
};

} // namespace keystone::observability
```

#### 3.7.5. Checkpoint File Format

**File Structure** (Cista binary):

```
┌─────────────────────────────────┐
│ Magic Number: "KCHK" (4 bytes) │
├─────────────────────────────────┤
│ Version: uint32_t               │
├─────────────────────────────────┤
│ Timestamp: int64_t              │
├─────────────────────────────────┤
│ Cista-serialized                │
│ HierarchyCheckpoint             │
│ (variable length)               │
└─────────────────────────────────┘
```

**File Naming**: `keystone_checkpoint_<timestamp>.ckpt`

**Compression**: Optional gzip compression for large checkpoints

**Versioning**: Version field allows future format evolution

#### 3.7.6. Advanced Checkpoint Features (Future)

**Incremental Checkpointing**:

- Only serialize changed state since last checkpoint
- Delta compression

**Multi-Version Checkpoints**:

- Keep last N checkpoints for rollback
- Automatic cleanup of old checkpoints

**Distributed Checkpointing** (if hierarchy is distributed):

- Each agent writes local checkpoint
- Coordinator merges into global checkpoint

**Checkpoint Verification**:

- CRC checksum for corruption detection
- Automatic checkpoint integrity tests

#### 3.7.7. Testing

**Unit Tests**:

- Checkpoint serialization/deserialization (round-trip)
- Version compatibility (load old checkpoint format)
- Checkpoint verification (detect corruption)

**Integration Tests**:

- Safe point detection (all agents reach IDLE)
- Checkpoint during execution (trigger, wait, verify)
- Resume from checkpoint (restore hierarchy)

**E2E Tests**:

- Full workflow checkpoint/resume:
  1. Start hierarchy, process some tasks
  2. Take checkpoint at safe point
  3. Shutdown system
  4. Resume from checkpoint
  5. Complete remaining tasks
  6. Verify final result matches original workflow

**Chaos Tests**:

- Checkpoint with random agent failures (verify state consistency)
- Resume with missing checkpoint file (graceful error)
- Resume with corrupted checkpoint (validation catches error)

---

## 4. Migration Phases

### Phase A: Foundation (Weeks 1-3)

**Deliverables**:

- ✅ Custom `Task<T>` implementation
- ✅ ThreadPool with coroutine support
- ✅ WorkStealingQueue using concurrentqueue
- ✅ PullOrSteal awaitable
- ✅ WorkStealingScheduler
- ✅ Enhanced KeystoneMessage (KIM)
- ✅ Cista serialization layer
- ✅ **Logging infrastructure (spdlog integration)**
- ✅ **LogContext for thread-local context propagation**

**Test Coverage**:

- 25+ unit tests for new components (including logging tests)
- ThreadSanitizer clean (no data races)
- ASan clean (no memory leaks)
- Logging overhead < 1% (performance test)

**DO NOT BREAK**: Existing 17 E2E tests (keep old MessageBus temporarily)

---

### Phase B: Agent Migration (Weeks 4-6)

**Deliverables**:

- ✅ BaseAgent coroutine refactor
- ✅ TaskAgent coroutine implementation
- ✅ ModuleLead coroutine with `std::barrier`
- ✅ ComponentLead coroutine
- ✅ ChiefArchitect coroutine

**Test Coverage**:

- Migrate all 17 E2E tests to use coroutine-based agents
- All tests passing with WorkStealingScheduler
- Remove old MessageBus code

**Success Criteria**:

- 17/17 tests passing
- Work-stealing active (verified via metrics)
- Performance equal or better than baseline

---

### Phase C: C++20 Modules (Weeks 7-8)

**Deliverables**:

- ✅ Convert all headers to modules
- ✅ Update CMakeLists.txt
- ✅ Verify build on GCC 12+, Clang 15+

**Success Criteria**:

- All tests passing with modules
- Build time reduced by 20%+

---

### Phase D: Graceful Shutdown, Monitoring & Checkpointing (Weeks 9-11)

**Deliverables**:

- ✅ Signal handler thread
- ✅ Shutdown protocol
- ✅ Metrics collection (queue depth, steal rate, latency)
- ✅ Simple metrics exporter (stdout for now)
- ✅ **Checkpoint/Resume system (Cista serialization)**
- ✅ **Safe point detection and coordination**
- ✅ **Checkpoint file management (versioning, integrity checks)**

**Test Coverage**:

- Shutdown tests (SIGTERM during execution)
- Metrics accuracy tests
- **Checkpoint tests (save/load/verify)**
- **Resume tests (full workflow checkpoint/resume)**
- **Safe point coordination tests**

---

### Phase E: Performance & Load Testing (Weeks 12-13)

**Deliverables**:

- ✅ Load test: 100+ agents running concurrently
- ✅ Latency benchmarks (compare to Phase 1-3 baseline)
- ✅ Throughput benchmarks

**Success Criteria**:

- No performance regression
- Latency improvements from work-stealing
- System stable under load

---

## 5. Testing Strategy

### Unit Tests (NEW - ~30 tests)

**Concurrency**:

- Task<T> creation, suspension, resumption, exceptions
- ThreadPool submit, execution, shutdown
- WorkStealingQueue push, pop, steal, concurrent access
- PullOrSteal awaitable behavior
- WorkStealingScheduler routing, steal success rate

**Serialization**:

- Cista round-trip (serialize → deserialize)
- Binary compatibility
- Performance benchmarks

**Message**:

- Enhanced KeystoneMessage construction
- Session ID propagation

---

### Integration Tests (NEW - ~10 tests)

- ThreadPool + WorkStealingQueue
- Coroutine + Scheduler
- Multi-agent pull/steal interactions

---

### E2E Tests (MIGRATE - 17 tests)

**Phase 1** (3 tests):

- ChiefArchitect → TaskAgent (with work-stealing)
- ChiefArchitect → 3 TaskAgents
- MessageBus routes responses (now Scheduler)

**Phase 2** (2 tests):

- ModuleLead coordinates 3 TaskAgents (with barrier)
- ModuleLead handles variable task counts

**Phase 3** (1 test):

- Full 4-layer hierarchy (coroutines + barriers)

---

### Performance Tests (NEW - ~5 tests)

- Baseline latency (single message round-trip)
- Throughput (messages per second)
- Load test (100+ agents)
- Steal efficiency (steal success rate)
- Serialization overhead

---

### Shutdown Tests (NEW - ~3 tests)

- Graceful shutdown with in-flight work
- Shutdown during process execution
- Shutdown with suspended coroutines

---

## 6. Success Criteria

### Phase A (Foundation)

- ✅ All unit tests passing (20+)
- ✅ TSan clean (no data races)
- ✅ ASan clean (no memory leaks)
- ✅ No impact on existing 17 E2E tests

### Phase B (Agent Migration)

- ✅ 17/17 E2E tests passing with coroutines
- ✅ Work-stealing verified (metrics show steals occurring)
- ✅ Old MessageBus removed

### Phase C (Modules)

- ✅ All tests passing with C++20 Modules
- ✅ Build time reduced

### Phase D (Robustness)

- ✅ Graceful shutdown tests passing
- ✅ Metrics collection working

### Phase E (Performance)

- ✅ No performance regression
- ✅ Load test: 100+ agents stable
- ✅ Latency within acceptable range

---

## 7. Dependencies

**Third-Party Libraries**:

```cmake
# concurrentqueue (lock-free MPMC queue)
FetchContent_Declare(
    concurrentqueue
    GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
    GIT_TAG v1.0.4
)

# Cista (zero-copy serialization)
FetchContent_Declare(
    cista
    GIT_REPOSITORY https://github.com/felixguendling/cista.git
    GIT_TAG v0.14
)

# spdlog (structured, async logging)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.12.0
)
FetchContent_MakeAvailable(spdlog)
```

**Compiler Requirements**:

- GCC 12+ (C++20 coroutines + modules)
- Clang 15+ (C++20 coroutines + modules)

**NO Dependencies**:

- ❌ Facebook libraries (folly)
- ❌ CUDA/cuDNN
- ❌ ONNX Runtime
- ❌ gRPC (for now - model communication via filesystem)

---

## 8. Open Questions

1. **Process Execution Async I/O**: Should we use `io_uring` for truly async process execution, or keep `popen` for simplicity?

2. **External Interface**: How does the ChiefArchitect receive the initial user command and return the final result? (API, CLI, message queue?)

3. **Monitoring**: What metrics format? (Prometheus, statsd, custom?)

4. **Session Management**: How are session IDs generated and managed? (UUID per request? User-provided?)

5. **Load Balancing**: Should message routing to queues be round-robin, hash-based, or load-aware?

6. **Backpressure**: What happens when all queues are full? (Block sender, drop messages, return error?)

---

## 9. Next Steps

**Week 1**:

1. Implement custom `Task<T>` coroutine type
2. Implement ThreadPool
3. Write unit tests

**Week 2**:
4. Implement WorkStealingQueue (using concurrentqueue)
5. Implement PullOrSteal awaitable
6. Write unit tests

**Week 3**:
7. Implement WorkStealingScheduler
8. Extend KeystoneMessage (KIM)
9. Implement Cista serialization
10. Integration tests

**Week 4**:
11. Refactor BaseAgent to coroutines
12. Migrate TaskAgent
13. E2E tests

**Week 5**:
14. Migrate ModuleLead (with std::barrier)
15. Migrate ComponentLead
16. E2E tests

**Week 6**:
17. Migrate ChiefArchitect
18. All 17 E2E tests passing
19. Remove old MessageBus

**Weeks 7-12**: Modules, shutdown, monitoring, performance testing

---

## 10. Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Coroutine bugs (suspension, lifetime) | Extensive unit tests, use proven patterns, incremental migration |
| Work-stealing correctness (data races) | Use proven library (concurrentqueue), TSan on all tests |
| Performance regression | Benchmark early, compare to baseline, have rollback plan |
| Breaking existing tests | Maintain backward compatibility layer, migrate incrementally |
| Complexity explosion | Start simple (Phase A), add complexity gradually |

---

## Conclusion

This migration plan transforms ProjectKeystone from a synchronous MessageBus architecture to a high-performance Work-Stealing concurrent system using:

- **concurrentqueue** for lock-free queues
- **Custom Task<T>** for C++20 coroutines
- **Cista** for zero-copy serialization
- **spdlog** for structured, async logging
- **Checkpoint/Resume** for fault tolerance and debugging
- **C++20 Modules** for code organization
- **Process execution + filesystem** for model communication

The phased approach ensures we maintain test coverage, minimize risk, and deliver a production-ready system with comprehensive observability and fault tolerance.

**Estimated Timeline**: 13-15 weeks (13 weeks core features, +2 weeks for C++20 modules)
**Test Coverage Target**: 100% (all existing tests + new tests)
**Performance Target**: No regression, potential improvements from work-stealing
**Observability**: Comprehensive structured logging with context propagation
**Fault Tolerance**: Checkpoint/resume at safe points for production resilience

---

**Ready to begin Phase A?**
