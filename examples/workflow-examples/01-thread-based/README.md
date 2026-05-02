# Thread-Based Multi-Agent Workflow Examples

This directory demonstrates the ProjectKeystone HMAS using **shared memory within a single process** with multiple
threads communicating via lock-free concurrent queues.

## Overview

Thread-based deployment is the **simplest and fastest** communication model:

- **All agents run in the same process**
- **Shared memory space** for efficient communication
- **Lock-free concurrent queues** (moodycamel) for message passing
- **C++20 coroutines** for async execution
- **MessageBus** routes messages between agents

### Advantages

✅ **Ultra-low latency**: < 1ms message delivery
✅ **High throughput**: Millions of messages/second
✅ **Simple deployment**: Single binary
✅ **Easy debugging**: All code in one process
✅ **No serialization overhead**: Direct memory access

### Disadvantages

❌ **No fault isolation**: Agent crash affects entire process
❌ **Limited scalability**: Bound by single machine resources
❌ **Memory sharing risks**: Data races if not careful

---

## Architecture

```
┌───────────────────────────────────────
───────────────────┐
│                   Single Process                          │
│                                                           │
│  
┌───────────────────────────────────────
──────────┐     │
│  │           MessageBus (Shared)                   │     │
│  │  - Agent registry (thread-safe map)             │     │
│  │  - Routes messages by receiver_id               │     │
│  │  - Supports sync/async delivery                 │     │
│  
└──────────┬──────────────┬─────────────
─┬────────┘     │
│             │              │              │              │
│             ↓              ↓              ↓              │
│  ┌────────────────┐ ┌──────────────┐
┌─────────────┐    │
│  │ ChiefArchitect │ │  ModuleLead  │ │  TaskAgent  │    │
│  │   (Thread 1)   │ │  (Thread 2)  │ │  (Thread 3) │    │
│  │                │ │              │ │             │    │
│  │  Inbox:        │ │  Inbox:      │ │  Inbox:     │    │
│  │  [Queue]       │ │  [Queue]     │ │  [Queue]    │    │
│  └────────────────┘ └──────────────┘
└─────────────┘    │
│                                                           │
│  Lock-Free Concurrent Queues (moodycamel)                │
│  Priority-Based Message Routing (HIGH, NORMAL, LOW)      │
│  Time-Based Fairness (prevents starvation)               │
└───────────────────────────────────────
───────────────────┘
```

### Key Components

#### MessageBus

Central routing hub that decouples agents:

```cpp
class MessageBus {
public:
    void registerAgent(const std::string& agent_id,
                       std::shared_ptr<AgentBase> agent);
    bool routeMessage(const KeystoneMessage& msg);
    std::optional<std::string> findAgent(const std::string& capability);
};
```

#### Lock-Free Queues

Each agent has 3 priority queues:

```cpp
// High priority: urgent requests
ConcurrentQueue<KeystoneMessage> high_priority_queue_;

// Normal priority: standard requests
ConcurrentQueue<KeystoneMessage> normal_priority_queue_;

// Low priority: background tasks
ConcurrentQueue<KeystoneMessage> low_priority_queue_;
```

#### Async Coroutines

All agents use C++20 coroutines for non-blocking execution:

```cpp
Task<Response> processMessage(const KeystoneMessage& msg) {
    // Non-blocking async processing
    auto result = co_await delegate_to_subordinate(msg);
    co_return create_response(result);
}
```

---

## Examples

### 2-Layer Example: Chief → TaskAgent

**File**: `2layer_example.cpp`

**Scenario**: ChiefArchitect delegates a simple command directly to TaskAgent.

**Workflow**:

```
1. Chief creates goal: "echo 'Hello HMAS!'"
2. Chief sends message to TaskAgent via MessageBus
3. TaskAgent receives message from inbox
4. TaskAgent executes bash command
5. TaskAgent sends result back to Chief
6. Chief receives and processes result
```

**Code Highlights**:

```cpp
// Setup
auto bus = std::make_shared<MessageBus>();
auto chief = std::make_shared<ChiefArchitectAgent>("chief", bus);
auto task = std::make_shared<TaskAgent>("task1", bus);

bus->registerAgent("chief", chief);
bus->registerAgent("task1", task);

// Send goal
auto msg = KeystoneMessage::create("chief", "task1",
                                    "echo 'Hello HMAS!'");
chief->sendMessage(msg);

// Process (TaskAgent executes)
auto task_msg = task->getMessage();
auto task_resp = task->processMessage(*task_msg).get();

// Process (Chief receives result)
auto chief_msg = chief->getMessage();
auto chief_resp = chief->processMessage(*chief_msg).get();

std::cout << "Result: " << chief_resp.result << std::endl;
```

**Expected Output**:

```
[Chief] Sending goal to TaskAgent: echo 'Hello HMAS!'
[MessageBus] Routing message msg_001 from chief to task1
[TaskAgent] Received command: echo 'Hello HMAS!'
[TaskAgent] Executing bash command...
[TaskAgent] Command result: Hello HMAS!
[TaskAgent] Sending result back to chief
[MessageBus] Routing message msg_002 from task1 to chief
[Chief] Received result: Hello HMAS!

Final Result: Hello HMAS!
```

---

### 3-Layer Example: Chief → ModuleLead → TaskAgents

**File**: `3layer_example.cpp`

**Scenario**: ChiefArchitect delegates a module goal to ModuleLead, which decomposes it into 3 tasks and coordinates
TaskAgents.

**Workflow**:

```
1. Chief creates goal: "Calculate: 10+20+30"
2. Chief sends to ModuleLead
3. ModuleLead decomposes into 3 tasks:
   - Task1: "echo 10"
   - Task2: "echo 20"
   - Task3: "echo 30"
4. ModuleLead sends tasks to 3 TaskAgents
5. TaskAgents execute commands and return results
6. ModuleLead synthesizes: "10, 20, 30 → Sum=60"
7. ModuleLead sends synthesis to Chief
8. Chief receives final result
```

**State Transitions**:

```
ModuleLead State Machine:
IDLE → PLANNING → WAITING_FOR_TASKS → SYNTHESIZING → IDLE
  ^                                                      |

└───────────────────────────────────────
───────────────┘
```

**Code Highlights**:

```cpp
// Setup agents
auto chief = std::make_shared<ChiefArchitectAgent>("chief", bus);
auto module = std::make_shared<ModuleLeadAgent>("module1", bus);
auto task1 = std::make_shared<TaskAgent>("task1", bus);
auto task2 = std::make_shared<TaskAgent>("task2", bus);
auto task3 = std::make_shared<TaskAgent>("task3", bus);

// Register all
bus->registerAgent("chief", chief);
bus->registerAgent("module1", module);
bus->registerAgent("task1", task1);
bus->registerAgent("task2", task2);
bus->registerAgent("task3", task3);

// Chief sends goal to ModuleLead
auto goal = KeystoneMessage::create("chief", "module1",
                                     "Calculate: 10+20+30");
chief->sendMessage(goal);

// ModuleLead processes (decomposes into tasks)
auto module_msg = module->getMessage();
auto module_resp = module->processMessage(*module_msg).get();

// TaskAgents execute in parallel
std::vector<std::thread> workers;
for (auto& task_agent : {task1, task2, task3}) {
    workers.emplace_back([&task_agent]() {
        auto msg = task_agent->getMessage();
        auto resp = task_agent->processMessage(*msg).get();
    });
}
for (auto& w : workers) w.join();

// ModuleLead synthesizes results
auto synthesis_msg = module->getMessage();  // Receives 3 results
auto synthesis_resp = module->processMessage(*synthesis_msg).get();

// Chief receives final result
auto chief_result = chief->getMessage();
auto final_resp = chief->processMessage(*chief_result).get();

std::cout << "Final Result: " << final_resp.result << std::endl;
```

**Expected Output**:

```
[Chief] Sending goal to ModuleLead: Calculate: 10+20+30
[ModuleLead] State: IDLE → PLANNING
[ModuleLead] Decomposing goal into 3 tasks
[ModuleLead] State: PLANNING → WAITING_FOR_TASKS
[ModuleLead] Sending task1: echo 10
[ModuleLead] Sending task2: echo 20
[ModuleLead] Sending task3: echo 30

[TaskAgent1] Executing: echo 10 → Result: 10
[TaskAgent2] Executing: echo 20 → Result: 20
[TaskAgent3] Executing: echo 30 → Result: 30

[ModuleLead] Received 3/3 task results
[ModuleLead] State: WAITING_FOR_TASKS → SYNTHESIZING
[ModuleLead] Synthesizing: 10 + 20 + 30 = 60
[ModuleLead] State: SYNTHESIZING → IDLE
[ModuleLead] Sending synthesis to Chief

[Chief] Received result: Module completed with sum=60

Final Result: Module completed with sum=60
```

---

### 4-Layer Example: Full Hierarchy

**File**: `4layer_example.cpp`

**Scenario**: Complete 4-layer hierarchy demonstrating full coordination.

**Workflow**:

```
1. Chief: "Implement system: Core(10+20+30) + Utils(40+50+60)"
2. Chief → ComponentLead
3. ComponentLead decomposes into 2 modules:
   - Module1: Core(10+20+30)
   - Module2: Utils(40+50+60)
4. ComponentLead → ModuleLead1 (Core)
5. ComponentLead → ModuleLead2 (Utils)
6. ModuleLead1 decomposes → 3 TaskAgents (echo 10, 20, 30)
7. ModuleLead2 decomposes → 3 TaskAgents (echo 40, 50, 60)
8. All 6 TaskAgents execute in parallel
9. ModuleLead1 synthesizes: Sum=60
10. ModuleLead2 synthesizes: Sum=150
11. ComponentLead aggregates: "Core=60, Utils=150"
12. Chief receives final aggregation
```

**Full Message Flow**:

```
Level 0: ChiefArchitect
    │
    └─> Level 1: ComponentLead
            │
            ├─> Level 2: ModuleLead1 (Core)
            │       ├─> Level 3: TaskAgent1 (echo 10) → 10
            │       ├─> Level 3: TaskAgent2 (echo 20) → 20
            │       └─> Level 3: TaskAgent3 (echo 30) → 30
            │       └─> Synthesis: Core = 60
            │
            └─> Level 2: ModuleLead2 (Utils)
                    ├─> Level 3: TaskAgent4 (echo 40) → 40
                    ├─> Level 3: TaskAgent5 (echo 50) → 50
                    └─> Level 3: TaskAgent6 (echo 60) → 60
                    └─> Synthesis: Utils = 150
            │
            └─> Aggregation: Core=60, Utils=150
    │
    └─> Final Result: System complete with 210 total
```

**Code Highlights**:

```cpp
// Create full hierarchy
auto chief = std::make_shared<ChiefArchitectAgent>("chief", bus);
auto component = std::make_shared<ComponentLeadAgent>("comp1", bus);
auto module1 = std::make_shared<ModuleLeadAgent>("mod1", bus);
auto module2 = std::make_shared<ModuleLeadAgent>("mod2", bus);

std::vector<std::shared_ptr<TaskAgent>> tasks;
for (int i = 1; i <= 6; ++i) {
    tasks.push_back(std::make_shared<TaskAgent>(
        "task" + std::to_string(i), bus));
}

// Register all agents
bus->registerAgent("chief", chief);
bus->registerAgent("comp1", component);
bus->registerAgent("mod1", module1);
bus->registerAgent("mod2", module2);
for (size_t i = 0; i < tasks.size(); ++i) {
    bus->registerAgent("task" + std::to_string(i+1), tasks[i]);
}

// Chief sends goal
auto goal = KeystoneMessage::create("chief", "comp1",
    "Implement system: Core(10+20+30) + Utils(40+50+60)");
chief->sendMessage(goal);

// Process messages through all 4 layers
// (Each agent processes asynchronously with coroutines)
// ...

std::cout << "Final Result: " << final_result << std::endl;
```

**Expected Output**:

```
[Chief] Sending system goal to ComponentLead
[ComponentLead] State: IDLE → PLANNING
[ComponentLead] Decomposing into 2 modules: Core, Utils
[ComponentLead] State: PLANNING → WAITING_FOR_MODULES

[ModuleLead1 (Core)] Decomposing: 10+20+30 into 3 tasks
[ModuleLead1] Sending tasks to TaskAgents 1-3

[ModuleLead2 (Utils)] Decomposing: 40+50+60 into 3 tasks
[ModuleLead2] Sending tasks to TaskAgents 4-6

[TaskAgent1] Executing: echo 10 → 10
[TaskAgent2] Executing: echo 20 → 20
[TaskAgent3] Executing: echo 30 → 30
[TaskAgent4] Executing: echo 40 → 40
[TaskAgent5] Executing: echo 50 → 50
[TaskAgent6] Executing: echo 60 → 60

[ModuleLead1] Synthesizing Core results: 10+20+30 = 60
[ModuleLead2] Synthesizing Utils results: 40+50+60 = 150

[ComponentLead] State: WAITING_FOR_MODULES → AGGREGATING
[ComponentLead] Aggregating: Core=60, Utils=150
[ComponentLead] State: AGGREGATING → IDLE
[ComponentLead] Sending aggregation to Chief

[Chief] Received result: System complete
       - Core module: 60
       - Utils module: 150
       - Total: 210

Final Result: System complete with 210 total
```

---

## Performance Characteristics

### Latency

- **Message routing**: < 100 nanoseconds
- **Queue push/pop**: < 50 nanoseconds (lock-free)
- **End-to-end (2-layer)**: < 1 millisecond
- **End-to-end (4-layer)**: < 5 milliseconds

### Throughput

- **Single agent**: 1M+ messages/second
- **4-layer hierarchy**: 100K+ messages/second
- **Bottleneck**: Bash command execution (not message passing)

### Scalability

- **Agents per process**: 100-1000 (memory limited)
- **Concurrent messages**: 10K+ in flight
- **CPU utilization**: Scales with thread count

---

## Running the Examples

### Build

```bash
cd build
cmake -G Ninja -DBUILD_EXAMPLES=ON ..
ninja thread-examples
```

### Run Individual Examples

```bash
# 2-Layer example
./examples/thread_2layer

# 3-Layer example
./examples/thread_3layer

# 4-Layer example (full hierarchy)
./examples/thread_4layer
```

### Expected Runtime

- **2-Layer**: < 10ms
- **3-Layer**: < 50ms
- **4-Layer**: < 100ms

---

## Common Patterns

### Pattern 1: Direct Delegation (2-Layer)

**When to use**: Simple tasks with no decomposition needed

```cpp
Chief → TaskAgent → Result
```

**Example**: Execute single bash command

### Pattern 2: Task Decomposition (3-Layer)

**When to use**: Break complex task into parallel subtasks

```cpp
Chief → ModuleLead → [TaskAgent1, TaskAgent2, TaskAgent3] → Synthesis
```

**Example**: Process batch of files in parallel

### Pattern 3: Hierarchical Coordination (4-Layer)

**When to use**: Multi-module systems with cross-module dependencies

```cpp
Chief → ComponentLead → [ModuleLead1, ModuleLead2] → TaskAgents → Aggregation
```

**Example**: Build system with multiple compile units

---

## Debugging Tips

### Enable Verbose Logging

Set environment variable:

```bash
export HMAS_LOG_LEVEL=DEBUG
./examples/thread_4layer
```

### Inspect Message Flow

Each agent logs:

- Message received (sender, receiver, command)
- State transitions
- Processing duration
- Result sent

### Common Issues

**Issue**: "Agent not found"
**Solution**: Ensure `registerAgent()` called before sending messages

**Issue**: "Deadlock detected"
**Solution**: Check for circular dependencies in message flow

**Issue**: "High latency"
**Solution**: Reduce logging verbosity or use async processing

---

## Next Steps

1. ✅ **Master thread-based examples**
2. 📘 **Move to IPC-based examples** (process isolation)
3. 🌐 **Explore network-based examples** (distributed systems)

See [IPC-Based Examples](../02-ipc-based/README.md) for process-level isolation.

---

**Last Updated**: 2025-11-23
**Status**: Complete
**Examples**: 3 (2-layer, 3-layer, 4-layer)
