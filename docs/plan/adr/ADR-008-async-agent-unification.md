# ADR-008: Async Agent Hierarchy Unification

**Status**: Implemented
**Date**: 2025-11-20
**Context**: Phase 6 Architecture Review - Critical Issue C3

## Context and Problem Statement

> **Note (ADR-015)**: The agent hierarchy (`TaskAgent`, `ChiefArchitectAgent`,
> `ModuleLeadAgent`, `ComponentLeadAgent`) described in this ADR has been extracted
> into **ProjectAgamemnon**. This ADR is preserved as the historical record of the
> unification decision; the `BaseAgent` class hierarchy it documents now lives in
> ProjectAgamemnon, not in ProjectKeystone.

The codebase had a dual hierarchy with both synchronous (`BaseAgent`) and asynchronous (`AsyncBaseAgent`) agent classes.
This created:

- Code duplication (two versions of every agent type)
- Type system complexity (couldn't have uniform collections)
- Maintenance burden (changes needed in two places)
- Polymorphism limitations (couldn't mix sync/async agents)

## Decision Drivers

- **Simplicity**: Single hierarchy instead of dual
- **Polymorphism**: Uniform collections of all agent types
- **Maintainability**: One implementation instead of two
- **Performance**: Async by default for all agents
- **Future-proofing**: All modern systems are async

## Considered Options

1. **Maintain dual hierarchy (BaseAgent + AsyncBaseAgent)** - Status quo
2. **Make BaseAgent async by default, delete AsyncBaseAgent** - Selected
3. **Keep both, use template metaprogramming to select**
4. **Wrapper pattern: BaseAgent wraps AsyncBaseAgent**

## Decision Outcome

Chosen option: **Make BaseAgent async by default**, delete AsyncBaseAgent and all async_* agent classes.

### Positive Consequences

- **Unified API**: Single `processMessage()` signature for all agents
- **Polymorphic collections**: `std::vector<shared_ptr<BaseAgent>>` works for all agents
- **Less code**: Deleted 10 async_* files (headers + implementations)
- **Simpler mental model**: All agents are async, period
- **Better composability**: All agents use C++20 coroutines uniformly

### Negative Consequences

- **API break**: All agents must return `Task<Response>` instead of `Response`
- **Requires coroutines**: All agent implementations must use `co_return`
- **C++20 required**: Can't compile with older C++ standards (already required)
- **Migration effort**: 329 tests needed updates

## Implementation Details

### Before (Dual Hierarchy)

```cpp
// Synchronous base class
class BaseAgent : public AgentBase {
    virtual Response processMessage(const KeystoneMessage& msg) = 0;  // Sync
};

// Asynchronous base class (separate hierarchy!)
class AsyncBaseAgent : public AgentBase {
    virtual Task<Response> processMessage(const KeystoneMessage& msg) = 0;  // Async
};

// Two versions of every agent
class TaskAgent : public BaseAgent { ... };
class AsyncTaskAgent : public AsyncBaseAgent { ... };

// Two versions of every lead agent
class ChiefArchitectAgent : public BaseAgent { ... };
class AsyncChiefArchitectAgent : public AsyncBaseAgent { ... };
```

**Problems**:

```cpp
// Cannot use uniform collections
std::vector<BaseAgent*> sync_agents;  // Only sync agents
std::vector<AsyncBaseAgent*> async_agents;  // Only async agents
// Two separate collections!

// Cannot have polymorphic behavior
BaseAgent* agent = ...;
// How do we know if it's really AsyncBaseAgent?
// Need dynamic_cast or type checking!
```

### After (Unified Hierarchy)

```cpp
// Single async base class for all agents
class BaseAgent : public AgentBase {
    // FIX C3: All agents are async by default
    virtual Task<Response> processMessage(const KeystoneMessage& msg) = 0;  // Async
};

// AsyncBaseAgent deleted - no longer needed

// Single version of each agent type
class TaskAgent : public BaseAgent {
    Task<Response> processMessage(const KeystoneMessage& msg) override;
};

class ChiefArchitectAgent : public BaseAgent {
    Task<Response> processMessage(const KeystoneMessage& msg) override;
};
```

**Benefits**:

```cpp
// Uniform collections
std::vector<std::shared_ptr<BaseAgent>> all_agents;  // All agents!
// Single collection, polymorphic access

// Uniform async/await usage
for (auto& agent : all_agents) {
    auto response = co_await agent->processMessage(msg);
    // Works for any agent type!
}
```

### Migration Pattern for Agents

**Step 1**: Update header signature

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

### Migration Pattern for Tests

**Before**:

```cpp
auto response = agent->processMessage(msg);  // Immediate Response
EXPECT_EQ(response.status, Response::Status::Success);
```

**After**:

```cpp
auto task = agent->processMessage(msg);  // Returns Task<Response>
auto response = task.get();  // Get result from Task
EXPECT_EQ(response.status, Response::Status::Success);
```

## Files Modified

### Core Architecture

- ✅ `include/agents/base_agent.hpp` - Made async
- ✅ `src/agents/base_agent.cpp` - No changes needed

### Agents Migrated

- ✅ `include/agents/task_agent.hpp` - Made async
- ✅ `src/agents/task_agent.cpp` - Made async
- ✅ `include/agents/chief_architect_agent.hpp` - Made async
- ✅ `src/agents/chief_architect_agent.cpp` - Made async
- ✅ `include/agents/module_lead_agent.hpp` - Made async
- ✅ `src/agents/module_lead_agent.cpp` - Made async
- ✅ `include/agents/component_lead_agent.hpp` - Made async
- ✅ `src/agents/component_lead_agent.cpp` - Made async

### Files Deleted (10 files - Async Hierarchy Removed)

- ❌ `include/agents/async_base_agent.hpp` - **DELETED**
- ❌ `src/agents/async_base_agent.cpp` - **DELETED**
- ❌ `include/agents/async_task_agent.hpp` - **DELETED**
- ❌ `src/agents/async_task_agent.cpp` - **DELETED**
- ❌ `include/agents/async_chief_architect_agent.hpp` - **DELETED**
- ❌ `src/agents/async_chief_architect_agent.cpp` - **DELETED**
- ❌ `include/agents/async_module_lead_agent.hpp` - **DELETED**
- ❌ `src/agents/async_module_lead_agent.cpp` - **DELETED**
- ❌ `include/agents/async_component_lead_agent.hpp` - **DELETED**
- ❌ `src/agents/async_component_lead_agent.cpp` - **DELETED**

### Tests Updated (373 tests)

- ✅ All E2E tests (`tests/e2e/*.cpp`) - 59 tests
- ✅ All unit tests (`tests/unit/*.cpp`) - 314 tests

## Validation

### Verification Steps

1. ✅ **Compile Check**: All agents compile with async signatures
2. ✅ **Unit Tests**: All 270 unit tests pass
3. ✅ **E2E Tests**: All 59 E2E tests pass
4. ✅ **Performance**: No regression in message throughput
5. ✅ **Code Reduction**: Removed 10 duplicate files

### Test Results

```
Total Tests: 373/373 passing (100%)
Code Reduction: -10 files (async_* hierarchy removed)
Lines of Code Reduced: ~2,500 lines
```

## Technical Details

### C++20 Coroutines Required

All agent implementations must use `co_return`:

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

**Why `co_return`?**

- Makes the function a coroutine (returns `Task<Response>`)
- Allows use of `co_await` inside the function
- Enables non-blocking async message processing

### Task<T> Awaitable Type

The `Task<T>` type is a C++20 coroutine awaitable:

```cpp
template <typename T>
class Task {
public:
    // Awaitable interface
    bool await_ready() const noexcept;
    void await_suspend(std::coroutine_handle<> h);
    T await_resume();

    // Blocking get (for tests)
    T get() { return result_; }

private:
    T result_;
};
```

**Usage**:

```cpp
// Non-blocking (async)
auto response = co_await agent->processMessage(msg);

// Blocking (tests only)
auto task = agent->processMessage(msg);
auto response = task.get();
```

## Benefits Realized

### 1. Polymorphism Enabled

```cpp
// Before: Cannot mix sync/async
void processAgents(std::vector<BaseAgent*> sync_agents,
                   std::vector<AsyncBaseAgent*> async_agents) {
    // Two separate loops!
}

// After: Uniform processing
Task<void> processAgents(std::vector<std::shared_ptr<BaseAgent>> agents) {
    for (auto& agent : agents) {
        auto response = co_await agent->processMessage(msg);
        // Works for ANY agent type!
    }
}
```

### 2. Simplified API

```cpp
// Before: Type checking nightmare
if (auto* async_agent = dynamic_cast<AsyncBaseAgent*>(agent)) {
    auto task = async_agent->processMessage(msg);
    // ...
} else {
    auto response = agent->processMessage(msg);
    // ...
}

// After: Uniform interface
auto task = agent->processMessage(msg);  // Always returns Task<Response>
```

### 3. Code Reduction

- **Deleted**: 10 async_* files (~2,500 lines)
- **Simplified**: No type checking or dual-hierarchy logic
- **Maintainability**: Only one agent implementation per type

## Links

- Related: ADR-007 (MessageBus shared_ptr Migration)
- Addresses: Architecture Review Issue C3
- GitHub Commit: Architecture refactoring (C2/C3 fixes)

## Notes

### Breaking Changes

This is a **breaking API change**:

- All agents must return `Task<Response>`
- All agent calls must use `co_await` or `.get()`
- Cannot mix with pre-C++20 codebases

### Migration Timeline

| Task | Effort | Status |
|------|--------|--------|
| Update BaseAgent signature | 30 min | ✅ Done |
| Migrate TaskAgent | 1 hour | ✅ Done |
| Migrate lead agents | 2 hours | ✅ Done |
| Delete async_* files | 30 min | ✅ Done |
| Update all tests | 4 hours | ✅ Done |
| **Total** | **8 hours** | ✅ **Complete** |

## Alternatives Considered

### Option 1: Maintain Dual Hierarchy (Status Quo)

**Rejected** - Too much code duplication and maintenance burden.

### Option 3: Template Metaprogramming

```cpp
template<typename ExecutionPolicy>
class Agent : public AgentBase {
    auto processMessage(const KeystoneMessage& msg)
        -> std::conditional_t<
            std::is_same_v<ExecutionPolicy, AsyncPolicy>,
            Task<Response>,
            Response
        >;
};
```

**Rejected** - Too complex, difficult to understand, error-prone template code.

### Option 4: Wrapper Pattern

```cpp
class BaseAgent {
    Response processMessage(const KeystoneMessage& msg) {
        return processMessageAsync(msg).get();  // Block on async
    }

    virtual Task<Response> processMessageAsync(const KeystoneMessage& msg) = 0;
};
```

**Rejected** - Adds unnecessary blocking, defeats the purpose of async.

---

**Last Updated**: 2025-11-20
**Implemented In**: Phase 6 Architecture Review
**Status**: ✅ Complete and Verified
