# ADR-009: Message Processing Strategy Pattern

**Status**: Proposed (Design Complete, Implementation Pending)
**Date**: 2025-11-22
**Context**: ARCH-007 - Deferred architectural improvement from code review

## Context and Problem Statement

> **Note (ADR-015)**: The `TaskAgent` and other agent types referenced throughout this
> ADR have been extracted into **ProjectAgamemnon**. This ADR documents a proposed design
> pattern for separating agent domain logic from infrastructure concerns; its
> implementation status applies to ProjectAgamemnon, not to Keystone's transport
> layer.

Agents currently mix domain logic with infrastructure concerns:

- `processMessage()` implementations contain business logic (bash execution, delegation, synthesis)
- Infrastructure concerns (inbox management, routing, metrics, deadlines) are coupled with domain logic
- Hard to test domain logic in isolation without full agent infrastructure
- Violates Single Responsibility Principle

**Example Problem** (TaskAgent):

```cpp
Task<Response> TaskAgent::processMessage(const KeystoneMessage& msg) {
  // Infrastructure: Metrics recording
  Metrics::getInstance().recordMessageProcessed(msg.msg_id);

  // Infrastructure: Deadline checking
  if (msg.hasDeadlinePassed()) { /* ... */ }

  // DOMAIN LOGIC: Bash command execution
  std::string result = executeBash(msg.command);

  // Infrastructure: Message sending
  sendMessage(response_msg);

  co_return response;
}
```

Domain logic (`executeBash`) is buried in infrastructure code, making it hard to:

- Test bash execution logic independently
- Reuse execution logic in different contexts
- Understand what the agent actually *does* vs. how it manages messages

## Decision

Introduce the **Strategy Pattern** to separate domain logic from agent infrastructure.

### Architecture

```
┌─────────────────────────────────────────┐
│          AsyncAgent (Base)              │
│  ┌───────────────────────────────────┐  │
│  │ Infrastructure:                   │  │
│  │ - inbox management                │  │
│  │ - message routing                 │  │
│  │ - priority handling               │  │
│  │ - backpressure                    │  │
│  └───────────────────────────────────┘  │
│                                         │
│  processMessage(msg):                   │
│    1. Record metrics        ────────────┼─ Infrastructure
│    2. Check deadline        ────────────┼─ Infrastructure
│    3. response = strategy.process(msg)  │─ DELEGATION to Strategy
│    4. Send response         ────────────┼─ Infrastructure
│    5. return response                   │
└─────────────────────────────────────────┘
                    │
                    │ delegates to
                    ↓
┌─────────────────────────────────────────┐
│   MessageProcessingStrategy (Interface) │
│                                         │
│   process(msg) -> Task<Response>       │
└─────────────────────────────────────────┘
                    ↑
                    │ implements
        ┌───────────┴───────────┐
        │                       │
┌───────────────────┐  ┌────────────────────┐
│ TaskExecution     │  │ ModuleDelegation   │
│ Strategy          │  │ Strategy           │
│                   │  │                    │
│ - Validate cmd    │  │ - Decompose goal   │
│ - Execute bash    │  │ - Delegate tasks   │
│ - Return result   │  │ - Synthesize       │
└───────────────────┘  └────────────────────┘
```

### Interface Design

```cpp
class MessageProcessingStrategy {
 public:
  virtual ~MessageProcessingStrategy() = default;

  // Pure domain logic - no infrastructure
  virtual Task<Response> process(const KeystoneMessage& msg) = 0;
};
```

**Key Characteristics**:

- No dependencies on agent infrastructure (inbox, routing, metrics)
- Focused solely on business logic
- Easily testable in isolation
- Reusable across different agent types

### Reference Implementation: TaskExecutionStrategy

**Files**:

- `include/agents/message_processing_strategy.hpp` - Base interface
- `include/agents/task_execution_strategy.hpp` - Concrete strategy for TaskAgent
- `src/agents/task_execution_strategy.cpp` - Implementation

**Extracted Domain Logic**:

1. Command validation against security whitelist
2. Bash command execution with RAII PipeHandle
3. Error sanitization
4. Result formatting

**What Stays in TaskAgent** (infrastructure):

- Metrics recording
- Deadline checking
- Message sending via MessageBus
- Coroutine management

## Consequences

### Positive

1. **Separation of Concerns**: Domain logic isolated from infrastructure
2. **Testability**: Can test business logic without full agent setup
3. **Clarity**: Easy to see what an agent *does* vs. how it manages messages
4. **Reusability**: Strategies can be reused across different agents
5. **Flexibility**: Easy to swap processing strategies at runtime
6. **Single Responsibility**: Agents manage infrastructure, strategies implement business logic

### Negative

1. **Additional Abstraction**: One more layer of indirection
2. **Code Organization**: Need to manage strategy files alongside agents
3. **Migration Effort**: Existing agents need refactoring to adopt pattern

### Neutral

1. **Performance**: Negligible overhead (virtual function call + unique_ptr)
2. **Complexity**: Slightly more complex initially, but clearer long-term

## Implementation Status

**Complete**:

- ✅ Strategy interface design (`MessageProcessingStrategy`)
- ✅ Reference implementation (`TaskExecutionStrategy`)
- ✅ Extracted TaskAgent domain logic
- ✅ Documentation (this ADR)

**Pending** (Future Work):

- ⏳ Refactor TaskAgent to use TaskExecutionStrategy
- ⏳ Create tests for TaskExecutionStrategy in isolation
- ⏳ Apply pattern to other agents (ModuleLead, ComponentLead)
- ⏳ Update TaskAgent to delegate to strategy

## Migration Example

**Before** (current TaskAgent):

```cpp
class TaskAgent : public AsyncAgent {
  Task<Response> processMessage(const KeystoneMessage& msg) override {
    // Infrastructure + domain logic mixed
    Metrics::getInstance().recordMessageProcessed(msg.msg_id);
    std::string result = executeBash(msg.command);  // Domain logic
    sendMessage(response_msg);
    co_return response;
  }

  std::string executeBash(const std::string& cmd);  // Domain logic
  void validateCommand(const std::string& cmd);     // Domain logic
};
```

**After** (with Strategy pattern):

```cpp
class TaskAgent : public AsyncAgent {
  std::unique_ptr<MessageProcessingStrategy> strategy_;

  TaskAgent(const std::string& id)
    : AsyncAgent(id),
      strategy_(std::make_unique<TaskExecutionStrategy>()) {}

  Task<Response> processMessage(const KeystoneMessage& msg) override {
    // Infrastructure only
    Metrics::getInstance().recordMessageProcessed(msg.msg_id);
    if (msg.hasDeadlinePassed()) { /* check deadline */ }

    // Delegate domain logic to strategy
    auto response = co_await strategy_->process(msg);

    // Infrastructure: send response
    auto response_msg = KeystoneMessage::create(...);
    sendMessage(response_msg);

    co_return response;
  }
};
```

## Testing Benefits

**Before**: Testing bash execution requires full TaskAgent + MessageBus + metrics

```cpp
TEST(TaskAgentTest, ExecutesBashCommand) {
  auto bus = std::make_shared<MessageBus>();
  auto agent = std::make_shared<TaskAgent>("task");
  bus->registerAgent("task", agent);
  agent->setMessageBus(bus.get());

  // Complex setup just to test bash execution
  // ...
}
```

**After**: Test domain logic in isolation

```cpp
TEST(TaskExecutionStrategyTest, ExecutesBashCommand) {
  TaskExecutionStrategy strategy;
  auto msg = KeystoneMessage::create("sender", "receiver", "echo 42");

  auto response = co_await strategy.process(msg);

  EXPECT_EQ(response.result, "42");
}
```

No MessageBus, no metrics, no agent infrastructure - just domain logic testing.

## Decision Rationale

The Strategy pattern is chosen because:

1. **Clear Responsibility Boundary**: Agents = infrastructure, Strategies = business logic
2. **Established Pattern**: Well-known GoF pattern with proven benefits
3. **Minimal Disruption**: Can be adopted incrementally without breaking changes
4. **Better Than Alternatives**:
   - vs. Extract Function: Strategy is more flexible and testable
   - vs. Template Method: Strategy allows runtime flexibility
   - vs. Command Pattern: Strategy focuses on processing logic, not command objects

## References

- Original Issue: Deferred ARCH-007 from comprehensive code review
- Related: ADR-008 (Async Agent Unification) - complements this by handling async infrastructure
- Pattern: Gang of Four Strategy Pattern

## Approval

**Author**: Claude Code (Architectural Improvement Agent)
**Reviewers**: Pending
**Status**: Design complete, awaiting review before full implementation
