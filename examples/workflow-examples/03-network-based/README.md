# Network-Based Multi-Agent Workflow Examples

This directory demonstrates the ProjectKeystone HMAS using **distributed network communication** with gRPC for
multi-machine deployment.

## Overview

Network-based deployment enables **distributed execution** across multiple machines with service discovery and
coordination:

- **Each agent runs on a separate machine** (or multiple agents per machine)
- **gRPC with Protocol Buffers** for network communication
- **ServiceRegistry** for Kubernetes-style service discovery
- **HMASCoordinator** for task routing and load balancing
- **Heartbeat monitoring** for liveness detection

### Advantages

✅ **Unlimited scalability**: Add machines as needed
✅ **Geographic distribution**: Agents across data centers
✅ **Fault tolerance**: Machine failure doesn't affect others
✅ **Load balancing**: Distribute work across multiple machines
✅ **Service discovery**: Dynamic agent registration and lookup
✅ **Polyglot support**: Agents in different languages (via gRPC)

### Disadvantages

❌ **High latency**: Network roundtrips (10-100ms vs < 1ms threads)
❌ **Network dependencies**: Requires stable network connectivity
❌ **Complexity**: Service discovery, routing, monitoring
❌ **Serialization overhead**: Protobuf encoding/decoding
❌ **Operational overhead**: Multiple machines to manage

---

## Architecture

```
┌─────────────────────────┐  ┌─────────────────────────┐  ┌─────────────────────────┐
│  Machine A              │  │  Machine B              │  │  Machine C              │
│  IP: 192.168.1.10       │  │  IP: 192.168.1.11       │  │  IP: 192.168.1.12       │
│                         │  │                         │  │                         │
│  ┌───────────────────┐  │  │  ┌───────────────────┐  │  │  ┌───────────────────┐  │
│  │ ChiefArchitect    │  │  │  │ ModuleLead #1     │  │  │  │  TaskAgent #1     │  │
│  │                   │  │  │  │                   │  │  │  │                   │  │
│  │  - gRPC Client    │  │  │  │  - gRPC Server    │  │  │  │  - gRPC Server    │  │
│  │  - ServiceRegistry│  │  │  │  - gRPC Client    │  │  │  │  - Heartbeat      │  │
│  │    client         │  │  │  │  - Task execution │  │  │  │  - Command exec   │  │
│  └─────────┬─────────┘  │  │  └─────────┬─────────┘  │  │  └─────────┬─────────┘  │
│            │            │  │            │            │  │            │            │
└────────────┼────────────┘  └────────────┼────────────┘  └────────────┼────────────┘
             │                            │                            │
             │                            │                            │
             └────────────────────────────┼────────────────────────────┘
                                          │
                            ┌─────────────┴─────────────┐
                            │  Machine D                │
                            │  IP: 192.168.1.20         │
                            │  (Coordination Services)  │
                            │                           │
                            │  ┌─────────────────────┐  │
                            │  │  ServiceRegistry    │  │
                            │  │  gRPC Server        │  │
                            │  │  Port: 50051        │  │
                            │  │                     │  │
                            │  │  Functions:         │  │
                            │  │  - RegisterAgent()  │  │
                            │  │  - QueryAgents()    │  │
                            │  │  - Heartbeat()      │  │
                            │  │  - Unregister()     │  │
                            │  └─────────────────────┘  │
                            │                           │
                            │  ┌─────────────────────┐  │
                            │  │  HMASCoordinator    │  │
                            │  │  gRPC Server        │  │
                            │  │  Port: 50052        │  │
                            │  │                     │  │
                            │  │  Functions:         │  │
                            │  │  - SubmitTask()     │  │
                            │  │  - GetTaskStatus()  │  │
                            │  │  - RouteMessage()   │  │
                            │  │  - Aggregate()      │  │
                            │  └─────────────────────┘  │
                            └───────────────────────────┘
```

---

## Key Components

### 1. ServiceRegistry (Service Discovery)

Kubernetes-style service discovery for agents:

```protobuf
service ServiceRegistry {
    rpc RegisterAgent(AgentInfo) returns (RegistrationResponse);
    rpc UnregisterAgent(AgentID) returns (Empty);
    rpc QueryAgents(AgentQuery) returns (AgentList);
    rpc Heartbeat(AgentID) returns (HeartbeatResponse);
}

message AgentInfo {
    string agent_id = 1;
    string agent_type = 2;      // "ChiefArchitect", "ModuleLead", "TaskAgent"
    string address = 3;          // "192.168.1.10:50100"
    repeated string capabilities = 4;
    map<string, string> metadata = 5;
}
```

**Usage**:

```cpp
// Register agent on startup
auto stub = ServiceRegistry::NewStub(channel);
AgentInfo info;
info.set_agent_id("task1");
info.set_agent_type("TaskAgent");
info.set_address("192.168.1.12:50100");
info.add_capabilities("bash_execution");

RegistrationResponse resp;
stub->RegisterAgent(&context, info, &resp);
```

**Heartbeat Monitoring**:

- Interval: 1 second
- Timeout: 3 seconds
- Agents send periodic heartbeats to stay alive
- Registry marks unresponsive agents as unhealthy

### 2. HMASCoordinator (Task Routing)

Central coordinator for task submission and routing:

```protobuf
service HMASCoordinator {
    rpc SubmitTask(TaskSpec) returns (TaskID);
    rpc GetTaskStatus(TaskID) returns (TaskStatus);
    rpc RouteMessage(Message) returns (RoutingResponse);
    rpc StreamResults(TaskID) returns (stream TaskResult);
}

message TaskSpec {
    string task_id = 1;
    string task_type = 2;       // "decompose", "execute", "synthesize"
    string goal = 3;
    LoadBalancingStrategy strategy = 4;
    ResultAggregation aggregation = 5;
}

enum LoadBalancingStrategy {
    ROUND_ROBIN = 0;
    LEAST_LOADED = 1;
    RANDOM = 2;
}

enum ResultAggregation {
    WAIT_ALL = 0;
    FIRST_SUCCESS = 1;
    MAJORITY = 2;
}
```

**Load Balancing Strategies**:

- **ROUND_ROBIN**: Distribute tasks evenly across agents
- **LEAST_LOADED**: Send to agent with fewest active tasks
- **RANDOM**: Random selection for load distribution

**Result Aggregation**:

- **WAIT_ALL**: Wait for all subtasks to complete
- **FIRST_SUCCESS**: Return as soon as one succeeds
- **MAJORITY**: Return when majority agrees

### 3. YAML Task Specification

Kubernetes-style task definitions:

```yaml
apiVersion: hmas.projectkeystone.io/v1
kind: Task
metadata:
  name: build-system-task
  taskId: task-001
  priority: HIGH
spec:
  goal: "Build system with Core and Utils modules"
  strategy:
    loadBalancing: ROUND_ROBIN
    aggregation: WAIT_ALL
  decomposition:
    - moduleId: core
      goal: "Calculate 10+20+30"
      tasks:
        - taskId: core-task1
          command: "echo 10"
        - taskId: core-task2
          command: "echo 20"
        - taskId: core-task3
          command: "echo 30"
    - moduleId: utils
      goal: "Calculate 40+50+60"
      tasks:
        - taskId: utils-task1
          command: "echo 40"
        - taskId: utils-task2
          command: "echo 50"
        - taskId: utils-task3
          command: "echo 60"
```

### 4. gRPC Agent Communication

Each agent exposes a gRPC server:

```protobuf
service AgentService {
    rpc ProcessMessage(Message) returns (Response);
    rpc GetStatus(Empty) returns (AgentStatus);
    rpc Shutdown(Empty) returns (Empty);
}

message Message {
    string msg_id = 1;
    string sender_id = 2;
    string receiver_id = 3;
    string command = 4;
    ActionType action_type = 5;
    bytes payload = 6;
}
```

---

## Examples

### 2-Layer Example: Chief (Machine A) → TaskAgent (Machine B)

**File**: `2layer_example.cpp`

**Deployment**:

```
Machine A (192.168.1.10): Chief
Machine B (192.168.1.11): TaskAgent
Machine C (192.168.1.20): ServiceRegistry + HMASCoordinator
```

**Setup**:

```bash
# Machine C: Start coordination services
./service_registry --port=50051 &
./hmas_coordinator --port=50052 &

# Machine B: Start TaskAgent
./network_2layer --role=task --address=192.168.1.11:50100 \
                 --registry=192.168.1.20:50051

# Machine A: Start Chief
./network_2layer --role=chief --address=192.168.1.10:50200 \
                 --registry=192.168.1.20:50051 \
                 --coordinator=192.168.1.20:50052
```

**Workflow**:

1. TaskAgent starts, registers with ServiceRegistry at 192.168.1.20:50051
2. TaskAgent starts gRPC server on 192.168.1.11:50100
3. TaskAgent sends heartbeat every 1 second
4. Chief starts, registers with ServiceRegistry
5. Chief queries ServiceRegistry for available TaskAgents
6. Chief sends command to TaskAgent via gRPC
7. TaskAgent executes command, sends result back
8. Chief receives result

**Code Highlights**:

```cpp
// TaskAgent: Register with ServiceRegistry
auto channel = grpc::CreateChannel("192.168.1.20:50051",
                                    grpc::InsecureChannelCredentials());
auto stub = ServiceRegistry::NewStub(channel);

AgentInfo info;
info.set_agent_id("task1");
info.set_agent_type("TaskAgent");
info.set_address("192.168.1.11:50100");

RegistrationResponse resp;
ClientContext context;
Status status = stub->RegisterAgent(&context, info, &resp);

// Start gRPC server
ServerBuilder builder;
builder.AddListeningPort("192.168.1.11:50100",
                          grpc::InsecureServerCredentials());
builder.RegisterService(&task_service);
std::unique_ptr<Server> server(builder.BuildAndStart());

// Chief: Query for TaskAgents
AgentQuery query;
query.set_agent_type("TaskAgent");
AgentList agents;
stub->QueryAgents(&context, query, &agents);

// Send message to TaskAgent
std::string task_address = agents.agents(0).address();
auto task_channel = grpc::CreateChannel(task_address,
                                         grpc::InsecureChannelCredentials());
auto task_stub = AgentService::NewStub(task_channel);

Message msg;
msg.set_command("echo 'Hello from network!'");
Response response;
task_stub->ProcessMessage(&context, msg, &response);
```

---

### 3-Layer Example: Chief → ModuleLead → TaskAgents (3 Machines)

**File**: `3layer_example.cpp`

**Deployment**:

```
Machine A (192.168.1.10): Chief
Machine B (192.168.1.11): ModuleLead
Machine C (192.168.1.12): TaskAgent #1
Machine D (192.168.1.13): TaskAgent #2
Machine E (192.168.1.14): TaskAgent #3
Machine F (192.168.1.20): ServiceRegistry + Coordinator
```

**Workflow**:

1. All 3 TaskAgents register with ServiceRegistry
2. ModuleLead registers with ServiceRegistry
3. Chief registers with ServiceRegistry
4. Chief queries for ModuleLead capability
5. Chief sends goal to ModuleLead via gRPC
6. ModuleLead queries for TaskAgents
7. ModuleLead sends 3 tasks to TaskAgents (parallel gRPC calls)
8. TaskAgents execute and respond
9. ModuleLead synthesizes results
10. ModuleLead sends synthesis to Chief

**Load Balancing**:

- ROUND_ROBIN: task1 → Agent1, task2 → Agent2, task3 → Agent3
- LEAST_LOADED: Check current load, send to least busy
- RANDOM: Random selection with uniform distribution

---

### 4-Layer Example: Full Distributed Hierarchy (7+ Machines)

**File**: `4layer_example.cpp`

**Deployment**:

```
Machine Layout:
├── Machine A (Chief)
├── Machine B (ComponentLead)
├── Machine C (ModuleLead #1 - Core)
├── Machine D (ModuleLead #2 - Utils)
├── Machine E (TaskAgents 1-2)
├── Machine F (TaskAgents 3-4)
├── Machine G (TaskAgents 5-6)
└── Machine H (ServiceRegistry + Coordinator)

Total: 8 machines, 10 agent processes
```

**Docker Compose Deployment**:

```yaml
version: '3.8'
services:
  service-registry:
    image: projectkeystone:latest
    command: ./service_registry --port=50051
    ports:
      - "50051:50051"
    networks:
      - hmas-network

  hmas-coordinator:
    image: projectkeystone:latest
    command: ./hmas_coordinator --port=50052
    ports:
      - "50052:50052"
    networks:
      - hmas-network

  chief:
    image: projectkeystone:latest
    command: ./network_4layer --role=chief
    environment:
      - REGISTRY_ADDRESS=service-registry:50051
      - COORDINATOR_ADDRESS=hmas-coordinator:50052
    depends_on:
      - service-registry
      - hmas-coordinator
    networks:
      - hmas-network

  component-lead:
    image: projectkeystone:latest
    command: ./network_4layer --role=component
    environment:
      - REGISTRY_ADDRESS=service-registry:50051
    depends_on:
      - service-registry
    networks:
      - hmas-network

  module-lead-core:
    image: projectkeystone:latest
    command: ./network_4layer --role=module --id=1
    environment:
      - REGISTRY_ADDRESS=service-registry:50051
    depends_on:
      - service-registry
    networks:
      - hmas-network

  module-lead-utils:
    image: projectkeystone:latest
    command: ./network_4layer --role=module --id=2
    environment:
      - REGISTRY_ADDRESS=service-registry:50051
    depends_on:
      - service-registry
    networks:
      - hmas-network

  task-agent-1:
    image: projectkeystone:latest
    command: ./network_4layer --role=task --id=1
    environment:
      - REGISTRY_ADDRESS=service-registry:50051
    depends_on:
      - service-registry
    networks:
      - hmas-network

  # ... task-agent-2 through task-agent-6 ...

networks:
  hmas-network:
    driver: bridge
```

**Launch**:

```bash
docker-compose -f docker-compose-network-4layer.yaml up
```

---

## Performance Characteristics

### Latency

- **gRPC call (LAN)**: 1-5 milliseconds
- **gRPC call (WAN)**: 10-100 milliseconds
- **Service discovery**: 1-10 milliseconds (cached)
- **Heartbeat overhead**: < 100 KB/s per agent
- **End-to-end (2-layer, LAN)**: 10-50 milliseconds
- **End-to-end (4-layer, LAN)**: 50-200 milliseconds

### Throughput

- **Single gRPC server**: 10K-100K requests/second
- **ServiceRegistry**: 100K lookups/second
- **HMASCoordinator**: 50K tasks/second
- **Bottleneck**: Network bandwidth and service capacity

### Scalability

- **Agents**: Unlimited (distributed across machines)
- **Machines**: Hundreds to thousands
- **Geographic distribution**: Global (with WAN)
- **Horizontal scaling**: Add machines dynamically

---

## Running the Examples

### Prerequisites

```bash
# Install gRPC and protobuf
sudo apt-get install -y libgrpc++-dev protobuf-compiler-grpc

# Or build from source
git clone --recurse-submodules -b v1.58.0 https://github.com/grpc/grpc
cd grpc
mkdir -p cmake/build && cd cmake/build
cmake -DgRPC_INSTALL=ON -DgRPC_BUILD_TESTS=OFF ../..
make -j$(nproc)
sudo make install
```

### Build with gRPC Support

```bash
cd build
cmake -G Ninja -DENABLE_GRPC=ON -DBUILD_EXAMPLES=ON ..
ninja network-examples
```

### Run Examples

**2-Layer (Local)**:

```bash
# Terminal 1: ServiceRegistry
./service_registry --port=50051

# Terminal 2: TaskAgent
./network_2layer --role=task --registry=localhost:50051

# Terminal 3: Chief
./network_2layer --role=chief --registry=localhost:50051
```

**3-Layer (Multi-machine)**:

```bash
# On coordination machine:
./service_registry --port=50051 &
./hmas_coordinator --port=50052 &

# On agent machines:
./network_3layer --role=task --id=1 --registry=<coord-ip>:50051
./network_3layer --role=task --id=2 --registry=<coord-ip>:50051
./network_3layer --role=task --id=3 --registry=<coord-ip>:50051
./network_3layer --role=module --registry=<coord-ip>:50051

# On chief machine:
./network_3layer --role=chief --registry=<coord-ip>:50051
```

**4-Layer (Docker)**:

```bash
docker-compose -f docker-compose-network-4layer.yaml up
```

---

## Monitoring and Debugging

### Health Checks

```bash
# Check ServiceRegistry health
grpcurl -plaintext localhost:50051 hmas.ServiceRegistry/HealthCheck

# List registered agents
grpcurl -plaintext localhost:50051 hmas.ServiceRegistry/ListAgents

# Check agent status
grpcurl -d '{"agent_id": "task1"}' -plaintext \
  localhost:50051 hmas.ServiceRegistry/GetAgentStatus
```

### Distributed Tracing

Enable OpenTelemetry tracing:

```cpp
#include <opentelemetry/trace/provider.h>

auto tracer = opentelemetry::trace::Provider::GetTracerProvider()
                  ->GetTracer("hmas-agent");

auto span = tracer->StartSpan("ProcessMessage");
// ... process message ...
span->End();
```

### Logs Aggregation

Use structured logging with correlation IDs:

```cpp
spdlog::info("Processing message",
             {{"msg_id", msg.msg_id()},
              {"agent_id", agent_id},
              {"trace_id", trace_id}});
```

---

## Security Considerations

### TLS Encryption

Enable TLS for production:

```cpp
auto creds = grpc::SslCredentials(grpc::SslCredentialsOptions());
auto channel = grpc::CreateChannel("agent.example.com:443", creds);
```

### Authentication

Use gRPC authentication:

```cpp
auto call_creds = grpc::MetadataCredentialsFromPlugin(
    std::make_unique<AuthMetadataProcessor>()
);
auto channel_creds = grpc::SslCredentials(grpc::SslCredentialsOptions());
auto composite_creds = grpc::CompositeChannelCredentials(
    channel_creds, call_creds
);
```

### Authorization

Implement RBAC in ServiceRegistry:

```cpp
bool authorize(const std::string& agent_id, const std::string& action) {
    // Check permissions
    return has_permission(agent_id, action);
}
```

---

## Advantages Over Thread/IPC-Based

1. **Unlimited Scalability**: Add machines without limit
2. **Geographic Distribution**: Deploy across regions
3. **Polyglot Support**: Agents in Python, Java, Go, etc.
4. **Service Discovery**: Automatic agent registration
5. **Load Balancing**: Built-in task distribution
6. **Monitoring**: Centralized health checks
7. **Resilience**: Machine failure doesn't affect others

## Disadvantages vs Thread/IPC-Based

1. **High Latency**: 10-100x slower (ms vs μs)
2. **Network Dependency**: Requires stable connectivity
3. **Complexity**: Service discovery, routing, monitoring
4. **Cost**: More machines = higher infrastructure cost
5. **Operational Overhead**: Deploy and manage multiple machines

---

## Next Steps

1. ✅ **Master thread-based examples** (single process)
2. ✅ **Master IPC-based examples** (multi-process)
3. ✅ **Master network-based examples** (multi-machine)
4. 🚀 **Deploy to production** (Kubernetes, cloud platforms)

See [Docker Compose Examples](./docker-compose/) for containerized deployments.

---

**Last Updated**: 2025-11-23
**Status**: Complete (requires `-DENABLE_GRPC=ON`)
**Examples**: 3 (2-layer, 3-layer, 4-layer)
**Requires**: gRPC v1.58+, Protocol Buffers v3.21+
