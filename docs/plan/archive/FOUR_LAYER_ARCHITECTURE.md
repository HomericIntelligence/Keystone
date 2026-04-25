# Four-Layer Hierarchical Architecture

## Overview

ProjectKeystone uses a **4-layer hierarchical architecture** that mirrors the agent development structure used to build it. This creates a self-similar, fractal-like organization where the system's structure reflects its development process.

## Four-Layer Hierarchy

```
Level 0: Chief Architect Agent
    │
    ├─── Level 1: Component Lead Agent (Core)
    │        │
    │        ├─── Level 2: Module Lead Agent (Messaging)
    │        │        │
    │        │        ├─── Level 3: Task Agent (MessageQueue impl)
    │        │        ├─── Level 3: Task Agent (MessageBus impl)
    │        │        └─── Level 3: Task Agent (Routing impl)
    │        │
    │        └─── Level 2: Module Lead Agent (Concurrency)
    │                 │
    │                 ├─── Level 3: Task Agent (ThreadPool impl)
    │                 └─── Level 3: Task Agent (Coroutines impl)
    │
    ├─── Level 1: Component Lead Agent (Protocol)
    │        │
    │        ├─── Level 2: Module Lead Agent (KIM)
    │        └─── Level 2: Module Lead Agent (Serialization)
    │
    ├─── Level 1: Component Lead Agent (Agents)
    │        │
    │        ├─── Level 2: Module Lead Agent (Base)
    │        ├─── Level 2: Module Lead Agent (ChiefArchitect)
    │        ├─── Level 2: Module Lead Agent (ComponentLead)
    │        └─── Level 2: Module Lead Agent (ModuleLead)
    │
    └─── Level 1: Component Lead Agent (Integration)
             │
             ├─── Level 2: Module Lead Agent (AI)
             └─── Level 2: Module Lead Agent (Monitoring)
```

## Layer Definitions

### Level 0: Chief Architect Agent

**Responsibilities:**

- Strategic architecture decisions
- System-wide coordination
- Technology selection
- High-level goal interpretation
- Cross-component orchestration
- Resource allocation across components

**Analogy:** CEO/CTO of the system

**Corresponds To (in dev agents):** `chief-architect` orchestrator

**Example Decisions:**

- "Use C++20 modules for the build system"
- "Implement Actor Model for concurrency"
- "Prioritize Core infrastructure before Agents"

**Communication:**

- Receives: User goals, system-wide requests
- Sends: Component-level directives to Level 1 agents

---

### Level 1: Component Lead Agent

**Responsibilities:**

- Component-level architecture
- Module coordination within component
- Technology implementation for component
- Cross-module dependencies
- Component-level testing strategy
- Interface definition

**Analogy:** VP of Engineering for a product area

**Corresponds To (in dev agents):**

- `foundation-orchestrator`
- `shared-library-orchestrator`
- `agentic-workflows-orchestrator`
- `cicd-orchestrator`
- `tooling-orchestrator`

**Examples:**

- **Core Component Lead**: Manages Messaging + Concurrency + Utilities modules
- **Protocol Component Lead**: Manages KIM + Serialization + GRPC modules
- **Agents Component Lead**: Manages agent implementations
- **Integration Component Lead**: Manages external integrations

**Communication:**

- Receives: Component directives from Level 0
- Sends: Module specifications to Level 2 agents

---

### Level 2: Module Lead Agent

**Responsibilities:**

- Module-level design
- Task decomposition within module
- Task coordination
- Module testing
- Result synthesis from tasks
- Code review coordination

**Analogy:** Tech Lead for a specific feature

**Corresponds To (in dev agents):**

- `implementation-specialist`
- `architecture-design`
- `test-specialist`
- `documentation-specialist`
- `performance-specialist`

**Examples:**

- **Messaging Module Lead**: Manages MessageQueue, MessageBus, Routing tasks
- **Concurrency Module Lead**: Manages ThreadPool, Coroutines, Barriers tasks
- **KIM Module Lead**: Manages message structure, builders, validation tasks

**Communication:**

- Receives: Module specifications from Level 1
- Sends: Specific task assignments to Level 3 agents

---

### Level 3: Task Agent

**Responsibilities:**

- Concrete task execution
- Code implementation
- Unit test execution
- External tool invocation
- Result reporting
- Error handling

**Analogy:** Individual contributor engineer

**Corresponds To (in dev agents):**

- `senior-implementation-engineer`
- `implementation-engineer`
- `junior-implementation-engineer`
- `test-engineer`
- `junior-test-engineer`
- `documentation-engineer`

**Examples:**

- Implement `MessageQueue::push()` method
- Write unit tests for `AsyncQueuePop`
- Implement `ThreadPool::submit()` method
- Create gRPC client for AI service

**Communication:**

- Receives: Task specifications from Level 2
- Sends: Task results and completion status to Level 2

---

## Message Flow Example

### Scenario: User Request to Implement Message Passing

```
USER
  │
  │ "Implement message passing between agents"
  ▼
┌─────────────────────────────────────┐
│ Level 0: Chief Architect Agent     │
│ Decision: Break into components    │
└──────────┬──────────────────────────┘
           │
           │ Directive: "Implement Core.Messaging component"
           ▼
┌─────────────────────────────────────┐
│ Level 1: Core Component Lead       │
│ Decision: Break into modules       │
└──────────┬──────────────────────────┘
           │
           │ Spec: "Implement MessageQueue module"
           ▼
┌─────────────────────────────────────┐
│ Level 2: Messaging Module Lead     │
│ Decision: Break into tasks         │
└──────────┬──────────────────────────┘
           │
           ├─ Task: "Implement MessageQueue::push()"
           ├─ Task: "Implement MessageQueue::try_pop()"
           └─ Task: "Write unit tests"
           │
           ▼
┌─────────────────────────────────────┐
│ Level 3: Task Agents (3x)          │
│ Execute: Write code, tests         │
└──────────┬──────────────────────────┘
           │
           │ Results: Code + Tests
           ▼
┌─────────────────────────────────────┐
│ Level 2: Messaging Module Lead     │
│ Synthesize: MessageQueue complete  │
└──────────┬──────────────────────────┘
           │
           │ Result: MessageQueue module
           ▼
┌─────────────────────────────────────┐
│ Level 1: Core Component Lead       │
│ Aggregate: Core.Messaging complete │
└──────────┬──────────────────────────┘
           │
           │ Result: Messaging ready
           ▼
┌─────────────────────────────────────┐
│ Level 0: Chief Architect Agent     │
│ Finalize: Message passing available│
└──────────┬──────────────────────────┘
           │
           ▼
         USER (confirmation)
```

## Incremental TDD Build Path

### Phase 1: Two-Agent Prototype (Weeks 1-3)

Start with the **simplest working hierarchy**:

```
Level 0: Chief Architect (simple orchestrator)
    │
    └─── Level 3: Task Agent (single worker)
```

**Justification**: Validates message passing without intermediate layers

**E2E Test:**

```cpp
TEST(E2E_TwoAgent, ChiefArchitectDelegatesToTaskAgent) {
    auto chief = std::make_unique<ChiefArchitectAgent>();
    auto task_agent = std::make_unique<TaskAgent>();

    auto result = chief->processGoal("Implement MessageQueue");

    EXPECT_TRUE(result.isSuccess());
}
```

---

### Phase 2: Add Module Lead Layer (Weeks 4-6)

Insert **Level 2 in the middle**:

```
Level 0: Chief Architect
    │
    └─── Level 2: Module Lead
             │
             ├─── Level 3: Task Agent 1
             ├─── Level 3: Task Agent 2
             └─── Level 3: Task Agent 3
```

**Justification**: Validates result synthesis and multi-task coordination

**E2E Test:**

```cpp
TEST(E2E_ThreeLayer, ModuleLeadCoordinatesMultipleTasks) {
    auto chief = std::make_unique<ChiefArchitectAgent>();
    auto module_lead = std::make_unique<ModuleLeadAgent>();
    auto task_agents = createTaskAgents(3);

    auto result = chief->processGoal("Implement MessageQueue");

    // Verify all 3 task agents participated
    EXPECT_EQ(task_agents[0]->getTasksCompleted(), 1);
    EXPECT_EQ(task_agents[1]->getTasksCompleted(), 1);
    EXPECT_EQ(task_agents[2]->getTasksCompleted(), 1);
}
```

---

### Phase 3: Add Component Lead Layer (Weeks 7-9)

Insert **Level 1**:

```
Level 0: Chief Architect
    │
    └─── Level 1: Component Lead (Core)
             │
             ├─── Level 2: Module Lead (Messaging)
             │        │
             │        └─── Level 3: Task Agents
             │
             └─── Level 2: Module Lead (Concurrency)
                      │
                      └─── Level 3: Task Agents
```

**Justification**: Validates component-level coordination and cross-module dependencies

**E2E Test:**

```cpp
TEST(E2E_FourLayer, ComponentLeadManagesMultipleModules) {
    auto hierarchy = createFullHierarchy(
        /*components=*/1,
        /*modules_per_component=*/2,
        /*tasks_per_module=*/3
    );

    auto result = hierarchy.chief->processGoal(
        "Implement Core component: Messaging + Concurrency"
    );

    // Verify both modules completed
    EXPECT_TRUE(result.modules_completed.contains("Messaging"));
    EXPECT_TRUE(result.modules_completed.contains("Concurrency"));
}
```

---

### Phase 4: Multiple Components (Weeks 10-12)

Full **4-layer hierarchy**:

```
Level 0: Chief Architect
    │
    ├─── Level 1: Component Lead (Core)
    │        │
    │        └─── Multiple Module Leads → Task Agents
    │
    ├─── Level 1: Component Lead (Protocol)
    │        │
    │        └─── Multiple Module Leads → Task Agents
    │
    └─── Level 1: Component Lead (Agents)
             │
             └─── Multiple Module Leads → Task Agents
```

**E2E Test:**

```cpp
TEST(E2E_FullSystem, ChiefArchitectOrchestratesAllComponents) {
    auto system = createFullHierarchy(
        /*components=*/3,
        /*modules_per_component=*/3,
        /*tasks_per_module=*/5
    );

    auto result = system.chief->processGoal(
        "Build complete HMAS: Core + Protocol + Agents"
    );

    // Verify all components completed
    EXPECT_TRUE(result.components_completed.contains("Core"));
    EXPECT_TRUE(result.components_completed.contains("Protocol"));
    EXPECT_TRUE(result.components_completed.contains("Agents"));
}
```

---

## Agent Type Implementations

### ChiefArchitectAgent (Level 0)

```cpp
class ChiefArchitectAgent : public AgentBase {
public:
    // Strategic decisions
    Task<void> decomposeGoal(const std::string& goal);
    Task<void> selectTechnologyStack();
    Task<void> allocateResources();

    // Component coordination
    Task<void> createComponentPlan(const std::string& component);
    Task<void> delegateToComponentLead(const ComponentDirective& directive);

    // Aggregation
    Task<SystemResult> synthesizeComponentResults();

private:
    std::vector<std::unique_ptr<ComponentLeadAgent>> component_leads_;
    GlobalContext context_;
    ResourceAllocator allocator_;
};
```

### ComponentLeadAgent (Level 1)

```cpp
class ComponentLeadAgent : public AgentBase {
public:
    // Component architecture
    Task<void> designComponentArchitecture();
    Task<void> defineModuleInterfaces();

    // Module coordination
    Task<void> delegateToModuleLead(const ModuleSpec& spec);
    Task<void> resolveModuleDependencies();

    // Synthesis
    Task<ComponentResult> synthesizeModuleResults();

private:
    std::vector<std::unique_ptr<ModuleLeadAgent>> module_leads_;
    ComponentContext context_;
};
```

### ModuleLeadAgent (Level 2)

```cpp
class ModuleLeadAgent : public AgentBase {
public:
    // Module design
    Task<void> designModule();
    Task<void> decomposeIntoTasks();

    // Task coordination
    Task<void> delegateToTaskAgent(const TaskSpec& task);
    Task<void> coordinateTaskExecution();

    // Synthesis
    Task<ModuleResult> synthesizeTaskResults();
    Task<void> performCodeReview();

private:
    std::vector<std::unique_ptr<TaskAgent>> task_agents_;
    ModuleContext context_;
    PhaseBarrier task_completion_barrier_;
};
```

### TaskAgent (Level 3)

```cpp
class TaskAgent : public AgentBase {
public:
    // Execution
    Task<void> executeTask(const TaskSpec& task);
    Task<void> invokeCompiler();
    Task<void> runTests();

    // External integration
    Task<void> callLLMForCodeGeneration(const std::string& spec);
    Task<void> callStaticAnalyzer();

    // Reporting
    Task<TaskResult> reportResult();

private:
    ToolRegistry tools_;
    std::unique_ptr<GRPCClient> llm_client_;
};
```

---

## Advantages of 4-Layer Architecture

### 1. Self-Similarity

The system structure mirrors the development process, making it intuitive for developers familiar with the agent orchestration system.

### 2. Clear Separation of Concerns

| Layer | Concern | Scope |
|-------|---------|-------|
| L0 | Strategic | Entire system |
| L1 | Tactical | Component |
| L2 | Operational | Module |
| L3 | Execution | Task |

### 3. Scalability

Each layer can be scaled independently:

- L0: Single instance (one chief architect)
- L1: Multiple components (3-5)
- L2: Multiple modules per component (3-10)
- L3: Multiple tasks per module (5-20)

### 4. Failure Isolation

Failures at lower layers don't cascade to higher layers:

- Task failure → Module Lead retries or delegates to different Task Agent
- Module failure → Component Lead reorganizes or restarts module
- Component failure → Chief Architect re-architects component

### 5. Parallel Execution

Maximum parallelism at each layer:

- L1: Components execute in parallel
- L2: Modules within component execute in parallel
- L3: Tasks within module execute in parallel

---

## Mapping to Original 3-Layer Plan

The original plan had:

- **Root** (L1) → Now **Chief Architect** (L0)
- **Branch** (L2) → Split into **Component Lead** (L1) + **Module Lead** (L2)
- **Leaf** (L3) → Now **Task Agent** (L3)

**Key insight**: The original "Branch" layer was doing two jobs:

1. Component coordination (now L1)
2. Module/task coordination (now L2)

Splitting into 4 layers provides **clearer separation of concerns**.

---

**Document Version**: 1.0
**Last Updated**: 2025-11-17
**Supersedes**: TWO_AGENT_ARCHITECTURE.md (2-layer) and architecture.md (3-layer)
