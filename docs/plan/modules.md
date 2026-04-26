# ProjectKeystone Component Architecture

## Overview

ProjectKeystone is a **pure invisible transport layer** for HomericIntelligence. It provides transparent, zero-configuration message routing using two complementary transports:

1. **Local Transport** (intra-host): BlazingMQ + lock-free concurrent queue via `concurrentqueue`
2. **Cross-Host Transport** (via Tailscale mesh): NATS JetStream v3.12.0

The two transports are automatically bridged via the **Transparent Bridge** — components publish and subscribe to logical NATS subjects without awareness of whether peers are local or remote.

> **Note**: The 4-layer HMAS agent hierarchy (ChiefArchitectAgent → ComponentLeadAgent → ModuleLeadAgent → TaskAgent) was extracted and moved to **ProjectAgamemnon** per ADR-006/ADR-015. Keystone retains only transport primitives.

---

## Source Layout

ProjectKeystone is organized by transport and infrastructure concerns:

```
src/
├── core/               # Message routing, KIM protocol, transport bridge
├── concurrency/        # Thread pools, synchronization primitives
├── network/            # NATS JetStream client integration
├── transport/          # Transport abstraction (local/remote)
├── monitoring/         # Logging, metrics, health checks
├── simulation/         # Test simulation framework
├── agents/             # Minimal agent stubs (for tests/examples)
├── daemon/             # Keystone daemon process
└── keystone/           # Primary library interface
```

## Core Components

### 1. Message Bus (`src/core/`)

The **MessageBus** is the central coordination layer:

- Routes KIM (Keystone Interchange Message) protocol messages
- Enforces backpressure on local queues
- Delegates to **Transparent Bridge** for off-host routing
- Target: **<500 ns local routing latency**, **>2 M msg/sec throughput**

**Key Classes**:

- `MessageBus` — Central message coordinator
- `KIM` — Keystone Interchange Message structure
- `Router` — Routing logic for local/remote destinations

### 2. Concurrency Utilities (`src/concurrency/`)

Thread-safe building blocks for high-performance message handling:

- **ThreadPool** — Fixed-size worker threads
- **Synchronization primitives** — Barriers, latches, atomic state
- **Lock-free queuing** — Via `concurrentqueue` library

### 3. Network / NATS Integration (`src/network/`)

NATS JetStream client integration:

- Durable consumer streams (per myrmidon / consumer)
- Pull-based, rate-limited delivery (`MaxAckPending=1`)
- Automatic reconnection and message deduplication
- Subject-based routing to `hi.*` streams

**Managed Streams**:

| Stream | Subject | Consumer Type |
|--------|---------|---------------|
| `homeric-research` | `hi.research.>` | PULL |
| `homeric-myrmidon` | `hi.myrmidon.{type}.>` | PULL |
| `homeric-pipeline` | `hi.pipeline.>` | PUB/SUB |
| `homeric-agents` | `hi.agents.>` | PUB/SUB |
| `homeric-tasks` | `hi.tasks.>` | PUB/SUB |
| `homeric-logs` | `hi.logs.>` | PUB |

### 4. Transparent Bridge

Automatically promotes messages between local MessageBus and NATS JetStream:

```
Publisher (local)
    ↓
MessageBus.route(msg)
    ↓
    ├─ local destination → deliver via concurrentqueue
    │
    └─ remote destination → publish to NATS subject
```

Consumer receives identically whether peer is local or remote.

### 5. Monitoring (`src/monitoring/`)

Logging, metrics, and health checks:

- **Logger** — spdlog-based structured logging
- **Metrics** — Performance counters and histograms
- **Health checks** — Agent and transport readiness probes

### 6. Simulation Framework (`src/simulation/`)

Test harness for deterministic testing:

- Virtual time control
- Fault injection (network delays, message drops)
- Deterministic scheduling

### 7. Agents Module (`src/agents/`)

Minimal agent implementations for testing and examples. **Note**: Real agent implementations (ChiefArchitectAgent, etc.) are in **ProjectAgamemnon**.

---

## Transport Decision Flow

```
Publisher calls:
    MessageBus::publish(message, destination_subject)
                            ↓
                Does destination_subject route locally?
                    ├─ YES → deliver via lock-free queue
                    └─ NO → publish to NATS subject
                              ↓
                          Remote NATS broker
                              ↓
                          Remote MessageBus receives
                              ↓
                          Local delivery on remote host
```

No component needs to know whether a peer is local or remote. The bridge handles routing transparently.

---

## Message Flow Example

**Scenario**: Local publisher → remote subscriber (off-host)

1. Publisher calls: `bus.publish(msg, "hi.agents.commander-1")`
2. MessageBus routes to **Transparent Bridge**
3. Bridge promotes message to NATS: `publish("hi.agents.commander-1", msg_bytes)`
4. NATS broker stores in `homeric-agents` stream
5. Remote host's MessageBus pulls from stream via durable consumer
6. Remote subscriber receives via lock-free queue
7. Subscriber sends ACK back to NATS
8. Message removed from stream after durable consumer ACK

---

## Backpressure and Flow Control

Keystone enforces pull-based, rate-limited delivery:

- `MaxAckPending = 1` per consumer (one in-flight message per myrmidon)
- Each consumer calls `natsSubscription_Fetch(batch=1)` — explicit pull
- Slow consumers don't block publishers
- Messages accumulate in NATS stream until consumer is ready
- No drop, no loss — durable delivery guaranteed

---

## Key Dependencies

| Dependency | Version | Purpose |
|------------|---------|---------|
| **BlazingMQ** | latest | Local durable queue (optional, can use in-memory queue) |
| **concurrentqueue** | latest | Lock-free intra-host message queue |
| **nats.c** | 3.12.0 | NATS JetStream C client |
| **spdlog** | latest | Structured logging |
| **CMake** | 3.20+ | Build system |

---

## Build Configuration

ProjectKeystone uses **CMake 3.20+** with traditional C++ header-based organization (not C++20 modules):

```cmake
cmake_minimum_required(VERSION 3.20)
project(ProjectKeystone CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

**Target Library**: `keystone_core` — the main transport library

**Optional Features**:

- `ENABLE_GRPC` — Distributed multi-node gRPC support (Phase 8)
- `ENABLE_COVERAGE` — Code coverage instrumentation
- `ENABLE_PROFILING` — Profiling test suite
- `ENABLE_CLANG_TIDY` — Static analysis

---

## What Was Moved to ProjectAgamemnon

The following **are NOT part of Keystone** — they live in ProjectAgamemnon:

- L0 **ChiefArchitectAgent** (system coordinator)
- L1 **ComponentLeadAgent** (component lifecycle)
- L2 **ModuleLeadAgent** (module orchestration)
- L3 **TaskAgent** (task execution)
- Delegation and escalation logic
- Work-stealing scheduler
- HMAS 4-layer hierarchy

Keystone's only concern: **moving bytes from publisher to subscriber, reliably and transparently**.

---

## Concurrency Model

- **ThreadPool** — Fixed-size worker threads for I/O and message handling
- **Lock-free queuing** — `concurrentqueue` for intra-thread message passing
- **C++20 synchronization** — `std::latch`, `std::barrier`, `std::atomic<T>`
- **No coroutines** (in transport layer) — synchronous messaging primitives

---

## Design Principles

1. **Invisible Transport**: Components never address Keystone directly — they publish to NATS subjects
2. **Transparent Bridging**: Local MessageBus and NATS seamlessly bridge — no component awareness needed
3. **Pull-Based, Rate-Limited**: Myrmidons pull at their own pace; Keystone never overloads
4. **Durable Delivery**: NATS JetStream ensures at-least-once; consumer ACKs track progress
5. **C++20 Only**: Pure C++20 with BlazingMQ and nats.c — no Python, gRPC middleware, or agents
6. **Tailscale Required**: All cross-host NATS traffic runs via WireGuard mesh (`tail8906b5.ts.net`)

---

**Document Version**: 2.0 (Pure Transport — HMAS moved to ProjectAgamemnon)
**Last Updated**: 2026-04-25

---

## Module 1: Keystone.Core

**Purpose**: Foundational infrastructure for concurrency, messaging, and utilities.

### Module Interface (`modules/Keystone.Core/Core.cppm`)

```cpp
module;

// Global module fragment (standard library imports)
#include <coroutine>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include <functional>

export module Keystone.Core;

// Re-export submodules
export import :Concurrency;
export import :Messaging;
export import :Synchronization;
export import :Utilities;

export namespace Keystone::Core {
    // Version information
    constexpr int VERSION_MAJOR = 1;
    constexpr int VERSION_MINOR = 0;
    constexpr int VERSION_PATCH = 0;
}
```

### Submodules

#### Keystone.Core:Concurrency

**File**: `modules/Keystone.Core/Concurrency.cppm`

**Exports**:

- `ThreadPool` - Fixed-size thread pool for coroutine execution
- `Task<T>` - Coroutine return type with promise_type
- `AsyncOperation<T>` - Base for async operations
- `CoroutineHandle` - Utilities for coroutine management

**Example**:

```cpp
export module Keystone.Core:Concurrency;

export namespace Keystone::Core {
    template<typename T = void>
    class Task {
    public:
        struct promise_type {
            Task get_return_object();
            std::suspend_never initial_suspend();
            std::suspend_never final_suspend() noexcept;
            void return_value(T value);
            void unhandled_exception();
        private:
            T value_;
        };

        bool await_ready() const noexcept;
        void await_suspend(std::coroutine_handle<> handle);
        T await_resume();
    };

    class ThreadPool {
    public:
        explicit ThreadPool(size_t num_threads);
        ~ThreadPool();

        template<typename F>
        void submit(F&& work);

        void shutdown();
        size_t activeThreads() const;
    };
}
```

#### Keystone.Core:Messaging

**File**: `modules/Keystone.Core/Messaging.cppm`

**Exports**:

- `MessageQueue<T>` - Thread-safe queue wrapper
- `AsyncQueuePop<T>` - Custom awaitable for queue operations
- `MessageRouter` - Route messages to agents
- `MessageBus` - Central message bus coordination

**Example**:

```cpp
export module Keystone.Core:Messaging;

import :Concurrency;
import <concurrentqueue>;

export namespace Keystone::Core {
    template<typename T>
    class MessageQueue {
    public:
        void push(T&& item);
        bool try_pop(T& item);
        void registerWaiter(std::coroutine_handle<> handle);
        size_t size() const;
    private:
        moodycamel::ConcurrentQueue<T> queue_;
        // Waiter management...
    };

    template<typename T>
    class AsyncQueuePop {
    public:
        explicit AsyncQueuePop(MessageQueue<T>& queue);

        bool await_ready();
        void await_suspend(std::coroutine_handle<> handle);
        T await_resume();
    };
}
```

#### Keystone.Core:Synchronization

**File**: `modules/Keystone.Core/Synchronization.cppm`

**Exports**:

- `InitializationLatch` - Wrapper for std::latch
- `PhaseBarrier` - Wrapper for std::barrier
- `AtomicState` - Atomic state management utilities

**Example**:

```cpp
export module Keystone.Core:Synchronization;

export namespace Keystone::Core {
    class InitializationLatch {
    public:
        explicit InitializationLatch(std::ptrdiff_t count);
        void count_down();
        void wait();
        bool try_wait();
    };

    class PhaseBarrier {
    public:
        explicit PhaseBarrier(std::ptrdiff_t count);
        void arrive_and_wait();
        void arrive_and_drop();
    };
}
```

#### Keystone.Core:Utilities

**File**: `modules/Keystone.Core/Utilities.cppm`

**Exports**:

- `UUID` - UUID generation and handling
- `Logger` - Structured logging interface
- `Config` - Configuration management
- `Metrics` - Performance metrics collection

### Dependencies

- **External**: `concurrentqueue`, `spdlog`
- **Internal**: None (foundation module)

### Directory Structure

```
modules/Keystone.Core/
├── CMakeLists.txt
├── Core.cppm                 # Main module interface
├── Concurrency.cppm          # Submodule
├── Messaging.cppm            # Submodule
├── Synchronization.cppm      # Submodule
├── Utilities.cppm            # Submodule
└── impl/                     # Implementation files
    ├── ThreadPool.cpp
    ├── MessageQueue.cpp
    └── ...
```

---

## Module 2: Keystone.Protocol

**Purpose**: Communication protocols, serialization, and message definitions.

### Module Interface (`modules/Keystone.Protocol/Protocol.cppm`)

```cpp
module;

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>

export module Keystone.Protocol;

export import :KIM;
export import :Serialization;
export import :GRPC;

export namespace Keystone::Protocol {
    constexpr int PROTOCOL_VERSION = 1;
}
```

### Submodules

#### Keystone.Protocol:KIM

**File**: `modules/Keystone.Protocol/KIM.cppm`

**Exports**:

- `KeystoneMessage` - Inter-agent message structure
- `ActionType` - Message action enumeration
- `Priority` - Message priority levels
- `MessageBuilder` - Fluent API for message construction

**Example**:

```cpp
export module Keystone.Protocol:KIM;

export namespace Keystone::Protocol {
    enum class ActionType {
        DECOMPOSE,
        EXECUTE_TOOL,
        RETURN_RESULT,
        SHUTDOWN,
        HEALTH_CHECK
    };

    enum class Priority {
        LOW = 0,
        NORMAL = 1,
        HIGH = 2,
        CRITICAL = 3
    };

    struct KeystoneMessage {
        // Routing
        UUID message_id;
        AgentId sender_id;
        AgentId recipient_id;

        // Control
        ActionType action_type;
        Priority priority;

        // Payload
        std::string content_type;
        std::vector<std::byte> payload;

        // Context
        SessionId session_id;
        std::chrono::system_clock::time_point timestamp;
        std::chrono::milliseconds timeout;

        // Metadata
        std::unordered_map<std::string, std::string> metadata;
    };

    class MessageBuilder {
    public:
        MessageBuilder& from(AgentId sender);
        MessageBuilder& to(AgentId recipient);
        MessageBuilder& action(ActionType type);
        MessageBuilder& payload(std::vector<std::byte> data);
        MessageBuilder& session(SessionId id);
        KeystoneMessage build();
    };
}
```

#### Keystone.Protocol:Serialization

**File**: `modules/Keystone.Protocol/Serialization.cppm`

**Exports**:

- `SerializationFormat` - Format enumeration (Cista, Protobuf)
- `serialize<T>()` - Generic serialization
- `deserialize<T>()` - Generic deserialization
- Common serializable types

**Example**:

```cpp
export module Keystone.Protocol:Serialization;

import <cista>;

export namespace Keystone::Protocol {
    enum class SerializationFormat {
        CISTA,      // Internal zero-copy
        PROTOBUF    // External gRPC
    };

    template<typename T>
    std::vector<std::byte> serialize(const T& obj) {
        return cista::serialize(obj);
    }

    template<typename T>
    T* deserialize(const std::vector<std::byte>& data) {
        return cista::deserialize<T>(data);
    }

    // Common serializable types
    struct TaskContext {
        cista::offset::string goal;
        cista::offset::vector<cista::offset::string> sub_tasks;
        int64_t timestamp;
    };

    struct InferenceTask {
        cista::offset::string prompt;
        int32_t max_tokens;
        float temperature;
    };
}
```

#### Keystone.Protocol:GRPC

**File**: `modules/Keystone.Protocol/GRPC.cppm`

**Exports**:

- `GRPCClient<ServiceT>` - Async gRPC client wrapper
- `GRPCError` - gRPC error handling
- Protocol Buffer message types (generated from .proto)

**Example**:

```cpp
export module Keystone.Protocol:GRPC;

import Keystone.Core;

export namespace Keystone::Protocol {
    template<typename ServiceT>
    class GRPCClient {
    public:
        explicit GRPCClient(std::string endpoint);

        template<typename RequestT, typename ResponseT>
        Task<ResponseT> call(
            RequestT request,
            std::chrono::milliseconds timeout
        );

        void shutdown();
    };

    class GRPCError : public std::runtime_error {
    public:
        explicit GRPCError(grpc::Status status);
        grpc::StatusCode code() const;
    };
}
```

### Dependencies

- **External**: `cista`, `grpc++`, `protobuf`
- **Internal**: `Keystone.Core` (for coroutines)

### Directory Structure

```
modules/Keystone.Protocol/
├── CMakeLists.txt
├── Protocol.cppm
├── KIM.cppm
├── Serialization.cppm
├── GRPC.cppm
├── proto/                    # Protocol Buffer definitions
│   ├── ai_service.proto
│   └── health.proto
└── impl/
    ├── KIM.cpp
    ├── GRPCClient.cpp
    └── ...
```

---

## Module 3: Keystone.Agents

**Purpose**: Agent implementations (base, root, branch, leaf) and lifecycle management.

### Module Interface (`modules/Keystone.Agents/Agents.cppm`)

```cpp
module;

#include <memory>
#include <vector>

export module Keystone.Agents;

export import :Base;
export import :Root;
export import :Branch;
export import :Leaf;
export import :StateMachine;
export import :Supervision;

export namespace Keystone::Agents {
    // Factory functions
    std::unique_ptr<RootAgent> createRootAgent(/* config */);
    std::unique_ptr<BranchAgent> createBranchAgent(/* config */);
    std::unique_ptr<LeafAgent> createLeafAgent(/* config */);
}
```

### Submodules

#### Keystone.Agents:Base

**File**: `modules/Keystone.Agents/Base.cppm`

**Exports**:

- `AgentBase` - Base class for all agents
- `AgentId` - Agent identifier type
- `SessionId` - Session identifier type
- `AgentConfig` - Configuration structure

**Example**:

```cpp
export module Keystone.Agents:Base;

import Keystone.Core;
import Keystone.Protocol;

export namespace Keystone::Agents {
    class AgentBase {
    public:
        virtual ~AgentBase() = default;

        // Lifecycle
        virtual Task<void> initialize() = 0;
        virtual Task<void> run() = 0;
        virtual Task<void> shutdown() = 0;

        // Message handling
        Task<void> sendMessage(const KeystoneMessage& msg);
        Task<KeystoneMessage> receiveMessage();

        // State
        AgentState getState() const;
        AgentId getId() const;

    protected:
        MessageQueue<KeystoneMessage> mailbox_;
        AgentState state_;
        AgentId id_;
        SessionId session_id_;
        AgentBase* supervisor_;
        std::vector<AgentBase*> children_;
    };
}
```

#### Keystone.Agents:Root

**File**: `modules/Keystone.Agents/Root.cppm`

**Exports**:

- `RootAgent` - L1 orchestrator agent

**Example**:

```cpp
export module Keystone.Agents:Root;

import :Base;
import Keystone.Core;
import Keystone.Protocol;

export namespace Keystone::Agents {
    class RootAgent : public AgentBase {
    public:
        Task<void> initialize() override;
        Task<void> run() override;
        Task<void> shutdown() override;

        // Strategic coordination
        Task<void> decomposeGoal(const std::string& goal);
        Task<void> coordinateBranches();
        Task<std::string> synthesizeResults();

    private:
        std::vector<std::unique_ptr<BranchAgent>> branches_;
        GlobalContext context_;
    };
}
```

#### Keystone.Agents:Branch

**File**: `modules/Keystone.Agents/Branch.cppm`

**Exports**:

- `BranchAgent` - L2 planning/zone manager

**Example**:

```cpp
export module Keystone.Agents:Branch;

import :Base;
import Keystone.Core;

export namespace Keystone::Agents {
    class BranchAgent : public AgentBase {
    public:
        Task<void> initialize() override;
        Task<void> run() override;
        Task<void> shutdown() override;

        // Tactical planning
        Task<void> decomposeTasks();
        Task<void> delegateToLeaves();
        Task<void> synthesizeResults();

        // Transaction management
        void saveTransactionState();
        void restoreTransactionState();

    private:
        std::vector<std::unique_ptr<LeafAgent>> leaves_;
        PhaseBarrier completion_barrier_;
        TransactionState tx_state_;
    };
}
```

#### Keystone.Agents:Leaf

**File**: `modules/Keystone.Agents/Leaf.cppm`

**Exports**:

- `LeafAgent` - L3 worker/tool controller

**Example**:

```cpp
export module Keystone.Agents:Leaf;

import :Base;
import Keystone.Core;
import Keystone.Protocol;

export namespace Keystone::Agents {
    class LeafAgent : public AgentBase {
    public:
        Task<void> initialize() override;
        Task<void> run() override;
        Task<void> shutdown() override;

        // Execution
        Task<void> executeTool(const std::string& tool_name);
        Task<void> performInference();
        void signalBackpressure();

    private:
        ToolRegistry tools_;
        size_t queue_depth_threshold_{100};
    };
}
```

#### Keystone.Agents:StateMachine

**File**: `modules/Keystone.Agents/StateMachine.cppm`

**Exports**:

- `AgentState` - State enumeration
- `StateTransition` - Transition validation
- `StateHistory` - State transition tracking

#### Keystone.Agents:Supervision

**File**: `modules/Keystone.Agents/Supervision.cppm`

**Exports**:

- `SupervisionStrategy` - Failure handling strategies
- `HealthMonitor` - Agent health checking
- `FailureRecovery` - Recovery coordination

### Dependencies

- **External**: None
- **Internal**: `Keystone.Core`, `Keystone.Protocol`

### Directory Structure

```
modules/Keystone.Agents/
├── CMakeLists.txt
├── Agents.cppm
├── Base.cppm
├── Root.cppm
├── Branch.cppm
├── Leaf.cppm
├── StateMachine.cppm
├── Supervision.cppm
└── impl/
    ├── RootAgent.cpp
    ├── BranchAgent.cpp
    ├── LeafAgent.cpp
    └── ...
```

---

## Module 4: Keystone.Integration

**Purpose**: External integrations (AI models, monitoring, etc.).

### Module Interface (`modules/Keystone.Integration/Integration.cppm`)

```cpp
export module Keystone.Integration;

export import :AI;
export import :Monitoring;
export import :Tools;
```

### Submodules

#### Keystone.Integration:AI

**File**: `modules/Keystone.Integration/AI.cppm`

**Exports**:

- `ONNXRuntime` - Local model inference
- `RemoteAIClient` - Remote AI service clients
- `ModelRegistry` - Model loading and management

**Example**:

```cpp
export module Keystone.Integration:AI;

import Keystone.Core;
import Keystone.Protocol;

export namespace Keystone::Integration {
    class ONNXRuntime {
    public:
        void loadModel(const std::string& model_path);
        Task<std::vector<float>> infer(std::vector<float> input);
        void setExecutionProvider(ExecutionProvider provider);
    };

    class RemoteAIClient {
    public:
        explicit RemoteAIClient(std::string endpoint);
        Task<std::string> generateText(const std::string& prompt);
        Task<std::vector<float>> getEmbedding(const std::string& text);
    };
}
```

#### Keystone.Integration:Monitoring

**File**: `modules/Keystone.Integration/Monitoring.cppm`

**Exports**:

- `MetricsCollector` - Prometheus metrics
- `Tracer` - OpenTelemetry tracing
- `HealthCheck` - Health check endpoints

**Example**:

```cpp
export module Keystone.Integration:Monitoring;

export namespace Keystone::Integration {
    class MetricsCollector {
    public:
        void incrementCounter(const std::string& name);
        void recordHistogram(const std::string& name, double value);
        void setGauge(const std::string& name, double value);
        std::string exportMetrics();
    };

    class Tracer {
    public:
        SpanContext startSpan(const std::string& name);
        void endSpan(SpanContext ctx);
        void addAttribute(SpanContext ctx, std::string key, std::string value);
    };
}
```

### Dependencies

- **External**: `onnxruntime`, `prometheus-cpp`, `opentelemetry-cpp`
- **Internal**: `Keystone.Core`, `Keystone.Protocol`

### Directory Structure

```
modules/Keystone.Integration/
├── CMakeLists.txt
├── Integration.cppm
├── AI.cppm
├── Monitoring.cppm
├── Tools.cppm
└── impl/
    ├── ONNXRuntime.cpp
    ├── MetricsCollector.cpp
    └── ...
```

---

## Module Dependency Graph

```
Keystone.Integration
    ├── depends on: Keystone.Core
    ├── depends on: Keystone.Protocol
    └── depends on: External (ONNX, Prometheus, etc.)

Keystone.Agents
    ├── depends on: Keystone.Core
    ├── depends on: Keystone.Protocol
    └── no external dependencies

Keystone.Protocol
    ├── depends on: Keystone.Core
    └── depends on: External (Cista, gRPC, Protobuf)

Keystone.Core
    └── depends on: External (concurrentqueue, spdlog)
```

## CMake Module Configuration

### Root CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.28)
project(ProjectKeystone CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable C++20 modules
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "aa1f7df0-828a-4fcd-9afc-2dc80491aca7")
set(CMAKE_EXPERIMENTAL_CXX_MODULE_DYNDEP ON)

# Add modules
add_subdirectory(modules/Keystone.Core)
add_subdirectory(modules/Keystone.Protocol)
add_subdirectory(modules/Keystone.Agents)
add_subdirectory(modules/Keystone.Integration)
```

### Module CMakeLists.txt Example

```cmake
# modules/Keystone.Core/CMakeLists.txt

add_library(Keystone.Core)

target_sources(Keystone.Core
    PUBLIC
        FILE_SET CXX_MODULES FILES
            Core.cppm
            Concurrency.cppm
            Messaging.cppm
            Synchronization.cppm
            Utilities.cppm
    PRIVATE
        impl/ThreadPool.cpp
        impl/MessageQueue.cpp
)

target_link_libraries(Keystone.Core
    PUBLIC
        concurrentqueue::concurrentqueue
        spdlog::spdlog
)

target_compile_features(Keystone.Core PUBLIC cxx_std_20)
```

## Usage Examples

### Importing Modules

```cpp
// Import entire module
import Keystone.Core;
import Keystone.Protocol;
import Keystone.Agents;

// Import specific submodules
import Keystone.Core.Messaging;
import Keystone.Protocol.KIM;

// Use qualified names
using namespace Keystone::Core;
using namespace Keystone::Agents;

int main() {
    ThreadPool pool{8};
    auto root = createRootAgent();
    // ...
}
```

---

**Document Version**: 1.0
**Last Updated**: 2025-11-17
