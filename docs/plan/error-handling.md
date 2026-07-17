# Error Handling Policy

## Overview

This document defines the error handling strategy for Keystone HMAS. Consistent error handling ensures
reliability, debuggability, and maintainability across all 4 layers of the agent hierarchy.

## Exceptions vs Return Values

### Use Exceptions For

- **Programming errors**: Null pointers, invalid state, contract violations
- **Resource failures**: File I/O errors, network errors, memory allocation failures
- **Unexpected system errors**: Operating system errors, library errors
- **Unrecoverable errors**: Errors that cannot be handled locally

**Example**:

```cpp
void MessageBus::registerAgent(const std::string& agent_id, BaseAgent* agent) {
    if (!agent) {
        throw std::invalid_argument("Cannot register null agent");
    }
    // ...
}
```

### Use Response::Status For

- **Expected failures**: Command execution errors, validation failures
- **Business logic errors**: Task completion failures, workflow errors
- **User-facing errors**: Errors that should be reported to users/agents
- **Recoverable errors**: Errors that can be handled by the caller

**Example**:

```cpp
core::Response TaskAgent::processMessage(const core::KeystoneMessage& msg) {
    try {
        std::string result = executeBash(msg.command);
        return core::Response::createSuccess(msg, agent_id_, result);
    } catch (const std::exception& e) {
        return core::Response::createError(msg, agent_id_, e.what());
    }
}
```

## Exception Safety Guarantees

All code must provide at least **basic exception safety**:

- No resource leaks
- Objects remain in valid (though possibly changed) state after exception
- Invariants are maintained

Critical paths should provide **strong exception safety**:

- Rollback changes on exception
- State unchanged if operation fails (commit-or-rollback semantics)

**Example of strong exception safety**:

```cpp
void MessageBus::registerAgent(const std::string& agent_id, BaseAgent* agent) {
    std::lock_guard<std::mutex> lock(registry_mutex_);

    // Check before modifying state
    if (agents_.find(agent_id) != agents_.end()) {
        throw std::runtime_error("Agent ID already registered: " + agent_id);
    }

    // Only modify state if check passes
    agents_[agent_id] = agent;
}
```

## RAII Requirements

All resources must use RAII (Resource Acquisition Is Initialization):

### File Handles

```cpp
struct PipeDeleter {
    void operator()(FILE* pipe) const {
        if (pipe) pclose(pipe);
    }
};
using PipeHandle = std::unique_ptr<FILE, PipeDeleter>;

// Usage
PipeHandle pipe(popen(command.c_str(), "r"));
// Automatically closed on exception or return
```

### Locks

```cpp
std::lock_guard<std::mutex> lock(mutex_);
// Automatically released on exception or return
```

### Memory

```cpp
auto agent = std::make_unique<TaskAgent>("task_1");
// Automatically freed on exception or return
```

## Error Propagation

### Layer-by-Layer Strategy

**Level 3 (TaskAgent)**:

- Catch system/OS errors
- Convert to Response::Status::Error
- Send error responses via MessageBus

**Level 2 (ModuleLead)** *(Phase 2+)*:

- Aggregate errors from multiple tasks
- Decide on retry/fallback strategy
- Report synthesized results upstream

**Level 1 (ComponentLead)** *(Phase 3+)*:

- Handle module-level failures
- Coordinate recovery across modules
- Report component-level status

**Level 0 (ChiefArchitect)**:

- Receive final results
- Make strategic decisions
- No exceptions should escape to user

### Error Context

Always include context in error messages:

```cpp
// BAD
throw std::runtime_error("Failed");

// GOOD
throw std::runtime_error(
    "Failed to execute command '" + command + "': " +
    std::string(strerror(errno))
);
```

## Defensive Programming

### Validate Inputs

```cpp
core::Response ChiefArchitectAgent::processMessage(const core::KeystoneMessage& msg) {
    // Defensive: check payload exists before dereferencing
    if (!msg.payload) {
        return core::Response::createError(msg, agent_id_, "Missing payload in message");
    }

    return core::Response::createSuccess(msg, agent_id_, *msg.payload);
}
```

### Check Preconditions

```cpp
void BaseAgent::sendMessage(const core::KeystoneMessage& msg) {
    if (!message_bus_) {
        throw std::runtime_error("Message bus not set for agent: " + agent_id_);
    }

    message_bus_->routeMessage(msg);
}
```

## Logging and Diagnostics

### Error Logging (Future)

```cpp
// Phase 2+: Add structured logging
LOG_ERROR("TaskAgent", "Command execution failed")
    .field("agent_id", agent_id_)
    .field("command", command)
    .field("error", e.what());
```

### Debugging Information

- Include agent ID in all error messages
- Include message ID for traceability
- Include command/payload context

## Testing Error Paths

All error paths must be tested:

```cpp
TEST(E2E_Phase1, TaskAgentHandlesCommandErrors) {
    // Test invalid command
    std::string invalid_command = "this_command_does_not_exist";

    // Verify error response
    auto response = task_agent->processMessage(msg);
    EXPECT_EQ(response.status, Response::Status::Error);
    EXPECT_FALSE(response.result.empty());
}
```

## Common Pitfalls to Avoid

### ❌ DON'T

```cpp
// Don't use raw pointers for ownership
FILE* pipe = popen(cmd.c_str(), "r");
// ... exception might be thrown
pclose(pipe);  // Never reached!

// Don't ignore errors
pclose(pipe);  // What if this fails?

// Don't throw generic exceptions
throw std::exception();  // What went wrong?

// Don't catch and discard
try {
    doSomething();
} catch (...) {
    // Silently discarded!
}
```

### ✅ DO

```cpp
// Use RAII for resources
PipeHandle pipe(popen(cmd.c_str(), "r"));
// Automatically cleaned up

// Check and report errors
int status = pclose(pipe.release());
if (status != 0) {
    throw std::runtime_error("Command failed with status " + std::to_string(status));
}

// Throw specific exceptions
throw std::invalid_argument("Agent ID cannot be empty");

// Catch, log, and re-throw or convert
try {
    doSomething();
} catch (const std::exception& e) {
    LOG_ERROR("Operation failed: " << e.what());
    return Response::createError(msg, agent_id_, e.what());
}
```

## Summary

| Error Type | Handling Strategy | Example |
|------------|-------------------|---------|
| Programming errors | Throw exceptions | Null pointer, invalid state |
| Resource failures | Throw exceptions | File open failed, malloc failed |
| Command failures | Return Response::Error | Bash command non-zero exit |
| Validation failures | Return Response::Error | Missing payload, invalid format |
| Recoverable errors | Return Response::Error | Task timeout, retry available |

**Golden Rule**: If the caller can reasonably handle it, use Response::Status. If it's a programming error or
unrecoverable, throw an exception.

---

**Last Updated**: 2025-11-18
**Version**: 1.0
**Project**: Keystone HMAS (C++20)
