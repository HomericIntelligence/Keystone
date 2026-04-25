# Phase 8: Distributed System Plan

**Status**: 📝 Planning
**Target Start**: 2026-01-15
**Estimated Duration**: 6-8 weeks
**Branch**: TBD (claude/phase-8-distributed-*)

## Overview

Phase 8 transforms ProjectKeystone from a **single-node multi-threaded system** into a **distributed multi-node cluster** with real network communication, distributed work-stealing, fault tolerance, and consensus algorithms. This enables horizontal scalability beyond a single machine's limits.

### Current Status (Post-Phase 7)

**What We Have**:

- ✅ Production deployment on Kubernetes (Phase 6)
- ✅ AI-powered agents with LLM integration (Phase 7)
- ✅ Simulated distributed work-stealing (Phase D.3)
- ✅ Simulated network with latency injection (Phase D.3)
- ✅ Single-node work-stealing scheduler
- ✅ Message serialization with Cista (ready for network transport)

**What Phase 8 Adds**:

- Real network communication using gRPC/Protobuf
- Multi-node HMAS cluster (3-5 nodes)
- Distributed work-stealing across network
- Raft consensus for fault tolerance
- Agent migration between nodes
- Network partition handling
- Cross-node message routing

---

## Phase 8 Architecture

```
┌────────────────────────────────────────────────────────────┐
│                   Load Balancer / Ingress                  │
└──────────────┬─────────────────────┬─────────────────┬─────┘
               │                     │                 │
       ┌───────┴───────┐     ┌───────┴───────┐ ┌──────┴──────┐
       │   Node 1      │     │   Node 2      │ │   Node 3    │
       │               │     │               │ │             │
       │  ┌─────────┐  │     │  ┌─────────┐  │ │  ┌────────┐ │
       │  │ Chief   │  │     │  │Comp Lead│  │ │  │Module  │ │
       │  │Architect│  │     │  │  Agents │  │ │  │Leads   │ │
       │  └─────────┘  │     │  └─────────┘  │ │  └────────┘ │
       │  ┌─────────┐  │     │  ┌─────────┐  │ │  ┌────────┐ │
       │  │Work Steal│◄─┼─────┼─►│Work Steal│◄─┼─►│  Work  │ │
       │  │Scheduler │  │     │  │Scheduler │  │ │  Steal │ │
       │  └─────────┘  │     │  └─────────┘  │ │  └────────┘ │
       │  ┌─────────┐  │     │  ┌─────────┐  │ │  ┌────────┐ │
       │  │ Raft    │◄─┼─────┼─►│ Raft    │◄─┼─►│  Raft  │ │
       │  │Consensus│  │     │  │Consensus│  │ │  │Consens.│ │
       │  └─────────┘  │     │  └─────────┘  │ │  └────────┘ │
       └───────────────┘     └───────────────┘ └─────────────┘
               │                     │                 │
               └──────────┬──────────┴─────────────────┘
                          │
                  ┌───────┴────────┐
                  │  Shared State  │
                  │  (Raft Log)    │
                  └────────────────┘
```

**Key Features**:

- **gRPC communication**: Inter-node message passing
- **Raft consensus**: Leader election, log replication
- **Distributed work-stealing**: Cross-node task migration
- **Agent registry**: Distributed agent location tracking
- **Fault tolerance**: Node failures handled gracefully

---

## Phase 8 Sub-Phases

### Phase 8.1: gRPC Network Layer (Week 1-2)

**Goal**: Replace simulated network with real gRPC communication

**Tasks**:

1. **Define Protobuf schema** (`proto/keystone.proto`) - 6 hours
   - Message definition (equivalent to KeystoneMessage)
   - Agent metadata (id, type, state)
   - Work-stealing RPC (StealWork, SubmitWork)
   - Heartbeat and health check RPCs
   - Compile to C++ with protoc

2. **Implement gRPC server** (`src/network/grpc_server.cpp`) - 8 hours
   - Start gRPC server on configurable port (default: 50051)
   - Implement service methods:
     - `SendMessage(MessageRequest) → MessageResponse`
     - `StealWork(StealRequest) → WorkItem`
     - `Heartbeat(HeartbeatRequest) → HeartbeatResponse`
   - Thread-safe message routing to local MessageBus
   - Health check endpoint

3. **Implement gRPC client** (`src/network/grpc_client.cpp`) - 6 hours
   - Connection pooling to multiple nodes
   - Async RPC calls with futures
   - Retry logic with exponential backoff
   - Timeout handling (5s default)
   - Circuit breaker per remote node

4. **Create NetworkTransport abstraction** (`include/network/network_transport.hpp`) - 5 hours
   - Interface: `send(node_id, message)`, `receive()`, `broadcast(message)`
   - Implementations: gRPCTransport, SimulatedTransport (for testing)
   - Fallback to local delivery if same node
   - Metrics: messages sent, received, latency

5. **Integration with MessageBus** - 5 hours
   - MessageBus checks if agent is local or remote
   - If remote, use gRPCTransport to send message
   - Receive messages from gRPC server, route to local agents
   - Handle network failures (retry, circuit breaker)

6. **Unit tests** (`tests/unit/test_grpc_transport.cpp`) - 6 hours
   - Test gRPC client/server communication
   - Test message serialization (Protobuf ↔ KeystoneMessage)
   - Test retry logic and timeouts
   - Test connection pooling

**Deliverables**:

- ✅ Protobuf schema for all messages
- ✅ gRPC server and client
- ✅ NetworkTransport abstraction
- ✅ MessageBus integration
- ✅ Unit tests (15+ tests)

**Estimated Time**: 36 hours

---

### Phase 8.2: Distributed Agent Registry (Week 2-3)

**Goal**: Track which agents are on which nodes

**Tasks**:

1. **Implement DistributedAgentRegistry** (`src/network/distributed_agent_registry.cpp`) - 8 hours
   - Map: agent_id → node_id
   - Register agent on node
   - Unregister agent (on failure or migration)
   - Lookup agent location (local or remote)
   - Replicate registry across nodes (eventually consistent)

2. **Use Raft for registry replication** - 10 hours
   - Integrate Raft library (e.g., braft or logcabin)
   - Raft log entry types: RegisterAgent, UnregisterAgent
   - Leader receives writes, replicates to followers
   - Followers serve reads (may be slightly stale)
   - Handle leader election (automatic)

3. **Agent registration on startup** - 4 hours
   - Each agent registers with local node
   - Local node sends RegisterAgent to Raft leader
   - Leader replicates to all nodes
   - Registry update propagates to all nodes

4. **Agent discovery API** - 4 hours
   - `getAgentLocation(agent_id) → node_id`
   - `listAgentsOnNode(node_id) → [agent_ids]`
   - `listAllAgents() → [agent_ids]`
   - Cache locally for fast lookup

5. **Handle node failures** - 5 hours
   - If node fails, mark all its agents as unavailable
   - Raft detects missing node via heartbeat timeout
   - Failover: migrate agents to other nodes (Phase 8.4)

6. **Integration tests** (`tests/integration/test_distributed_registry.cpp`) - 5 hours
   - Deploy 3-node cluster
   - Register agents on different nodes
   - Query registry from each node
   - Simulate node failure, verify registry update

**Deliverables**:

- ✅ Distributed agent registry
- ✅ Raft-based replication
- ✅ Agent lookup API
- ✅ Node failure handling
- ✅ Integration tests (3-node cluster)

**Estimated Time**: 36 hours

---

### Phase 8.3: Distributed Work-Stealing (Week 3-4)

**Goal**: Work-stealing across network boundaries

**Tasks**:

1. **Implement RemoteWorkStealingScheduler** (`src/concurrency/remote_work_stealing_scheduler.cpp`) - 10 hours
   - Extends WorkStealingScheduler
   - Local work-stealing (same as before)
   - Remote work-stealing (via gRPC)
   - Steal threshold: only steal remotely if local queues empty
   - Network-aware stealing policy (prefer local)

2. **gRPC StealWork RPC** - 6 hours
   - RPC definition: `StealWork(node_id, worker_id) → WorkItem`
   - Server: check local queues, return work if available
   - Client: call remote nodes round-robin
   - Serialize WorkItem (std::function → cannot serialize!)
   - **Solution**: Only steal agent IDs, not work items

3. **Agent-level work-stealing** - 8 hours
   - Instead of stealing work items, steal entire agents
   - RPC: `StealAgent(node_id) → agent_id`
   - Migrate agent to requesting node
   - Update distributed registry
   - Resume agent processing on new node

4. **Load balancing across nodes** - 6 hours
   - Periodically check load (queue depth) on all nodes
   - If imbalanced (max > 2x avg), migrate agents
   - Use Raft to coordinate migrations (avoid conflicts)
   - Metrics: agents per node, queue depth per node

5. **Network-aware scheduling** - 5 hours
   - Track network latency between nodes
   - Prefer stealing from low-latency nodes
   - Avoid cross-datacenter steals if possible
   - Adaptive threshold based on network conditions

6. **E2E test: Cross-node work-stealing** (`tests/e2e/phase_8_distributed_stealing.cpp`) - 6 hours
   - Deploy 3-node cluster
   - Submit 100 tasks to Node 1
   - Verify tasks distributed to Node 2 and Node 3
   - Measure load balancing (each node processes ~33 tasks)

**Deliverables**:

- ✅ Distributed work-stealing scheduler
- ✅ Agent-level migration
- ✅ Load balancing algorithm
- ✅ Network-aware scheduling
- ✅ E2E test for cross-node stealing

**Estimated Time**: 41 hours

---

### Phase 8.4: Fault Tolerance & Recovery (Week 4-5)

**Goal**: Handle node failures gracefully

**Tasks**:

1. **Implement heartbeat monitoring across nodes** - 5 hours
   - Each node sends heartbeat to all other nodes
   - Heartbeat interval: 1 second
   - Timeout: 5 seconds (3 missed heartbeats)
   - Raft also provides node failure detection

2. **Detect node failures** - 4 hours
   - Heartbeat timeout triggers failure detection
   - Mark node as failed in registry
   - Mark all agents on failed node as unavailable
   - Notify dependent agents (send error response)

3. **Agent replication for fault tolerance** - 8 hours
   - Critical agents (ChiefArchitect) replicated on 2-3 nodes
   - Standby agents wait for heartbeat timeout
   - On failure, standby takes over
   - State replication via Raft (agent state machine)

4. **Work redistribution on failure** - 6 hours
   - When node fails, redistribute its pending work
   - Reassign agents to healthy nodes
   - Replay messages from Raft log (at-least-once delivery)
   - Idempotency required for message processing

5. **Raft leader election** - 5 hours
   - If leader fails, Raft elects new leader
   - Leader handles all writes (agent registration, migrations)
   - Followers forward writes to leader
   - Split-brain prevention via quorum (N/2 + 1)

6. **Network partition handling** - 7 hours
   - Raft uses quorum for writes (partition tolerance)
   - Minority partition cannot make progress
   - Majority partition continues operating
   - Partitions heal: rejoin Raft cluster, sync state

7. **E2E test: Node failure recovery** (`tests/e2e/phase_8_fault_tolerance.cpp`) - 6 hours
   - Deploy 3-node cluster
   - Submit work to all nodes
   - Kill Node 2 (simulate failure)
   - Verify agents on Node 2 reassigned to Node 1 or 3
   - Verify work continues processing
   - Restart Node 2, verify rejoin

**Deliverables**:

- ✅ Heartbeat-based failure detection
- ✅ Agent replication (standby agents)
- ✅ Work redistribution on failure
- ✅ Raft leader election
- ✅ Network partition handling
- ✅ E2E fault tolerance tests

**Estimated Time**: 41 hours

---

### Phase 8.5: Distributed Tracing & Monitoring (Week 5-6)

**Goal**: Observability for distributed system

**Tasks**:

1. **Implement distributed tracing with Jaeger** - 8 hours
   - OpenTelemetry SDK integration
   - Create spans for:
     - Message send (start span on sender)
     - Network RPC (span across nodes)
     - Message receive (end span on receiver)
   - Propagate trace context in gRPC metadata
   - Export traces to Jaeger collector

2. **Add cross-node metrics** - 5 hours
   - Prometheus metrics:
     - `hmas_grpc_calls_total{node, method}` - RPC count
     - `hmas_grpc_latency_seconds{node, method}` - RPC latency
     - `hmas_agents_per_node{node}` - Agent distribution
     - `hmas_work_steals_remote_total{from_node, to_node}` - Cross-node steals
   - Aggregate metrics across all nodes

3. **Create distributed dashboard** (`monitoring/grafana/distributed-cluster.json`) - 5 hours
   - Panels:
     - Cluster topology (nodes, agents per node)
     - Cross-node message flow (heatmap)
     - Network latency between nodes
     - Raft leader election events
     - Node health status

4. **Distributed log aggregation** - 4 hours
   - Loki already deployed (Phase 6)
   - Add node_id label to all logs
   - Cross-node log correlation via trace_id
   - Search logs across all nodes

5. **Alerting for distributed issues** - 4 hours
   - Prometheus alerting rules:
     - Node down (heartbeat timeout)
     - Raft leader election in progress
     - Network partition detected
     - High cross-node latency (>100ms)
   - Send alerts to Slack/email

6. **Documentation** (`docs/DISTRIBUTED_SYSTEM.md`) - 5 hours
   - Architecture overview
   - Deployment guide (multi-node cluster)
   - Troubleshooting distributed issues
   - Performance tuning tips

**Deliverables**:

- ✅ Distributed tracing with Jaeger
- ✅ Cross-node Prometheus metrics
- ✅ Distributed Grafana dashboard
- ✅ Alerting for distributed issues
- ✅ Documentation: DISTRIBUTED_SYSTEM.md

**Estimated Time**: 31 hours

---

### Phase 8.6: Performance Optimization (Week 6-8)

**Goal**: Optimize performance for distributed deployment

**Tasks**:

1. **Connection pooling** - 4 hours
   - Pool of gRPC channels per remote node
   - Reuse connections to avoid handshake overhead
   - Health check idle connections

2. **Async RPC with futures** - 5 hours
   - Non-blocking RPC calls
   - Use C++ coroutines for async I/O
   - Parallel RPCs to multiple nodes

3. **Batching messages** - 6 hours
   - Batch multiple messages into single RPC
   - Reduces network overhead
   - Flush batch after 10ms or 100 messages
   - Trade-off: latency vs throughput

4. **Compression** - 4 hours
   - gRPC compression (gzip or snappy)
   - Compress large messages (>1KB)
   - Benchmark compression overhead

5. **Zero-copy serialization** - 6 hours
   - Use Cista for zero-copy deserialization
   - Protobuf → Cista bridge
   - Avoid extra copies in network stack

6. **Benchmark distributed performance** - 6 hours
   - Throughput: messages/sec across cluster
   - Latency: P50/P95/P99 for cross-node messages
   - Scalability: 1 node vs 3 nodes vs 5 nodes
   - Identify bottlenecks (network, serialization, Raft)

7. **Tune Raft parameters** - 4 hours
   - Election timeout (100-500ms)
   - Heartbeat interval (50ms)
   - Log compaction threshold
   - Benchmark impact on throughput

**Deliverables**:

- ✅ Connection pooling
- ✅ Async RPC with futures
- ✅ Message batching
- ✅ Compression enabled
- ✅ Zero-copy serialization
- ✅ Performance benchmarks
- ✅ Tuned Raft parameters

**Estimated Time**: 35 hours

---

## Success Criteria

### Must Have ✅

- [ ] 3-node cluster communicates via gRPC
- [ ] Distributed agent registry with Raft replication
- [ ] Work-stealing across network boundaries
- [ ] Node failure detection and recovery
- [ ] Leader election working (Raft)
- [ ] Network partition handling
- [ ] Distributed tracing with Jaeger
- [ ] All existing tests pass (329+ tests)
- [ ] New E2E tests for distributed scenarios (15+ tests)

### Should Have 🎯

- [ ] Agent replication for fault tolerance (standby agents)
- [ ] Load balancing across nodes (agents evenly distributed)
- [ ] Cross-node latency <50ms (within same datacenter)
- [ ] Throughput scales linearly to 3 nodes (3x single node)
- [ ] Horizontal Pod Autoscaler (HPA) for dynamic scaling

### Nice to Have 💡

- [ ] 5-node cluster tested
- [ ] Multi-datacenter deployment (with higher latency tolerance)
- [ ] Dynamic cluster membership (add/remove nodes without restart)
- [ ] Multi-region deployment (geographic distribution)
- [ ] Edge deployment (edge nodes closer to users)

---

## Technology Stack

### Network Communication

- **gRPC**: Fast, efficient RPC framework
- **Protobuf**: Binary serialization
- **C++ gRPC**: Official C++ implementation
- **HTTP/2**: Underlying protocol for gRPC

### Consensus Algorithm

- **Raft**: Understandable consensus algorithm
- **Library**: braft (Baidu's C++ Raft implementation) or logcabin
- **Features**: Leader election, log replication, snapshots

### Serialization

- **Protobuf**: For gRPC messages
- **Cista**: Zero-copy deserialization for large payloads
- **JSON**: For configuration and monitoring

### Distributed Tracing

- **OpenTelemetry**: Standard tracing SDK
- **Jaeger**: Distributed tracing backend
- **Zipkin**: Alternative to Jaeger

### Deployment

- **Kubernetes**: Multi-node orchestration
- **Helm**: Templated deployments
- **StatefulSet**: For Raft nodes (persistent identity)

---

## Risk Mitigation

### Risk 1: Network Latency

**Impact**: High (affects performance)
**Likelihood**: Medium
**Mitigation**:

- Network-aware scheduling (prefer local)
- Batch messages to reduce round-trips
- Compression for large payloads
- Deploy nodes in same datacenter initially

### Risk 2: Raft Complexity

**Impact**: High (correctness critical)
**Likelihood**: Medium
**Mitigation**:

- Use battle-tested Raft library (braft)
- Extensive testing (Jepsen-style chaos tests)
- Start with simple use case (agent registry only)
- Read Raft paper thoroughly

### Risk 3: Network Partitions

**Impact**: Critical (split-brain risk)
**Likelihood**: Low
**Mitigation**:

- Raft quorum prevents split-brain
- Minority partition cannot make progress
- Monitor partition events, alert operators

### Risk 4: Message Loss

**Impact**: High (correctness)
**Likelihood**: Low (gRPC is reliable)
**Mitigation**:

- Retry logic with exponential backoff
- At-least-once delivery guarantee
- Idempotent message processing
- Message deduplication via msg_id

### Risk 5: Performance Overhead

**Impact**: Medium
**Likelihood**: High
**Mitigation**:

- Benchmark early and often
- Optimize hot paths (serialization, RPC)
- Use profiling to identify bottlenecks
- Accept some overhead for distributed benefits

---

## Performance Expectations

**Latency**:

- **Local message**: <100ns (in-memory)
- **Cross-node message (same datacenter)**: 1-10ms (network + serialization)
- **Cross-node message (different datacenter)**: 50-200ms

**Throughput**:

- **Single node**: 20k messages/sec (Phase D baseline)
- **3-node cluster**: 50-60k messages/sec (parallel processing, network overhead)
- **5-node cluster**: 80-100k messages/sec

**Scalability**:

- **Linear scaling**: Up to network bandwidth limits
- **Bottleneck**: Raft leader (all writes go through leader)
- **Mitigation**: Shard agents across multiple Raft groups

**Resource Usage** (per node):

- **CPU**: 1-2 cores (same as single node)
- **Memory**: 1-2 GB (same as single node)
- **Network**: 10-50 Mbps (depends on message volume)

---

## Testing Strategy

### Unit Tests

1. **gRPC Transport**: Mock server/client, test RPCs
2. **Protobuf Serialization**: Round-trip tests
3. **Distributed Registry**: CRUD operations
4. **Raft Integration**: Log append, leader election (using Raft test harness)

### Integration Tests

1. **3-Node Cluster**: Deploy locally, test communication
2. **Agent Migration**: Move agent from Node 1 to Node 2
3. **Cross-Node Work-Stealing**: Verify load balancing
4. **Heartbeat Monitoring**: Detect node failure

### E2E Tests

1. **Full Distributed Workflow**: User goal → 3 nodes → result
2. **Node Failure Recovery**: Kill node, verify recovery
3. **Network Partition**: Partition cluster, verify majority continues
4. **Raft Leader Election**: Kill leader, verify new leader elected

### Chaos Tests (Jepsen-style)

1. **Random node failures**: Kill random nodes during execution
2. **Network partitions**: Split cluster randomly
3. **Clock skew**: Simulate time drift between nodes
4. **Packet loss**: Drop random network packets

---

## Documentation Plan

### Required Documentation

1. **DISTRIBUTED_SYSTEM.md** - Architecture overview
2. **GRPC_NETWORK.md** - gRPC setup and usage
3. **RAFT_CONSENSUS.md** - Raft integration guide
4. **DISTRIBUTED_DEPLOYMENT.md** - Multi-node deployment
5. **FAULT_TOLERANCE.md** - Failure handling and recovery
6. **DISTRIBUTED_MONITORING.md** - Observability for distributed system

### README Updates

- Add "Distributed System" section
- Describe multi-node architecture
- Link to deployment guide
- Performance characteristics

---

## Next Steps

**Week 1-2**: gRPC network layer
**Week 2-3**: Distributed agent registry (Raft)
**Week 3-4**: Distributed work-stealing
**Week 4-5**: Fault tolerance and recovery
**Week 5-6**: Distributed tracing and monitoring
**Week 6-8**: Performance optimization

**After Phase 8**: Move to **Phase 10: Production Hardening**

- Security hardening (TLS, authentication, authorization)
- Performance tuning at scale (1000+ agents)
- Operational runbooks
- Incident response procedures

---

**Status**: 📝 Planning Complete - Ready for Implementation
**Total Estimated Time**: 220 hours (~6-8 weeks)
**Dependencies**: Phase 6 (Kubernetes deployment) and Phase 7 (AI integration) complete
**Prerequisites**: 3+ Kubernetes nodes, network connectivity between nodes
**Last Updated**: 2025-11-19
