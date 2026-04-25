# TDD Roadmap for 4-Layer Architecture

## Overview

This document outlines the **Test-Driven Development (TDD) roadmap** for building the 4-layer Hierarchical Multi-Agent System, starting with a minimal 2-agent prototype and incrementally adding layers until the full hierarchy is complete.

## Incremental Build Strategy

```
Phase 1: L0 ←→ L3 (2 agents)
    ↓
Phase 2: L0 ←→ L2 ←→ L3 (3 layers)
    ↓
Phase 3: L0 ←→ L1 ←→ L2 ←→ L3 (4 layers, single component)
    ↓
Phase 4: L0 ←→ [L1 × 3] ←→ [L2 × n] ←→ [L3 × m] (full system)
```

---

## Phase 1: Chief Architect + Task Agent (Weeks 1-3)

### Goal

Validate core infrastructure with the simplest possible hierarchy.

### Architecture

```
┌──────────────────────────┐
│ Level 0                  │
│ ChiefArchitectAgent      │
│ (Orchestrator)           │
└──────────┬───────────────┘
           │
           │ Delegates task via KIM message
           │
           ▼
┌──────────────────────────┐
│ Level 3                  │
│ TaskAgent                │
│ (Executor)               │
└──────────────────────────┘
```

### E2E Test 1.1: Basic Delegation

```cpp
// tests/e2e/Phase1_BasicDelegation.cpp

TEST(E2E_Phase1, ChiefArchitectDelegatesToTaskAgent) {
    // ARRANGE
    ThreadPool pool{2};
    MessageBus bus;

    auto chief = std::make_unique<ChiefArchitectAgent>("chief_1");
    auto task_agent = std::make_unique<TaskAgent>("task_1");

    bus.registerAgent("chief_1", chief.get());
    bus.registerAgent("task_1", task_agent.get());

    // Start agent loops
    pool.submit([&]() { chief->run(); });
    pool.submit([&]() { task_agent->run(); });

    // ACT: User sends goal to Chief Architect
    std::string goal = "Implement MessageQueue::push() method";
    auto result_future = chief->processGoal(goal);

    // ASSERT: Verify end-to-end execution
    auto status = result_future.wait_for(std::chrono::seconds(5));
    ASSERT_EQ(status, std::future_status::ready);

    TaskResult result = result_future.get();
    EXPECT_EQ(result.status, "completed");
    EXPECT_FALSE(result.output.empty());

    // Verify message flow
    EXPECT_EQ(chief->getMessagesSent(), 1);      // Task to L3
    EXPECT_EQ(task_agent->getMessagesReceived(), 1); // Task received
    EXPECT_EQ(task_agent->getMessagesSent(), 1);     // Result sent
    EXPECT_EQ(chief->getMessagesReceived(), 1);      // Result received

    // Cleanup
    chief->shutdown();
    task_agent->shutdown();
    pool.shutdown();
}
```

### E2E Test 1.2: Multiple Task Agents

```cpp
TEST(E2E_Phase1, ChiefArchitectCoordinatesThreeTaskAgents) {
    ThreadPool pool{8};
    MessageBus bus;

    auto chief = std::make_unique<ChiefArchitectAgent>("chief_1");

    std::vector<std::unique_ptr<TaskAgent>> task_agents;
    for (int i = 0; i < 3; ++i) {
        auto agent = std::make_unique<TaskAgent>("task_" + std::to_string(i));
        bus.registerAgent(agent->getId(), agent.get());
        pool.submit([&agent]() { agent->run(); });
        task_agents.push_back(std::move(agent));
    }

    bus.registerAgent(chief->getId(), chief.get());
    pool.submit([&]() { chief->run(); });

    // Send goal requiring 3 parallel tasks
    std::string goal = "Implement MessageQueue: push(), try_pop(), size()";
    auto result = chief->processGoal(goal);

    EXPECT_EQ(result.get().subtasks_completed, 3);

    // Verify all 3 agents participated
    for (const auto& agent : task_agents) {
        EXPECT_EQ(agent->getTasksCompleted(), 1);
    }

    // Cleanup
    chief->shutdown();
    for (auto& agent : task_agents) {
        agent->shutdown();
    }
    pool.shutdown();
}
```

### Deliverables (Phase 1)

- ✅ `ChiefArchitectAgent` class with task delegation
- ✅ `TaskAgent` class with execution
- ✅ `MessageBus` for routing
- ✅ `KeystoneMessage` structure (KIM protocol)
- ✅ `ThreadPool` with coroutine support
- ✅ `MessageQueue` with `concurrentqueue`
- ✅ Custom `AsyncQueuePop` awaitable
- ✅ E2E test: 1 Chief + 1 Task agent
- ✅ E2E test: 1 Chief + 3 Task agents (parallel execution)

**Timeline**: 3 weeks

---

## Phase 2: Add Module Lead Layer (Weeks 4-6)

### Goal

Introduce intermediate coordination layer for task synthesis.

### Architecture

```
┌──────────────────────────┐
│ Level 0                  │
│ ChiefArchitectAgent      │
└──────────┬───────────────┘
           │
           │ "Implement Messaging module"
           ▼
┌──────────────────────────┐
│ Level 2                  │
│ ModuleLeadAgent          │
│ (Messaging)              │
└──────────┬───────────────┘
           │
           ├─ Task: "Implement push()"
           ├─ Task: "Implement try_pop()"
           └─ Task: "Write tests"
           │
           ▼
┌──────────────────────────┐
│ Level 3 (×3)             │
│ TaskAgent                │
└──────────────────────────┘
```

### E2E Test 2.1: Module Lead Synthesizes Results

```cpp
TEST(E2E_Phase2, ModuleLeadSynthesizesTaskResults) {
    // Setup: Chief → Module Lead → 3 Task Agents
    auto system = createThreeLayerSystem(
        /*module_leads=*/1,
        /*tasks_per_module=*/3
    );

    // Goal requires module-level coordination
    std::string goal = "Implement MessageQueue module (push, pop, size)";
    auto result = system.chief->processGoal(goal);

    // Verify Module Lead synthesized results from 3 Task Agents
    auto module_result = result.get();
    EXPECT_EQ(module_result.tasks_completed, 3);
    EXPECT_TRUE(module_result.module_complete);
    EXPECT_FALSE(module_result.code_output.empty());

    // Verify state transitions
    auto trace = system.module_lead->getExecutionTrace();
    ASSERT_GE(trace.size(), 5);
    EXPECT_EQ(trace[0], "IDLE");
    EXPECT_EQ(trace[1], "PLANNING");
    EXPECT_EQ(trace[2], "WAITING_FOR_TASKS");
    EXPECT_EQ(trace[3], "SYNTHESIZING");
    EXPECT_EQ(trace[4], "IDLE");
}
```

### E2E Test 2.2: Module Lead Handles Task Failure

```cpp
TEST(E2E_Phase2, ModuleLeadRecoversFromTaskFailure) {
    auto system = createThreeLayerSystem(1, 3);

    // Inject failure in one Task Agent
    system.task_agents[1]->injectFailure("timeout");

    std::string goal = "Implement MessageQueue module";
    auto result = system.chief->processGoal(goal);

    // Module Lead should detect failure and retry
    auto module_result = result.get();
    EXPECT_TRUE(module_result.module_complete);

    // Verify retry logic
    auto trace = system.module_lead->getExecutionTrace();
    EXPECT_TRUE(containsPattern(trace, "Task failed → Retrying"));

    // Verify alternative Task Agent was used
    int tasks_completed = 0;
    for (const auto& agent : system.task_agents) {
        tasks_completed += agent->getTasksCompleted();
    }
    EXPECT_GE(tasks_completed, 3);  // At least 3 (including retry)
}
```

### Deliverables (Phase 2)

- ✅ `ModuleLeadAgent` class
- ✅ Task decomposition logic
- ✅ Result synthesis logic
- ✅ `PhaseBarrier` for task synchronization
- ✅ Transaction state management
- ✅ Retry/recovery logic
- ✅ E2E test: Chief → Module Lead → 3 Tasks
- ✅ E2E test: Module Lead handles task failure

**Timeline**: 3 weeks

---

## Phase 3: Add Component Lead Layer (Weeks 7-9)

### Goal

Introduce component-level coordination across multiple modules.

### Architecture

```
┌──────────────────────────┐
│ Level 0                  │
│ ChiefArchitectAgent      │
└──────────┬───────────────┘
           │
           │ "Implement Core component"
           ▼
┌──────────────────────────┐
│ Level 1                  │
│ ComponentLeadAgent       │
│ (Core)                   │
└──────────┬───────────────┘
           │
           ├─ Module: Messaging
           └─ Module: Concurrency
           │
           ▼
┌──────────────────────────┐
│ Level 2 (×2)             │
│ ModuleLeadAgent          │
└──────────┬───────────────┘
           │
           └─ Tasks...
           │
           ▼
┌──────────────────────────┐
│ Level 3 (×6)             │
│ TaskAgent                │
└──────────────────────────┘
```

### E2E Test 3.1: Component Lead Coordinates Modules

```cpp
TEST(E2E_Phase3, ComponentLeadCoordinatesMultipleModules) {
    // Setup: Chief → Component Lead → 2 Module Leads → 6 Task Agents
    auto system = createFourLayerSystem(
        /*components=*/1,
        /*modules_per_component=*/2,
        /*tasks_per_module=*/3
    );

    // Goal requires component-level coordination
    std::string goal = "Implement Core component: Messaging + Concurrency";
    auto result = system.chief->processGoal(goal);

    // Verify Component Lead coordinated both modules
    auto component_result = result.get();
    EXPECT_EQ(component_result.modules_completed.size(), 2);
    EXPECT_TRUE(component_result.modules_completed.contains("Messaging"));
    EXPECT_TRUE(component_result.modules_completed.contains("Concurrency"));

    // Verify all Module Leads participated
    for (const auto& module_lead : system.module_leads) {
        EXPECT_GT(module_lead->getTasksProcessed(), 0);
    }

    // Verify all Task Agents participated
    for (const auto& task_agent : system.task_agents) {
        EXPECT_GT(task_agent->getTasksCompleted(), 0);
    }
}
```

### E2E Test 3.2: Component Lead Resolves Module Dependencies

```cpp
TEST(E2E_Phase3, ComponentLeadResolvesDependencies) {
    auto system = createFourLayerSystem(1, 2, 3);

    // Messaging module depends on Concurrency (ThreadPool for MessageBus)
    system.component_lead->addDependency("Messaging", "Concurrency");

    std::string goal = "Implement Core: Messaging (depends on Concurrency)";
    auto result = system.chief->processGoal(goal);

    // Verify Concurrency completed before Messaging started
    auto execution_order = system.component_lead->getModuleExecutionOrder();
    ASSERT_EQ(execution_order.size(), 2);
    EXPECT_EQ(execution_order[0], "Concurrency");
    EXPECT_EQ(execution_order[1], "Messaging");

    auto component_result = result.get();
    EXPECT_TRUE(component_result.all_modules_complete);
}
```

### Deliverables (Phase 3)

- ✅ `ComponentLeadAgent` class
- ✅ Module coordination logic
- ✅ Dependency resolution
- ✅ Cross-module interface validation
- ✅ E2E test: Component Lead manages 2 modules
- ✅ E2E test: Dependency-ordered execution

**Timeline**: 3 weeks

---

## Phase 4: Full Multi-Component System (Weeks 10-12)

### Goal

Complete 4-layer hierarchy with multiple components executing in parallel.

### Architecture

```
┌──────────────────────────────────────────────────────────┐
│ Level 0: ChiefArchitectAgent                             │
└──────────┬───────────────────────────────────────────────┘
           │
           ├─ Component: Core
           ├─ Component: Protocol
           ├─ Component: Agents
           └─ Component: Integration
           │
           ▼
┌──────────────────────────────────────────────────────────┐
│ Level 1: ComponentLeadAgent (×4)                         │
└──────────┬───────────────────────────────────────────────┘
           │
           └─ Modules (3-5 per component)
           │
           ▼
┌──────────────────────────────────────────────────────────┐
│ Level 2: ModuleLeadAgent (×15)                           │
└──────────┬───────────────────────────────────────────────┘
           │
           └─ Tasks (5-10 per module)
           │
           ▼
┌──────────────────────────────────────────────────────────┐
│ Level 3: TaskAgent (×75)                                 │
└──────────────────────────────────────────────────────────┘
```

### E2E Test 4.1: Full System Execution

```cpp
TEST(E2E_Phase4, FullSystemImplementsAllComponents) {
    // Create full hierarchy
    auto system = createFullHierarchy(
        /*components=*/4,
        /*modules_per_component=*/3,
        /*tasks_per_module=*/5
    );

    // Complex goal requiring all components
    std::string goal = R"(
        Build complete HMAS:
        - Core: Messaging, Concurrency, Utilities
        - Protocol: KIM, Serialization, GRPC
        - Agents: Base, ChiefArchitect, ComponentLead, ModuleLead, Task
        - Integration: AI, Monitoring
    )";

    auto result = system.chief->processGoal(goal);

    // Verify all components completed
    auto system_result = result.get();
    EXPECT_EQ(system_result.components_completed.size(), 4);
    EXPECT_TRUE(system_result.components_completed.contains("Core"));
    EXPECT_TRUE(system_result.components_completed.contains("Protocol"));
    EXPECT_TRUE(system_result.components_completed.contains("Agents"));
    EXPECT_TRUE(system_result.components_completed.contains("Integration"));

    // Verify statistics
    EXPECT_EQ(system_result.total_modules_completed, 12);
    EXPECT_GT(system_result.total_tasks_completed, 50);
}
```

### E2E Test 4.2: Parallel Component Execution

```cpp
TEST(E2E_Phase4, ComponentsExecuteInParallel) {
    auto system = createFullHierarchy(4, 3, 5);

    // Components Core and Protocol are independent
    std::string goal = "Implement Core and Protocol in parallel";

    auto start = std::chrono::high_resolution_clock::now();
    auto result = system.chief->processGoal(goal);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // Verify parallelism: should take ~1x time, not 2x
    // (If sequential: 2000ms, if parallel: ~1100ms due to overhead)
    EXPECT_LT(duration_ms, 1500);  // Much less than 2x

    // Verify both components completed
    auto system_result = result.get();
    EXPECT_TRUE(system_result.components_completed.contains("Core"));
    EXPECT_TRUE(system_result.components_completed.contains("Protocol"));
}
```

### E2E Test 4.3: Cross-Component Dependencies

```cpp
TEST(E2E_Phase4, ChiefArchitectResolvesCrossComponentDependencies) {
    auto system = createFullHierarchy(4, 3, 5);

    // Agents component depends on Protocol and Core
    system.chief->addComponentDependency("Agents", {"Core", "Protocol"});

    std::string goal = "Implement all components with dependencies";
    auto result = system.chief->processGoal(goal);

    // Verify execution order respected dependencies
    auto execution_order = system.chief->getComponentExecutionOrder();

    auto core_idx = std::find(execution_order.begin(), execution_order.end(), "Core") - execution_order.begin();
    auto protocol_idx = std::find(execution_order.begin(), execution_order.end(), "Protocol") - execution_order.begin();
    auto agents_idx = std::find(execution_order.begin(), execution_order.end(), "Agents") - execution_order.begin();

    // Agents must come after both Core and Protocol
    EXPECT_LT(core_idx, agents_idx);
    EXPECT_LT(protocol_idx, agents_idx);
}
```

### Deliverables (Phase 4)

- ✅ Multi-component coordination
- ✅ Cross-component dependency resolution
- ✅ Parallel component execution
- ✅ Global resource allocation
- ✅ E2E test: All 4 components complete
- ✅ E2E test: Parallel execution verification
- ✅ E2E test: Cross-component dependencies

**Timeline**: 3 weeks

---

## Phase 5: Performance & Robustness (Weeks 13-14)

### E2E Test 5.1: Performance at Scale

```cpp
TEST(E2E_Phase5, SystemScalesTo100TaskAgents) {
    auto system = createFullHierarchy(
        /*components=*/4,
        /*modules_per_component=*/5,
        /*tasks_per_module=*/5  // 100 Task Agents total
    );

    auto start = std::chrono::high_resolution_clock::now();

    std::string goal = "Implement all components";
    auto result = system.chief->processGoal(goal);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // Should complete in reasonable time despite scale
    EXPECT_LT(duration_ms, 10000);  // < 10 seconds

    // Verify all agents participated
    auto system_result = result.get();
    EXPECT_EQ(system_result.total_tasks_completed, 100);
}
```

### E2E Test 5.2: Chaos Engineering

```cpp
TEST(E2E_Phase5, SystemRecoversFro mRandomFailures) {
    auto system = createFullHierarchy(3, 4, 6);

    // Inject chaos while processing
    std::thread chaos_thread([&]() {
        for (int i = 0; i < 20; ++i) {
            std::this_thread::sleep_for(100ms);

            // Random failure injection
            auto random_agent = system.task_agents[rand() % system.task_agents.size()];
            random_agent->injectFailure("crash");
        }
    });

    std::string goal = "Implement all components despite chaos";
    auto result = system.chief->processGoal(goal);

    chaos_thread.join();

    // System should recover and complete
    auto system_result = result.get();
    EXPECT_GT(system_result.components_completed.size(), 2);  // Most completed

    // Verify recovery mechanisms worked
    auto recovery_stats = system.chief->getRecoveryStatistics();
    EXPECT_GT(recovery_stats.failures_detected, 0);
    EXPECT_GT(recovery_stats.successful_recoveries, 0);
}
```

### Deliverables (Phase 5)

- ✅ Performance benchmarks (100+ agents)
- ✅ Chaos testing framework
- ✅ Failure injection utilities
- ✅ Recovery statistics
- ✅ E2E test: Scale to 100 agents
- ✅ E2E test: Chaos resilience
- ✅ E2E test: 24-hour stability

**Timeline**: 2 weeks

---

## Total Timeline Summary

| Phase | Weeks | Layers | Focus |
|-------|-------|--------|-------|
| **Phase 1** | 1-3 | L0 + L3 | Core infrastructure, basic delegation |
| **Phase 2** | 4-6 | L0 + L2 + L3 | Task synthesis, retry logic |
| **Phase 3** | 7-9 | L0 + L1 + L2 + L3 | Component coordination, dependencies |
| **Phase 4** | 10-12 | Full system | Multi-component, parallelism |
| **Phase 5** | 13-14 | Full system | Performance, chaos testing |
| **Total** | **14 weeks** | | |

---

## Daily TDD Workflow

### Morning (9 AM - 12 PM)

1. Review failing E2E test from yesterday
2. Identify minimal code needed to pass
3. Implement in TDD cycles:
   - Write smallest unit test
   - Make it pass
   - Refactor
   - Repeat

### Afternoon (1 PM - 5 PM)

4. Run E2E test
5. If failing: debug and iterate
6. If passing: refactor and optimize
7. Write next E2E test for tomorrow
8. Commit: "GREEN: E2E test XYZ now passing"

### Weekly Checkpoint (Friday)

- All E2E tests for the week must pass
- Performance benchmarks run
- Code review session
- Plan next week's E2E tests

---

## Success Criteria (E2E-Based)

### Week 3 (Phase 1)

- [ ] E2E: Chief delegates to 1 Task Agent
- [ ] E2E: Chief coordinates 3 Task Agents in parallel

### Week 6 (Phase 2)

- [ ] E2E: Module Lead synthesizes 3 task results
- [ ] E2E: Module Lead recovers from task failure

### Week 9 (Phase 3)

- [ ] E2E: Component Lead coordinates 2 modules
- [ ] E2E: Dependencies resolved correctly

### Week 12 (Phase 4)

- [ ] E2E: All 4 components complete
- [ ] E2E: Parallel execution verified

### Week 14 (Phase 5)

- [ ] E2E: 100 agents execute successfully
- [ ] E2E: System recovers from 20 random failures
- [ ] E2E: 24-hour stability test passes

---

**Document Version**: 1.0
**Last Updated**: 2025-11-17
**Status**: Primary implementation guide (TDD + 4-layer architecture)
