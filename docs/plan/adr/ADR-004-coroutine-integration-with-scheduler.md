# ADR-004: Coroutine Integration with Scheduler

**Status**: Accepted
**Date**: 2025-11-21
**Deciders**: ProjectKeystone Development Team
**Tags**: architecture, coroutines, async, phase-b

## Context

ProjectKeystone agents require asynchronous message processing to avoid blocking worker threads while waiting for
responses. C++20 coroutines provide `co_await` syntax for async operations, but integrating them with a custom
work-stealing scheduler requires careful design decisions around suspension, resumption, and lifecycle management.

### Requirements

1. **Async/Await Syntax**: Agents use `co_await` for readable async code
2. **Scheduler Integration**: Coroutines resume on work-stealing scheduler threads
3. **Exception Safety**: Propagate exceptions across suspension points
4. **Efficient Resumption**: Minimize stack consumption and context switches
5. **Lifecycle Management**: Automatic cleanup of coroutine frames

### Design Challenges

1. **Where to Resume**: Submit to scheduler or use symmetric transfer?
2. **Stack Growth**: Recursive resumption vs queue-based dispatch
3. **Promise Design**: How to store results and exceptions?
4. **Continuation Chaining**: How to link dependent coroutines?

## Decision

Implement `Task<T>` as a C++20 awaitable type with **symmetric transfer** for efficient coroutine chaining and **lazy
evaluation** for explicit control flow.

### Design Choices

#### 1. Symmetric Transfer in final_suspend

```cpp
template <typename T>
class Task {
  struct promise_type {
    std::coroutine_handle<> continuation;  // Stores awaiting coroutine

    auto final_suspend() noexcept {
      struct final_awaiter {
        std::coroutine_handle<> continuation;

        bool await_ready() noexcept { return false; }

        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<promise_type> h) noexcept {
          // Symmetric transfer: return continuation handle
          if (continuation) {
            return continuation;  // Resume awaiting coroutine directly
          }
          return std::noop_coroutine();  // No continuation, suspend forever
        }

        void await_resume() noexcept {}
      };

      return final_awaiter{continuation};
    }
  };
};
```

**Symmetric Transfer Rationale**:

- **No Stack Growth**: Compiler converts tail-recursive resumption to jump (O(1) stack)
- **No Queue Submission**: Direct transfer without scheduler round-trip
- **Efficient**: Single assembly jump, not function call + queue + context switch
- **Standard Pattern**: Matches cppcoro and other C++20 coroutine libraries

**Alternative (Rejected): Explicit Scheduler Submission**:

```cpp
// Explicit submission (REJECTED)
void final_suspend() {
  if (continuation) {
    scheduler->submit(continuation);  // Indirect, queue overhead
  }
}
```

- **Stack Safe**: Yes (no recursion)
- **Inefficient**: Extra queue operation + context switch
- **Latency**: ~1-10μs scheduler latency vs instant symmetric transfer
- **When to Use**: Never for coroutine chains, only for initial Task submission

#### 2. await_suspend with Symmetric Transfer

```cpp
std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
  // Store the awaiting coroutine as our continuation
  handle_.promise().continuation = awaiting;

  // Return our handle for symmetric transfer
  // Runtime resumes this handle, which will eventually resume awaiting
  return handle_;
}
```

**Flow**:

1. `co_await task` suspends awaiting coroutine
2. `await_suspend` stores awaiting as continuation
3. Runtime resumes `task` (via symmetric transfer)
4. `task` completes, reaches `final_suspend`
5. `final_suspend` returns continuation handle (symmetric transfer back)
6. Awaiting coroutine resumes with result

**Key Insight**: Bidirectional symmetric transfer forms chain without stack growth.

#### 3. Lazy Evaluation (initial_suspend)

```cpp
std::suspend_always initial_suspend() noexcept { return {}; }
```

**Rationale**:

- **Explicit Control**: Task starts only when `co_await`ed or `.resume()` called
- **Scheduler Integration**: Can submit to specific worker before start
- **Testability**: Can set up test harness before coroutine executes
- **Predictable**: No surprising eager execution on construction

**Alternative (Rejected): Eager Evaluation**:

```cpp
std::suspend_never initial_suspend() noexcept { return {}; }
```

- **Unpredictable**: Coroutine starts immediately in constructor
- **Hard to Control**: Cannot choose which worker executes it
- **Testing Issues**: Starts before test assertions set up

#### 4. Exception Propagation

```cpp
struct promise_type {
  std::exception_ptr exception;

  void unhandled_exception() {
    exception = std::current_exception();  // Capture exception
  }
};

T await_resume() {
  // ... resume until done ...

  if (handle_.promise().exception) {
    std::rethrow_exception(handle_.promise().exception);  // Propagate
  }

  return std::move(handle_.promise().result.value());
}
```

**Rationale**:

- **Transparent**: Exceptions cross suspension points naturally
- **Safe**: No exception lost during async execution
- **Standard Pattern**: Matches `std::future` and other async types

#### 5. Move-Only Semantics

```cpp
Task(const Task&) = delete;
Task& operator=(const Task&) = delete;

Task(Task&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)) {}

~Task() {
  if (handle_) {
    handle_.destroy();  // RAII: automatic cleanup
  }
}
```

**Rationale**:

- **Unique Ownership**: Only one Task owns the coroutine frame
- **RAII**: Automatic cleanup prevents leaks
- **Move-Efficient**: Transfer ownership without deep copy

## Consequences

### Positive

1. **Efficient Coroutine Chains**:
   - Symmetric transfer eliminates stack growth
   - No scheduler overhead for chained `co_await`
   - Compile-time tail call optimization
2. **Clean Agent Code**:

   ```cpp
   Task<std::string> AgentBase::processAsync(KeystoneMessage msg) {
     auto response = co_await sendAndWait(msg);
     auto result = co_await processResult(response);
     co_return result;
   }
   ```

3. **Scheduler Integration**:
   - Initial Task submitted to scheduler
   - Continuations resume via symmetric transfer (no resubmission)
4. **Exception Safety**:
   - Exceptions propagate across `co_await` naturally
   - No special error handling code needed
5. **Memory Efficient**:
   - O(1) stack usage for any chain depth
   - Coroutine frames allocated on heap, not stack

### Negative

1. **Complexity**:
   - Symmetric transfer harder to understand than explicit submission
   - Requires understanding of C++20 coroutine mechanics
2. **Debugging**:
   - Stack traces show suspended coroutines, not call chain
   - Need coroutine-aware debuggers (GDB 10+)
3. **Compiler Support**:
   - Requires GCC 11+ or Clang 14+ for full C++20 coroutines
   - Not all platforms supported (embedded, older compilers)
4. **Initial Submission Overhead**:
   - First Task must be submitted to scheduler explicitly
   - Not zero-cost like synchronous calls

### Migration Path

**Breaking Changes**:

- None (new feature, agents opt-in to async methods)

**Gradual Adoption**:

1. ✅ Implement Task<T> with symmetric transfer
2. ✅ Add async methods alongside synchronous ones
3. ✅ Convert agents one-by-one to async
4. Phase 4: Deprecate synchronous methods
5. Phase 5: Remove synchronous methods (breaking)

## Alternatives Considered

### Alternative 1: Explicit Scheduler Submission in final_suspend

```cpp
void final_suspend() {
  if (continuation) {
    scheduler_->submit(continuation);  // Explicit queue
  }
}
```

**Rejected**:

- **Overhead**: Every continuation pays scheduler queue cost
- **Latency**: ~1-10μs vs instant symmetric transfer
- **Complexity**: Need to pass scheduler pointer through coroutines

**When Acceptable**: Only for top-level Task submission, not chains.

### Alternative 2: Eager Evaluation (suspend_never)

```cpp
std::suspend_never initial_suspend() noexcept { return {}; }
```

**Rejected**:

- **Loss of Control**: Cannot choose execution worker
- **Surprising Behavior**: Task starts in constructor
- **Testing Difficulty**: Cannot intercept before execution

### Alternative 3: Callback-Based Async (No Coroutines)

```cpp
void processAsync(KeystoneMessage msg, std::function<void(Response)> callback) {
  // ... manual callback chaining ...
}
```

**Rejected**:

- **Callback Hell**: Deeply nested lambdas
- **Error Handling**: Manual propagation through callbacks
- **Readability**: Lost linear control flow

**Benefit of Coroutines**: Async code looks like sync code.

### Alternative 4: std::future and std::promise

```cpp
std::future<Response> processAsync(KeystoneMessage msg) {
  std::promise<Response> p;
  auto f = p.get_future();
  // ... set promise later ...
  return f;
}
```

**Rejected**:

- **Blocking**: `future.get()` blocks thread (not async)
- **Overhead**: Shared state allocation
- **Not Composable**: Cannot `co_await std::future`

**Limited Use**: Only for interop with existing code.

### Alternative 5: Third-Party Coroutine Library (cppcoro)

**Rejected**:

- **External Dependency**: Large library (not just Task<T>)
- **Integration Complexity**: May not fit work-stealing scheduler
- **Learning Curve**: Team must learn library API

**Prefer**: Minimal custom Task<T> tailored to ProjectKeystone needs.

## Implementation Details

### Promise Storage

```cpp
struct promise_type {
  std::optional<T> result;           // Result storage
  std::exception_ptr exception;      // Exception storage
  std::coroutine_handle<> continuation;  // Awaiting coroutine
};
```

**Rationale**:

- `std::optional<T>`: Handles void vs non-void uniformly
- `std::exception_ptr`: Type-erased exception storage
- `continuation`: Single awaiter (not multi-cast)

### Thread Safety

- **Not Thread-Safe**: Task is single-threaded (resumed by one worker at a time)
- **Move Between Threads**: Continuation may resume on different worker
- **Scheduler Responsibility**: Ensure work-stealing is thread-safe

### Scheduler Lifecycle

```cpp
// Initial submission to scheduler
Task<int> task = computeAsync();
scheduler.submit(task.get_handle());  // Explicit submission

// Continuations use symmetric transfer (no resubmission)
int result = co_await task;  // Resumes directly, not via scheduler
```

**Pattern**:

1. Top-level Task submitted to scheduler explicitly
2. All `co_await` chains use symmetric transfer (automatic)
3. Scheduler only involved at entry point

## Validation

### Unit Tests

- ✅ Basic Task creation and resumption
- ✅ Task<T> with return values
- ✅ Task\<void\> for side effects
- ✅ Exception propagation across co_await
- ✅ Symmetric transfer (no stack overflow in long chains)
- ✅ Move semantics and RAII

### E2E Tests

- ✅ Phase 3 E2E: Async agent message processing
- ✅ Concurrent coroutines (1000+)
- ✅ Exception handling in agents

### Benchmarks

- ✅ co_await latency: ~50ns (symmetric transfer)
- ✅ Explicit submission latency: ~5μs (scheduler queue)
- ✅ Stack usage: O(1) for 1000-deep coroutine chain

## Future Evolution

### Phase 4: Multi-Await (when_all / when_any)

```cpp
Task<std::vector<Response>> when_all(std::vector<Task<Response>> tasks);
Task<Response> when_any(std::vector<Task<Response>> tasks);
```

### Phase 5: Coroutine Cancellation

```cpp
struct promise_type {
  std::atomic<bool> cancelled;

  void request_cancellation() {
    cancelled.store(true);
  }
};
```

### Async Generators

```cpp
Generator<int> range(int start, int end) {
  for (int i = start; i < end; ++i) {
    co_yield i;
  }
}
```

## References

- [C++20 Coroutines](https://en.cppreference.com/w/cpp/language/coroutines) - Language reference
- [Symmetric Transfer](https://lewissbaker.github.io/2020/05/11/understanding_symmetric_transfer) - Lewis Baker article
- [cppcoro](https://github.com/lewissbaker/cppcoro) - Reference coroutine library
- [ADR-002: Work-Stealing Scheduler](./ADR-002-work-stealing-scheduler-architecture.md) - Scheduler design
- [TDD_FOUR_LAYER_ROADMAP.md](../TDD_FOUR_LAYER_ROADMAP.md) - Phase B (coroutines)

## Acceptance Criteria

- ✅ All code compiles without warnings
- ✅ All coroutine unit tests pass
- ✅ All E2E tests pass (async agent processing)
- ✅ No memory leaks (valgrind clean)
- ✅ No stack overflow in long chains (tested 10,000 depth)
- ✅ Exceptions propagate correctly
- ✅ Thread-safe integration with scheduler

---

**Last Updated**: 2025-11-21
**Version**: 1.0
**Project**: ProjectKeystone HMAS (C++20)
