# ADR-013: Coroutine Safety Patterns and Best Practices

**Status**: Adopted
**Date**: 2025-11-26
**Context**: Phase 5.1 - Async Architecture Stabilization
**Addresses**: Issue #56 - Coroutine Safety Documentation

## Context and Problem Statement

Keystone extensively uses C++20 coroutines for asynchronous operations. Coroutines introduce unique safety
challenges compared to traditional synchronous code:

- **Lifetime Management**: Coroutine handles must be carefully managed to avoid use-after-free
- **Suspension Points**: Calling co_await can suspend execution unexpectedly
- **Exception Safety**: Exceptions in coroutines behave differently than in normal functions
- **Thread Safety**: Coroutines may resume on different threads, creating data races
- **Stack Safety**: Local variables become invalid after suspension if not properly captured

Without clear patterns and guidelines, developers risk:

- Memory leaks and dangling pointers
- Data races in concurrent coroutine execution
- Difficult-to-debug crashes from improper coroutine scheduling
- Resource exhaustion from uncancellable coroutines

## Decision Drivers

1. **Correctness**: Prevent use-after-free and memory safety violations
2. **Clarity**: Establish proven patterns developers can follow
3. **Maintainability**: Make coroutine code easier to understand and maintain
4. **Performance**: Minimize overhead of safety mechanisms
5. **Consistency**: Uniform patterns across the codebase

## Coroutine Safety Patterns

### Pattern 1: Task Ownership and Lifetime Management

**Problem**: Raw coroutine handles are dangerous. They must be destroyed exactly once.

**Solution**: Use the `Task<T>` wrapper class which manages handle lifetime.

```cpp
// WRONG - Manual handle management is error-prone
auto task_raw() -> std::coroutine_handle<> {
    // Dangerous! Caller must manage handle lifetime
    return std::coroutine_handle<promise_type>::from_promise(*this);
}

Task<int> consumer() {
    auto handle = task_raw();  // Caller owns handle now
    // If we throw here, handle leaks!
    auto result = handle.promise().result;
    handle.destroy();  // Manual destroy needed
    co_return result;
}

// CORRECT - Task<T> manages handle lifetime
Task<int> safe_task() {
    // Task constructor wraps handle
    co_return 42;
}

Task<void> consumer() {
    Task<int> task = safe_task();  // RAII - handle owned by Task
    auto result = co_await task;    // Task destroyed automatically
    co_return;
}
```

**Key Points**:

- Always return `Task<T>` from coroutine functions, never raw handles
- Task uses RAII: destructor calls `handle_.destroy()`
- Move-only semantics prevent accidental handle duplication

**Codebase Examples**:

- `include/concurrency/task.hpp` (lines 99-123): Task destructor and RAII management
- `include/agents/async_agent.hpp` (line 42): All agent coroutines return `Task<Response>`

### Pattern 2: Using co_await Correctly

**Problem**: Suspended coroutines can resume on different threads or contexts, invalidating assumptions.

**Solution**: Only co_await within coroutine functions; store continuation properly.

```cpp
// WRONG - Non-coroutine function cannot use co_await
void regular_function() {
    auto task = someCoroutine();
    // auto result = co_await task;  // COMPILE ERROR - not a coroutine
}

// WRONG - Awaiting outside coroutine context
void print_result() {
    auto value = co_await getValueAsync();  // Cannot compile in non-coroutine
    std::cout << value << std::endl;
}

// CORRECT - Use co_await in coroutine functions
Task<void> safe_usage() {
    auto value = co_await getValueAsync();
    // value is valid within this coroutine
    co_return;
}

// CORRECT - Blocking get() for non-coroutine contexts
void print_result() {
    Task<int> task = getValueAsync();
    auto value = task.get();  // Blocks until coroutine completes
    std::cout << value << std::endl;
}
```

**Key Points**:

- `co_await` can only be used inside coroutine functions
- Non-coroutine functions must use `task.get()` (blocking)
- Calling `co_await` stores the awaiting coroutine as continuation

**Codebase Examples**:

- `include/concurrency/task.hpp` (lines 192-213): `await_suspend()` stores continuation
- `src/agents/task_agent.cpp` (lines 78, 95): Using `co_return` in coroutine functions

### Pattern 3: Exception Safety in Coroutines

**Problem**: Exceptions in coroutines must be captured and re-thrown, not silently ignored.

**Solution**: Use try/catch inside coroutine, let promise capture exceptions.

```cpp
// WRONG - Exception silently disappears
Task<std::string> unsafe_operation() {
    std::string result = readFile("missing.txt");  // Throws!
    co_return result;
    // Exception propagates to promise::unhandled_exception()
    // But caller gets Task with undefined state!
}

Task<void> caller() {
    auto task = unsafe_operation();
    auto result = co_await task;  // May throw unexpected exception
    co_return;
}

// CORRECT - Catch exceptions, return error state
Task<std::string> safe_operation() {
    try {
        std::string result = readFile("missing.txt");  // May throw
        co_return result;
    } catch (const std::exception& e) {
        // Exception is caught, error can be returned
        co_return "ERROR: " + std::string(e.what());
    }
}

// CORRECT - Let promise handle uncaught exceptions
Task<std::string> operation_with_cleanup() {
    try {
        // ... operation code ...
        co_return "success";
    } catch (const std::exception& e) {
        // Cleanup can happen here if needed
        co_return "error: " + std::string(e.what());
    }
    // If exception is uncaught, promise::unhandled_exception()
    // captures it via std::current_exception()
}

// Accessing exception from Task
Task<void> exception_handling() {
    try {
        auto task = operation_with_cleanup();
        auto result = co_await task;  // May rethrow exception
    } catch (const std::exception& e) {
        // Caught here
    }
}
```

**Promise Exception Handling**:

```cpp
struct promise_type {
    std::exception_ptr exception;  // Stores exception

    void unhandled_exception() {
        exception = std::current_exception();  // Capture exception
    }
};

// In Task::get() and Task::await_resume()
void get() {
    if (handle_.promise().exception) {
        std::rethrow_exception(handle_.promise().exception);  // Re-throw
    }
}
```

**Key Points**:

- Use try/catch inside coroutine bodies for error handling
- Promise automatically captures uncaught exceptions
- Accessing result via `co_await` or `get()` re-throws exceptions
- Always check for exceptions before using results

**Codebase Examples**:

- `include/concurrency/task.hpp` (lines 92-95): `unhandled_exception()` captures exceptions
- `include/concurrency/task.hpp` (lines 159-160): `get()` re-throws exceptions
- `src/agents/task_agent.cpp` (lines 80-96): Try/catch with error handling in coroutine

### Pattern 4: Thread Safety with Coroutine Scheduling

**Problem**: Coroutines may resume on different threads, causing data races.

**Solution**: Integrate with WorkStealingScheduler for thread-safe scheduling.

```cpp
// WRONG - No scheduler integration, potential races
Task<int> unsafe_coro() {
    static int shared_counter = 0;
    shared_counter++;  // DATA RACE - multiple coroutines from different threads
    co_return shared_counter;
}

// CORRECT - Use scheduler-aware Task
Task<int> safe_coro() {
    // Task::await_suspend() checks for scheduler
    auto scheduler = getCurrentScheduler();
    if (scheduler) {
        // This coroutine will be scheduled safely
        // Resumption happens on scheduler's worker thread
    }
    co_return 42;
}

// CORRECT - Protect shared state with synchronization
Task<void> protected_access() {
    static std::mutex shared_mutex;
    static int shared_counter = 0;

    // NOTE: Cannot co_await while holding lock!
    // Lock must be released before any co_await point
    {
        std::lock_guard<std::mutex> lock(shared_mutex);
        shared_counter++;  // Protected
        // Must release lock before co_await
    }

    // Safe to co_await here (lock released)
    auto result = co_await someOtherTask();

    co_return;
}
```

**Scheduler Integration**:

```cpp
// In Task::await_suspend()
std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) {
    handle_.promise().continuation = awaiting;

    auto scheduler = getCurrentScheduler();
    if (scheduler) {
        // Submit to scheduler for execution
        scheduler->submit(std::coroutine_handle<>(handle_));
        return std::noop_coroutine();  // Don't resume immediately
    }

    // Fallback: symmetric transfer (synchronous)
    return handle_;
}
```

**Key Points**:

- Task automatically integrates with WorkStealingScheduler if available
- Coroutines may resume on different threads
- Cannot hold locks across co_await points
- Use atomic operations or scheduler-local state for shared data

**Codebase Examples**:

- `include/concurrency/task.hpp` (lines 192-214): Scheduler-aware `await_suspend()`
- `include/concurrency/work_stealing_scheduler.hpp` (lines 86-91): Scheduler submit interface
- `include/concurrency/pull_or_steal.hpp`: Work-stealing without explicit locking

### Pattern 5: Avoid get_handle() - EXPERT-ONLY API

**Problem**: Raw coroutine handles enable dangerous operations that violate safety invariants.

**Solution**: Never manually extract or manage handles.

```cpp
// EXTREMELY DANGEROUS - DO NOT DO THIS
Task<void> dangerous_usage() {
    Task<int> task = someCoroutine();
    auto handle = task.get_handle();  // DANGER!

    // Task still owns the handle!
    // If task is destroyed, handle becomes dangling
    scheduler.submit([handle]() {
        handle.resume();  // CRASH - use-after-free!
    });

    // task.~Task() called here, destroys handle
    // But scheduler still has dangling handle
}

// CORRECT - Use co_await for automatic scheduling
Task<void> safe_usage() {
    Task<int> task = someCoroutine();
    auto result = co_await task;  // Task::await_suspend() handles scheduling
    // No manual handle manipulation needed
}

// CORRECT - If debugging needed, keep Task alive
Task<void> debugging() {
    auto task = std::make_shared<Task<int>>(someCoroutine());
    auto handle = task->get_handle();  // Safer: shared_ptr keeps handle alive

    // ... use handle for debugging only ...
    // Never submit to scheduler
}
```

**Why get_handle() Is Dangerous**:

1. Task owns the handle and destroys it in destructor
2. Returned handle is non-owning (just a pointer)
3. Task destruction makes handle dangling
4. Calling resume() on dangling handle = undefined behavior
5. No thread-safety: concurrent operations cause data races

**Key Points**:

- **Avoid** manually extracting handles
- Let Task manage all handle operations
- Use `co_await` for automatic scheduler integration
- If you think you need `get_handle()`, you probably don't

**Codebase Examples**:

- `include/concurrency/task.hpp` (lines 224-262): `get_handle()` with extensive safety warnings
- `include/concurrency/task.hpp` (lines 222-243): Documented use-after-free risks

### Pattern 6: Symmetric Transfer for Efficient Continuation

**Problem**: Traditional suspend/resume can waste stack space with long chains of coroutines.

**Solution**: Use symmetric transfer to efficiently chain coroutines.

```cpp
// WRONG - Each suspension uses stack
auto final_awaiter::await_suspend(std::coroutine_handle<promise_type> h) {
    if (continuation) {
        continuation.resume();  // Stack used
        return std::noop_coroutine();
    }
    return std::noop_coroutine();
}

// CORRECT - Symmetric transfer avoids stack
auto final_awaiter::await_suspend(std::coroutine_handle<promise_type> h) {
    if (continuation) {
        return continuation;  // Transfer control directly (no stack!)
    }
    return std::noop_coroutine();
}

// Usage - Long chains work efficiently
Task<int> level_3() {
    co_return 3;
}

Task<int> level_2() {
    auto result = co_await level_3();  // Symmetric transfer used
    co_return result + 2;
}

Task<int> level_1() {
    auto result = co_await level_2();  // No stack overhead
    co_return result + 1;
}

Task<int> level_0() {
    auto result = co_await level_1();  // Deep chains work fine
    co_return result;
}
```

**Key Points**:

- Symmetric transfer avoids stack growth with chained coroutines
- Implement by returning handle from `await_suspend()` instead of calling `resume()`
- More efficient for deep coroutine hierarchies

**Codebase Examples**:

- `include/concurrency/task.hpp` (lines 65-85): `final_suspend()` using symmetric transfer
- `include/concurrency/task.hpp` (lines 71-78): Continuation handling in `final_awaiter`

## Common Pitfalls and Solutions

### Pitfall 1: Forgetting co_return or co_yield

**Problem**: Function signature says it returns `Task<T>` but doesn't use any coroutine keyword.

```cpp
// COMPILE ERROR - looks like coroutine but isn't
Task<int> bad_function() {
    return Task<int>();  // ERROR: Normal return in pseudo-coroutine
    // The compiler detects this mismatch
}

// CORRECT
Task<int> good_function() {
    co_return 42;  // Actually a coroutine
}

Task<void> void_function() {
    co_return;  // Must use co_return even for void
}
```

### Pitfall 2: Holding Locks Across Suspension Points

**Problem**: Lock held while coroutine suspends = deadlock or data races on resume.

```cpp
// DEADLOCK - Lock held across co_await
Task<void> unsafe_critical_section() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto result = co_await someTask();  // Lock held during suspension!
    // Coroutine resumes on different thread
    // Now we might have multiple threads holding the same lock

    use_result(result);
    co_return;
}

// CORRECT - Release lock before suspension
Task<void> safe_critical_section() {
    std::string result;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        result = get_shared_data();
        // Lock released here
    }

    // Safe to co_await - no lock held
    auto processed = co_await processAsync(result);

    co_return;
}

// ALSO CORRECT - Protect only critical sections
Task<void> fine_grained_locking() {
    auto task = asyncOperation();
    auto raw_result = co_await task;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        finalize_result(raw_result);  // Lock only when needed
    }

    co_return;
}
```

### Pitfall 3: Using Local Variables After Suspension

**Problem**: Local variables become invalid if referenced after suspension unless captured properly.

```cpp
// WRONG - Dangling reference
Task<std::string> unsafe_capture() {
    std::string local_data = "Hello";

    Task<void> async_task = [&]() -> Task<void> {  // Captures by reference
        co_await std::suspend_always{};  // Suspends here
        std::cout << local_data << std::endl;  // local_data destroyed!
        co_return;
    }();

    co_return "done";
}

// CORRECT - Capture by value
Task<std::string> safe_capture() {
    std::string local_data = "Hello";

    Task<void> async_task = [data = local_data]() -> Task<void> {  // By value
        co_await std::suspend_always{};  // Safe to suspend
        std::cout << data << std::endl;  // Copy still valid
        co_return;
    }();

    co_return "done";
}

// CORRECT - Store in coroutine state (automatically safe)
Task<void> inherent_safety() {
    std::string data = "Hello";

    // Function-local variables are stored in coroutine frame
    // Automatically valid across suspension points
    co_await std::suspend_always{};
    std::cout << data << std::endl;  // Safe
    co_return;
}
```

### Pitfall 4: Forgetting to Await or Get Result

**Problem**: Creating a Task but never awaiting it results in it never executing.

```cpp
// WRONG - Task created but never executed
Task<int> compute_value() {
    co_return 42;
}

void main() {
    Task<int> task = compute_value();  // Created but never awaited
    // Task never executes! Task destroyed without running
}

// CORRECT - Await to execute
Task<void> main_coro() {
    int value = co_await compute_value();  // Task executes here
    std::cout << value << std::endl;
}

// ALSO CORRECT - Use get() for blocking
int main() {
    Task<int> task = compute_value();
    int value = task.get();  // Blocks until task completes
    std::cout << value << std::endl;
    return 0;
}
```

### Pitfall 5: Incorrect Promise Type Specification

**Problem**: C++ requires exact promise_type specification for coroutine protocol.

```cpp
// WRONG - Task<int> without promise_type defined
template<typename T>
class BadTask {
    // No promise_type nested class
};

// Cannot use as coroutine return type
BadTask<int> will_not_compile() {
    co_return 42;  // COMPILE ERROR: no promise_type
}

// CORRECT - Provide promise_type
template<typename T>
class GoodTask {
    struct promise_type {
        Task get_return_object() { ... }
        auto initial_suspend() { ... }
        auto final_suspend() { ... }
        void return_value(T value) { ... }
        void unhandled_exception() { ... }
    };
};

GoodTask<int> will_compile() {
    co_return 42;  // OK: promise_type found
}
```

## Anti-Patterns to Avoid

| Anti-Pattern | Problem | Solution |
|---|---|---|
| **Manual handle management** | Use-after-free, memory leaks | Use `Task<T>` wrapper |
| **Holding locks across co_await** | Deadlock, data races | Release lock before suspension |
| **Calling resume() directly** | Thread-unsafe, inefficient | Use `co_await` or scheduler |
| **Extracting get_handle()** | Dangling pointers | Let Task manage handles |
| **Capturing by reference** | Dangling references after suspension | Capture by value or use function-local variables |
| **Mixing sync/async APIs** | Type confusion, hard to debug | Consistent coroutine usage throughout |
| **Creating Task but not awaiting** | Silent failures, no execution | Always `co_await` or `.get()` |
| **Destroying coroutine frame early** | Use-after-free in promise | Let RAII manage lifetime |

## Design Decisions

### Decision 1: Task<T> as the Only Coroutine Return Type

**Rationale**:

- Unified API for all coroutine functions
- Automatic handle lifetime management
- Clear ownership semantics
- Compiler checks promise_type specification

**Consequence**: All async operations must return `Task<T>`.

### Decision 2: Scheduler Integration in await_suspend()

**Rationale**:

- Automatic load balancing
- Work-stealing without explicit coordination
- Transparent to coroutine authors

**Consequence**: Must use `co_await` for automatic scheduling; raw resume() bypasses scheduler.

### Decision 3: Symmetric Transfer in final_suspend()

**Rationale**:

- Avoids stack growth with chained coroutines
- More efficient continuation handling
- Standard C++20 pattern

**Consequence**: Coroutine chains can be arbitrarily deep without stack issues.

## Validation

### Testing Coroutine Safety

```cpp
#include <gtest/gtest.h>
#include "concurrency/task.hpp"

// Unit test: Verify Task RAII
TEST(CoroutineSafetyTest, TaskDestructorCleansUpCoroutineHandle) {
    {
        Task<int> task = simple_coro();
        auto handle = task.get_handle();
        // Task destroyed here - handle should be cleaned up
    }
    // If TSan is enabled, would detect use-after-free
}

// Unit test: Exception safety
TEST(CoroutineSafetyTest, TaskCapturesAndRethrowsExceptions) {
    Task<int> task = throwing_coro();
    EXPECT_THROW(task.get(), std::runtime_error);
}

// Unit test: Symmetric transfer
TEST(CoroutineSafetyTest, CoroutineChainsWorkWithoutStackOverflow) {
    Task<int> result = deep_chain(1000);  // 1000 levels deep
    EXPECT_EQ(result.get(), 1000);  // Should complete without stack error
}
```

### Sanitizer Verification

Run with ThreadSanitizer to detect data races:

```bash
make test.debug.tsan  # Run all tests with ThreadSanitizer
```

## Implementation Checklist

For any new coroutine in the codebase:

- [ ] Function returns `Task<T>` or `Task<void>`
- [ ] Function body uses `co_return` (not regular return)
- [ ] All suspension points (co_await) properly documented
- [ ] No locks held across suspension points
- [ ] Exception handling with try/catch or promise::unhandled_exception()
- [ ] No manual handle extraction with get_handle()
- [ ] No capturing by reference across suspension points
- [ ] Task is always co_awaited or `.get()` called on it
- [ ] Lifetime of Task matches lifetime of any captured data

## References

### C++20 Coroutine Standard

- [cppreference: Coroutines (C++20)](https://en.cppreference.com/w/cpp/language/coroutines)
- [C++20 Coroutine Semantics](https://isocpp.org/files/papers/P0914r1.pdf)

### Related ADRs

- [ADR-008: Async Agent Hierarchy Unification](ADR-008-async-agent-unification.md)
- [ADR-001: Message Bus Architecture](ADR-001-message-bus-architecture.md)

### Code Examples in Codebase

- Task implementation: `include/concurrency/task.hpp`
- AsyncAgent usage: `include/agents/async_agent.hpp`
- Agent implementations: `src/agents/*.cpp`
- Scheduler integration: `include/concurrency/work_stealing_scheduler.hpp`
- Work-stealing awaitable: `include/concurrency/pull_or_steal.hpp`

## Revision History

| Date | Status | Changes |
|------|--------|---------|
| 2025-11-26 | Adopted | Initial version - Coroutine safety patterns, common pitfalls, design decisions |

---

**Last Updated**: 2025-11-26
**Status**: Adopted
**Supersedes**: None
**Superseded By**: None
