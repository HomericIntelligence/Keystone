# C2/C3 Architecture Migration Guide

## Overview
This migration fixes two critical P0 architecture issues by:
1. **C2**: Changing MessageBus to use `shared_ptr<BaseAgent>` (prevents use-after-free)
2. **C3**: Making BaseAgent async by default (eliminates dual hierarchy)

## What Changed

### 1. BaseAgent is Now Async By Default
**Before (dual hierarchy)**:
```cpp
class BaseAgent : public AgentBase {
    virtual Response processMessage(const KeystoneMessage& msg) = 0;  // Sync
};

class AsyncBaseAgent : public AgentBase {
    virtual Task<Response> processMessage(const KeystoneMessage& msg) = 0;  // Async
};
```

**After (unified hierarchy)**:
```cpp
class BaseAgent : public AgentBase {
    // FIX C3: Single async base class for all agents
    virtual Task<Response> processMessage(const KeystoneMessage& msg) = 0;  // Async
};
// AsyncBaseAgent deleted - no longer needed
```

### 2. MessageBus Uses shared_ptr for Lifetime Safety
**Before (use-after-free risk)**:
```cpp
class MessageBus {
    void registerAgent(const std::string& id, AgentBase* agent);  // Raw pointer

private:
    std::unordered_map<std::string, AgentBase*> agents_;  // Raw pointers
};

// Async routing captured raw pointer (DANGER!)
sched->submit([agent, msg]() {  // agent might be deleted!
    agent->receiveMessage(msg);
});
```

**After (safe with shared_ptr)**:
```cpp
class MessageBus {
    // FIX C2: shared_ptr for safe lifetime management
    void registerAgent(const std::string& id, std::shared_ptr<AgentBase> agent);

private:
    std::unordered_map<std::string, std::shared_ptr<AgentBase>> agents_;
};

// Async routing keeps agent alive (SAFE!)
sched->submit([agent, msg]() {  // agent kept alive by shared_ptr
    agent->receiveMessage(msg);
});
```

## Migration Steps for Agents

### Pattern: Converting Sync Agent to Async

**Step 1**: Update header file signature
```cpp
// Before
class MyAgent : public BaseAgent {
    Response processMessage(const KeystoneMessage& msg) override;
};

// After
class MyAgent : public BaseAgent {
    Task<Response> processMessage(const KeystoneMessage& msg) override;  // Returns Task
};
```

**Step 2**: Update implementation to use coroutine
```cpp
// Before
Response MyAgent::processMessage(const KeystoneMessage& msg) {
    // ... process message ...
    return Response::createSuccess(msg, agent_id_, result);
}

// After
Task<Response> MyAgent::processMessage(const KeystoneMessage& msg) {
    // ... process message (same logic) ...
    co_return Response::createSuccess(msg, agent_id_, result);  // Use co_return
}
```

### Example: TaskAgent Migration

**Before** (`src/agents/task_agent.cpp`):
```cpp
Response TaskAgent::processMessage(const KeystoneMessage& msg) {
    try {
        std::string result = executeBash(msg.command);
        return Response::createSuccess(msg, agent_id_, result);
    } catch (const std::exception& e) {
        return Response::createError(msg, agent_id_, e.what());
    }
}
```

**After** (`src/agents/task_agent.cpp`):
```cpp
Task<Response> TaskAgent::processMessage(const KeystoneMessage& msg) {
    try {
        std::string result = executeBash(msg.command);
        co_return Response::createSuccess(msg, agent_id_, result);  // co_return
    } catch (const std::exception& e) {
        co_return Response::createError(msg, agent_id_, e.what());  // co_return
    }
}
```

## Migration Steps for Tests

### Pattern: Using shared_ptr for Agent Creation

**Step 1**: Change agent creation to use `make_shared`
```cpp
// Before
auto agent = std::make_unique<TaskAgent>("agent1");
bus.registerAgent("agent1", agent.get());  // Raw pointer

// After
auto agent = std::make_shared<TaskAgent>("agent1");
bus.registerAgent("agent1", agent);  // shared_ptr
```

**Step 2**: Update test expectations for async behavior
```cpp
// Before (sync)
auto response = agent->processMessage(msg);  // Immediate Response
EXPECT_EQ(response.status, Response::Status::Success);

// After (async)
auto task = agent->processMessage(msg);  // Returns Task<Response>
auto response = task.get();  // Get result from Task
EXPECT_EQ(response.status, Response::Status::Success);
```

### Example: E2E Test Migration

**Before** (`tests/e2e/phase1_basic_delegation.cpp`) - C1 (unique_ptr + raw pointers):
```cpp
TEST(E2E_Phase1, ChiefArchitectDelegatesToTaskAgent) {
    MessageBus bus;

    // Create agents (unique_ptr, raw pointer registration)
    auto chief = std::make_unique<ChiefArchitectAgent>("chief");
    auto task = std::make_unique<TaskAgent>("task");

    bus.registerAgent("chief", chief.get());  // Raw pointer
    bus.registerAgent("task", task.get());

    chief->setMessageBus(&bus);
    task->setMessageBus(&bus);

    // Send message (sync)
    auto msg = KeystoneMessage::create("chief", "task", "echo hello");
    auto response = task->processMessage(msg);  // Sync

    EXPECT_EQ(response.status, Response::Status::Success);
}
```

**After** (`tests/e2e/phase1_basic_delegation.cpp`) - C2/C3 (shared_ptr + async):
```cpp
TEST(E2E_Phase1, ChiefArchitectDelegatesToTaskAgent) {
    MessageBus bus;

    // FIX C2: Create agents with shared_ptr
    auto chief = std::make_shared<ChiefArchitectAgent>("chief");
    auto task = std::make_shared<TaskAgent>("task");

    bus.registerAgent("chief", chief);  // shared_ptr
    bus.registerAgent("task", task);

    chief->setMessageBus(&bus);
    task->setMessageBus(&bus);

    // FIX C3: Send message (async)
    auto msg = KeystoneMessage::create("chief", "task", "echo hello");
    auto task_result = task->processMessage(msg);  // Returns Task<Response>
    auto response = task_result.get();  // Get result from coroutine

    EXPECT_EQ(response.status, Response::Status::Success);
}
```

## Files Requiring Update

### Core Architecture (✅ DONE)
- ✅ `include/agents/base_agent.hpp` - Made async
- ✅ `src/agents/base_agent.cpp` - No changes needed
- ✅ `include/core/message_bus.hpp` - Use shared_ptr
- ✅ `src/core/message_bus.cpp` - Use shared_ptr

### Agents to Migrate (⏳ IN PROGRESS)
- ✅ `include/agents/task_agent.hpp` - Made async
- ✅ `src/agents/task_agent.cpp` - Made async
- ⏳ `include/agents/chief_architect_agent.hpp` - TODO
- ⏳ `src/agents/chief_architect_agent.cpp` - TODO
- ⏳ `include/agents/module_lead_agent.hpp` - TODO
- ⏳ `src/agents/module_lead_agent.cpp` - TODO
- ⏳ `include/agents/component_lead_agent.hpp` - TODO
- ⏳ `src/agents/component_lead_agent.cpp` - TODO

### Files to DELETE (⏳ PENDING)
- ❌ `include/agents/async_base_agent.hpp` - DELETE (replaced by async BaseAgent)
- ❌ `src/agents/async_base_agent.cpp` - DELETE
- ❌ `include/agents/async_task_agent.hpp` - DELETE (merged into TaskAgent)
- ❌ `src/agents/async_task_agent.cpp` - DELETE
- ❌ `include/agents/async_chief_architect_agent.hpp` - DELETE
- ❌ `src/agents/async_chief_architect_agent.cpp` - DELETE
- ❌ `include/agents/async_module_lead_agent.hpp` - DELETE
- ❌ `src/agents/async_module_lead_agent.cpp` - DELETE
- ❌ `include/agents/async_component_lead_agent.hpp` - DELETE
- ❌ `src/agents/async_component_lead_agent.cpp` - DELETE

### Tests Requiring Update (⏳ PENDING - ALL)
- ⏳ `tests/e2e/phase1_basic_delegation.cpp` - shared_ptr + async
- ⏳ `tests/e2e/phase2_module_coordination.cpp` - shared_ptr + async
- ⏳ `tests/e2e/phase3_component_coordination.cpp` - shared_ptr + async
- ⏳ `tests/e2e/phase_4_multi_component.cpp` - shared_ptr + async
- ⏳ `tests/e2e/phase_5_chaos_engineering.cpp` - shared_ptr + async
- ⏳ `tests/e2e/phase_a_async_delegation.cpp` - shared_ptr + async
- ⏳ `tests/e2e/phase_b_async_agents.cpp` - shared_ptr + async
- ⏳ `tests/e2e/phase_b_full_async_hierarchy.cpp` - shared_ptr + async
- ⏳ `tests/e2e/phase_d3_distributed_hierarchy.cpp` - shared_ptr + async
- ⏳ `tests/unit/*.cpp` (13 files) - shared_ptr + async

## Benefits of This Migration

### C2 Fix: Eliminates Use-After-Free
```cpp
// BEFORE: Race condition leading to crash
Thread A: captures raw agent* in lambda
Thread B: unregisterAgent() → agent deleted
Thread A: lambda executes → SEGFAULT (use-after-free)

// AFTER: Safe with shared_ptr
Thread A: captures shared_ptr<agent> in lambda → ref count++
Thread B: unregisterAgent() → agent NOT deleted (ref count > 0)
Thread A: lambda executes → SAFE (agent still alive)
Thread A: lambda completes → ref count-- → agent deleted if last ref
```

### C3 Fix: Enables Polymorphism
```cpp
// BEFORE: Cannot use uniform collections
std::vector<BaseAgent*> sync_agents;  // Only sync agents
std::vector<AsyncBaseAgent*> async_agents;  // Only async agents
// Two separate collections!

// AFTER: Uniform collections
std::vector<std::shared_ptr<BaseAgent>> all_agents;  // All agents!
// Single collection, polymorphic access
```

## Testing Strategy

### Expected Test Failures
**ALL tests will initially fail** due to:
1. API change: `registerAgent()` expects `shared_ptr` (not raw pointer)
2. Return type change: `processMessage()` returns `Task<Response>` (not `Response`)

### Verification Steps
1. **Compile Check**: Ensure all agents compile with new signatures
2. **Unit Tests**: Update and verify each agent type individually
3. **E2E Tests**: Update and verify full message flow
4. **ThreadSanitizer**: Verify no use-after-free with `docker-compose up test-tsan`
5. **Performance**: Ensure no regression in message throughput

## Estimated Effort

| Task | Files | Estimated Time |
|------|-------|----------------|
| Core architecture (DONE) | 4 files | ✅ Complete |
| Agent migration | 8 files | 2-3 hours |
| Delete async_* files | 10 files | 30 minutes |
| Update E2E tests | 9 files | 3-4 hours |
| Update unit tests | 13 files | 2-3 hours |
| **Total** | **44 files** | **8-11 hours** |

## Rollback Plan
If issues arise:
1. Revert commit with C2/C3 changes
2. Return to dual hierarchy (BaseAgent/AsyncBaseAgent)
3. Use raw pointers in MessageBus (document lifetime requirements)

## Next Steps
1. ✅ Complete TaskAgent migration (done)
2. ⏳ Migrate remaining concrete agents (ChiefArchitect, ModuleLead, ComponentLead)
3. ⏳ Delete async_* files
4. ⏳ Update all tests (E2E + unit)
5. ⏳ Verify with CI/CD and ThreadSanitizer

---

**Status**: Architecture refactoring in progress (C2/C3 fixes)
**Last Updated**: 2025-11-20
**Committed**: Core architecture changes (BaseAgent, MessageBus, TaskAgent)
**Remaining**: Other agents, tests, async_* deletion
