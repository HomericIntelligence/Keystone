# ProjectKeystone HMAS Workflow Examples

This directory contains comprehensive examples demonstrating the Hierarchical Multi-Agent System (HMAS) across three
different deployment models. Each example showcases how agents communicate and coordinate tasks across 2, 3, and 4
layers of hierarchy.

## Table of Contents

1. [Overview](#overview)
2. [Deployment Models](#deployment-models)
3. [Example Structure](#example-structure)
4. [Building and Running](#building-and-running)
5. [Architecture Patterns](#architecture-patterns)

---

## Overview

ProjectKeystone implements a **4-layer hierarchical agent system** where agents coordinate to accomplish complex tasks
through message passing and delegation:

```
Level 0: ChiefArchitectAgent    ← Strategic orchestration
    ↓
Level 1: ComponentLeadAgent     ← Component coordination
    ↓
Level 2: ModuleLeadAgent        ← Module task decomposition
    ↓
Level 3: TaskAgent              ← Concrete execution
```

### Communication Patterns

All examples use the **Keystone Interchange Message (KIM)** protocol for agent communication:

```cpp
struct KeystoneMessage {
    std::string msg_id;           // Unique identifier (UUID)
    std::string sender_id;        // Sending agent ID
    std::string receiver_id;      // Receiving agent ID
    ActionType action_type;       // DECOMPOSE, EXECUTE, RETURN_RESULT
    std::string command;          // Command or goal description
    Priority priority;            // HIGH, NORMAL, LOW
    std::optional<std::string> payload;  // Optional data
};
```

### Asynchronous Execution

All agents use **C++20 coroutines** for non-blocking async operations:

```cpp
Task<Response> processMessage(const KeystoneMessage& msg) {
    // Process asynchronously
    auto result = co_await delegate_to_subordinate(msg);
    co_return Response{...};
}
```

---

## Deployment Models

### 1. Thread-Based (Shared Memory within Process)

**Location**: `01-thread-based/`

- **Communication**: Lock-free concurrent queues (moodycamel)
- **Message Bus**: Central MessageBus routes messages between agents
- **Concurrency**: Multiple threads share memory space
- **Best For**: Single-process applications, tight latency requirements

**Architecture**:

```
┌─────────────────────────────────────────────┐
│         Single Process                       │
│  ┌────────────────────────────────────────┐ │
│  │         MessageBus                     │ │
│  │  (Routes messages between agents)      │ │
│  └────────────────────────────────────────┘ │
│         ↑         ↑         ↑              │
│         │         │         │              │
│  ┌──────┴───┐ ┌──┴──────┐ ┌┴───────────┐  │
│  │ Chief    │ │ Module  │ │ TaskAgent  │  │
│  │ Thread 1 │ │ Thread 2│ │ Thread 3   │  │
│  └──────────┘ └─────────┘ └────────────┘  │
│                                             │
│  Shared Memory: Lock-Free Queues            │
└─────────────────────────────────────────────┘
```

**Examples**:

- **2-Layer**: Chief → TaskAgent (direct delegation)
- **3-Layer**: Chief → ModuleLead → TaskAgents (coordination)
- **4-Layer**: Chief → ComponentLead → ModuleLead → TaskAgents (full hierarchy)

---

### 2. IPC-Based (Multiple Processes with Shared Memory)

**Location**: `02-ipc-based/`

- **Communication**: Boost.Interprocess shared memory queues
- **Message Bus**: Shared memory MessageBus accessible across processes
- **Concurrency**: Multiple processes with isolated memory
- **Best For**: Process isolation, fault tolerance, resource limits

**Architecture**:

```
┌──────────────┐  ┌──────────────┐  ┌──────────────┐
│  Process 1   │  │  Process 2   │  │  Process 3   │
│              │  │              │  │              │
│  ┌────────┐  │  │ ┌─────────┐  │  │ ┌─────────┐  │
│  │ Chief  │  │  │ │ Module  │  │  │ │  Task   │  │
│  │ Agent  │  │  │ │  Lead   │  │  │ │  Agent  │  │
│  └────────┘  │  │ └─────────┘  │  │ └─────────┘  │
│      ↓       │  │      ↓       │  │      ↓       │
└──────┼───────┘  └──────┼───────┘  └──────┼───────┘
       │                 │                 │
       └─────────────────┼─────────────────┘
                         ↓
            ┌─────────────────────────┐
            │  Shared Memory Segment  │
            │                         │
            │  ┌──────────────────┐   │
            │  │   MessageBus     │   │
            │  │   (IPC Queues)   │   │
            │  └──────────────────┘   │
            └─────────────────────────┘
```

**Key Differences from Thread-Based**:

- Each agent runs in a separate OS process
- MessageBus lives in shared memory segment
- Queues use Boost.Interprocess named_mutex for synchronization
- Process crash doesn't affect other agents

**Examples**:

- **2-Layer**: Chief process → Task process
- **3-Layer**: Chief → ModuleLead → Task processes
- **4-Layer**: Chief → ComponentLead → ModuleLead → Task processes

---

### 3. Network-Based (Multiple Machines with gRPC)

**Location**: `03-network-based/`

- **Communication**: gRPC with Protocol Buffers serialization
- **Service Discovery**: ServiceRegistry (Kubernetes-style)
- **Task Coordination**: HMASCoordinator service
- **Best For**: Distributed systems, scalability, geographic distribution

**Architecture**:

```
┌──────────────────────┐  ┌──────────────────────┐  ┌──────────────────────┐
│   Machine A          │  │   Machine B          │  │   Machine C          │
│   IP: 192.168.1.10   │  │   IP: 192.168.1.11   │  │   IP: 192.168.1.12   │
│                      │  │                      │  │                      │
│  ┌────────────────┐  │  │  ┌────────────────┐  │  │  ┌────────────────┐  │
│  │ ChiefArchitect │  │  │  │ ModuleLead #1  │  │  │  │  TaskAgent #1  │  │
│  │                │  │  │  │                │  │  │  │                │  │
│  │  gRPC Client   │  │  │  │  gRPC Server   │  │  │  │  gRPC Server   │  │
│  └────────┬───────┘  │  │  └────────┬───────┘  │  │  └────────┬───────┘  │
│           │          │  │           │          │  │           │          │
└───────────┼──────────┘  └───────────┼──────────┘  └───────────┼──────────┘
            │                         │                         │
            └─────────────────────────┼─────────────────────────┘
                                      │
                          ┌───────────┴───────────┐
                          │   Machine D           │
                          │   IP: 192.168.1.20    │
                          │                       │
                          │  ┌─────────────────┐  │
                          │  │ ServiceRegistry │  │
                          │  │  (gRPC Server)  │  │
                          │  │                 │  │
                          │  │ - Agent lookup  │  │
                          │  │ - Health checks │  │
                          │  │ - Load balance  │  │
                          │  └─────────────────┘  │
                          │                       │
                          │  ┌─────────────────┐  │
                          │  │ HMASCoordinator │  │
                          │  │  (gRPC Server)  │  │
                          │  │                 │  │
                          │  │ - Task routing  │  │
                          │  │ - Result aggr.  │  │
                          │  └─────────────────┘  │
                          └───────────────────────┘
```

**Key Features**:

- **Service Discovery**: Agents register with ServiceRegistry on startup
- **YAML Task Specs**: Kubernetes-style task definitions
- **Load Balancing**: ROUND_ROBIN, LEAST_LOADED, RANDOM strategies
- **Result Aggregation**: WAIT_ALL, FIRST_SUCCESS, MAJORITY
- **Heartbeat Monitoring**: 1s interval, 3s timeout for liveness

**Examples**:

- **2-Layer**: Chief (Machine A) → TaskAgent (Machine B)
- **3-Layer**: Chief → ModuleLead → TaskAgents (3 machines)
- **4-Layer**: Chief → ComponentLead → ModuleLead → TaskAgents (4+ machines)

---

## Example Structure

Each deployment model directory contains:

```
01-thread-based/
├── README.md               # Detailed documentation for this model
├── 2layer_example.cpp      # Chief → TaskAgent
├── 3layer_example.cpp      # Chief → ModuleLead → TaskAgents
└── 4layer_example.cpp      # Full hierarchy
```

### Example Naming Convention

- **2-Layer**: Minimal hierarchy (Chief → TaskAgent)
- **3-Layer**: Mid-level coordination (Chief → ModuleLead → TaskAgents)
- **4-Layer**: Full hierarchy (Chief → ComponentLead → ModuleLead → TaskAgents)

### Code Structure

Each example follows this pattern:

```cpp
#include <agents/...>
#include <core/message_bus.hpp>

int main() {
    // 1. Setup: Create agents and message bus
    auto bus = std::make_shared<MessageBus>();
    auto chief = std::make_shared<ChiefArchitectAgent>("chief", bus);
    auto task = std::make_shared<TaskAgent>("task1", bus);

    // 2. Register agents with message bus
    bus->registerAgent(chief->getAgentId(), chief);
    bus->registerAgent(task->getAgentId(), task);

    // 3. Send initial goal from chief
    auto goal_msg = KeystoneMessage::create(
        "chief", "task1", "echo 'Hello from HMAS!'"
    );
    chief->sendMessage(goal_msg);

    // 4. Process messages (each agent receives and responds)
    auto task_msg = task->getMessage();
    auto task_response = task->processMessage(*task_msg).get();

    auto chief_msg = chief->getMessage();
    auto chief_response = chief->processMessage(*chief_msg).get();

    // 5. Display results
    std::cout << "Final Result: " << chief_response.result << std::endl;

    return 0;
}
```

---

## Building and Running

### Prerequisites

- **C++20 compiler** (GCC 12+, Clang 14+)
- **CMake** 3.20+
- **Docker** (recommended for consistent builds)
- **gRPC** and **protobuf** (for network-based examples)
- **Boost.Interprocess** (for IPC-based examples)

### Build All Examples

```bash
# Using Docker (recommended)
docker-compose up examples

# Or manually
mkdir -p build && cd build
cmake -G Ninja -DBUILD_EXAMPLES=ON ..
ninja workflow-examples

# Run individual examples
./examples/thread_2layer
./examples/thread_3layer
./examples/thread_4layer
```

### Build Specific Deployment Model

```bash
# Thread-based only
ninja thread-examples

# IPC-based only
ninja ipc-examples

# Network-based only (requires gRPC)
cmake -DENABLE_GRPC=ON ..
ninja network-examples
```

### Running Examples

Each example is a standalone executable:

```bash
# Thread-based
./examples/thread_2layer
# Output:
# [Chief] Sending goal to TaskAgent...
# [Task] Executing: echo 'Hello from HMAS!'
# [Task] Result: Hello from HMAS!
# [Chief] Received result: Hello from HMAS!

# IPC-based (starts separate processes)
./examples/ipc_2layer
# Opens 2 processes communicating via shared memory

# Network-based (requires ServiceRegistry running)
# Terminal 1: Start ServiceRegistry
./examples/service_registry

# Terminal 2: Run example
./examples/network_2layer
```

---

## Architecture Patterns

### 1. Message Flow Patterns

#### Request-Response (2-Layer)

```
Chief --[Goal]--> TaskAgent
      <--[Result]--
```

#### Decomposition-Synthesis (3-Layer)

```
Chief --[Goal]--> ModuleLead
                      |
                      +--[Task1]--> TaskAgent1 --[R1]-->
                      +--[Task2]--> TaskAgent2 --[R2]--> [Synthesize]
                      +--[Task3]--> TaskAgent3 --[R3]-->
                      |
      <--[Summary]---+
```

#### Full Hierarchy (4-Layer)

```
Chief --[Goal]--> ComponentLead
                      |
                      +--[Module1]--> ModuleLead1
                      |                   +--[Tasks]--> TaskAgents
                      |                   <--[Results]--
                      |
                      +--[Module2]--> ModuleLead2
                                          +--[Tasks]--> TaskAgents
                                          <--[Results]--
                      |
      <--[Aggregate]--+
```

### 2. State Machine Patterns

#### ModuleLead State Machine

```
IDLE --> PLANNING --> WAITING_FOR_TASKS --> SYNTHESIZING --> IDLE
  ↑                                                            │
  └────────────────────────────────────────────────────────────┘
```

#### ComponentLead State Machine

```
IDLE --> PLANNING --> WAITING_FOR_MODULES --> AGGREGATING --> IDLE
  ↑                                                             │
  └─────────────────────────────────────────────────────────────┘
```

### 3. Concurrency Patterns

#### Thread-Based: Shared Memory

- Lock-free concurrent queues
- Mutex-protected agent registry
- Async coroutine execution

#### IPC-Based: Process Isolation

- Named shared memory segments
- Named mutexes for synchronization
- Process-safe message passing

#### Network-Based: Distributed

- gRPC bidirectional streaming
- Service discovery and registration
- Heartbeat-based health monitoring

---

## Example Use Cases

### Thread-Based Examples

**Use Case**: Real-time data processing pipeline

- Low latency requirements (< 1ms)
- All agents in same process
- Shared memory for efficiency

**Example**: Image processing pipeline

```
Chief: "Process batch of 1000 images"
  ↓
ModuleLead: "Decompose into 10 batches of 100"
  ↓
TaskAgents: Process batches in parallel
  ↓
ModuleLead: Synthesize results
  ↓
Chief: Return aggregated metrics
```

### IPC-Based Examples

**Use Case**: Fault-tolerant service architecture

- Process isolation for stability
- Independent restart capability
- Resource limits per agent

**Example**: Distributed build system

```
Chief Process: "Build project with 100 source files"
  ↓
ModuleLead Process: "Compile 25 files per batch"
  ↓
TaskAgent Processes: Compile source files (isolated crashes)
  ↓
ModuleLead: Collect object files
  ↓
Chief: Link final executable
```

### Network-Based Examples

**Use Case**: Cloud-native microservices

- Geographic distribution
- Dynamic scaling
- Service discovery

**Example**: Distributed ML training

```
Chief (Machine A): "Train model on 1TB dataset"
  ↓
ComponentLead (Machine B): "Coordinate data and compute"
  ↓
ModuleLead1 (Machine C): "Data preprocessing"
  → TaskAgents (Machines D-F): Preprocess data shards
  ↓
ModuleLead2 (Machine G): "Model training"
  → TaskAgents (Machines H-J): Train on data partitions
  ↓
ComponentLead: Aggregate models
  ↓
Chief: Return trained model
```

---

## Performance Characteristics

| Deployment Model | Latency | Throughput | Scalability | Fault Tolerance |
|------------------|---------|------------|-------------|-----------------|
| **Thread-Based** | < 1ms | High | Process-limited | Low |
| **IPC-Based** | 1-10ms | Medium | Machine-limited | Medium |
| **Network-Based** | 10-100ms | Lower | Unlimited | High |

---

## Next Steps

1. **Start with Thread-Based**: Understand basic message flow
2. **Explore IPC-Based**: Learn process isolation patterns
3. **Scale to Network-Based**: Implement distributed coordination

For detailed implementation guides, see the README in each subdirectory:

- [Thread-Based Examples](01-thread-based/README.md)
- [IPC-Based Examples](02-ipc-based/README.md)
- [Network-Based Examples](03-network-based/README.md)

---

## Additional Resources

- [FOUR_LAYER_ARCHITECTURE.md](../../docs/plan/FOUR_LAYER_ARCHITECTURE.md)
- [TDD_FOUR_LAYER_ROADMAP.md](../../docs/plan/TDD_FOUR_LAYER_ROADMAP.md)
- [ADR-001: MessageBus Architecture](../../docs/plan/adr/ADR-001-message-bus-architecture.md)
- [PHASE_8_COMPLETE.md](../../docs/PHASE_8_COMPLETE.md) - gRPC distributed features

---

**Last Updated**: 2025-11-23
**Version**: 1.0
**Project**: ProjectKeystone HMAS Examples
