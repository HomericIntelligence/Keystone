# ProjectKeystone Message Protocol Extensions

## Overview

This document describes extensions to the core Keystone Interchange Message (KIM) protocol implemented in
ProjectKeystone HMAS. These extensions add new capabilities while maintaining backward compatibility with the base
protocol.

**Base Protocol**: Keystone Interchange Message (KIM)
**Document Version**: 1.0
**Last Updated**: 2025-11-26

---

## Table of Contents

1. [Task Cancellation Protocol](#task-cancellation-protocol)
2. [Message Priority System](#message-priority-system)
3. [Deadline Scheduling](#deadline-scheduling)
4. [Future Extensions](#future-extensions)

---

## Task Cancellation Protocol

**Status**: ✅ **Implemented** in Phase 1.2 (Issue #52)
**Implementation Date**: 2025-11-26

### Overview

The Task Cancellation Protocol enables parent agents to request cancellation of tasks running in child agents. This is
a **cooperative cancellation** mechanism where agents must explicitly check for cancellation requests.

### Protocol Specification

#### New Action Type

Added to `core::ActionType` enum:

```cpp
enum class ActionType {
    EXECUTE,        // Execute a command or task
    CANCEL_TASK,    // Cancel a running task (NEW)
    DELEGATE,       // Delegate to subordinate
    AGGREGATE,      // Aggregate results
    RESPOND         // Send response
};
```

#### Message Field Extension

Added optional `task_id` field to `KeystoneMessage`:

```cpp
struct KeystoneMessage {
    std::string msg_id;
    std::string sender_id;
    std::string receiver_id;
    core::ActionType action_type;
    std::string command;
    std::optional<std::string> payload;
    std::optional<std::string> task_id;  // NEW: Task identifier for cancellation
    std::chrono::system_clock::time_point timestamp;
    MessagePriority priority;
    // ... other fields
};
```

#### Factory Method

```cpp
// Create a cancellation message
static KeystoneMessage createCancellation(
    const std::string& sender_id,
    const std::string& receiver_id,
    const std::string& task_id
);
```

**Behavior**:

- Sets `action_type` to `CANCEL_TASK`
- Sets `priority` to `HIGH` automatically
- Includes the `task_id` to identify which task to cancel

### Agent Implementation

#### AgentCore Cancellation API

All agents inherit cancellation tracking from `AgentCore`:

```cpp
class AgentCore {
public:
    // Mark a task for cancellation
    void requestCancellation(const std::string& task_id);

    // Check if a task is cancelled
    bool isCancelled(const std::string& task_id) const;

    // Clear cancellation flag (after handling)
    void clearCancellation(const std::string& task_id);

private:
    mutable std::mutex cancellation_mutex_;
    std::unordered_set<std::string> cancelled_tasks_;
};
```

**Thread Safety**: All methods use mutex protection for concurrent access.

#### AsyncAgent Helper

```cpp
class AsyncAgent : public AgentCore {
protected:
    // Handle cancellation message and return acknowledgement
    core::Response handleCancellation(const core::KeystoneMessage& msg);
};
```

**Implementation**:

1. Extracts `task_id` from message
2. Calls `requestCancellation(task_id)`
3. Returns success response with acknowledgement
4. Sends response message back to sender via MessageBus

### Message Flow

```
┌─────────────────┐  
┌─────────────────┐
│  Parent Agent   │                          │  Child Agent    │
│ (ChiefArchitect)│                          │   (TaskAgent)   │
└────────┬────────┘  
└────────┬────────┘
         │                                            │
         │  1. Create cancellation message            │
         │  msg = createCancellation(...)             │
         │                                            │
         │  2. Send via MessageBus                    │
         ├───────────── CANCEL_TASK ──────────────>  │
         │  (ActionType::CANCEL_TASK)                 │
         │  (Priority: HIGH)                          │
         │  (task_id: "task_12345")                   │
         │                                            │
         │                      3. Check action type  │
         │                      if CANCEL_TASK:       │
         │                         handleCancellation()│
         │                         requestCancellation()│
         │                                            │
         │  4. Send acknowledgement                   │
         │  <───────── ACK (Response) ────────────── │
         │  "Task task_12345 marked for              │
         │   cancellation"                            │
         │                                            │
         │                      5. Agent checks       │
         │                      if (isCancelled(id))  │
         │                         stop work          │
         │                         cleanup            │
         │                         clearCancellation()│
         │                                            │
```

### Usage Example

#### Sending Cancellation Request

```cpp
// Parent agent (e.g., ChiefArchitect) cancels a task
const std::string task_id = "task_12345";
auto cancel_msg = KeystoneMessage::createCancellation(
    chief->getAgentId(),
    task_agent->getAgentId(),
    task_id
);

chief->sendMessage(cancel_msg);

// Receive acknowledgement
auto ack_msg = chief->getMessage();
if (ack_msg.has_value()) {
    std::cout << "Cancellation acknowledged: "
              << ack_msg->payload.value_or("") << std::endl;
}
```

#### Checking for Cancellation in Long-Running Task

```cpp
concurrency::Task<core::Response> TaskAgent::longRunningTask(
    const core::KeystoneMessage& msg) {

    const std::string& task_id = *msg.task_id;

    // Perform work in chunks
    for (int i = 0; i < 100; ++i) {
        // Check for cancellation before each chunk
        if (isCancelled(task_id)) {
            clearCancellation(task_id);
            co_return core::Response::createError(
                msg, agent_id_, "Task cancelled by request");
        }

        // Do work
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    co_return core::Response::createSuccess(msg, agent_id_, "Completed");
}
```

### Design Decisions

#### Cooperative Cancellation

**Choice**: Cooperative cancellation (agent must check)
**Rationale**:

- Safer than preemptive termination
- Allows proper cleanup (RAII, resource release)
- Predictable behavior
- No risk of corrupting shared state

**Trade-off**: Agents that don't check will not cancel

#### High Priority

**Choice**: Cancellation messages have HIGH priority
**Rationale**:

- Ensures timely processing under load
- Prevents delays in cancellation response
- User expects cancellation to be fast

#### Optional task_id Field

**Choice**: `task_id` is optional in KeystoneMessage
**Rationale**:

- Backward compatible with existing messages
- Only required for cancellation
- Doesn't add overhead to other message types

### Testing

#### Unit Tests (10 tests)

Located in `tests/unit/test_task_cancellation.cpp`:

- Message creation and serialization
- ActionType conversion
- Single and multiple task tracking
- MessageBus routing
- Priority verification
- Error handling (missing task_id)
- Thread safety (concurrent requests)

#### E2E Tests (6 tests)

Located in `tests/e2e/task_cancellation_test.cpp`:

- Parent cancels child task
- Cancel specific task among multiple
- Priority processing verification
- Agent checks during work
- Task ID reuse after clearing
- Malformed message handling

**Test Pass Rate**: 16/16 tests passing (100%)

### Error Handling

#### Missing task_id

```cpp
if (!msg.task_id.has_value()) {
    return core::Response::createError(
        msg, agent_id_, "CANCEL_TASK message missing task_id");
}
```

#### Non-existent task_id

No error - cancellation is idempotent. Cancelling a non-existent or already-completed task is a no-op.

### Backward Compatibility

✅ **Fully backward compatible**:

- Existing messages work without `task_id`
- `task_id` field is optional
- New `CANCEL_TASK` action type is additive
- Existing agent implementations unchanged

### Performance Considerations

**Overhead**:

- Cancellation check: O(1) hash lookup with mutex lock (~100ns)
- Memory: Minimal (only stores task IDs of cancelled tasks)

**Recommendation**: Check for cancellation at logical breakpoints (e.g., loop iterations, between sub-tasks)

### Future Enhancements

1. **Cascading Cancellation**: Automatically cancel child tasks when parent is cancelled
2. **Timeout-based Cancellation**: Auto-cancel tasks exceeding deadlines
3. **Cancellation Callbacks**: Register cleanup callbacks
4. **Cancellation Reasons**: Add optional reason field
5. **Metrics**: Track cancellation rates

### Implementation Files

**Modified**:

- `include/core/message.hpp` (+29 lines)
- `src/core/message.cpp` (+19 lines)
- `include/agents/agent_core.hpp` (+42 lines)
- `src/agents/agent_core.cpp` (+16 lines)
- `include/agents/async_agent.hpp` (+13 lines)
- `src/agents/async_agent.cpp` (+17 lines)
- `src/agents/task_agent.cpp` (+15 lines)
- `src/agents/module_lead_agent.cpp` (+13 lines)
- `src/agents/component_lead_agent.cpp` (+13 lines)
- `src/agents/chief_architect_agent.cpp` (+13 lines)

**Created**:

- `tests/unit/test_task_cancellation.cpp` (206 lines)
- `tests/e2e/task_cancellation_test.cpp` (280 lines)

**Total**: 686 lines added

---

## Message Priority System

**Status**: ✅ **Implemented** (Base Protocol)

### Priority Levels

```cpp
enum class MessagePriority {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2,
    CRITICAL = 3
};
```

### Default Priorities

- Regular commands: `NORMAL`
- Cancellation messages: `HIGH`
- System shutdown: `CRITICAL`

### Processing Order

Messages are processed in priority order when multiple messages are queued.

---

## Deadline Scheduling

**Status**: ✅ **Implemented** (Base Protocol)

### Message Deadline Field

```cpp
struct KeystoneMessage {
    std::optional<std::chrono::system_clock::time_point> deadline;
    // ...
};
```

### Deadline Checking

```cpp
bool hasDeadlinePassed() const {
    if (!deadline.has_value()) return false;
    return std::chrono::system_clock::now() > *deadline;
}
```

### Metrics

Missed deadlines are tracked via `core::Metrics::recordDeadlineMiss()`.

---

## Future Extensions

### Planned for Phase 9

1. **Message Encryption**: End-to-end encryption for sensitive payloads
2. **Message Signing**: Cryptographic signatures for authentication
3. **Compression**: Optional payload compression for large messages
4. **Batching**: Combine multiple small messages for efficiency
5. **Streaming**: Long-running operations with progress updates

### Under Consideration

1. **Transactional Messages**: Atomic multi-message operations
2. **Causal Ordering**: Preserve causality across distributed agents
3. **Message TTL**: Auto-expire old messages
4. **Retry Policies**: Automatic retry on transient failures

---

## References

- [GLOSSARY.md](./GLOSSARY.md) - KIM base protocol definition
- [NETWORK_PROTOCOL.md](./NETWORK_PROTOCOL.md) - gRPC network layer
- Issue #52 - Task Cancellation Implementation
- [ADR-001](./plan/adr/ADR-001-message-bus-architecture.md) - MessageBus architecture

---

**Document Version**: 1.0
**Last Updated**: 2025-11-26
**Status**: ACTIVE
