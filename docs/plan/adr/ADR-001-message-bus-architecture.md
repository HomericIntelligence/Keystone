# ADR-001: MessageBus Architecture

**Status**: Accepted
**Date**: 2025-11-18
**Deciders**: ProjectKeystone Development Team
**Tags**: architecture, messaging, phase1

## Context

Phase 1 of ProjectKeystone initially implemented direct agent-to-agent communication via
raw pointers (`sendMessage(msg, BaseAgent* target)`). Code review identified this as a
critical architectural debt that would not scale to the 4-layer hierarchy (L0→L1→L2→L3)
and 100+ agent deployments planned for Phases 2-5.

> **Note (ADR-015)**: The 4-layer HMAS hierarchy (L0 `ChiefArchitectAgent`,
> L1 `ComponentLeadAgent`, L2 `ModuleLeadAgent`, L3 `TaskAgent`) has since been
> extracted from ProjectKeystone into **ProjectAgamemnon** (see ADR-015). This ADR
> documents the original MessageBus design decision, which still applies to
> Keystone's transport primitives. References to agent types below are historical.

### Problems with Direct Coupling

1. **Tight Coupling**: Agents require direct pointer references to communicate
2. **No Dynamic Discovery**: Cannot add/remove agents at runtime
3. **Testing Complexity**: Difficult to mock or substitute agents
4. **No Message Routing**: Manual routing logic scattered across agents
5. **Not Scalable**: Cannot support async messaging, remote agents, or distributed deployments
6. **Thread Safety**: Direct pointer access without synchronization

## Decision

Implement **MessageBus** as the central routing hub for all agent communication.

### Design Choices

#### 1. Synchronous Routing (Phase 1)

```cpp
bool MessageBus::routeMessage(const KeystoneMessage& msg) {
    std::lock_guard<std::mutex> lock(registry_mutex_);

    auto it = agents_.find(msg.receiver_id);
    if (it == agents_.end()) {
        return false;  // Receiver not found
    }

    // Synchronous delivery for Phase 1
    it->second->receiveMessage(msg);
    return true;
}
```

**Rationale**: Simplifies Phase 1 implementation while providing clear evolution path to async in Phase 2.

#### 2. Agent Registry

```cpp
class MessageBus {
private:
    mutable std::mutex registry_mutex_;
    std::unordered_map<std::string, BaseAgent*> agents_;
};
```

**Rationale**:

- Thread-safe registration/unregistration
- O(1) lookup by agent ID
- Allows dynamic agent management
- Non-owning pointers (agents manage own lifetime)

#### 3. Automatic Response Routing

Agents send to `sender_id`, not direct pointers:

```cpp
// TaskAgent sends response
auto response_msg = KeystoneMessage::create(
    agent_id_,
    msg.sender_id,  // Route back to original sender
    "response",
    result
);
sendMessage(response_msg);  // MessageBus routes it
```

**Rationale**: Decouples agents from each other's lifecycle and location.

#### 4. Interface Design

```cpp
class MessageBus {
public:
    void registerAgent(const std::string& agent_id, BaseAgent* agent);
    void unregisterAgent(const std::string& agent_id);
    bool routeMessage(const KeystoneMessage& msg);
    bool hasAgent(const std::string& agent_id) const;
    std::vector<std::string> listAgents() const;
};
```

**Rationale**: Minimal API surface, clear responsibilities, testable.

## Consequences

### Positive

1. **Decoupling**: Agents no longer need direct references to each other
2. **Dynamic Discovery**: Agents can be registered/unregistered at runtime
3. **Clear Evolution Path**:
   - Phase 2: Add async message queue (std::queue + worker threads)
   - Phase 3: Add priority routing, message filtering
   - Phase 4: Add distributed MessageBus (network transport)
4. **Testability**: Easy to inject mock MessageBus for testing
5. **Scalability**: Supports 100+ agents without modification
6. **Thread Safety**: Mutex-protected registry ensures safe concurrent access
7. **Separation of Concerns**: Routing logic centralized, not scattered

### Negative

1. **Extra Indirection**: One additional pointer hop per message (negligible performance impact)
2. **Lifetime Management**: Agents must outlive their MessageBus registration
3. **Additional Dependency**: BaseAgent now depends on MessageBus

### Migration Path

**Breaking Changes**:

- `BaseAgent::sendMessage(msg, target)` → `BaseAgent::sendMessage(msg)`
- `ChiefArchitectAgent::sendCommand(cmd, agent*)` → `sendCommand(cmd, agent_id)`
- All tests updated to create and configure MessageBus

**Migration Steps**:

1. ✅ Create MessageBus header and implementation
2. ✅ Update BaseAgent to use MessageBus
3. ✅ Remove test infrastructure (storeResponse/getStoredResponse)
4. ✅ Update all agent implementations (TaskAgent, ChiefArchitect)
5. ✅ Update all E2E tests
6. ✅ Add unit tests for MessageBus

## Alternatives Considered

### Alternative 1: Keep Direct Pointer Coupling

**Rejected**: Does not scale to 4-layer hierarchy, prevents async messaging, poor testability.

### Alternative 2: Observer Pattern

```cpp
class MessageBroker {
    void subscribe(const std::string& topic, BaseAgent* agent);
    void publish(const std::string& topic, const KeystoneMessage& msg);
};
```

**Rejected**: Overly complex for Phase 1 needs, topics not required for direct agent-to-agent communication.

### Alternative 3: Actor Framework (e.g., CAF)

**Rejected**: Large external dependency, overengineered for Phase 1, learning curve.

## Implementation Details

### Thread Safety

- `std::mutex registry_mutex_` protects agent registry
- `std::lock_guard` ensures exception safety
- Each agent has own inbox mutex for message queue

### Performance

- O(1) message routing (unordered_map lookup)
- Synchronous delivery (no thread overhead in Phase 1)
- Minimal memory overhead (single pointer per agent)

### Error Handling

- Returns `false` if receiver not found (caller decides how to handle)
- Throws `std::runtime_error` if duplicate agent ID registered
- Throws `std::invalid_argument` if null agent registered

## Validation

### Unit Tests

- ✅ Register/unregister agents
- ✅ Route messages to registered agents
- ✅ Handle routing to unregistered agents
- ✅ Thread safety (concurrent registration and routing)
- ✅ Bidirectional message flow

### E2E Tests

- ✅ ChiefArchitect ↔ TaskAgent delegation via MessageBus
- ✅ Multiple sequential commands
- ✅ Error handling and response routing

### Code Review

All critical issues from code review resolved:

- ✅ Thread-unsafe UUID generation → thread_local
- ✅ Resource leak in popen → RAII PipeHandle
- ✅ Direct agent coupling → MessageBus abstraction
- ✅ Response routing → MessageBus automatic routing
- ✅ Test infrastructure → Removed from production code

## Future Evolution

### Phase 2: Async Messaging

```cpp
class MessageBus {
private:
    ThreadPool worker_pool_;
    ConcurrentQueue<KeystoneMessage> message_queue_;

public:
    void routeMessageAsync(const KeystoneMessage& msg);
};
```

### Phase 3: Priority Routing

```cpp
struct KeystoneMessage {
    enum class Priority { Low, Normal, High, Critical };
    Priority priority{Priority::Normal};
};
```

### Phase 4: Distributed MessageBus

```cpp
class DistributedMessageBus : public MessageBus {
public:
    void registerRemoteAgent(const std::string& agent_id, const std::string& host);
    bool routeMessage(const KeystoneMessage& msg) override;
};
```

## References

- [FOUR_LAYER_ARCHITECTURE.md](../FOUR_LAYER_ARCHITECTURE.md)
- [TDD_FOUR_LAYER_ROADMAP.md](../TDD_FOUR_LAYER_ROADMAP.md)
- [error-handling.md](../error-handling.md)

## Acceptance Criteria

- ✅ All code compiles without warnings
- ✅ All E2E tests pass (3/3)
- ✅ All unit tests pass (MessageBus tests)
- ✅ No valgrind errors or leaks
- ✅ No data races (ThreadSanitizer clean)
- ✅ All public APIs have Doxygen comments
- ✅ No direct agent-to-agent coupling
- ✅ All messages routed via MessageBus

---

**Last Updated**: 2025-11-18
**Version**: 1.0
**Project**: ProjectKeystone HMAS (C++20)
