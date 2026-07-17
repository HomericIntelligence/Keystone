# YAML Task Specification Reference

## Overview

Keystone HMAS uses YAML task specifications to coordinate work across the 4-layer agent hierarchy. The format is
inspired by Argo Workflows and follows Kubernetes-style conventions.

**API Version**: `keystone.hmas.io/v1alpha1`

---

## Complete Specification

```yaml
apiVersion: keystone.hmas.io/v1alpha1
kind: HierarchicalTask

metadata:
  name: <task-name>                    # Required: kebab-case task name
  taskId: <unique-id>                  # Required: Unique task identifier
  parentTaskId: <parent-id>            # Optional: Parent task ID
  sessionId: <session-id>              # Optional: Session identifier
  createdAt: <ISO-8601-timestamp>      # Required: Creation timestamp
  deadline: <ISO-8601-timestamp>       # Optional: Task deadline

spec:
  routing:
    originNode: <ip:port>              # Required: Origin node for results
    targetLevel: <0-3>                 # Required: Target hierarchy level
    targetAgentType: <agent-type>      # Required: Target agent type
    targetAgentId: <agent-id>          # Optional: Specific target agent

  hierarchy:
    level0_goal: <goal>                # Required: Top-level goal
    level1_directive: <directive>      # Optional: Component directive
    level2_module: <module-name>       # Optional: Module name
    level3_task: <task-description>    # Optional: Concrete task

  action:
    type: <action-type>                # Required: DECOMPOSE | EXECUTE | ...
    contentType: <content-type>        # Optional: TEXT_PLAIN | BINARY_CISTA
    priority: <priority>               # Optional: LOW | NORMAL | HIGH | CRITICAL

  payload:
    command: <command-string>          # Required: Command or directive
    data: |                            # Optional: Multi-line data
      <additional-data>
    metadata:
      estimatedDuration: <duration>    # Optional: e.g., "15m", "2h"
      requiredCapabilities:            # Optional: List of capabilities
        - <capability-1>
        - <capability-2>

  tasks:                               # Optional: Array of subtasks (DAG)
    - name: <subtask-name>
      agentType: <agent-type>
      depends: <dependency-expression>
      spec:
        <subtask-specific-fields>

  aggregation:
    strategy: <strategy>               # Required: WAIT_ALL | FIRST_SUCCESS | MAJORITY
    timeout: <duration>                # Required: e.g., "25m"
    retryPolicy:
      maxRetries: <count>              # Optional: Default 3
      backoff: <backoff-type>          # Optional: exponential | linear | constant
      initialDelay: <duration>         # Optional: Default "1s"
      maxDelay: <duration>             # Optional: Default "30s"

status:                                # Updated by agents during execution
  phase: <phase>                       # PENDING | PLANNING | EXECUTING | COMPLETED | FAILED
  startTime: <ISO-8601-timestamp>      # Optional: Start timestamp
  completionTime: <ISO-8601-timestamp> # Optional: Completion timestamp
  result: |                            # Optional: Task result (multi-line)
    <result-data>
  error: <error-message>               # Optional: Error message if failed
  subtasks:                            # Optional: Subtask status
    - name: <subtask-name>
      status: <status>
      assignedNode: <ip:port>
```

---

## Field Descriptions

### Top-Level Fields

#### `apiVersion` (Required)

API version of the task specification.

**Format**: `keystone.hmas.io/v{version}[{stability}]`

**Examples**:

- `keystone.hmas.io/v1alpha1` - Alpha release
- `keystone.hmas.io/v1beta1` - Beta release
- `keystone.hmas.io/v1` - Stable release

#### `kind` (Required)

Resource kind. Always `HierarchicalTask` for task specifications.

---

### Metadata Section

#### `metadata.name` (Required)

Human-readable task name in kebab-case.

**Pattern**: `^[a-z0-9-]+$`

**Examples**:

- `implement-core-messaging`
- `run-unit-tests`
- `build-project`

#### `metadata.taskId` (Required)

Unique task identifier. Can be UUID or hierarchical ID.

**Examples**:

- `task-1700000000000-1234` (timestamp + random)
- `uuid-550e8400-e29b-41d4-a716-446655440000`
- `chief-001-component-002-module-003` (hierarchical)

#### `metadata.parentTaskId` (Optional)

ID of the parent task if this is a subtask.

**Usage**: Enables result routing back up the hierarchy.

#### `metadata.sessionId` (Optional)

Session identifier for tracing related tasks.

**Usage**: All tasks in a user session share the same session ID.

#### `metadata.createdAt` (Required)

Task creation timestamp in ISO 8601 format.

**Format**: `YYYY-MM-DDTHH:MM:SSZ`

**Example**: `2025-11-20T14:30:00Z`

#### `metadata.deadline` (Optional)

Task deadline in ISO 8601 format.

**Usage**: Tasks exceeding deadline transition to `TIMEOUT` phase.

---

### Spec Section

#### `spec.routing` (Required)

##### `routing.originNode` (Required)

IP address and port of the node that submitted this task.

**Format**: `<ip>:<port>` or `<hostname>:<port>`

**Examples**:

- `192.168.100.10:50051`
- `chief-node.hmas.local:50051`

**Usage**: Results are sent back to this address via `SubmitResult` RPC.

##### `routing.targetLevel` (Required)

Target hierarchy level (0-3).

| Level | Agent Type | Description |
|-------|-----------|-------------|
| **0** | ChiefArchitectAgent | Strategic coordination |
| **1** | ComponentLeadAgent | Component management |
| **2** | ModuleLeadAgent | Module coordination |
| **3** | TaskAgent | Concrete execution |

##### `routing.targetAgentType` (Required)

Target agent type name.

**Valid Values**:

- `ChiefArchitectAgent`
- `ComponentLeadAgent`
- `ModuleLeadAgent`
- `TaskAgent`

##### `routing.targetAgentId` (Optional)

Specific target agent ID.

**Usage**:

- If specified, router tries this agent first
- If unspecified, router queries registry and applies load balancing

---

#### `spec.hierarchy` (Required)

Preserves context from all hierarchy levels.

##### `hierarchy.level0_goal` (Required)

Top-level goal from ChiefArchitect.

**Example**: `"Build complete HMAS infrastructure"`

##### `hierarchy.level1_directive` (Optional, for L1+)

Component-level directive from ComponentLead.

**Example**: `"Implement Core component"`

##### `hierarchy.level2_module` (Optional, for L2+)

Module name from ModuleLead.

**Example**: `"Messaging"`

##### `hierarchy.level3_task` (Optional, for L3)

Concrete task description for TaskAgent.

**Example**: `"Implement MessageQueue::push() method"`

---

#### `spec.action` (Required)

##### `action.type` (Required)

Action type to perform.

| Type | Description | Used By |
|------|-------------|---------|
| **DECOMPOSE** | Decompose into subtasks | L0, L1, L2 |
| **EXECUTE** | Execute concrete task | L3 |
| **RETURN_RESULT** | Return result to parent | All levels |
| **SHUTDOWN** | Shutdown agent | All levels |

##### `action.contentType` (Optional)

Payload content type.

**Valid Values**:

- `TEXT_PLAIN` (default)
- `BINARY_CISTA` (serialized binary)
- `JSON`

##### `action.priority` (Optional)

Task priority.

**Valid Values** (default: `NORMAL`):

- `LOW`
- `NORMAL`
- `HIGH`
- `CRITICAL`

**Usage**: Higher priority tasks are processed first.

---

#### `spec.payload` (Required)

##### `payload.command` (Required)

Command or directive to execute.

**Examples**:

- `"Implement Core.Messaging component"`
- `"Run unit tests for MessageQueue"`
- `"echo 'Hello World'"`

##### `payload.data` (Optional)

Additional data or instructions (supports multi-line).

**Example**:

```yaml
data: |
  Implement the following modules:
  - MessageQueue with push/pop operations
  - MessageBus with routing
  - Message serialization
```

##### `payload.metadata.estimatedDuration` (Optional)

Estimated duration for task completion.

**Format**: `<number><unit>` where unit is `ms`, `s`, `m`, or `h`

**Examples**:

- `"500ms"` - 500 milliseconds
- `"30s"` - 30 seconds
- `"15m"` - 15 minutes
- `"2h"` - 2 hours

##### `payload.metadata.requiredCapabilities` (Optional)

List of capabilities required to execute this task.

**Examples**:

```yaml
requiredCapabilities:
  - cpp20
  - cmake
  - catch2
  - bash
```

**Usage**: Router selects agents that have ALL listed capabilities.

---

#### `spec.tasks` (Optional)

Array of subtasks forming a Directed Acyclic Graph (DAG).

**Used By**: L0, L1, L2 agents to specify decomposed subtasks

##### Task Entry Fields

**`name`** (Required): Unique subtask name within this task

**`agentType`** (Required): Agent type to execute subtask

**`depends`** (Required): Dependency expression

**`spec`** (Optional): Subtask-specific specification (flexible structure)

##### Dependency Syntax

**Empty dependency** - No dependencies, runs immediately:

```yaml
depends: ""
```

**Single dependency** - Waits for one task:

```yaml
depends: "task-a"
```

**AND dependency** - Waits for multiple tasks:

```yaml
depends: "task-a && task-b"
depends: "messaging-module && concurrency-module"
```

**OR dependency** (Future):

```yaml
depends: "task-a || task-b"
```

##### Example

```yaml
tasks:
  - name: messaging-module
    agentType: ModuleLeadAgent
    depends: ""                        # No dependencies
    spec:
      module: Messaging

  - name: concurrency-module
    agentType: ModuleLeadAgent
    depends: "messaging-module"        # Waits for messaging
    spec:
      module: Concurrency

  - name: integration-tests
    agentType: TaskAgent
    depends: "messaging-module && concurrency-module"  # Waits for both
    spec:
      task: "Run integration tests"
```

---

#### `spec.aggregation` (Required)

Configuration for aggregating subtask results.

##### `aggregation.strategy` (Required)

| Strategy | Description | Use Case |
|----------|-------------|----------|
| **WAIT_ALL** | Wait for all subtasks | Default, ensures completeness |
| **FIRST_SUCCESS** | Return on first success | Fast path, redundant execution |
| **MAJORITY** | Wait for majority (⌈N/2⌉) | Consensus, fault tolerance |

##### `aggregation.timeout` (Required)

Maximum time to wait for all results.

**Format**: Same as `estimatedDuration` (`<number><unit>`)

**Examples**:

- `"25m"` - 25 minutes
- `"1h"` - 1 hour

**Behavior**: If timeout exceeded, task transitions to `TIMEOUT` phase.

##### `aggregation.retryPolicy` (Optional)

Retry configuration for failed subtasks.

**`maxRetries`** (Optional, default: 3): Maximum retry attempts

**`backoff`** (Optional, default: `exponential`): Backoff strategy

- `constant` - Same delay each time (e.g., 1s, 1s, 1s)
- `linear` - Linear increase (e.g., 1s, 2s, 3s)
- `exponential` - Exponential increase (e.g., 1s, 2s, 4s, 8s)

**`initialDelay`** (Optional, default: `"1s"`): Initial retry delay

**`maxDelay`** (Optional, default: `"30s"`): Maximum retry delay

**Exponential Backoff Formula**:

```
delay = min(initialDelay * 2^attempt, maxDelay)
```

**Example**:

```yaml
retryPolicy:
  maxRetries: 3
  backoff: exponential
  initialDelay: 1s
  maxDelay: 30s

# Results in delays: 1s, 2s, 4s
```

---

### Status Section

Updated by agents during task execution. Read-only from task submitter perspective.

#### `status.phase` (Optional)

Current execution phase.

| Phase | Description |
|-------|-------------|
| `PENDING` | Task accepted, not started |
| `PLANNING` | Agent planning subtask decomposition |
| `WAITING` | Waiting for subtask results |
| `EXECUTING` | Executing concrete task |
| `SYNTHESIZING` | Synthesizing subtask results |
| `COMPLETED` | Task completed successfully |
| `FAILED` | Task failed with error |
| `TIMEOUT` | Task exceeded deadline |
| `CANCELLED` | Task cancelled by user |

#### `status.startTime` (Optional)

Timestamp when execution started.

#### `status.completionTime` (Optional)

Timestamp when execution completed.

#### `status.result` (Optional)

Task result (multi-line string).

**Example**:

```yaml
result: |
  MessageQueue implementation complete:
  - Implemented push() using concurrentqueue
  - Implemented try_pop() with lock-free algorithm
  - Added 12 unit tests (all passing)
```

#### `status.error` (Optional)

Error message if task failed.

**Example**:

```yaml
error: "Compilation failed: missing header file <grpcpp/grpcpp.h>"
```

#### `status.subtasks` (Optional)

Array of subtask status.

**Example**:

```yaml
subtasks:
  - name: messaging-module
    status: COMPLETED
    assignedNode: 192.168.100.30:50053
  - name: concurrency-module
    status: EXECUTING
    assignedNode: 192.168.100.31:50054
```

---

## Complete Examples

### Example 1: ChiefArchitect → ComponentLead

```yaml
apiVersion: keystone.hmas.io/v1alpha1
kind: HierarchicalTask

metadata:
  name: core-component-implementation
  taskId: chief-task-001
  sessionId: session-xyz-456
  createdAt: "2025-11-20T14:00:00Z"
  deadline: "2025-11-20T15:00:00Z"

spec:
  routing:
    originNode: "192.168.100.10:50051"
    targetLevel: 1
    targetAgentType: ComponentLeadAgent
    targetAgentId: component-lead-core

  hierarchy:
    level0_goal: "Build HMAS infrastructure"
    level1_directive: "Implement Core component"

  action:
    type: DECOMPOSE
    priority: HIGH

  payload:
    command: "Implement Core component with Messaging and Concurrency modules"
    data: |
      Component: Core
      Modules:
        - Messaging (MessageQueue, MessageBus, Routing)
        - Concurrency (ThreadPool, WorkStealingScheduler)
    metadata:
      estimatedDuration: "55m"

  tasks:
    - name: messaging-module
      agentType: ModuleLeadAgent
      depends: ""
      spec:
        module: Messaging
        deliverables:
          - MessageQueue
          - MessageBus
          - Routing

    - name: concurrency-module
      agentType: ModuleLeadAgent
      depends: "messaging-module"
      spec:
        module: Concurrency
        deliverables:
          - ThreadPool
          - WorkStealingScheduler

  aggregation:
    strategy: WAIT_ALL
    timeout: "55m"
    retryPolicy:
      maxRetries: 3
      backoff: exponential
      initialDelay: "1s"
      maxDelay: "30s"

status:
  phase: PENDING
```

### Example 2: ModuleLead → TaskAgent

```yaml
apiVersion: keystone.hmas.io/v1alpha1
kind: HierarchicalTask

metadata:
  name: message-queue-implementation
  taskId: module-task-001-1-1
  parentTaskId: component-task-001-1
  sessionId: session-xyz-456
  createdAt: "2025-11-20T14:00:30Z"
  deadline: "2025-11-20T14:10:00Z"

spec:
  routing:
    originNode: "192.168.100.30:50053"
    targetLevel: 3
    targetAgentType: TaskAgent

  hierarchy:
    level0_goal: "Build HMAS infrastructure"
    level1_directive: "Implement Core component"
    level2_module: "Messaging"
    level3_task: "Implement MessageQueue class"

  action:
    type: EXECUTE
    priority: HIGH

  payload:
    command: "Implement MessageQueue::push() and MessageQueue::try_pop()"
    data: |
      File: include/core/message_queue.hpp

      Implement concurrent message queue with:
      - push(KeystoneMessage msg)
      - std::optional<KeystoneMessage> try_pop()
      - Lock-free using concurrentqueue library

      Test file: tests/unit/message_queue_test.cpp
    metadata:
      estimatedDuration: "8m"
      requiredCapabilities:
        - cpp20
        - cmake
        - catch2

  tasks: []  # No subtasks for L3

  aggregation:
    strategy: WAIT_ALL
    timeout: "8m"
    retryPolicy:
      maxRetries: 2
      backoff: exponential
      initialDelay: "1s"

status:
  phase: PENDING
```

### Example 3: Task Result

```yaml
apiVersion: keystone.hmas.io/v1alpha1
kind: HierarchicalTask

metadata:
  name: message-queue-implementation
  taskId: module-task-001-1-1
  parentTaskId: component-task-001-1
  sessionId: session-xyz-456
  createdAt: "2025-11-20T14:00:30Z"
  deadline: "2025-11-20T14:10:00Z"

spec:
  # ... (same as above)

status:
  phase: COMPLETED
  startTime: "2025-11-20T14:00:35Z"
  completionTime: "2025-11-20T14:05:12Z"
  result: |
    MessageQueue implementation complete:

    Files created:
    - include/core/message_queue.hpp
    - src/core/message_queue.cpp
    - tests/unit/message_queue_test.cpp

    Implementation details:
    - Implemented push() using concurrentqueue::enqueue()
    - Implemented try_pop() using concurrentqueue::try_dequeue()
    - Lock-free, thread-safe operations
    - RAII-compliant resource management

    Test results:
    - 12 unit tests written
    - 12/12 tests passing
    - Code coverage: 98%

    Build: SUCCESS
  subtasks: []
```

---

## Best Practices

### 1. Task Naming

✅ **DO**:

- Use kebab-case: `implement-core-messaging`
- Be descriptive: `run-unit-tests-for-message-queue`
- Include context: `phase-1-basic-delegation-test`

❌ **DON'T**:

- Use camelCase: `implementCoreMessaging`
- Be vague: `task-1`, `do-thing`
- Use special characters: `implement_core!messaging`

### 2. Dependency Chains

✅ **DO**:

```yaml
tasks:
  - name: build
    depends: ""
  - name: test
    depends: "build"
  - name: deploy
    depends: "test"
```

❌ **DON'T** (circular dependency):

```yaml
tasks:
  - name: task-a
    depends: "task-b"
  - name: task-b
    depends: "task-a"  # ERROR: Circular!
```

### 3. Timeout Values

✅ **DO**:

- Set realistic timeouts based on task complexity
- Add buffer for network latency and retries
- Use parent timeout > sum of child timeouts

**Example**:

```yaml
# Parent timeout: 55m
tasks:
  - name: module-1
    spec:
      timeout: "25m"
  - name: module-2
    spec:
      timeout: "25m"

aggregation:
  timeout: "55m"  # 50m + 5m buffer
```

❌ **DON'T**:

- Set parent timeout less than children
- Use very short timeouts (<1s) for complex tasks

### 4. Capability Matching

✅ **DO**:

```yaml
requiredCapabilities:
  - cpp20        # Language
  - cmake        # Build system
  - catch2       # Test framework
  - bash         # Shell access
```

❌ **DON'T**:

- List capabilities the task doesn't need
- Use vague capabilities: `"programming"`, `"testing"`

---

## Validation

Use `YamlParser::validateTaskSpec()` to validate YAML against schema:

```cpp
#include "network/yaml_parser.hpp"

auto node = YAML::Load(yaml_string);
if (YamlParser::validateTaskSpec(node)) {
  // Valid YAML
  auto spec = YamlParser::parseTaskSpec(node);
} else {
  // Invalid YAML
}
```

**Common Validation Errors**:

- Missing required fields (`apiVersion`, `kind`, `metadata.name`, etc.)
- Invalid `targetLevel` (must be 0-3)
- Invalid `action.type` (not in enum)
- Circular dependencies in DAG

---

## Version History

| Version | Release Date | Changes |
|---------|--------------|---------|
| v1alpha1 | 2025-11-20 | Initial release |

---

**Last Updated**: 2025-11-20
**Specification Version**: v1alpha1
