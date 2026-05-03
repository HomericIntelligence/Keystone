# ADR-010: P0 Critical Architecture Issues - RESOLVED ✅

> **Note (ADR-015)**: Agent types referenced in code examples below
> (`ChiefArchitectAgent`, `TaskAgent`, etc.) have been extracted into
> **ProjectAgamemnon**. The transport-layer fixes documented here (MessageBus
> lifetime safety, scheduler thread safety) remain part of ProjectKeystone.

## Overview

This document tracked the P0 (critical) architecture issues identified in the comprehensive code review.

**STATUS: ALL P0 ISSUES RESOLVED** (as of 2025-11-21)

## Status of P0 Fixes

- ✅ **C1**: Data race on `last_low_priority_check_` - **FIXED** (commit: 4c070c3)
- ✅ **C2**: Use-after-free risk in MessageBus - **FIXED** (commit: adef068)
- ✅ **C3**: Dual base class hierarchy - **FIXED** (commit: 07a1ab2)
- ✅ **C4**: Backpressure flag race condition - **FIXED** (commit: 4c070c3)
- ✅ **C5**: Scheduler pointer thread safety - **FIXED** (commit: 4c070c3)

**All critical thread safety and architecture issues have been resolved.**

---

## C2: Use-After-Free Risk in MessageBus (CRITICAL)

### Issue

Raw agent pointers captured in async lambdas without lifetime guarantees.

**Location**: `src/core/message_bus.cpp:63`

```cpp
sched->submit([agent, msg]() {  // DANGER: agent might be deleted!
    agent->receiveMessage(msg);
});
```

### Problem Scenario

1. Thread A captures raw `agent` pointer in lambda
2. Thread B calls `unregisterAgent()` and agent is destroyed
3. Scheduler executes lambda → **segfault/use-after-free**

### Required Fix

**Option 1: Shared Ownership Model** (Recommended)

```cpp
// Change MessageBus API to use shared_ptr
class MessageBus {
public:
    void registerAgent(std::string agent_id, std::shared_ptr<AgentBase> agent);

private:
    std::unordered_map<std::string, std::shared_ptr<AgentBase>> agents_;
};

// Lambda safely holds agent alive
sched->submit([agent, msg]() {  // agent = shared_ptr, kept alive
    agent->receiveMessage(msg);
});
```

**Option 2: Weak Pointer with Validation**

```cpp
// Less intrusive, but requires checking if agent still exists
std::weak_ptr<AgentBase> weak_agent = agents_[msg.receiver_id];

sched->submit([weak_agent, msg]() {
    if (auto agent = weak_agent.lock()) {  // Check if still alive
        agent->receiveMessage(msg);
    }
});
```

### Impact

- **API Breaking Change**: All agent registration code must be updated
- **Memory Model Change**: Agents must be heap-allocated with shared_ptr
- **Test Updates**: All tests creating agents must use shared_ptr

### Effort Estimate

**3-5 days** - Affects ~30 files (all agent creation and registration sites)

### Dependencies

- None (can be done independently)

### Priority

**CRITICAL** - Production systems will crash under concurrent agent lifecycle operations

---

## C3: Dual Base Class Hierarchy Prevents Polymorphism (CRITICAL)

### Issue

Two incompatible agent hierarchies prevent uniform collections and violate LSP.

**Location**: `include/agents/`

```
AgentBase (common base - no processMessage)
├── BaseAgent (sync) → processMessage() -> Response
└── AsyncBaseAgent (async) → processMessage() -> Task<Response>
```

### Problem

- Cannot store agents in `std::vector<AgentBase*>` (no unified interface)
- Sync returns `Response`, async returns `Task<Response>` → not polymorphic
- Violates Liskov Substitution Principle
- MessageBus must handle both types separately (currently uses raw BaseAgent*)

### Required Fix

**Strategy Pattern for Execution Model**

```cpp
// 1. Unified interface
class Agent {
public:
    virtual void handleMessage(const KeystoneMessage& msg) = 0;
    virtual ~Agent() = default;

protected:
    std::string agent_id_;
    MessageBus* message_bus_;
};

// 2. Execution strategies (injected)
class MessageProcessor {
public:
    virtual void process(Agent* agent, const KeystoneMessage& msg) = 0;
};

class SyncProcessor : public MessageProcessor {
    void process(Agent* agent, const KeystoneMessage& msg) override {
        auto response = agent->processSync(msg);
        agent->sendResponse(response);
    }
};

class AsyncProcessor : public MessageProcessor {
    void process(Agent* agent, const KeystoneMessage& msg) override {
        scheduler_->submit([agent, msg]() {
            auto task = agent->processAsync(msg);
            // Handle Task<Response>
        });
    }
};

// 3. Concrete agents choose execution model at construction
class ChiefArchitectAgent : public Agent {
public:
    ChiefArchitectAgent(std::unique_ptr<MessageProcessor> processor)
        : processor_(std::move(processor)) {}

    void handleMessage(const KeystoneMessage& msg) override {
        processor_->process(this, msg);
    }
};
```

### Impact

- **Major Architectural Change**: All agent classes must be refactored
- **API Breaking Change**: Agent construction and usage patterns change
- **Test Updates**: All 329 tests must be updated
- **MessageBus Simplification**: Can treat all agents uniformly

### Benefits

- ✅ Single base class for all agents (uniform collections)
- ✅ Execution model becomes a runtime choice
- ✅ Easy migration: swap sync processor for async
- ✅ Follows Strategy Pattern and Open/Closed Principle

### Effort Estimate

**5-7 days** - Affects ALL agent files (~50 files), MessageBus, and all tests

### Dependencies

- Should be done **after** C2 (shared_ptr migration)
- Can combine with C2 refactoring for efficiency

### Priority

**CRITICAL** - Blocks scalable async migration and violates fundamental OOP principles

---

## Recommended Approach

### Phase 1: Fix C2 (Use-After-Free) - 3-5 days

1. Change MessageBus to use `shared_ptr<AgentBase>`
2. Update all agent creation sites to use `make_shared`
3. Update all tests
4. Verify with ThreadSanitizer

### Phase 2: Fix C3 (Dual Hierarchy) - 5-7 days

1. Extract Strategy Pattern for execution model
2. Create unified `Agent` base class
3. Refactor all agent types (Chief, Component, Module, Task)
4. Update MessageBus to use unified interface
5. Update all 329 tests
6. Verify with full test suite

### Total Effort: 8-12 days

---

## Testing Strategy

### After C2 Fix

- Run all tests with ThreadSanitizer (TSan)
- Add stress test: concurrent agent registration/unregistration + message routing
- Verify no use-after-free with ASan

### After C3 Fix

- Run full test suite (329 tests)
- Add polymorphism tests (uniform agent collections)
- Verify async/sync execution mode switching
- Performance regression tests

---

## Rollback Plan

### If C2 Causes Issues

- Revert to raw pointers
- Document lifetime requirements clearly
- Add assertions in debug builds

### If C3 Causes Issues

- Keep dual hierarchy but document limitations
- Add static analysis to prevent polymorphic usage
- Consider gradual migration (new agents use Strategy, old agents stay)

---

**Created**: 2025-11-20
**Status**: C1, C4, C5 fixed. C2 and C3 require separate refactoring effort.
**Next Steps**: Commit current fixes, then plan C2+C3 refactoring sprint.
