#!/usr/bin/env bash
# Script to file GitHub issues for Keystone codebase analysis findings
# Generated from comprehensive code review on 2025-11-21
#
# Usage: ./scripts/file_code_review_issues.sh
# Requires: gh (GitHub CLI) installed and authenticated

set -euo pipefail

# Check if gh is available
if ! command -v gh &> /dev/null; then
    echo "Error: GitHub CLI (gh) is not installed"
    echo "Install from: https://cli.github.com/"
    exit 1
fi

# Check if authenticated
if ! gh auth status &> /dev/null; then
    echo "Error: Not authenticated with GitHub CLI"
    echo "Run: gh auth login"
    exit 1
fi

echo "Filing GitHub issues for code review findings..."
echo ""

# =============================================================================
# P1-01: Missing await in Task<>::await_suspend (CRITICAL FOR ASYNC)
# =============================================================================
echo "Creating issue: P1-01 - Fix Task<>::await_suspend for proper async execution"
gh issue create \
  --title "P1-01: Fix Task<>::await_suspend for proper async execution" \
  --label "priority:high,type:bug,component:concurrency" \
  --body "## Problem

**Location**: \`include/concurrency/task.hpp:152\`
**Priority**: P1 (High)
**Severity**: Critical for async execution

The \`Task<T>::await_suspend()\` method immediately resumes the awaited coroutine instead of delegating to the scheduler, defeating the purpose of async/await.

### Current Code
\`\`\`cpp
void await_suspend([[maybe_unused]] std::coroutine_handle<> awaiting) {
    // For now, immediately resume the awaited coroutine
    // In a full scheduler, this would schedule the coroutine  // ⚠️ TODO comment
    if (handle_ && !handle_.done()) {
        handle_.resume();  // Should submit to scheduler instead
    }
}
\`\`\`

### Impact
- Coroutines run synchronously on the calling thread
- No true async execution
- Defeats work-stealing benefits
- No load balancing across worker threads

### Recommended Fix

**Option 1: Symmetric Transfer**
\`\`\`cpp
std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) {
    // Return handle to resume (symmetric transfer)
    return handle_;  // Framework resumes this, then awaiting when done
}
\`\`\`

**Option 2: Scheduler Integration**
\`\`\`cpp
void await_suspend(std::coroutine_handle<> awaiting) {
    // Submit both coroutines to scheduler
    scheduler_->submit(handle_);
    scheduler_->submit(awaiting);  // Resume caller after completion
}
\`\`\`

### Discussion Needed
1. Should Task<T> hold a reference to the scheduler?
2. Should we use symmetric transfer or explicit scheduler submission?
3. How do we handle the case where no scheduler is available (fallback to sync)?

### Effort Estimate
2-3 hours

### Testing
- Update \`tests/unit/test_task.cpp\` to verify async execution
- Add test that coroutines run on different threads
- Verify work-stealing actually distributes work

### References
- [C++20 Coroutines Tutorial](https://lewissbaker.github.io/)
- WorkStealingScheduler integration points"

echo "✅ Created P1-01"
echo ""

# =============================================================================
# P1-02: WorkStealingScheduler shutdown potential infinite loop
# =============================================================================
echo "Creating issue: P1-02 - Add condition variable to WorkStealingScheduler shutdown"
gh issue create \
  --title "P1-02: Add condition variable for immediate worker shutdown" \
  --label "priority:high,type:bug,component:concurrency" \
  --body "## Problem

**Location**: \`src/concurrency/work_stealing_scheduler.cpp:155-191\`
**Priority**: P1 (High)
**Severity**: Delayed shutdown (up to 1ms per worker)

Worker threads use exponential backoff with max 1ms sleep. When shutdown is requested, workers may not notice until they wake from sleep, leading to delayed shutdown.

### Current Code
\`\`\`cpp
while (!shutdown_requested_.load()) {
    auto work = own_queue.pop();
    // ... steal from others ...
    if (!work.has_value()) {
        idle_count++;
        // ⚠️ If shutdown set during sleep, thread wakes but has delay
        std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
    }
}
\`\`\`

### Impact
- Worker threads may take up to 1ms to notice shutdown signal
- With 100 workers, total shutdown delay could be 100ms
- Not critical for most use cases, but suboptimal

### Recommended Fix

Add condition variable for immediate wakeup:

\`\`\`cpp
class WorkStealingScheduler {
    std::condition_variable shutdown_cv_;
    std::mutex shutdown_mutex_;
};

// In workerLoop
while (!shutdown_requested_.load()) {
    // ... try to get work ...
    if (!work.has_value()) {
        std::unique_lock lock(shutdown_mutex_);
        shutdown_cv_.wait_for(lock, std::chrono::microseconds(sleep_us),
            [this]() { return shutdown_requested_.load(); });
    }
}

// In shutdown()
{
    std::lock_guard lock(shutdown_mutex_);
    shutdown_requested_.store(true);
}
shutdown_cv_.notify_all();  // Wake all workers immediately
\`\`\`

### Effort Estimate
2 hours

### Testing
- Add test that measures shutdown latency
- Verify workers exit within 10ms of shutdown request
- Test with varying numbers of workers (1, 10, 100)

### Alternative Approaches
1. Use pipe/eventfd for wakeup (Linux-specific)
2. Reduce max backoff to 100μs (trades CPU for responsiveness)
3. Accept current behavior (1ms delay is acceptable)"

echo "✅ Created P1-02"
echo ""

# =============================================================================
# P2-01: Coroutine-Scheduler Integration Missing
# =============================================================================
echo "Creating issue: P2-01 - Integrate coroutines with WorkStealingScheduler"
gh issue create \
  --title "P2-01: Integrate Task<T> coroutines with WorkStealingScheduler" \
  --label "priority:medium,type:enhancement,component:concurrency" \
  --body "## Problem

**Priority**: P2 (Medium)
**Type**: Architectural Enhancement

Task<T> coroutines currently resume synchronously in \`await_suspend()\` instead of submitting to the WorkStealingScheduler. This prevents true async execution and load balancing.

### Current State
- Task<T>::await_suspend() immediately resumes (sync)
- No scheduler integration
- Coroutines block calling thread

### Desired State
- Task<T> submits work to scheduler
- Coroutines distributed across worker threads
- True async/await semantics

### Design Questions
1. **Scheduler Reference**: How should Task<T> get access to the scheduler?
   - Global singleton? (easy but couples code)
   - Thread-local? (flexible but complex)
   - Explicit parameter? (clean but verbose)

2. **Fallback Behavior**: What if no scheduler available?
   - Synchronous execution (current behavior)
   - Throw exception
   - Queue to default scheduler

3. **Lifetime Management**: Who owns the scheduler?
   - Application (current approach)
   - Task system
   - Hybrid

### Implementation Plan
1. Add scheduler accessor to Task<T> (discuss design)
2. Update await_suspend() to submit to scheduler
3. Implement fallback for non-scheduled execution
4. Update all tests to use scheduler
5. Add benchmarks comparing sync vs async

### Related Issues
- Depends on P1-01 (await_suspend fix)
- Blocks full async agent system

### Effort Estimate
4 hours (after design decisions)

### Discussion Needed
Please comment with preferences on design questions above."

echo "✅ Created P2-01"
echo ""

# =============================================================================
# P2-02: Missing Architecture Decision Records
# =============================================================================
echo "Creating issue: P2-02 - Write missing Architecture Decision Records"
gh issue create \
  --title "P2-02: Document architectural decisions in ADRs" \
  --label "priority:medium,type:documentation" \
  --body "## Problem

**Priority**: P2 (Medium)
**Type**: Documentation

Only one ADR exists (ADR-001: Message Bus), but several major architectural decisions lack documentation.

### Missing ADRs

1. **ADR-002: Work-Stealing Scheduler Architecture**
   - Why work-stealing over thread pool?
   - LIFO owner, FIFO steal rationale
   - CPU affinity decision

2. **ADR-003: Priority Queue Anti-Starvation Strategy**
   - Why time-based (not count-based) fairness?
   - 100ms interval choice
   - Tradeoffs: latency vs throughput

3. **ADR-004: Coroutine Integration with Scheduler**
   - Task<T> design decisions
   - Symmetric transfer vs explicit submission
   - Scheduler lifecycle management

4. **ADR-005: Message Pool Design**
   - Object pooling rationale
   - Pool sizing strategy
   - Thread-safety approach

### Template
Use existing ADR-001 as template:
\`\`\`
# ADR-XXX: Title

## Status
Accepted

## Context
Problem statement

## Decision
What we decided

## Consequences
Positive and negative outcomes

## Alternatives Considered
Other options and why rejected
\`\`\`

### Effort Estimate
4-6 hours (1-1.5 hours per ADR)

### Benefits
- New developers understand design rationale
- Future refactorings preserve intent
- Design decisions auditable"

echo "✅ Created P2-02"
echo ""

# =============================================================================
# P2-05: Add Agent Concepts for Type Safety
# =============================================================================
echo "Creating issue: P2-05 - Add C++20 concepts for Agent type safety"
gh issue create \
  --title "P2-05: Add C++20 concepts for compile-time Agent verification" \
  --label "priority:medium,type:enhancement,component:agents" \
  --body "## Problem

**Priority**: P2 (Medium)
**Type**: Type Safety Enhancement

No compile-time verification that agents implement required interface. Errors caught at link time or runtime instead of compile time.

### Current State
\`\`\`cpp
class BaseAgent : public AgentBase {
    virtual Task<Response> processMessage(const KeystoneMessage& msg) = 0;
};
\`\`\`

### Proposed Enhancement

Add concepts for agent types:

\`\`\`cpp
template<typename T>
concept Agent = requires(T agent, KeystoneMessage msg) {
    { agent.processMessage(msg) } -> std::same_as<Task<Response>>;
    { agent.getAgentId() } -> std::convertible_to<std::string>;
    { agent.sendMessage(msg) } -> std::same_as<void>;
};

// Use in templates
template<Agent A>
void registerAgentGeneric(MessageBus& bus, std::shared_ptr<A> agent) {
    bus.registerAgent(agent->getAgentId(), agent);
}
\`\`\`

### Benefits
- **Compile-time errors**: Catch missing methods early
- **Better error messages**: Concepts give clearer errors than SFINAE
- **Documentation**: Concepts document requirements explicitly
- **Generic code**: Enable generic agent algorithms

### Implementation Plan
1. Define \`Agent\` concept in \`include/agents/concepts.hpp\`
2. Add \`AsyncAgent\`, \`SyncAgent\` specializations if needed
3. Update MessageBus::registerAgent to use concept
4. Add concept-based tests
5. Document in ADR

### Effort Estimate
2-3 hours

### Optional Extensions
- \`MessageHandler\` concept for processMessage
- \`Identifiable\` concept for getAgentId
- \`MessageSender\` concept for sendMessage"

echo "✅ Created P2-05"
echo ""

# =============================================================================
# P2-06: Optimize String Allocations in Hot Path
# =============================================================================
echo "Creating issue: P2-06 - Profile and optimize string allocations in message passing"
gh issue create \
  --title "P2-06: Profile and optimize string allocations in hot paths" \
  --label "priority:medium,type:performance" \
  --body "## Problem

**Priority**: P2 (Medium - measure first)
**Type**: Performance Optimization

Frequent string allocations in message passing hot path. Each message creation allocates 4+ strings (msg_id UUID, sender, receiver, command).

### Current Behavior
\`\`\`cpp
auto msg = KeystoneMessage::create(\"sender\", \"receiver\", \"command\");
// Creates 4+ heap allocations (msg_id UUID, sender, receiver, command)
\`\`\`

### Impact
- High allocation rate under load (10K msgs/sec = 40K+ allocations/sec)
- GC pressure (shared_ptr control blocks)
- Cache misses
- **Unknown actual impact - needs profiling**

### Action Plan

**Phase 1: Measure (Required First)**
1. Benchmark message creation rate
2. Profile allocation hotspots with \`perf\` or \`valgrind --tool=massif\`
3. Measure message throughput with/without allocations
4. Determine if optimization is worth effort

**Phase 2: Optimize (If Metrics Justify)**

**Option 1: String Interning for Agent IDs**
\`\`\`cpp
class StringPool {
    std::unordered_set<std::string> pool_;
    std::mutex mutex_;
public:
    const std::string& intern(const std::string& str) {
        std::lock_guard lock(mutex_);
        return *pool_.insert(str).first;  // Returns existing or inserts new
    }
};
\`\`\`

**Option 2: Integer Agent IDs**
\`\`\`cpp
using AgentId = uint64_t;
std::unordered_map<AgentId, std::shared_ptr<AgentBase>> agents_;
\`\`\`

**Option 3: Verify Message Pooling is Enabled**
- Check if Phase D.2 message pooling is active
- Measure pool hit rate
- Tune pool size

### Benchmarks Needed
- Message creation rate (msgs/sec)
- Allocation count (allocs/sec)
- Memory usage under load
- Latency percentiles (p50, p95, p99)

### Effort Estimate
- Profiling: 2 hours
- Optimization (if needed): 4-6 hours

### Success Criteria
- 2x improvement in message throughput, OR
- 50% reduction in allocations, OR
- Data shows optimization not needed (close issue)"

echo "✅ Created P2-06"
echo ""

# =============================================================================
# P2-07: Make Priority Queue Fairness Configurable
# =============================================================================
echo "Creating issue: P2-07 - Make priority queue fairness interval configurable per-agent"
gh issue create \
  --title "P2-07: Make low-priority fairness interval configurable" \
  --label "priority:medium,type:enhancement,component:agents" \
  --body "## Problem

**Priority**: P2 (Medium)
**Type**: Configurability Enhancement

Time-based fairness check (100ms) is a global constant, but different agents may have different latency requirements.

### Current State
\`\`\`cpp
static constexpr std::chrono::milliseconds AGENT_LOW_PRIORITY_CHECK_INTERVAL{100};
\`\`\`

### Scenario
- High-throughput system (10K msgs/sec)
- LOW priority messages wait 100ms minimum
- This is 1,000 messages worth of latency!
- Some agents need lower latency, others can tolerate higher

### Proposed Solution

**Option 1: Per-Agent Configuration**
\`\`\`cpp
class AgentBase {
    std::chrono::milliseconds low_priority_interval_;
public:
    void setFairnessInterval(std::chrono::milliseconds interval) {
        low_priority_interval_ = interval;
    }
};
\`\`\`

**Option 2: Adaptive Based on Queue Depth**
\`\`\`cpp
auto interval = (high_queue_depth > 1000) ? 50ms : 100ms;
\`\`\`

**Option 3: Priority-Specific Intervals**
\`\`\`cpp
struct PriorityConfig {
    std::chrono::milliseconds high_check_interval{10};
    std::chrono::milliseconds normal_check_interval{50};
    std::chrono::milliseconds low_check_interval{100};
};
\`\`\`

### Discussion Needed
1. Which approach provides best flexibility/complexity tradeoff?
2. Should this be runtime configurable or compile-time?
3. Do we need per-agent or system-wide is sufficient?

### Effort Estimate
2 hours

### Benefits
- Latency-sensitive agents can reduce interval
- Throughput-optimized agents can increase interval
- Adaptive approach automatically tunes"

echo "✅ Created P2-07"
echo ""

# =============================================================================
# Summary
# =============================================================================
echo ""
echo "=========================================="
echo "✅ Successfully created 7 GitHub issues"
echo "=========================================="
echo ""
echo "Issues created:"
echo "  - P1-01: Fix Task<>::await_suspend (priority:high)"
echo "  - P1-02: Add shutdown condition variable (priority:high)"
echo "  - P2-01: Integrate coroutines with scheduler (priority:medium)"
echo "  - P2-02: Write missing ADRs (priority:medium)"
echo "  - P2-05: Add Agent concepts (priority:medium)"
echo "  - P2-06: Profile string allocations (priority:medium)"
echo "  - P2-07: Configurable fairness interval (priority:medium)"
echo ""
echo "View all issues:"
echo "  gh issue list --label priority:high,priority:medium"
echo ""
echo "Note: Issues P2-08 and P2-09 (tests) will be implemented directly."
echo "Note: P3-08 (error sanitization) is minor and can be done as needed."
