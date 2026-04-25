# Phase 5: Robustness & Chaos Engineering Plan

**Status**: 📝 Planning Phase
**Date**: 2025-11-19
**Branch**: `claude/work-stealing-migration-01D8QY8wG6fDLMjVeu3H8UCC`

## Overview

Phase 5 focuses on **robustness and fault tolerance** by subjecting the HMAS to chaos engineering scenarios. The goal is to ensure the system gracefully handles failures, recovers from errors, and maintains correctness under adverse conditions.

### Current Status (Post-Phase 4)

**What We Have**:

- ✅ Complete 4-layer hierarchy (L0 ↔ L1 ↔ L2 ↔ L3)
- ✅ Multi-component coordination with dependency resolution
- ✅ Work-stealing scheduler with 4 workers
- ✅ Message priorities and deadline scheduling
- ✅ Performance optimizations (CPU affinity, message pooling)
- ✅ Distributed simulation framework (NUMA nodes, network simulation)
- ✅ Scale testing: 100-150 agents successfully running
- ✅ **263 tests passing** in ~9 seconds

**What Phase 5 Adds**:

- Agent failure injection (random crashes, timeouts)
- Network partition simulation (split-brain scenarios)
- Message loss and duplication scenarios
- Recovery mechanisms (retry logic, fallback strategies)
- Chaos testing framework

---

## Phase 5 Architecture

### Chaos Engineering Principles

1. **Define Steady State**: Normal operation with all 263 tests passing
2. **Hypothesis**: System should tolerate X% agent failures without total failure
3. **Introduce Chaos**: Inject failures, partitions, message loss
4. **Verify Hypothesis**: Measure recovery time, correctness, throughput
5. **Improve**: Add recovery mechanisms based on findings

### Fault Types to Simulate

```
┌────────────────────────────────────────────────────┐
│ Agent Failures                                     │
│  - Random crashes (agent stops processing)         │
│  - Timeouts (agent slow to respond)                │
│  - Resource exhaustion (queue overflow)            │
└────────────────────────────────────────────────────┘
           │
           ▼
┌────────────────────────────────────────────────────┐
│ Network Failures                                   │
│  - Partitions (nodes can't communicate)            │
│  - Packet loss (messages dropped)                  │
│  - Message duplication (same msg delivered 2x)     │
│  - Out-of-order delivery                           │
└────────────────────────────────────────────────────┘
           │
           ▼
┌────────────────────────────────────────────────────┐
│ Recovery Mechanisms                                │
│  - Retry logic (exponential backoff)               │
│  - Fallback strategies (alternate agents)          │
│  - Circuit breakers (stop cascading failures)      │
│  - Heartbeat monitoring (detect dead agents)       │
└────────────────────────────────────────────────────┘
```

---

## Phase 5 Implementation Plan

### Phase 5.1: Agent Failure Simulation (Week 1)

**Goal**: Simulate agent crashes and measure system resilience

#### Tasks

1. **Create `FailureInjector` class** (4 hours)
   - `injectAgentCrash(agent_id)` - Stop agent from processing messages
   - `injectAgentTimeout(agent_id, delay)` - Delay responses
   - `injectAgentOverload(agent_id)` - Fill queue to capacity
   - Track failure events for analysis

2. **Add failure modes to agents** (3 hours)
   - Add `is_failed_` flag to `AsyncBaseAgent`
   - When failed, agents reject all messages with error response
   - Add `markAsFailed()` and `recover()` methods

3. **E2E Test: Single Agent Failure** (3 hours)
   - Start 4-layer hierarchy (Chief → Component → Module → 3 Tasks)
   - Fail 1 TaskAgent mid-execution
   - Verify: ModuleLead detects failure and reports error
   - Verify: System continues processing other tasks

**Deliverables**:

- ✅ FailureInjector class
- ✅ Agent failure modes implemented
- ✅ E2E test verifying graceful degradation

**Estimated Time**: 10 hours

---

### Phase 5.2: Network Partition Simulation (Week 2)

**Goal**: Test distributed system behavior under network partitions

#### Tasks

1. **Enhance `SimulatedNetwork` for partitions** (4 hours)
   - `createPartition(nodes_A, nodes_B)` - Split cluster
   - Messages between partitions are dropped
   - Messages within partition work normally
   - `healPartition()` - Restore connectivity

2. **Split-brain scenario test** (3 hours)
   - 4-node cluster: [Node 0, 1] vs [Node 2, 3]
   - ChiefArchitect on Node 0, TaskAgents on Nodes 2-3
   - Partition occurs → messages fail
   - Heal partition → messages resume

3. **E2E Test: Network Partition Recovery** (3 hours)
   - Start distributed hierarchy across 4 nodes
   - Create partition mid-execution
   - Verify: Messages fail gracefully (no crashes)
   - Heal partition
   - Verify: System recovers and completes work

**Deliverables**:

- ✅ Network partition support in SimulatedNetwork
- ✅ Split-brain scenario implemented
- ✅ E2E test for partition + recovery

**Estimated Time**: 10 hours

---

### Phase 5.3: Message Loss Scenarios (Week 3)

**Goal**: Test message delivery guarantees and retries

#### Tasks

1. **Add message loss to `SimulatedNetwork`** (3 hours)
   - Configurable loss rate (e.g., 10% of messages dropped)
   - Random vs deterministic loss
   - Loss tracking for analysis

2. **Implement retry logic in agents** (4 hours)
   - Add `RetryPolicy` class (max attempts, backoff strategy)
   - Agents retry failed messages with exponential backoff
   - Track retry attempts in metrics

3. **E2E Test: Message Loss with Retries** (3 hours)
   - Configure 20% message loss rate
   - Send 100 commands through hierarchy
   - Verify: All commands eventually complete (via retries)
   - Measure: Average retry count, total time

**Deliverables**:

- ✅ Message loss simulation
- ✅ Retry policy implementation
- ✅ E2E test with retries

**Estimated Time**: 10 hours

---

### Phase 5.4: Recovery Mechanisms (Week 4)

**Goal**: Implement automated recovery strategies

#### Tasks

1. **Heartbeat monitoring** (4 hours)
   - Agents send periodic heartbeats
   - Monitor detects missing heartbeats → marks agent as failed
   - Automatic failover to backup agent

2. **Circuit breaker pattern** (3 hours)
   - Track consecutive failures per agent
   - If failures > threshold, open circuit (stop sending)
   - After timeout, try again (half-open state)
   - If successful, close circuit (resume normal)

3. **E2E Test: Full Chaos Scenario** (3 hours)
   - 100-agent system with:
     - 10% random agent failures
     - 10% message loss
     - 1 network partition (heals after 5 sec)
   - Verify: System completes all work (with retries)
   - Measure: Recovery time, throughput degradation

**Deliverables**:

- ✅ Heartbeat monitoring system
- ✅ Circuit breaker implementation
- ✅ Full chaos E2E test

**Estimated Time**: 10 hours

---

## Success Criteria

### Must Have ✅

- [ ] System tolerates 10% agent failures without total failure
- [ ] Network partitions handled gracefully (no crashes)
- [ ] Message loss scenarios recovered via retries
- [ ] All existing 263 tests still passing
- [ ] New E2E tests for chaos scenarios

### Should Have 🎯

- [ ] Recovery time < 5 seconds for single agent failure
- [ ] Retry logic succeeds for 95% of lost messages
- [ ] Circuit breaker prevents cascading failures
- [ ] Heartbeat monitoring detects failures within 1 second

### Nice to Have 💡

- [ ] Chaos test suite runs automatically in CI/CD
- [ ] Failure injection via configuration file
- [ ] Real-time monitoring dashboard (ASCII art in terminal)
- [ ] Automated chaos experiments (Netflix Chaos Monkey style)

---

## Test Plan

### E2E Tests (Phase 5)

1. **SingleAgentFailure** (Priority: CRITICAL)
   - Fail 1 TaskAgent in 4-layer hierarchy
   - Verify graceful degradation
   - System continues with remaining agents

2. **NetworkPartitionRecovery** (Priority: HIGH)
   - Split 4-node cluster into 2 partitions
   - Verify messages fail gracefully
   - Heal partition and verify recovery

3. **MessageLossWithRetries** (Priority: HIGH)
   - Configure 20% message loss
   - Send 100 commands
   - Verify all complete via retries

4. **CascadingFailurePrevention** (Priority: HIGH)
   - Fail multiple agents in sequence
   - Circuit breaker should prevent cascade
   - System stabilizes after failures stop

5. **FullChaosScenario** (Priority: CRITICAL)
   - 100 agents + 10% failures + 10% message loss + partition
   - Measure: Recovery time, throughput, correctness
   - All work completes eventually

---

## Performance Expectations

Based on Phase 4 results:

**Baseline (No Failures)**:

- 100-agent test: ~50-80ms creation time
- All 263 tests pass in ~9 seconds

**Phase 5 Targets (With Chaos)**:

- **Single failure**: Recovery within 5 seconds
- **10% failures**: System completes with ≤20% throughput degradation
- **Message loss (20%)**: ≤3 retries average per message
- **Network partition**: Recovery within 10 seconds of heal

---

## Risk Mitigation

### Risk 1: Cascading Failures

**Impact**: High
**Likelihood**: Medium
**Mitigation**: Circuit breaker pattern stops propagation

### Risk 2: Infinite Retry Loops

**Impact**: Medium
**Likelihood**: High
**Mitigation**: Max retry limit (e.g., 5 attempts) with exponential backoff

### Risk 3: Test Flakiness (Non-Deterministic)

**Impact**: High
**Likelihood**: High
**Mitigation**: Seeded random failures, multiple test runs for verification

### Risk 4: Recovery Mechanisms Add Complexity

**Impact**: Medium
**Likelihood**: Low
**Mitigation**: Keep recovery simple, test thoroughly, document well

---

## Implementation Notes

### FailureInjector Pattern

```cpp
class FailureInjector {
public:
    // Crash simulation
    void injectAgentCrash(const std::string& agent_id);

    // Timeout simulation
    void injectAgentTimeout(const std::string& agent_id, std::chrono::milliseconds delay);

    // Random failures
    void setFailureRate(double rate);  // 0.0 - 1.0
    bool shouldFail();  // Roll dice based on rate

    // Statistics
    int getTotalFailures() const;
    std::vector<std::string> getFailedAgents() const;
};
```

### RetryPolicy Pattern

```cpp
struct RetryPolicy {
    int max_attempts{5};
    std::chrono::milliseconds initial_delay{100};
    double backoff_multiplier{2.0};
    std::chrono::milliseconds max_delay{5000};
};

class RetryableAgent : public AsyncBaseAgent {
    Task<Response> processMessageWithRetry(const KeystoneMessage& msg);
};
```

### Circuit Breaker Pattern

```cpp
class CircuitBreaker {
public:
    enum class State { CLOSED, OPEN, HALF_OPEN };

    bool allowRequest(const std::string& agent_id);
    void recordSuccess(const std::string& agent_id);
    void recordFailure(const std::string& agent_id);

private:
    int failure_threshold_{5};
    std::chrono::seconds timeout_{10};
};
```

---

## Next Steps

1. **Week 1**: Implement agent failure simulation
   - Create FailureInjector class
   - Add failure modes to agents
   - E2E test for single failure

2. **Week 2**: Network partition simulation
   - Enhance SimulatedNetwork
   - Implement split-brain scenarios
   - E2E test for partition recovery

3. **Week 3**: Message loss scenarios
   - Add loss simulation to network
   - Implement retry logic
   - E2E test with retries

4. **Week 4**: Recovery mechanisms
   - Heartbeat monitoring
   - Circuit breaker
   - Full chaos E2E test

**After Phase 5**: System is production-ready for real-world deployment

- All failure modes tested
- Recovery mechanisms validated
- Robustness demonstrated at scale

---

**Status**: 📝 Planning Complete - Ready for Implementation
**Total Estimated Time**: 40 hours (~4 weeks)
**Last Updated**: 2025-11-19
