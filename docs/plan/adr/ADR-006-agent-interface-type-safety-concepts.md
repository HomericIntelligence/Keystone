# ADR-006: Agent Interface Type Safety using C++20 Concepts

**Status**: Accepted
**Date**: 2025-11-21
**Deciders**: ProjectKeystone Development Team
**Tags**: type-safety, concepts, c++20, interface-verification, issue-24

## Context

> **Note (ADR-015)**: The concrete agent types discussed in this ADR
> (`TaskAgent`, `ChiefArchitectAgent`, `ModuleLeadAgent`, `ComponentLeadAgent`) have
> been extracted into **ProjectAgamemnon**. The C++20 Concepts defined here continue to
> apply to agent implementations in ProjectAgamemnon; this ADR is the authoritative
> record for the compile-time interface verification design.

Issue #24 identified that the agent interface has no compile-time verification that agents
implement the required methods. Errors are only caught at link time or runtime when methods
are missing or have incorrect signatures.

### Problems with Current Interface

1. **Late Error Detection**: Missing methods caught at link time, not compile time
2. **Poor Error Messages**: SFINAE-based errors are cryptic and hard to debug
3. **No Interface Documentation**: Required interface is implicit, not explicit
4. **Runtime Failures**: Type mismatches can cause runtime errors
5. **Difficult Generic Code**: Hard to write generic algorithms over agent types

### Current Interface (Implicit)

```cpp
class BaseAgent : public AgentBase {
    virtual Task<Response> processMessage(const KeystoneMessage& msg) = 0;
};

class AgentBase {
    void sendMessage(const KeystoneMessage& msg);
    void receiveMessage(const KeystoneMessage& msg);
    const std::string& getAgentId() const;
    void setMessageBus(MessageBus* bus);
    void setScheduler(WorkStealingScheduler* scheduler);
};
```

**Problem**: Interface requirements are scattered across inheritance hierarchy and not
explicitly documented or verified.

## Decision

Implement **C++20 concepts** to provide compile-time interface verification for agents.

### Design Choices

#### 1. Core Concepts Hierarchy

```cpp
// Basic concepts for individual capabilities
template<typename T>
concept Identifiable = requires(const T& entity) {
    { entity.getAgentId() } -> std::convertible_to<std::string>;
};

template<typename T>
concept MessageSender = requires(T& sender, const core::KeystoneMessage& msg) {
    { sender.sendMessage(msg) } -> std::same_as<void>;
};

template<typename T>
concept MessageReceiver = requires(T& receiver, const core::KeystoneMessage& msg) {
    { receiver.receiveMessage(msg) } -> std::same_as<void>;
};

template<typename T>
concept AsyncMessageHandler = requires(T& handler, const core::KeystoneMessage& msg) {
    { handler.processMessage(msg) } -> std::same_as<concurrency::Task<core::Response>>;
};
```

**Rationale**: Small, composable concepts following Single Responsibility Principle.

#### 2. Composite Agent Concept

```cpp
template<typename T>
concept Agent =
    Identifiable<T> &&
    MessageSender<T> &&
    MessageReceiver<T> &&
    AsyncMessageHandler<T>;
```

**Rationale**: Combines all required capabilities into a single verifiable contract.

#### 3. Integration Concepts

```cpp
template<typename T>
concept SchedulerAware = requires(T& agent, concurrency::WorkStealingScheduler* scheduler) {
    { agent.setScheduler(scheduler) } -> std::same_as<void>;
};

template<typename T>
concept MessageBusAware = requires(T& agent, core::MessageBus* bus) {
    { agent.setMessageBus(bus) } -> std::same_as<void>;
};

template<typename T>
concept IntegratedAgent =
    Agent<T> &&
    SchedulerAware<T> &&
    MessageBusAware<T>;
```

**Rationale**: Separates optional integration from core agent interface.

#### 4. Concept-Based MessageBus Registration

```cpp
class MessageBus {
public:
    // Existing method (backward compatible)
    void registerAgent(const std::string& agent_id, std::shared_ptr<AgentBase> agent);

    // New concept-based method (compile-time verification)
    template<agents::Agent A>
    void registerAgent(std::shared_ptr<A> agent) {
        if (!agent) {
            throw std::runtime_error("MessageBus::registerAgent: null agent pointer");
        }

        std::string agent_id = agent->getAgentId();
        std::shared_ptr<agents::AgentBase> base_agent = agent;
        registerAgent(agent_id, base_agent);
    }
};
```

**Rationale**:

- Type-safe registration with compile-time interface verification
- Backward compatible with existing code
- Automatic agent ID extraction
- Clear error messages for missing methods

## Consequences

### Positive

1. **Compile-Time Errors**: Missing or incorrect methods caught at compile time

   ```cpp
   // Example: Missing processMessage() causes clear compile error
   class BrokenAgent {
       std::string getAgentId() const { return "id"; }
       void sendMessage(const KeystoneMessage& msg) {}
       void receiveMessage(const KeystoneMessage& msg) {}
       // Missing: Task<Response> processMessage(msg)
   };

   auto agent = std::make_shared<BrokenAgent>();
   bus.registerAgent(agent);  // COMPILE ERROR: BrokenAgent doesn't satisfy Agent concept
   ```

2. **Better Error Messages**: Concepts provide clear, readable error messages

   ```
   error: 'registerAgent' requires 'agents::Agent<BrokenAgent>'
   note: concept not satisfied because 'handler.processMessage(msg)'
         does not return 'concurrency::Task<core::Response>'
   ```

3. **Self-Documenting Code**: Concepts explicitly document interface requirements

   ```cpp
   // Clear documentation of what an Agent must provide
   template<typename T>
   concept Agent =
       Identifiable<T> &&
       MessageSender<T> &&
       MessageReceiver<T> &&
       AsyncMessageHandler<T>;
   ```

4. **Generic Algorithms**: Enable writing generic code over agent types

   ```cpp
   template<Agent A>
   void registerAgentGeneric(MessageBus& bus, std::shared_ptr<A> agent) {
       bus.registerAgent(agent);  // Works for any Agent type
   }
   ```

5. **Type Safety**: Prevents accidental use of non-agent types
6. **No Runtime Overhead**: Concepts are compile-time only (zero-cost abstraction)
7. **IDE Support**: Better autocomplete and error highlighting

### Negative

1. **Requires C++20**: Concepts are C++20 only (already required by project)
2. **Learning Curve**: Team needs to understand concepts (minor, concepts are intuitive)
3. **Compilation Time**: May slightly increase compilation time (negligible in practice)

### Neutral

1. **Backward Compatibility**: Old-style registration still works
2. **Opt-In**: New concept-based API is optional

## Alternatives Considered

### Alternative 1: SFINAE (Substitution Failure Is Not An Error)

```cpp
template<typename T>
typename std::enable_if<
    std::is_base_of<AgentBase, T>::value,
    void
>::type registerAgent(std::shared_ptr<T> agent) {
    // ...
}
```

**Rejected**:

- Cryptic error messages
- Difficult to read and maintain
- Doesn't check actual interface, only inheritance
- Error messages don't indicate what's wrong

### Alternative 2: Runtime Type Checking

```cpp
void registerAgent(std::shared_ptr<AgentBase> agent) {
    // Runtime checks
    if (!agent->hasMethod("processMessage")) {
        throw std::runtime_error("Agent missing processMessage");
    }
}
```

**Rejected**:

- Errors caught at runtime, not compile time
- Performance overhead
- Requires reflection or RTTI
- Poor developer experience

### Alternative 3: Static Assertions

```cpp
template<typename T>
void registerAgent(std::shared_ptr<T> agent) {
    static_assert(std::is_base_of<AgentBase, T>::value, "Must inherit AgentBase");
    // ...
}
```

**Rejected**:

- Doesn't verify actual interface
- Less expressive than concepts
- Multiple static_asserts needed for complete verification
- Inferior error messages

## Implementation Details

### File Organization

```
include/agents/concepts.hpp      # Agent concept definitions
include/core/message_bus.hpp     # Concept-based registerAgent template
tests/unit/test_agent_concepts.cpp  # Concept verification tests
```

### Concept Definitions

**Location**: `include/agents/concepts.hpp`

**Contents**:

- `Identifiable` - Basic entity identification
- `MessageSender` - Message sending capability
- `MessageReceiver` - Message receiving capability
- `AsyncMessageHandler` - Async message processing
- `Agent` - Complete agent interface (composite)
- `SchedulerAware` - Scheduler integration
- `MessageBusAware` - Message bus integration
- `IntegratedAgent` - Full integration (composite)
- `AgentPtr` - Shared pointer to agent (convenience)

### Usage Examples

#### Example 1: Register with Compile-Time Verification

```cpp
MessageBus bus;

// Valid agent - compiles
auto task = std::make_shared<TaskAgent>("task");
bus.registerAgent(task);  // OK

// Invalid agent - compile error with clear message
auto invalid = std::make_shared<InvalidAgent>();
bus.registerAgent(invalid);  // ERROR: doesn't satisfy Agent concept
```

#### Example 2: Generic Agent Function

```cpp
template<Agent A>
void setupAgent(MessageBus& bus, WorkStealingScheduler& scheduler,
                std::shared_ptr<A> agent) {
    agent->setMessageBus(&bus);
    agent->setScheduler(&scheduler);
    bus.registerAgent(agent);
}

// Works for any Agent type
setupAgent(bus, scheduler, task_agent);
setupAgent(bus, scheduler, chief_agent);
```

#### Example 3: Concept Verification in Tests

```cpp
TEST(AgentConcepts, TaskAgentSatisfiesAgentConcept) {
    static_assert(Agent<TaskAgent>, "TaskAgent should satisfy Agent concept");
    static_assert(IntegratedAgent<TaskAgent>, "TaskAgent should be IntegratedAgent");
    EXPECT_TRUE(true);
}
```

### Error Message Examples

**Scenario**: Missing `processMessage` method

**Old Error** (without concepts):

```
undefined reference to `vtable for BrokenAgent'
note: virtual function 'processMessage' not defined
```

**New Error** (with concepts):

```
error: no matching function for call to 'MessageBus::registerAgent(std::shared_ptr<BrokenAgent>)'
note: candidate template ignored: constraints not satisfied [with A = BrokenAgent]
note: because 'agents::Agent<BrokenAgent>' evaluated to false
note: because 'agents::AsyncMessageHandler<BrokenAgent>' evaluated to false
note: because 'handler.processMessage(msg)' does not satisfy return type requirement
```

**Benefit**: Clear indication of what's wrong and how to fix it.

## Validation

### Unit Tests

**File**: `tests/unit/test_agent_concepts.cpp`

- ✅ Static assertions for all concrete agent types
- ✅ Verify TaskAgent, ChiefArchitectAgent, ModuleLeadAgent, ComponentLeadAgent satisfy concepts
- ✅ Verify sub-concepts (Identifiable, MessageSender, etc.)
- ✅ Verify integration concepts (SchedulerAware, MessageBusAware)
- ✅ Test concept-based registerAgent with all agent types
- ✅ Test null pointer handling
- ✅ Test duplicate ID handling
- ✅ Test message routing after concept-based registration
- ✅ Document negative test cases (compile-time failures)

### Compile-Time Tests

Negative tests (intentionally commented out to show compile errors):

- Type missing `getAgentId()` → Clear compile error
- Type with wrong `processMessage` return type → Clear compile error
- Type missing `sendMessage()` → Clear compile error

### Integration Tests

- ✅ All existing tests still pass (backward compatibility)
- ✅ New concept-based registration works with all agent types
- ✅ Message routing works identically to old registration

## Future Evolution

### Potential Extensions

1. **Specialized Agent Concepts**

   ```cpp
   template<typename T>
   concept HierarchicalAgent = Agent<T> && requires(T agent) {
       { agent.getLevel() } -> std::convertible_to<int>;
   };
   ```

2. **Message Type Concepts**

   ```cpp
   template<typename T>
   concept Message = requires(const T& msg) {
       { msg.msg_id } -> std::convertible_to<std::string>;
       { msg.sender_id } -> std::convertible_to<std::string>;
       { msg.receiver_id } -> std::convertible_to<std::string>;
   };
   ```

3. **Generic Agent Algorithms**

   ```cpp
   template<Agent A, std::ranges::range R>
   void registerAllAgents(MessageBus& bus, R&& agents) {
       for (auto& agent : agents) {
           bus.registerAgent(agent);
       }
   }
   ```

4. **Concept-Based Factory**

   ```cpp
   template<Agent A, typename... Args>
   std::shared_ptr<A> createAndRegisterAgent(MessageBus& bus, Args&&... args) {
       auto agent = std::make_shared<A>(std::forward<Args>(args)...);
       bus.registerAgent(agent);
       return agent;
   }
   ```

## References

- C++20 Concepts: <https://en.cppreference.com/w/cpp/language/constraints>
- [FOUR_LAYER_ARCHITECTURE.md](../FOUR_LAYER_ARCHITECTURE.md)
- [base_agent.hpp](../../include/agents/base_agent.hpp)
- [message_bus.hpp](../../include/core/message_bus.hpp)
- Issue #24: Agent Interface Type Safety

## Acceptance Criteria

- ✅ concepts.hpp created with all agent concepts
- ✅ MessageBus::registerAgent template method implemented
- ✅ All concrete agent types satisfy Agent concept
- ✅ Unit tests for concept verification
- ✅ Compile-time error tests documented
- ✅ All existing tests pass (backward compatibility)
- ✅ New concept-based registration tested with all agent types
- ✅ ADR documentation complete

## Benefits Summary

| Aspect | Before | After |
|--------|--------|-------|
| Error Detection | Link time / Runtime | Compile time |
| Error Messages | Cryptic linker errors | Clear concept violations |
| Interface Documentation | Implicit (comments) | Explicit (concepts) |
| Generic Code | Difficult (SFINAE) | Easy (concepts) |
| Type Safety | Weak (inheritance only) | Strong (interface verification) |
| Developer Experience | Poor error messages | Clear, actionable errors |
| Performance | N/A | Zero overhead (compile-time only) |

---

**Last Updated**: 2025-11-21
**Version**: 1.0
**Project**: ProjectKeystone HMAS (C++20)
**Issue**: #24 - Agent Interface Type Safety Enhancement
