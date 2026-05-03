# ProjectKeystone HMAS - Glossary

**Comprehensive reference of terms, concepts, and acronyms used throughout ProjectKeystone**

---

## Core Architecture Terms

### HMAS

**Hierarchical Multi-Agent System** - A distributed computing architecture where agents are organized in multiple
hierarchical layers, communicating via message passing. ProjectKeystone implements a 4-layer HMAS with strategic
coordination at the top (Level 0) down to task execution at the bottom (Level 3).

### KIM

**Keystone Interchange Message** - The standard message format for inter-agent communication in ProjectKeystone. Each
KIM contains:

- `msg_id`: Unique message identifier
- `sender_id`: ID of the sending agent
- `receiver_id`: ID of the receiving agent
- `command`: The action to perform
- `payload`: Optional JSON data attached to the message
- `timestamp`: When the message was created

See [docs/NETWORK_PROTOCOL.md](./NETWORK_PROTOCOL.md) for complete KIM specification.

### MessageBus

**Central Message Routing Hub** - A decoupled communication infrastructure that handles routing KIM messages between
agents without direct agent-to-agent dependencies. The MessageBus:

- Registers and discovers agents dynamically
- Routes messages to their intended recipients
- Enables agent decoupling (agents communicate through IDs, not pointers)
- Maintains thread-safe agent registry with mutex protection
- Supports both synchronous (Phase 1) and asynchronous (Phase 2+) delivery

See [docs/plan/adr/ADR-001-message-bus-architecture.md](./plan/adr/ADR-001-message-bus-architecture.md) for architecture
decisions.

---

## Agent Hierarchy Levels

### L0 (Level 0)

**Strategic Orchestration Layer** - The top level of the HMAS hierarchy, containing the ChiefArchitectAgent. L0 is
responsible for:

- Strategic system-wide decisions
- Component selection and coordination
- Overall goal decomposition
- High-level task scheduling

Analogy: CEO/CTO of the system

### L1 (Level 1)

**Component Coordination Layer** - Coordinates multiple modules within a component. Implemented by ComponentLeadAgent.
Responsibilities:

- Module coordination and sequencing
- Component-level resource management
- Cross-module dependency resolution
- Result aggregation from modules

Analogy: VP of Engineering for a component

### L2 (Level 2)

**Module Coordination Layer** - Decomposes module-level goals into concrete tasks. Implemented by ModuleLeadAgent.
Responsibilities:

- Task decomposition and planning
- Task scheduling and monitoring
- Result synthesis from task agents
- Module-level error handling

Analogy: Tech Lead managing a module

### L3 (Level 3)

**Task Execution Layer** - The lowest level where concrete work happens. Implemented by TaskAgent. Responsibilities:

- Execute individual tasks
- Command execution (bash, system calls, etc.)
- Result reporting back to ModuleLeads
- Task-level error handling

Analogy: Individual Contributor implementing features

---

## Agent Types

### ChiefArchitect (L0)

**Strategic Orchestrator Agent** - The Level 0 agent that orchestrates the entire HMAS system. It:

- Receives high-level goals or commands
- Decomposes goals into components
- Delegates to ComponentLeadAgents (Phase 3+) or ModuleLeadAgents (Phase 2)
- Aggregates results from lower levels
- Makes strategic architectural decisions

### ComponentLead (L1)

**Component Coordinator Agent** - The Level 1 agent that coordinates multiple modules within a single component. It:

- Receives module planning requests from ChiefArchitect
- Decomposes component goals into module tasks
- Coordinates 2+ ModuleLeadAgents
- Monitors module progress
- Aggregates module results for the ChiefArchitect

Introduced in: Phase 3 (Full 4-Layer Hierarchy)

### ModuleLead (L2)

**Module Coordinator Agent** - The Level 2 agent that breaks down module goals into executable tasks. It:

- Receives task planning goals
- Decomposes goals into concrete executable tasks
- Coordinates 2+ TaskAgents
- Monitors task progress and results
- Synthesizes task results into module output
- Manages state transitions (IDLE → PLANNING → WAITING → SYNTHESIZING)

Introduced in: Phase 2 (3-Layer Hierarchy)

### TaskAgent (L3)

**Task Executor Agent** - The Level 3 agent that executes concrete work. It:

- Receives executable commands
- Executes commands (bash, system operations, etc.)
- Reports results back to ModuleLeads
- Handles task-level errors
- Manages task state (IDLE → EXECUTING → COMPLETED)

Introduced in: Phase 1 (2-Layer Hierarchy)

---

## Concurrency and Performance Terms

### Work-Stealing Scheduler

**Lock-Free Task Distribution System** - A high-performance task scheduling mechanism that distributes work across
worker threads without using locks or mutex primitives. Key characteristics:

- Each worker thread has a local work queue
- When a thread runs out of work, it "steals" tasks from other threads' queues
- Uses atomic operations for lock-free synchronization
- Scales well with many threads
- Minimizes contention and context switching
- Used in Phase 7+ for distributed task execution

### ThreadPool

**Fixed-Size Thread Executor** - A pool of worker threads that execute tasks concurrently. ProjectKeystone uses
ThreadPool for:

- Parallel agent execution
- Concurrent message processing
- Load distribution across cores
- Resource-controlled parallelism

### Coroutine

**C++20 Asynchronous Function** - A function that can be suspended and resumed using C++20 coroutine primitives
(`co_await`, `co_return`). ProjectKeystone uses coroutines for:

- Non-blocking message processing
- Asynchronous task coordination
- Efficient async/await semantics

### Task<T>

**Custom Awaitable Type** - A C++20 awaitable type that wraps coroutine execution. Features:

- Implements `operator co_await()` for use with `co_await`
- Returns type `T` when completed
- Supports exception propagation
- Used extensively in async message processing

### Coroutine Safety

**Practices for Correct Coroutine Usage** - Guidelines for using C++20 coroutines safely in ProjectKeystone:

- **Lifetime Management**: Always use `Task<T>` wrapper; never manually manage `std::coroutine_handle`
- **Suspension Points**: Only use `co_await` inside coroutine functions
- **Exception Safety**: Capture exceptions in promise or use try/catch inside coroutine
- **Thread Safety**: Never hold locks across `co_await` points; use scheduler for safe execution
- **Data Validity**: Capture by value across suspension points; function-local variables are safe
- **Symmetric Transfer**: Used in `final_suspend()` to avoid stack growth in chained coroutines
- **Pitfalls to Avoid**: Forgetting `co_return`, extracting `get_handle()`, creating Task but not awaiting it

See [ADR-013: Coroutine Safety Patterns](./plan/adr/ADR-013-coroutine-safety-patterns.md) for detailed patterns and
anti-patterns.

### co_await

**Coroutine Suspension Operator** - A keyword that suspends the current coroutine and awaits completion of another
asynchronous operation. Usage:

- Can only be used inside coroutine functions
- Automatically integrates with `Task<T>` type
- Stores awaiting coroutine as continuation
- Resumed when awaited coroutine completes
- Example: `auto result = co_await someTask();`

### co_return

**Coroutine Return Statement** - A keyword that returns a value from a coroutine and suspends. Usage:

- Must be used in any function returning `Task<T>`
- Stores value in promise and triggers `final_suspend()`
- Can return `void` in `Task<void>` functions
- Example: `co_return 42;`

### Promise Type

**Coroutine Protocol Handler** - A nested type inside coroutine return types (like `Task<T>`) that defines:

- `get_return_object()`: Creates the Task from promise
- `initial_suspend()`: Controls start behavior
- `final_suspend()`: Handles completion and continuation
- `return_value()` or `return_void()`: Stores result
- `unhandled_exception()`: Captures exceptions thrown in coroutine
- See `Task<T>::promise_type` in `include/concurrency/task.hpp` for implementation

### Awaitable Interface

**Protocol for co_await Operations** - Requires three methods for any type usable with `co_await`:

- `bool await_ready()`: Returns true if result immediately available
- `std::coroutine_handle<> await_suspend(std::coroutine_handle<> h)`: Handles suspension
- `T await_resume()`: Returns result when resumed
- Both `Task<T>` and `PullOrSteal` implement this protocol
- See [ADR-013](./plan/adr/ADR-013-coroutine-safety-patterns.md) Pattern 6 for symmetric transfer optimization

---

## Testing and Quality Assurance

### TDD

**Test-Driven Development** - A software development methodology where tests are written BEFORE implementation:

1. **RED**: Write failing E2E test
2. **GREEN**: Implement minimal code to pass test
3. **REFACTOR**: Improve code while keeping tests green
4. **COMMIT**: Submit changes when tests pass

ProjectKeystone strictly follows TDD throughout all phases.

### E2E Tests

**End-to-End Integration Tests** - Comprehensive tests that verify system behavior across multiple layers.
ProjectKeystone E2E tests:

- Test full message flows from L0 down to L3
- Verify agent coordination and delegation
- Use Google Test (GTest) framework
- Use descriptive test names (e.g., `TEST(E2E_Phase1, BasicDelegation)`)
- Drive development priorities (TDD approach)

Examples:

- Phase 1: ChiefArchitect → TaskAgent delegation
- Phase 2: ModuleLead coordinates multiple TaskAgents
- Phase 3: ComponentLead coordinates multiple ModuleLeads
- Phase 8: Distributed gRPC communication

### Unit Tests

**Isolated Component Tests** - Tests for individual components in isolation:

- MessageBus routing and discovery
- MessageQueue operations
- ThreadPool task execution
- Coroutine helpers
- Message serialization/deserialization

### Chaos Engineering

**Fault Injection Testing Methodology** - Deliberately introducing failures to test system resilience:

- Random message delays
- Agent process crashes
- Network partition simulation
- Resource exhaustion scenarios
- Used in Phase 5+ for robustness validation

Typical chaos tests:

- 20 random agent failures tolerated
- Message delivery guaranteed despite chaos
- System recovers within reasonable bounds

---

## Documentation and Decision Making

### ADR

**Architecture Decision Record** - A document that captures an important architectural decision, including:

- **Context**: The issue or problem being addressed
- **Decision**: What was decided
- **Consequences**: Positive and negative outcomes
- **Status**: PROPOSED, ACCEPTED, SUPERSEDED, etc.

ProjectKeystone ADRs are stored in `docs/plan/adr/` with naming convention `ADR-###-title.md`

Examples:

- [ADR-001](./plan/adr/ADR-001-message-bus-architecture.md) - Message Bus Architecture
- [ADR-002](./plan/adr/ADR-002-coroutine-task-design.md) - Coroutine Task Design
- [ADR-003](./plan/adr/ADR-003-work-stealing-scheduler.md) - Work-Stealing Scheduler

### Phase

**Development Iteration** - ProjectKeystone is built through phases following TDD principles:

- **Phase 1** (Weeks 1-3): L0 ↔ L3 basic delegation (2 agents)
- **Phase 2** (Weeks 4-6): L0 ↔ L2 ↔ L3 module coordination (3 layers)
- **Phase 3** (Weeks 7-9): L0 ↔ L1 ↔ L2 ↔ L3 full hierarchy (4 layers)
- **Phase 4-5**: Multi-component and scale testing
- **Phase 6-7**: Performance and distributed features
- **Phase 8** (Optional): gRPC-based distributed multi-node communication

Each phase adds new agent levels or features while maintaining all previous functionality.

---

## Technology Stack

### C++20

**Modern C++ Standard** - ProjectKeystone is implemented exclusively in C++20, utilizing:

- Coroutines (`std::coroutine_handle`)
- Concepts (template constraints)
- Structured bindings
- Smart pointers (`std::unique_ptr`, `std::shared_ptr`)
- Modules (when compiler support available)
- Ranges and views

### CMake

**Build Configuration System** - Used to:

- Configure the build with options (`-DENABLE_GRPC=ON`, etc.)
- Manage dependencies (Google Test, nlohmann/json, etc.)
- Generate build artifacts for multiple targets
- Support sanitizers (ASan, UBSan, TSan, MSan)

### Google Test (GTest)

**C++ Unit and E2E Testing Framework** - Features used:

- Test cases with `TEST()` and test fixtures with `TEST_F()`
- Assertions (`ASSERT_*`, `EXPECT_*`)
- Test parameterization
- Death tests for crash scenarios
- Comprehensive mocking framework (GMock)

### Docker

**Container Platform** - Used for:

- Consistent build and test environments
- Multi-stage builds (builder, runtime, development)
- Docker Compose for local orchestration
- Kubernetes-ready images

### gRPC (Phase 8 Optional)

**RPC Framework for Distributed Communication** - Enables:

- Multi-node agent communication
- Protocol Buffers message serialization
- Service definitions for HMAS coordinator
- Load balancing strategies
- Heartbeat monitoring between nodes

---

## Message Patterns and Communication

### Message Bus Pattern

The pattern where a central bus routes messages between decoupled agents:

- Agents don't need to know physical addresses of other agents
- Dynamic agent registration and discovery
- Synchronous routing in Phase 1, async in Phase 2+

### Delegation Pattern

The pattern where higher-level agents break down goals and delegate work to lower levels:

- ChiefArchitect delegates to ComponentLead (L1)
- ComponentLead delegates to ModuleLead (L2)
- ModuleLead delegates to TaskAgent (L3)

### Result Synthesis Pattern

The pattern where a parent agent aggregates results from child agents:

- ModuleLead waits for all TaskAgent results
- ComponentLead waits for all ModuleLead results
- ChiefArchitect waits for all ComponentLead results

---

## Performance and Optimization

### SIMD

**Single Instruction, Multiple Data** - Vector operations used in performance-critical sections (when applicable to
agent operations).

### Lock-Free

**Synchronization without Mutex** - Used in:

- Work-stealing scheduler queues
- Message queue implementation
- Atomic counters for result tracking

### Zero-Copy

**Data Passing without Duplication** - Achieved through:

- Move semantics (C++20)
- Message serialization with minimal overhead
- Shared ownership via `std::shared_ptr` when necessary

---

## State Management

### State Machine

ProjectKeystone agents use finite state machines for coordinated workflows:

**ModuleLeadAgent States**:

- `IDLE`: Waiting for commands
- `PLANNING`: Decomposing goals into tasks
- `WAITING`: Monitoring task execution
- `SYNTHESIZING`: Aggregating task results
- Return to `IDLE`

**ComponentLeadAgent States**:

- `IDLE`: Waiting for commands
- `PLANNING`: Decomposing component goals into modules
- `WAITING_FOR_MODULES`: Monitoring module execution
- `AGGREGATING`: Collecting module results
- Return to `IDLE`

---

## Deployment

### Kubernetes (K8s)

**Container Orchestration Platform** - ProjectKeystone supports K8s deployment with:

- Deployment manifests with health checks
- Service definitions (ClusterIP, Headless)
- ConfigMaps for configuration
- Metrics endpoints for monitoring

### Docker Compose

**Multi-Container Orchestration (Local)** - Used for:

- Local development with mounted source
- Multi-node simulation
- Phase 8 distributed testing
- CI/CD pipelines

---

## Error Handling and Resilience

### Circuit Breaker

**Pattern for Handling Degradation** - Prevents cascading failures by:

- Monitoring for error rates
- Opening circuit if threshold exceeded
- Failing fast instead of retrying indefinitely
- Optional feature in Phase 5+

### Retry Policy

**Configurable Retry Strategy** - Handles transient failures:

- Exponential backoff
- Maximum retry attempts
- Optional jitter for distributed systems

### Heartbeat

**Periodic Health Check** - Used in Phase 8:

- 1-second interval for distributed nodes
- 3-second timeout threshold
- Detects agent and node failures
- Triggers failover or recovery

---

## References

- [CLAUDE.md](../CLAUDE.md) - Project configuration and overview
- [TDD_FOUR_LAYER_ROADMAP.md](./plan/TDD_FOUR_LAYER_ROADMAP.md) - Phase-by-phase development plan
- [ARCHITECTURE.md](./ARCHITECTURE.md) - Detailed architecture documentation
- [NETWORK_PROTOCOL.md](./NETWORK_PROTOCOL.md) - Message protocol specifications
- [KUBERNETES_DEPLOYMENT.md](./KUBERNETES_DEPLOYMENT.md) - Kubernetes deployment guide

---

**Last Updated**: 2025-11-26
**Version**: 1.1 (Added coroutine safety terminology)
**Status**: ACTIVE
