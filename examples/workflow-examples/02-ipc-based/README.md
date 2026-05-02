# IPC-Based Multi-Agent Workflow Examples

This directory demonstrates the ProjectKeystone HMAS using **inter-process communication (IPC)** with shared memory for
message passing between separate OS processes.

## Overview

IPC-based deployment provides **process isolation** while maintaining high-performance local communication:

- **Each agent runs in a separate OS process**
- **Shared memory segments** for inter-process communication
- **Boost.Interprocess** for named shared memory and synchronization
- **Process-safe message queues** using named mutexes
- **Fault isolation**: Process crash doesn't affect other agents

### Advantages

✅ **Process isolation**: Agent crashes are contained
✅ **Resource limits**: Per-process memory/CPU limits via OS
✅ **Security**: Process-level sandboxing
✅ **Debugging**: Attach debugger to individual processes
✅ **High throughput**: Shared memory avoids serialization
✅ **Fault tolerance**: Agents can restart independently

### Disadvantages

❌ **Higher latency**: ~10-100x slower than threads (1-10ms vs < 1ms)
❌ **Complexity**: Shared memory management and synchronization
❌ **Platform dependencies**: OS-specific shared memory APIs
❌ **Cleanup required**: Shared memory segments must be explicitly removed

---

## Architecture

```
┌──────────────┐  ┌──────────────┐  
┌──────────────┐  ┌──────────────┐
│  Process 1   │  │  Process 2   │  │  Process 3   │  │  Process 4   │
│              │  │              │  │              │  │              │
│  ┌────────┐  │  │ ┌─────────┐  │  │
┌─────────┐  │  │ ┌─────────┐  │
│  │ Chief  │  │  │ │ Module  │  │  │ │  Task1  │  │  │ │  Task2  │  │
│  │ Agent  │  │  │ │  Lead   │  │  │ │  Agent  │  │  │ │  Agent  │  │
│  └────┬───┘  │  │ └────┬────┘  │  │
└────┬────┘  │  │ └────┬────┘  │
│       │      │  │      │       │  │      │       │  │      │       │
└───────┼──────┘  └──────┼───────┘  
└──────┼───────┘  └──────┼───────┘
        │                │                 │                 │

└────────────────┼─────────────────┼────
─────────────┘
                         │                 │
                         ↓                 ↓

┌───────────────────────────────────────
─┐
            │    Shared Memory Segment               │
            │    (Boost.Interprocess)                │
            │                                        │
            │  
┌──────────────────────────────────┐  │
            │  │     IPC MessageBus               │  │
            │  │  - Agent registry                │  │
            │  │  - Named message queues          │  │
            │  │  - Named mutexes/semaphores      │  │
            │  
└──────────────────────────────────┘  │
            │                                        │
            │  ┌─────────┐  ┌─────────┐             │
            │  │ Queue   │  │ Queue   │  ...        │
            │  │ (Chief) │  │ (Task1) │             │
            │  └─────────┘  └─────────┘             │

└───────────────────────────────────────
─┘
```

### Key Components

#### Shared Memory Segment

Created using Boost.Interprocess:

```cpp
#include <boost/interprocess/managed_shared_memory.hpp>
using namespace boost::interprocess;

// Create shared memory segment (first process)
managed_shared_memory segment(
    create_only,
    "HMASSharedMemory",  // Segment name
    65536                // Size: 64KB
);

// Open existing segment (other processes)
managed_shared_memory segment(
    open_only,
    "HMASSharedMemory"
);
```

#### IPC Message Queues

Process-safe queues in shared memory:

```cpp
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/containers/deque.hpp>

// Allocator for shared memory
using ShmemAllocator = scoped_allocator_adaptor<
    allocator<KeystoneMessage, managed_shared_memory::segment_manager>
>;

// Deque in shared memory
using MessageQueue = deque<KeystoneMessage, ShmemAllocator>;

// Create queue in shared memory
MessageQueue* queue = segment.construct<MessageQueue>(
    "agent_inbox"
)(ShmemAllocator(segment.get_segment_manager()));

// Synchronization
interprocess_mutex* mutex = segment.construct<interprocess_mutex>(
    "queue_mutex"
)();
```

#### Named Synchronization Primitives

```cpp
// Named mutex for queue synchronization
named_mutex mtx(open_or_create, "hmas_queue_mutex");

// Named semaphore for signaling
named_semaphore sem(open_or_create, "hmas_sem", 0);

// Named condition variable
named_condition cond(open_or_create, "hmas_cond");
```

#### Process Lifecycle

Each agent process:

1. **Attach** to shared memory segment
2. **Find** its message queue by name
3. **Register** with IPC MessageBus
4. **Process** messages from its queue
5. **Send** messages to other agents' queues
6. **Detach** and cleanup on shutdown

---

## Examples

### 2-Layer Example: Chief Process → Task Process

**File**: `2layer_example.cpp`

**Scenario**: Two separate processes communicate via shared memory.

**Setup**:

```bash
# Terminal 1: Start TaskAgent process
./ipc_2layer_task

# Terminal 2: Start Chief process
./ipc_2layer_chief
```

**Shared Memory Layout**:

```
HMASSharedMemory (64KB)
├── MessageBus Registry
│   ├── "chief" → Process PID 1234
│   └── "task1" → Process PID 1235
├── Message Queues
│   ├── queue_chief (Chief's inbox)
│   └── queue_task1 (Task's inbox)
└── Synchronization
    ├── mutex_chief
    ├── mutex_task1
    └── semaphore_new_message
```

**Workflow**:

```
Process 1 (Chief):
1. Create shared memory segment
2. Create Chief's inbox queue
3. Register with IPC MessageBus
4. Send command to Task
5. Wait for response
6. Process result

Process 2 (Task):
1. Open shared memory segment
2. Create Task's inbox queue
3. Register with IPC MessageBus
4. Wait for command
5. Execute command
6. Send result back to Chief
```

**Code Highlights**:

```cpp
// Process 1: Chief
managed_shared_memory segment(create_only, "HMASSharedMemory", 65536);
auto chief = std::make_shared<ChiefArchitectAgent>("chief");

// Create inbox in shared memory
auto chief_inbox = segment.construct<IPCMessageQueue>("queue_chief")(...);

// Send message (writes to shared memory)
auto msg = KeystoneMessage::create("chief", "task1", "echo 'IPC Hello!'");
send_ipc_message(segment, msg);

// Process 2: Task
managed_shared_memory segment(open_only, "HMASSharedMemory");
auto task = std::make_shared<TaskAgent>("task1");

// Find inbox in shared memory
auto task_inbox = segment.find<IPCMessageQueue>("queue_task1").first;

// Receive message (reads from shared memory)
auto msg = receive_ipc_message(segment, "task1");
auto result = task->processMessage(msg).get();

// Send response back
send_ipc_message(segment, result);
```

---

### 3-Layer Example: Chief → ModuleLead → TaskAgents

**File**: `3layer_example.cpp`

**Scenario**: 5 separate processes (Chief + ModuleLead + 3 TaskAgents).

**Setup**:

```bash
# Terminal 1-3: Start TaskAgent processes
./ipc_3layer_task 1  # TaskAgent1
./ipc_3layer_task 2  # TaskAgent2
./ipc_3layer_task 3  # TaskAgent3

# Terminal 4: Start ModuleLead process
./ipc_3layer_module

# Terminal 5: Start Chief process
./ipc_3layer_chief
```

**Process Architecture**:

```
┌──────────┐  ┌──────────┐  ┌──────────┐  
┌──────────┐  ┌──────────┐
│ Chief    │  │ Module   │  │ Task1    │  │ Task2    │  │ Task3    │
│ Process  │  │ Process  │  │ Process  │  │ Process  │  │ Process  │
│ PID 1000 │  │ PID 1001 │  │ PID 1002 │  │ PID 1003 │  │ PID 1004 │
└────┬─────┘  └────┬─────┘  └────┬─────┘  
└────┬─────┘  └────┬─────┘
     │             │             │             │             │

└─────────────┼─────────────┼───────────
──┼─────────────┘
                   ↓             ↓             ↓

┌───────────────────────────────────────
──────┐
         │      Shared Memory: HMASSharedMemory        │
         │                                             │
         │  Queues: [chief][module1][task1][task2][task3] │
         │  Mutexes: [chief_mtx][module_mtx][task_mtx]... │

└───────────────────────────────────────
──────┘
```

**Workflow**:

1. TaskAgent processes (PID 1002-1004) start first, create queues
2. ModuleLead process (PID 1001) starts, creates queue
3. Chief process (PID 1000) starts, sends goal to ModuleLead
4. ModuleLead decomposes, sends tasks to TaskAgents
5. TaskAgents execute in parallel (separate processes)
6. ModuleLead synthesizes results
7. Chief receives final result

**State Tracking**:

- Each process maintains its own state machine
- State transitions logged to shared memory trace buffer
- Process crashes are detected via PID checking

---

### 4-Layer Example: Full Hierarchy Across Processes

**File**: `4layer_example.cpp`

**Scenario**: 10 separate processes for full 4-layer hierarchy.

**Setup**:

```bash
# Start all 10 processes
./scripts/start_ipc_4layer.sh
```

**Process Layout**:

```
Process Distribution:
├── 1 Chief process
├── 1 ComponentLead process
├── 2 ModuleLead processes
└── 6 TaskAgent processes
─────────────────────────
  10 total processes
```

**Shared Memory Size**: 256KB (larger for 10 agents)

**Message Flow**:

```
Process Flow (by PID):

PID 2000: Chief
    ↓ (writes to queue_comp1)
PID 2001: ComponentLead
    ├─ (writes to queue_mod1)
    │   PID 2002: ModuleLead1
    │       ├─ (writes to queue_task1) PID 2003: Task1
    │       ├─ (writes to queue_task2) PID 2004: Task2
    │       └─ (writes to queue_task3) PID 2005: Task3
    │
    └─ (writes to queue_mod2)
        PID 2006: ModuleLead2
            ├─ (writes to queue_task4) PID 2007: Task4
            ├─ (writes to queue_task5) PID 2008: Task5
            └─ (writes to queue_task6) PID 2009: Task6

All communication via shared memory queues
```

**Fault Tolerance**:

```cpp
// Process monitoring
void monitor_processes() {
    for (const auto& [agent_id, pid] : agent_pids) {
        if (!is_process_alive(pid)) {
            log("WARNING: Agent " + agent_id + " crashed (PID " +
                std::to_string(pid) + ")");
            // Restart or failover logic
            restart_agent(agent_id);
        }
    }
}
```

---

## IPC Implementation Details

### Shared Memory Initialization

```cpp
// Cleanup any existing segment first
struct shm_remove {
    shm_remove() { shared_memory_object::remove("HMASSharedMemory"); }
    ~shm_remove() { shared_memory_object::remove("HMASSharedMemory"); }
} remover;

// Create segment
managed_shared_memory segment(
    create_only,
    "HMASSharedMemory",
    65536  // 64KB
);

// Register cleanup handler
std::atexit([]() {
    shared_memory_object::remove("HMASSharedMemory");
});
```

### Process-Safe Message Passing

```cpp
// Send message to another agent
void send_ipc_message(managed_shared_memory& segment,
                       const KeystoneMessage& msg) {
    // Find receiver's queue
    std::string queue_name = "queue_" + msg.receiver_id;
    auto queue = segment.find<IPCMessageQueue>(queue_name.c_str()).first;

    if (!queue) {
        throw std::runtime_error("Queue not found: " + queue_name);
    }

    // Find mutex
    std::string mutex_name = "mutex_" + msg.receiver_id;
    auto mutex = segment.find<interprocess_mutex>(mutex_name.c_str()).first;

    // Lock and push message
    scoped_lock<interprocess_mutex> lock(*mutex);
    queue->push_back(msg);

    // Signal semaphore (wake up receiver)
    named_semaphore sem(open_only, ("sem_" + msg.receiver_id).c_str());
    sem.post();
}

// Receive message (blocking)
KeystoneMessage receive_ipc_message(managed_shared_memory& segment,
                                     const std::string& agent_id) {
    std::string queue_name = "queue_" + agent_id;
    auto queue = segment.find<IPCMessageQueue>(queue_name.c_str()).first;

    std::string sem_name = "sem_" + agent_id;
    named_semaphore sem(open_only, sem_name.c_str());

    // Wait for message
    sem.wait();

    // Lock and pop message
    std::string mutex_name = "mutex_" + agent_id;
    auto mutex = segment.find<interprocess_mutex>(mutex_name.c_str()).first;

    scoped_lock<interprocess_mutex> lock(*mutex);
    KeystoneMessage msg = queue->front();
    queue->pop_front();

    return msg;
}
```

### Agent Registration in Shared Memory

```cpp
// Registry structure in shared memory
struct IPCAgentRegistry {
    struct Entry {
        char agent_id[64];
        pid_t process_id;
        char queue_name[64];
        bool active;
    };

    Entry agents[MAX_AGENTS];
    size_t count;
    interprocess_mutex registry_mutex;
};

// Register agent
void register_agent_ipc(managed_shared_memory& segment,
                         const std::string& agent_id) {
    auto registry = segment.find_or_construct<IPCAgentRegistry>("registry")();

    scoped_lock<interprocess_mutex> lock(registry->registry_mutex);

    if (registry->count >= MAX_AGENTS) {
        throw std::runtime_error("Agent registry full");
    }

    auto& entry = registry->agents[registry->count++];
    strncpy(entry.agent_id, agent_id.c_str(), 63);
    entry.process_id = getpid();
    strncpy(entry.queue_name, ("queue_" + agent_id).c_str(), 63);
    entry.active = true;
}
```

---

## Running the Examples

### Prerequisites

Install Boost.Interprocess:

```bash
# Ubuntu/Debian
sudo apt-get install libboost-dev

# macOS
brew install boost

# Or build from source
wget https://boostorg.jfrog.io/artifactory/main/release/1.82.0/source/boost_1_82_0.tar.gz
tar xzf boost_1_82_0.tar.gz
cd boost_1_82_0
./bootstrap.sh
sudo ./b2 install
```

### Build

```bash
cd build
cmake -G Ninja -DBUILD_EXAMPLES=ON -DENABLE_IPC=ON ..
ninja ipc-examples
```

### Run Examples

**2-Layer**:

```bash
# Terminal 1
./examples/ipc_2layer_task

# Terminal 2 (once task is ready)
./examples/ipc_2layer_chief
```

**3-Layer**:

```bash
# Use helper script
./examples/scripts/run_ipc_3layer.sh
```

**4-Layer**:

```bash
# Use helper script
./examples/scripts/run_ipc_4layer.sh
```

### Cleanup

Remove shared memory segments after use:

```bash
# Linux: Remove shared memory segments
ipcs -m | grep HMASSharedMemory | awk '{print $2}' | xargs -n1 ipcrm -m

# macOS: Automatic cleanup on process exit
# (Named shared memory objects are automatically removed)
```

---

## Performance Characteristics

### Latency

- **Message send/receive**: 1-10 milliseconds
- **Lock acquisition**: 0.1-1 millisecond (interprocess_mutex)
- **End-to-end (2-layer)**: 5-20 milliseconds
- **End-to-end (4-layer)**: 20-100 milliseconds

### Throughput

- **Single queue**: 10K-100K messages/second
- **Multiple processes**: Scales with CPU cores
- **Bottleneck**: Mutex contention for shared queues

### Scalability

- **Processes per machine**: 100-1000
- **Shared memory size**: Limited by OS (typically 64MB-2GB)
- **Message queue depth**: 1000+ messages per queue

---

## Debugging Tips

### Inspect Shared Memory

```bash
# Linux: List shared memory segments
ipcs -m

# macOS: List shared memory
ls -l /dev/shm/

# Dump segment contents
cat /dev/shm/HMASSharedMemory | xxd
```

### Monitor Processes

```bash
# Watch all agent processes
watch -n1 'ps aux | grep ipc_.*layer'

# Monitor CPU/memory usage
htop -p $(pgrep -d, ipc_)
```

### Common Issues

**Issue**: "Shared memory segment already exists"
**Solution**: Clean up old segments with `ipcrm -m <id>`

**Issue**: "Permission denied when accessing segment"
**Solution**: Ensure all processes run as same user

**Issue**: "Process hangs waiting for message"
**Solution**: Check semaphore state with `ipcs -s`, ensure sender is running

---

## Advantages Over Thread-Based

1. **Fault Isolation**: TaskAgent crash doesn't kill Chief
2. **Resource Limits**: OS can limit per-process memory
3. **Security**: Sandboxing possible with seccomp/AppArmor
4. **Debugging**: Can attach debugger to single agent process
5. **Profiling**: Per-process CPU/memory profiling

## Disadvantages vs Thread-Based

1. **Latency**: 10-100x higher (ms vs μs)
2. **Complexity**: Shared memory management is error-prone
3. **Cleanup**: Manual cleanup of shared segments required
4. **Platform-specific**: Different APIs on Linux/macOS/Windows

---

## Next Steps

1. ✅ **Master thread-based examples**
2. ✅ **Master IPC-based examples** (process isolation)
3. 🌐 **Explore network-based examples** (distributed systems)

See [Network-Based Examples](../03-network-based/README.md) for multi-machine deployment.

---

**Last Updated**: 2025-11-23
**Status**: Complete
**Examples**: 3 (2-layer, 3-layer, 4-layer)
**Platform Support**: Linux (primary), macOS (experimental), Windows (not tested)
