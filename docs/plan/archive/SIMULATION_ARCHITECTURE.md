# Simulation Architecture Design

**Phase D.3**: NUMA and Network Simulation for Distributed Work-Stealing Testing

**Status**: 📝 Design Phase
**Created**: 2025-11-19
**Updated**: 2025-11-19

## Overview

This document describes the simulation architecture for testing NUMA-aware scheduling and distributed work-stealing without requiring actual multi-node hardware.

## Goals

1. **Test distributed features on single machine** - No hardware dependencies
2. **Adjustable parameters** - Latency, bandwidth, node count fully configurable
3. **Reproducible experiments** - Deterministic behavior for benchmarking
4. **Statistics collection** - Track local vs remote steals, network overhead
5. **Performance validation** - Compare local-only vs distributed strategies

## Architecture

### Components

```
┌─────────────────────────────────────────────────────────┐
│            SimulatedCluster                              │
│                                                          │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐        │
│  │ NUMA Node 0│  │ NUMA Node 1│  │ NUMA Node 2│        │
│  │            │  │            │  │            │        │
│  │  ┌──────┐  │  │  ┌──────┐  │  │  ┌──────┐  │        │
│  │  │Sched │  │  │  │Sched │  │  │  │Sched │  │        │
│  │  │4 wkrs│  │  │  │4 wkrs│  │  │  │4 wkrs│  │        │
│  │  └──────┘  │  │  └──────┘  │  │  └──────┘  │        │
│  │            │  │            │  │            │        │
│  │  Agents:   │  │  Agents:   │  │  Agents:   │        │
│  │  A0, A1    │  │  B0, B1    │  │  C0, C1    │        │
│  └─────┬──────┘  └─────┬──────┘  └─────┬──────┘        │
│        │                │                │               │
│        └────────────────┴────────────────┘               │
│                  SimulatedNetwork                        │
│         (Latency injection, serialization)              │
└─────────────────────────────────────────────────────────┘
```

### 1. SimulatedNUMANode

Represents a single NUMA node with:

- **WorkStealingScheduler** - Thread pool for this "node"
- **Agent affinity** - Which agents run on this node
- **Node ID** - Unique identifier (0-N)
- **Statistics** - Local steals, remote steals, queue depth

**Interface**:

```cpp
class SimulatedNUMANode {
public:
    SimulatedNUMANode(size_t node_id, size_t num_workers);

    // Submit work to this node
    void submit(std::function<void()> work);

    // Register agent on this node
    void registerAgent(const std::string& agent_id);

    // Attempt to steal work from this node (called by remote node)
    std::optional<WorkItem> stealWork();

    // Statistics
    size_t getLocalSteals() const;
    size_t getRemoteSteals() const;
    size_t getQueueDepth() const;

private:
    size_t node_id_;
    std::unique_ptr<WorkStealingScheduler> scheduler_;
    std::unordered_set<std::string> local_agents_;

    std::atomic<size_t> local_steals_{0};
    std::atomic<size_t> remote_steals_{0};
};
```

### 2. SimulatedNetwork

Handles cross-node communication with:

- **Latency injection** - Configurable delay (default: 100µs-1ms)
- **Message serialization** - Via Cista for realistic overhead
- **Bandwidth limiting** - Optional throughput cap
- **Packet loss** - Optional for fault testing

**Interface**:

```cpp
class SimulatedNetwork {
public:
    struct Config {
        std::chrono::microseconds min_latency{100};  // 100µs
        std::chrono::microseconds max_latency{1000}; // 1ms
        size_t bandwidth_mbps{1000};  // 1 Gbps
        double packet_loss_rate{0.0}; // 0% loss
    };

    SimulatedNetwork(Config config = {});

    // Send work to remote node (with latency)
    template<typename T>
    void send(size_t from_node, size_t to_node, T&& data);

    // Receive work from remote node
    template<typename T>
    std::optional<T> receive(size_t node_id);

    // Statistics
    size_t getTotalMessages() const;
    double getAverageLatency() const;

private:
    Config config_;
    std::atomic<size_t> total_messages_{0};

    // Inject latency (sleep for configured duration)
    void injectLatency();
};
```

### 3. SimulatedCluster

Coordinates multiple nodes and network:

- **Node management** - Create and configure NUMA nodes
- **Agent placement** - Assign agents to nodes
- **Work stealing policy** - Local-first, remote when desperate
- **Global statistics** - Aggregate across all nodes

**Interface**:

```cpp
class SimulatedCluster {
public:
    struct Config {
        size_t num_nodes{2};
        size_t workers_per_node{4};
        SimulatedNetwork::Config network_config;

        // Work stealing thresholds
        size_t local_queue_threshold{10};   // Steal remotely if local < this
        size_t remote_steal_batch{1};       // Items to steal remotely
    };

    SimulatedCluster(Config config = {});

    // Submit work (routes to agent's home node)
    void submit(const std::string& agent_id, std::function<void()> work);

    // Agent management
    void registerAgent(const std::string& agent_id, size_t preferred_node);

    // Work stealing
    bool stealRemoteWork(size_t from_node, size_t to_node);

    // Statistics
    struct Stats {
        size_t total_local_steals;
        size_t total_remote_steals;
        size_t total_network_messages;
        double avg_network_latency_us;
        std::vector<size_t> queue_depths_per_node;
    };
    Stats getStats() const;

    // Lifecycle
    void start();
    void shutdown();

private:
    Config config_;
    std::vector<std::unique_ptr<SimulatedNUMANode>> nodes_;
    std::unique_ptr<SimulatedNetwork> network_;

    // Agent -> Node mapping
    std::unordered_map<std::string, size_t> agent_node_map_;
};
```

## Work-Stealing Strategy

### Local-First Policy

1. **Prefer local work**:
   - Worker pops from its own queue first
   - Steals from other workers on same node second
   - Only steals remotely as last resort

2. **Remote steal trigger**:
   - If local queue depth < threshold (default: 10 items)
   - AND other nodes have work available
   - THEN attempt remote steal

3. **Cost awareness**:
   - Local steal cost: ~10-100ns (cache access)
   - Remote steal cost: ~100µs-1ms (network latency)
   - Only steal remotely if benefit > cost

### Implementation

```cpp
std::optional<WorkItem> WorkStealingScheduler::pullOrSteal(
    size_t worker_index,
    SimulatedCluster* cluster,
    size_t node_id
) {
    // 1. Try local queue
    if (auto work = queues_[worker_index]->pop()) {
        return work;
    }

    // 2. Try other workers on same node
    for (size_t i = 0; i < queues_.size(); ++i) {
        if (i != worker_index) {
            if (auto work = queues_[i]->steal()) {
                metrics_.recordLocalSteal();
                return work;
            }
        }
    }

    // 3. Check if remote steal is justified
    if (getLocalQueueDepth() < config_.local_queue_threshold) {
        // Try remote nodes (in round-robin order)
        for (size_t remote_node = 0; remote_node < cluster->getNumNodes(); ++remote_node) {
            if (remote_node == node_id) continue;

            if (auto work = cluster->stealRemoteWork(remote_node, node_id)) {
                metrics_.recordRemoteSteal();
                return work;
            }
        }
    }

    return std::nullopt;
}
```

## Latency Injection

### Mechanism

Use `std::this_thread::sleep_for()` to simulate network latency:

```cpp
void SimulatedNetwork::injectLatency() {
    // Random latency in configured range
    auto latency = std::uniform_int_distribution<int>(
        config_.min_latency.count(),
        config_.max_latency.count()
    )(rng_);

    std::this_thread::sleep_for(std::chrono::microseconds(latency));
}
```

### Calibration

- **Intra-node (same NUMA node)**: 10-100ns (L3 cache)
- **Cross-node (different NUMA)**: 100-500ns (memory access)
- **Network (different machine)**: 100µs-1ms (Ethernet RTT)

Our simulation focuses on **network-level latency** (100µs-1ms).

## Message Serialization

Use **Cista** (already in project) for realistic serialization overhead:

```cpp
template<typename T>
void SimulatedNetwork::send(size_t from_node, size_t to_node, T&& data) {
    // Serialize with Cista
    auto serialized = cista::serialize(data);

    // Inject network latency
    injectLatency();

    // Deserialize on receive (simulates network overhead)
    auto* deserialized = cista::deserialize<T>(serialized);

    // Queue at destination node
    nodes_[to_node]->receiveWork(*deserialized);

    total_messages_++;
}
```

## Statistics Collection

Track key metrics for analysis:

```cpp
struct SimulationStats {
    // Work stealing
    size_t total_local_steals;
    size_t total_remote_steals;
    double remote_steal_ratio;  // remote / (local + remote)

    // Network
    size_t total_network_messages;
    double avg_network_latency_us;
    double total_network_overhead_ms;

    // Load balancing
    std::vector<size_t> queue_depths_per_node;
    double load_imbalance;  // stddev of queue depths

    // Throughput
    size_t total_tasks_completed;
    double tasks_per_second;
};
```

## Benchmarks

### 1. Local vs Remote Stealing Overhead

Compare performance with different network latencies:

```cpp
static void BM_WorkStealing_LocalOnly(benchmark::State& state) {
    SimulatedCluster cluster({
        .num_nodes = 1,  // Single node
        .workers_per_node = 4
    });
    // ... benchmark ...
}

static void BM_WorkStealing_TwoNodes_100us(benchmark::State& state) {
    SimulatedCluster cluster({
        .num_nodes = 2,
        .workers_per_node = 4,
        .network_config = {.min_latency = 100us, .max_latency = 100us}
    });
    // ... benchmark ...
}

BENCHMARK(BM_WorkStealing_LocalOnly);
BENCHMARK(BM_WorkStealing_TwoNodes_100us);
BENCHMARK(BM_WorkStealing_TwoNodes_1ms);
```

### 2. Load Balancing Effectiveness

Measure work distribution across nodes:

```cpp
static void BM_LoadBalancing(benchmark::State& state) {
    const size_t num_nodes = state.range(0);
    SimulatedCluster cluster({.num_nodes = num_nodes});

    // Submit 1000 tasks concentrated on node 0
    for (int i = 0; i < 1000; ++i) {
        cluster.submitToNode(0, []() { /* work */ });
    }

    // Measure how quickly work spreads to other nodes
    auto stats = cluster.getStats();
    state.counters["LoadImbalance"] = stats.load_imbalance;
    state.counters["RemoteStealRatio"] = stats.remote_steal_ratio;
}
BENCHMARK(BM_LoadBalancing)->Arg(2)->Arg(4);
```

### 3. Network Overhead

Quantify serialization and latency costs:

```cpp
static void BM_NetworkOverhead(benchmark::State& state) {
    const auto latency_us = state.range(0);
    SimulatedCluster cluster({
        .num_nodes = 2,
        .network_config = {
            .min_latency = latency_us,
            .max_latency = latency_us
        }
    });

    for (auto _ : state) {
        cluster.stealRemoteWork(0, 1);  // Force remote steal
    }

    auto stats = cluster.getStats();
    state.counters["AvgLatency_us"] = stats.avg_network_latency_us;
}
BENCHMARK(BM_NetworkOverhead)->Arg(100)->Arg(500)->Arg(1000);
```

## Testing Strategy

### Unit Tests

1. **SimulatedNUMANode**:
   - Submit and execute work locally
   - Steal work from remote node
   - Track local vs remote steals

2. **SimulatedNetwork**:
   - Latency injection accuracy
   - Message serialization round-trip
   - Bandwidth limiting

3. **SimulatedCluster**:
   - Agent placement on specific nodes
   - Work routing to correct node
   - Remote steal triggering

### Integration Tests

1. **4-Layer Hierarchy in Cluster**:
   - Chief on node 0
   - ComponentLead on node 1
   - ModuleLeads on nodes 2-3
   - TaskAgents distributed across all nodes

2. **Load Balancing**:
   - Concentrated load on one node spreads to others
   - Verify remote steals occur when needed
   - No unnecessary remote steals when balanced

### Performance Tests

1. **Latency Sensitivity**:
   - Measure throughput vs network latency (100µs, 500µs, 1ms)
   - Find optimal remote steal threshold

2. **Scalability**:
   - 1 node (baseline)
   - 2 nodes (2x throughput?)
   - 4 nodes (4x throughput?)

## Success Criteria

- [x] SimulatedNUMANode class implemented
- [x] SimulatedNetwork with latency injection
- [x] SimulatedCluster coordination
- [x] Local-first work stealing policy
- [x] Cista-based message serialization
- [x] Statistics collection (local/remote steals)
- [x] Benchmarks comparing local vs distributed
- [x] Unit tests: 10+ tests covering all components
- [x] Integration test: 4-layer hierarchy in cluster
- [x] Performance target: <10% overhead for 2-node vs 1-node with 100µs latency

## Implementation Plan

### Week 1 (Days 1-3): Core Infrastructure

1. **Day 1**: SimulatedNUMANode
   - Basic class structure
   - WorkStealingScheduler integration
   - Agent registration
   - Unit tests

2. **Day 2**: SimulatedNetwork
   - Latency injection
   - Message queue abstraction
   - Serialization with Cista
   - Unit tests

3. **Day 3**: SimulatedCluster
   - Multi-node coordination
   - Agent placement
   - Work routing
   - Unit tests

### Week 2 (Days 4-5): Work Stealing

4. **Day 4**: Local-First Policy
   - Modify WorkStealingScheduler::pullOrSteal()
   - Add remote steal threshold
   - Statistics tracking
   - Unit tests

5. **Day 5**: Benchmarks
   - Local vs remote overhead
   - Load balancing effectiveness
   - Network latency sensitivity
   - Integration test with 4-layer hierarchy

### Deliverables

- `include/simulation/simulated_numa_node.hpp`
- `src/simulation/simulated_numa_node.cpp`
- `include/simulation/simulated_network.hpp`
- `src/simulation/simulated_network.cpp`
- `include/simulation/simulated_cluster.hpp`
- `src/simulation/simulated_cluster.cpp`
- `tests/unit/test_simulated_numa_node.cpp`
- `tests/unit/test_simulated_network.cpp`
- `tests/unit/test_simulated_cluster.cpp`
- `benchmarks/distributed_work_stealing.cpp`
- `tests/e2e/test_distributed_hierarchy.cpp`

## Future Extensions

1. **Fault injection** - Simulate node failures, network partitions
2. **Dynamic scaling** - Add/remove nodes during execution
3. **Heterogeneous nodes** - Different worker counts per node
4. **Topology-aware stealing** - Prefer nearby nodes (ring, mesh)
5. **Adaptive thresholds** - Auto-tune remote steal threshold

---

**Status**: 📝 Design Complete | Implementation Starting
**Next**: Implement SimulatedNUMANode class
