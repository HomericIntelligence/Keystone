# Keystone HMAS Network Protocol

## Overview

Keystone uses **gRPC** (gRPC Remote Procedure Call) with **Protocol Buffers** for distributed agent
communication. This document describes the network protocol, RPC methods, error handling, and best practices.

**Protocol Version**: v1alpha1
**gRPC Version**: 1.50+
**Protocol Buffers Version**: 3.20+

---

## Table of Contents

1. [Services](#services)
2. [Service Registry RPCs](#service-registry-rpcs)
3. [HMAS Coordinator RPCs](#hmas-coordinator-rpcs)
4. [Message Flow](#message-flow)
5. [Error Handling](#error-handling)
6. [Performance Tuning](#performance-tuning)
7. [Security](#security)

---

## Services

Keystone defines two gRPC services:

### 1. ServiceRegistry

**Purpose**: Agent registration, discovery, and heartbeat monitoring

**Server**: Main node (Chief) at port `50051`

**Clients**: All subordinate nodes (Component, Module, Task agents)

**RPCs**: 6 methods

### 2. HMASCoordinator

**Purpose**: Task submission, status tracking, and result retrieval

**Server**: Main node (Chief) at port `50051`

**Clients**: All agents (for task submission and result routing)

**RPCs**: 6 methods

---

## Service Registry RPCs

### 1. RegisterAgent

Register an agent with the central registry.

**RPC Signature**:

```protobuf
rpc RegisterAgent(AgentRegistration) returns (RegistrationResponse);
```

**Request**:

```protobuf
message AgentRegistration {
  string agent_id = 1;              // Unique agent identifier
  string agent_type = 2;            // e.g., "ComponentLeadAgent"
  int32 level = 3;                  // 0-3 (hierarchy level)
  string ip_port = 4;               // "192.168.100.20:50052"
  repeated string capabilities = 5;  // ["cpp20", "cmake", "catch2"]
  int64 timestamp_unix_ms = 6;      // Registration timestamp
}
```

**Response**:

```protobuf
message RegistrationResponse {
  bool success = 1;                 // true if registered
  string message = 2;               // Error message if failed
  string assigned_id = 3;           // Assigned ID (if auto-generated)
}
```

**Example (C++)**:

```cpp
#include "network/grpc_client.hpp"

GrpcClientConfig config;
config.server_address = "192.168.100.10:50051";

ServiceRegistryClient client(config);

hmas::AgentRegistration reg;
reg.set_agent_id("component-lead-core-1");
reg.set_agent_type("ComponentLeadAgent");
reg.set_level(1);
reg.set_ip_port("192.168.100.20:50052");
reg.add_capabilities("cpp20");
reg.add_capabilities("cmake");
reg.set_timestamp_unix_ms(
    chrono::duration_cast<chrono::milliseconds>(
        chrono::system_clock::now().time_since_epoch()).count());

auto response = client.registerAgent(reg);
if (response.success()) {
  cout << "Registered successfully\n";
} else {
  cerr << "Registration failed: " << response.message() << "\n";
}
```

**Error Cases**:

- `ALREADY_EXISTS`: Agent ID already registered
- `INVALID_ARGUMENT`: Invalid level or missing required fields

**Best Practices**:

- Register immediately on agent startup
- Use descriptive agent IDs (e.g., `component-lead-core-1`)
- Include all capabilities the agent possesses

---

### 2. Heartbeat

Send keep-alive ping to indicate agent is still alive.

**RPC Signature**:

```protobuf
rpc Heartbeat(HeartbeatRequest) returns (HeartbeatResponse);
```

**Request**:

```protobuf
message HeartbeatRequest {
  string agent_id = 1;              // Agent identifier
  int64 timestamp_unix_ms = 2;      // Current timestamp
  float cpu_usage_percent = 3;      // Optional: CPU usage (0-100)
  float memory_usage_mb = 4;        // Optional: Memory usage in MB
  int32 active_tasks = 5;           // Optional: Number of active tasks
}
```

**Response**:

```protobuf
message HeartbeatResponse {
  bool acknowledged = 1;            // true if heartbeat received
  int64 server_timestamp_unix_ms = 2;  // Server timestamp
}
```

**Example (C++)**:

```cpp
// Send heartbeat every 1 second
while (running) {
  auto response = client.heartbeat(
      "component-lead-core-1",
      getCpuUsage(),
      getMemoryUsageMB(),
      getActiveTaskCount());

  if (!response.acknowledged()) {
    cerr << "Heartbeat not acknowledged - may need to re-register\n";
  }

  this_thread::sleep_for(chrono::seconds(1));
}
```

**Timeout**: Registry marks agents dead after **3 seconds** of no heartbeat.

**Best Practices**:

- Send heartbeat every 1 second
- Include metrics (CPU, memory, active tasks) for load balancing
- Re-register if heartbeat fails multiple times

---

### 3. UnregisterAgent

Remove agent from registry (e.g., on graceful shutdown).

**RPC Signature**:

```protobuf
rpc UnregisterAgent(UnregisterRequest) returns (UnregisterResponse);
```

**Request**:

```protobuf
message UnregisterRequest {
  string agent_id = 1;              // Agent to unregister
  string reason = 2;                // Optional: Reason (e.g., "shutdown")
}
```

**Response**:

```protobuf
message UnregisterResponse {
  bool success = 1;                 // true if unregistered
  string message = 2;               // Confirmation or error message
}
```

**Example (C++)**:

```cpp
// On agent shutdown
auto response = client.unregisterAgent("component-lead-core-1", "Graceful shutdown");
```

**Best Practices**:

- Always unregister on graceful shutdown
- Provide reason for debugging/logging

---

### 4. QueryAgents

Find agents matching criteria (type, level, capabilities).

**RPC Signature**:

```protobuf
rpc QueryAgents(AgentQuery) returns (AgentList);
```

**Request**:

```protobuf
message AgentQuery {
  string agent_type = 1;            // Filter by type (empty = all)
  int32 level = 2;                  // Filter by level (-1 = all)
  repeated string required_capabilities = 3;  // Must have ALL these
  int32 max_results = 4;            // Limit results (0 = unlimited)
  bool only_alive = 5;              // Only agents with recent heartbeat
}
```

**Response**:

```protobuf
message AgentList {
  repeated AgentInfo agents = 1;    // List of matching agents
  int32 total_count = 2;            // Total count
}

message AgentInfo {
  string agent_id = 1;
  string agent_type = 2;
  int32 level = 3;
  string ip_port = 4;
  repeated string capabilities = 5;
  int64 last_heartbeat_unix_ms = 6;
  float cpu_usage_percent = 7;
  float memory_usage_mb = 8;
}
```

**Example (C++)**:

```cpp
// Find all ModuleLeadAgents at level 2 with cpp20 capability
hmas::AgentQuery query;
query.set_agent_type("ModuleLeadAgent");
query.set_level(2);
query.add_required_capabilities("cpp20");
query.set_only_alive(true);

auto agent_list = client.queryAgents(query);

cout << "Found " << agent_list.total_count() << " matching agents:\n";
for (const auto& agent : agent_list.agents()) {
  cout << "  - " << agent.agent_id() << " at " << agent.ip_port() << "\n";
}
```

**Capability Matching**:

- Agent must have **ALL** capabilities listed in `required_capabilities`
- If query has `["cpp20", "cmake"]`, agent must have both

**Best Practices**:

- Use `only_alive=true` to exclude dead agents
- Query with specific capabilities to ensure compatibility
- Use `max_results` to limit query cost

---

### 5. GetAgent

Get information about a specific agent by ID.

**RPC Signature**:

```protobuf
rpc GetAgent(GetAgentRequest) returns (AgentInfo);
```

**Request**:

```protobuf
message GetAgentRequest {
  string agent_id = 1;              // Agent ID to lookup
}
```

**Response**: Same as `AgentInfo` above

**Error Cases**:

- `NOT_FOUND`: Agent ID not in registry

---

### 6. ListAllAgents

List all registered agents (optionally only alive ones).

**RPC Signature**:

```protobuf
rpc ListAllAgents(ListAllAgentsRequest) returns (AgentList);
```

**Request**:

```protobuf
message ListAllAgentsRequest {
  bool only_alive = 1;              // Filter to only alive agents
}
```

**Response**: Same as `AgentList` above

**Use Cases**:

- Monitoring dashboards
- Debugging cluster state
- Health checks

---

## HMAS Coordinator RPCs

### 1. SubmitTask

Submit a task for execution.

**RPC Signature**:

```protobuf
rpc SubmitTask(TaskRequest) returns (TaskResponse);
```

**Request**:

```protobuf
message TaskRequest {
  string yaml_spec = 1;             // Full YAML task specification
  string session_id = 2;            // Session identifier
  int64 deadline_unix_ms = 3;       // Task deadline timestamp
  TaskPriority priority = 4;        // Task priority enum
  string parent_task_id = 5;        // Parent task ID (if subtask)
}
```

**Response**:

```protobuf
message TaskResponse {
  string task_id = 1;               // Unique task ID
  bool accepted = 2;                // true if task accepted
  string assigned_node = 3;         // IP:port of assigned agent
  string assigned_agent_id = 4;     // ID of assigned agent
  string error = 5;                 // Error message if not accepted
  int64 accepted_at_unix_ms = 6;    // Acceptance timestamp
}
```

**Example (C++)**:

```cpp
#include "network/grpc_client.hpp"
#include "network/yaml_parser.hpp"

// Generate YAML task spec
HierarchicalTaskSpec spec;
spec.api_version = "keystone.hmas.io/v1alpha1";
spec.kind = "HierarchicalTask";
// ... fill in spec fields ...

string yaml_str = YamlParser::generateTaskSpec(spec);

// Submit task
HMASCoordinatorClient client(config);
auto response = client.submitTask(
    yaml_str,
    "session-xyz-456",
    deadline_unix_ms,
    hmas::TASK_PRIORITY_HIGH);

if (response.accepted()) {
  cout << "Task " << response.task_id()
       << " assigned to " << response.assigned_node() << "\n";
} else {
  cerr << "Task rejected: " << response.error() << "\n";
}
```

**Error Cases**:

- `INVALID_ARGUMENT`: Invalid YAML specification
- `RESOURCE_EXHAUSTED`: No available agents matching criteria
- `DEADLINE_EXCEEDED`: Deadline already passed

**Best Practices**:

- Validate YAML before submission
- Set realistic deadlines (task duration + buffer)
- Use appropriate priority levels

---

### 2. StreamTaskStatus

Stream real-time status updates for a task (server-side streaming).

**RPC Signature**:

```protobuf
rpc StreamTaskStatus(TaskStatusRequest)
    returns (stream TaskStatusUpdate);
```

**Request**:

```protobuf
message TaskStatusRequest {
  string task_id = 1;               // Task to monitor
  bool include_subtasks = 2;        // Include subtask status
}
```

**Response Stream**:

```protobuf
message TaskStatusUpdate {
  string task_id = 1;
  TaskPhase phase = 2;              // Current phase enum
  int32 progress_percent = 3;       // 0-100
  string current_subtask = 4;       // Currently executing subtask
  int64 updated_at_unix_ms = 5;
  repeated SubtaskStatus subtasks = 6;  // If include_subtasks=true
}
```

**Example (C++)**:

```cpp
// Stream status updates until task completes
hmas::TaskStatusRequest request;
request.set_task_id("task-12345");
request.set_include_subtasks(true);

grpc::ClientContext context;
auto reader = stub->StreamTaskStatus(&context, request);

hmas::TaskStatusUpdate update;
while (reader->Read(&update)) {
  cout << "Task " << update.task_id()
       << " phase: " << update.phase()
       << " progress: " << update.progress_percent() << "%\n";

  if (update.phase() == hmas::TASK_PHASE_COMPLETED ||
      update.phase() == hmas::TASK_PHASE_FAILED) {
    break;  // Task done
  }
}
```

**Streaming Interval**: Server sends updates every **500ms**

**Best Practices**:

- Use streaming for long-running tasks (>10s)
- Handle stream disconnections gracefully
- Set client-side deadline to prevent hanging

---

### 3. GetTaskResult

Fetch final task result (blocking or non-blocking).

**RPC Signature**:

```protobuf
rpc GetTaskResult(TaskResultRequest) returns (TaskResult);
```

**Request**:

```protobuf
message TaskResultRequest {
  string task_id = 1;               // Task ID
  int64 timeout_ms = 2;             // Wait timeout (0 = non-blocking)
}
```

**Response**:

```protobuf
message TaskResult {
  string task_id = 1;
  string parent_task_id = 2;        // Parent task (for routing)
  TaskPhase status = 3;             // COMPLETED | FAILED | TIMEOUT
  string result_yaml = 4;           // Result as YAML (status section)
  string error = 5;                 // Error message if failed
  int64 completed_at_unix_ms = 6;
  string origin_node = 7;           // Node that produced result
}
```

**Example (C++)**:

```cpp
// Non-blocking fetch
auto result = client.getTaskResult("task-12345", 0);
if (result.status() == hmas::TASK_PHASE_COMPLETED) {
  cout << "Result:\n" << result.result_yaml() << "\n";
}

// Blocking fetch with 30s timeout
auto result = client.getTaskResult("task-12345", 30000);
```

**Polling Interval**: If timeout > 0, client polls every **100ms**

**Error Cases**:

- `NOT_FOUND`: Task result not available
- `DEADLINE_EXCEEDED`: Timeout waiting for result

---

### 4. SubmitResult

Submit task result back to parent (used by agents).

**RPC Signature**:

```protobuf
rpc SubmitResult(TaskResult) returns (ResultAcknowledgement);
```

**Request**: Same as `TaskResult` above

**Response**:

```protobuf
message ResultAcknowledgement {
  bool acknowledged = 1;            // true if received
  string message = 2;               // Confirmation message
}
```

**Example (C++)**:

```cpp
// Agent submits result after task completion
hmas::TaskResult result;
result.set_task_id("task-12345");
result.set_parent_task_id("task-12340");
result.set_status(hmas::TASK_PHASE_COMPLETED);
result.set_result_yaml(generateResultYaml());
result.set_origin_node(my_ip_port);

auto ack = client.submitResult(result);
```

**Best Practices**:

- Submit results immediately after task completion
- Include `parent_task_id` for proper routing
- Set `origin_node` to agent's IP:port

---

### 5. CancelTask

Cancel a running task.

**RPC Signature**:

```protobuf
rpc CancelTask(CancelRequest) returns (CancelResponse);
```

**Request**:

```protobuf
message CancelRequest {
  string task_id = 1;               // Task to cancel
  string reason = 2;                // Cancellation reason
}
```

**Response**:

```protobuf
message CancelResponse {
  bool cancelled = 1;               // true if cancelled
  string message = 2;               // Confirmation or error
  TaskPhase current_phase = 3;      // Phase when cancelled
}
```

**Example (C++)**:

```cpp
auto response = client.cancelTask("task-12345", "User requested");
if (response.cancelled()) {
  cout << "Task cancelled (was in " << response.current_phase() << " phase)\n";
}
```

**Limitations**:

- Cannot cancel tasks in terminal states (COMPLETED, FAILED, CANCELLED)
- Cancellation is best-effort (agent may not stop immediately)

---

### 6. GetTaskProgress

Get task progress synchronously (polling alternative to streaming).

**RPC Signature**:

```protobuf
rpc GetTaskProgress(TaskProgressRequest) returns (TaskProgress);
```

**Request**:

```protobuf
message TaskProgressRequest {
  string task_id = 1;
  bool include_subtasks = 2;
}
```

**Response**:

```protobuf
message TaskProgress {
  string task_id = 1;
  TaskPhase phase = 2;
  int32 progress_percent = 3;
  string current_subtask = 4;
  int64 updated_at_unix_ms = 5;
  repeated SubtaskStatus subtasks = 6;
  bool is_complete = 7;             // true if in terminal state
}
```

**Example (C++)**:

```cpp
// Poll progress every 2 seconds
while (true) {
  auto progress = client.getTaskProgress("task-12345", true);
  cout << "Progress: " << progress.progress_percent() << "%\n";

  if (progress.is_complete()) {
    break;
  }

  this_thread::sleep_for(chrono::seconds(2));
}
```

**Best Practices**:

- Use `StreamTaskStatus` for continuous monitoring
- Use `GetTaskProgress` for occasional polling

---

## Message Flow

### Typical Task Submission Flow

```
┌──────────┐
│  Client  │
└────┬─────┘
     │
     │ 1. SubmitTask(YAML)
     ▼
┌─────────────────────┐
│ HMASCoordinator     │
│ (Main Node)         │
│                     │
│ 2. Parse YAML       │
│ 3. Query Registry   │
│ 4. Route to Agent   │
└─────┬───────────────┘
      │
      │ 5. Forward YAML (gRPC)
      ▼
┌─────────────────────┐
│  Agent              │
│  (Remote Node)      │
│                     │
│ 6. Execute Task     │
│ 7. Generate Result  │
└─────┬───────────────┘
      │
      │ 8. SubmitResult(YAML)
      ▼
┌─────────────────────┐
│ HMASCoordinator     │
│                     │
│ 9. Store Result     │
│ 10. Notify Client   │
└─────┬───────────────┘
      │
      │ 11. GetTaskResult()
      ▼
┌──────────┐
│  Client  │
└──────────┘
```

### Heartbeat Flow

```
┌─────────────┐
│   Agent     │
└──┬──────────┘
   │
   │ Every 1s: Heartbeat(id, metrics)
   ▼
┌────────────────────┐
│ ServiceRegistry    │
│                    │
│ Update last_seen   │
│ Update metrics     │
└────────────────────┘
```

---

## Error Handling

### gRPC Status Codes

| Code | Description | Example |
|------|-------------|---------|
| **OK** | Success | RPC completed successfully |
| **CANCELLED** | Client cancelled | User cancelled task |
| **INVALID_ARGUMENT** | Invalid request | Malformed YAML |
| **DEADLINE_EXCEEDED** | RPC timeout | Task took too long |
| **NOT_FOUND** | Resource not found | Task ID doesn't exist |
| **ALREADY_EXISTS** | Resource exists | Agent ID already registered |
| **RESOURCE_EXHAUSTED** | Out of resources | No agents available |
| **UNAVAILABLE** | Service unavailable | Network issue, retry |
| **INTERNAL** | Internal error | Server bug |

### Error Handling Best Practices

**1. Retry Transient Errors**:

```cpp
bool isRetriable(grpc::StatusCode code) {
  return code == grpc::UNAVAILABLE ||
         code == grpc::DEADLINE_EXCEEDED ||
         code == grpc::RESOURCE_EXHAUSTED;
}

// Retry with exponential backoff
int attempt = 0;
while (attempt < 3) {
  try {
    auto response = client.submitTask(...);
    return response;
  } catch (const grpc::Status& status) {
    if (isRetriable(status.error_code()) && attempt < 2) {
      int delay_ms = 1000 * std::pow(2, attempt);  // 1s, 2s, 4s
      std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
      ++attempt;
    } else {
      throw;
    }
  }
}
```

**2. Log Error Details**:

```cpp
try {
  auto response = client.submitTask(...);
} catch (const std::runtime_error& e) {
  // gRPC client throws runtime_error with detailed message
  cerr << "Task submission failed: " << e.what() << "\n";
}
```

**3. Handle Deadline Exceeded**:

```cpp
// Set per-RPC deadline
grpc::ClientContext context;
context.set_deadline(
    std::chrono::system_clock::now() + std::chrono::seconds(30));

auto status = stub->SubmitTask(&context, request, &response);
if (!status.ok() && status.error_code() == grpc::DEADLINE_EXCEEDED) {
  cerr << "RPC timed out after 30s\n";
}
```

---

## Performance Tuning

### Message Size Limits

**Default**: 100 MB

**Configuration**:

```cpp
GrpcServerConfig server_config;
server_config.max_message_size = 100 * 1024 * 1024;  // 100MB

GrpcClientConfig client_config;
client_config.max_message_size = 100 * 1024 * 1024;
```

**Recommendations**:

- Tasks with large YAML specs: increase to 200 MB
- Low-memory environments: decrease to 10 MB

### Timeouts

**Default RPC Timeout**: 30 seconds

**Configuration**:

```cpp
GrpcClientConfig config;
config.timeout_ms = 60000;  // 60 seconds
```

**Recommendations**:

- Short tasks (<10s): 10s timeout
- Long tasks (>1min): 120s timeout
- Streaming RPCs: No timeout (handled by keep-alive)

### Connection Pooling

gRPC reuses connections automatically via HTTP/2 multiplexing.

**Keep-Alive Configuration**:

```cpp
grpc::ChannelArguments args;
args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, 10000);           // 10s
args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 5000);         // 5s
args.SetInt(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);  // Send even if idle

auto channel = grpc::CreateCustomChannel(
    server_address,
    grpc::InsecureChannelCredentials(),
    args);
```

### Compression

**Enable gzip compression** for large payloads:

```cpp
grpc::ClientContext context;
context.set_compression_algorithm(GRPC_COMPRESS_GZIP);

stub->SubmitTask(&context, request, &response);
```

**Trade-off**: CPU time vs. network bandwidth

---

## Security

### TLS/SSL Support

**Status**: ✅ **Implemented** in Phase 1.1 (Issue #52)

**Implementation Date**: 2025-11-26

Keystone now supports TLS/SSL for secure gRPC communication using environment variable configuration.

#### Environment Variables

- **`KEYSTONE_TLS_CERT_PATH`**: Server certificate file path (PEM format)
- **`KEYSTONE_TLS_KEY_PATH`**: Server private key file path (PEM format)
- **`KEYSTONE_TLS_CA_PATH`**: CA certificate for client verification (PEM format)

#### Configuration Precedence

1. **Environment variables** (highest priority)
2. **Config struct values** (fallback)
3. **Insecure credentials** (default if TLS not configured)

#### Server Configuration

```cpp
// Using environment variables
export KEYSTONE_TLS_CERT_PATH=/path/to/server.crt
export KEYSTONE_TLS_KEY_PATH=/path/to/server.key
export KEYSTONE_TLS_CA_PATH=/path/to/ca.crt

// Or using config struct
GrpcServerConfig config;
config.enable_tls = true;
config.tls_cert_path = "/path/to/server.crt";
config.tls_key_path = "/path/to/server.key";
config.tls_ca_path = "/path/to/ca.crt";

// Server automatically uses TLS if configured
auto server = std::make_unique<GrpcServer>(config);
server->start();
```

#### Client Configuration

```cpp
// Using environment variables
export KEYSTONE_TLS_CA_PATH=/path/to/ca.crt

// Or using config struct
GrpcClientConfig config;
config.enable_tls = true;
config.tls_ca_path = "/path/to/ca.crt";

// Both HMASCoordinatorClient and ServiceRegistryClient support TLS
auto coordinator = std::make_unique<HMASCoordinatorClient>(server_address, config);
auto registry = std::make_unique<ServiceRegistryClient>(server_address, config);
```

#### Implementation Details

**Files Modified**:

- `src/network/grpc_server.cpp`: TLS credential building
- `src/network/grpc_client.cpp`: TLS channel creation
- `tests/unit/test_grpc_tls.cpp`: 14 comprehensive unit tests

**Key Features**:

- ✅ Backward compatible (TLS disabled by default)
- ✅ Environment variable overrides for easy deployment
- ✅ Clear error messages on misconfiguration
- ✅ Binary file reading for certificates
- ✅ Thread-safe implementation

**Testing**:

```bash
# Build with gRPC support
cmake -S . -B build/grpc -G Ninja -DENABLE_GRPC=ON
cmake --build build/grpc

# Run TLS tests
./build/grpc/bin/unit_tests --gtest_filter="GrpcTlsTest.*"
```

#### Certificate Generation (Development)

For local testing, generate self-signed certificates:

```bash
# Generate CA key and certificate
openssl genrsa -out ca.key 4096
openssl req -new -x509 -days 365 -key ca.key -out ca.crt -subj "/CN=Keystone CA"

# Generate server key and certificate
openssl genrsa -out server.key 4096
openssl req -new -key server.key -out server.csr -subj "/CN=localhost"
openssl x509 -req -days 365 -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out server.crt

# Set environment variables
export KEYSTONE_TLS_CERT_PATH=$(pwd)/server.crt
export KEYSTONE_TLS_KEY_PATH=$(pwd)/server.key
export KEYSTONE_TLS_CA_PATH=$(pwd)/ca.crt
```

#### Future Enhancements

**Phase 9 Plan**: Mutual TLS (mTLS) with client certificates

- Client certificate authentication
- Certificate-based agent identification
- Certificate rotation support

### Authentication (Future)

**Phase 9 Plan**: JWT token-based authentication

**Interceptor** (example):

```cpp
class AuthInterceptor : public grpc::experimental::Interceptor {
 public:
  void Intercept(grpc::experimental::InterceptorBatchMethods* methods) override {
    if (methods->QueryInterceptionHookPoint(
            grpc::experimental::InterceptionHookPoints::PRE_SEND_INITIAL_METADATA)) {
      auto* metadata = methods->GetSendInitialMetadata();
      metadata->insert(std::make_pair("authorization", "Bearer " + jwt_token_));
    }
    methods->Proceed();
  }
 private:
  std::string jwt_token_;
};
```

---

## Monitoring and Observability

### Metrics (Future - Phase 9)

**Prometheus Integration**:

- RPC count by method
- RPC latency histogram
- Active connections
- Error rate by status code

**Example Metrics**:

```
grpc_server_handled_total{grpc_method="SubmitTask",grpc_code="OK"} 1234
grpc_server_handling_seconds{grpc_method="SubmitTask",quantile="0.99"} 0.5
grpc_client_started_total{grpc_method="RegisterAgent"} 567
```

### Tracing (Future)

**OpenTelemetry Integration**:

- Distributed tracing across nodes
- Request ID propagation
- Span creation per RPC

---

## References

- [gRPC C++ Documentation](https://grpc.io/docs/languages/cpp/)
- [gRPC Error Handling Guide](https://grpc.io/docs/guides/error/)
- [Protocol Buffers Language Guide](https://protobuf.dev/programming-guides/proto3/)
- [gRPC Best Practices](https://grpc.io/docs/guides/performance/)

---

**Last Updated**: 2025-11-26
**Protocol Version**: v1alpha1
**gRPC Version**: 1.50+
