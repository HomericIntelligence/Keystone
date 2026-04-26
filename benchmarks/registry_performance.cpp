#include "agents/task_agent.hpp"
#include "core/agent_id_interning.hpp"
#include "core/message_bus.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include <benchmark/benchmark.h>

using namespace keystone::core;
using namespace keystone::agents;

// ============================================================================
// Baseline: String-based registry (pre-Phase A2)
// ============================================================================

// Simulate old MessageBus behavior with string-based registry
class StringBasedRegistry {
 public:
  void registerAgent(const std::string& id, std::shared_ptr<AgentCore> agent) {
    agents_[id] = agent;
  }

  bool hasAgent(const std::string& id) const { return agents_.find(id) != agents_.end(); }

  std::shared_ptr<AgentCore> getAgent(const std::string& id) const {
    auto it = agents_.find(id);
    return (it != agents_.end()) ? it->second : nullptr;
  }

 private:
  std::unordered_map<std::string, std::shared_ptr<AgentCore>> agents_;
};

// ============================================================================
// Optimized: Integer-based registry (Phase A2)
// ============================================================================

class IntegerBasedRegistry {
 public:
  void registerAgent(const std::string& id, std::shared_ptr<AgentCore> agent) {
    uint32_t int_id = interning_.intern(id);
    agents_[int_id] = agent;
  }

  bool hasAgent(const std::string& id) const {
    auto int_id = interning_.tryGetId(id);
    return int_id && agents_.find(*int_id) != agents_.end();
  }

  std::shared_ptr<AgentCore> getAgent(const std::string& id) const {
    auto int_id = interning_.tryGetId(id);
    if (!int_id)
      return nullptr;
    auto it = agents_.find(*int_id);
    return (it != agents_.end()) ? it->second : nullptr;
  }

 private:
  mutable AgentIdInterning interning_;
  std::unordered_map<uint32_t, std::shared_ptr<AgentCore>> agents_;
};

// ============================================================================
// Benchmark 1: RegisterAgent
// ============================================================================

static void BM_RegisterAgent_StringBased(benchmark::State& state) {
  StringBasedRegistry registry;
  int32_t agent_count = 0;

  for (auto _ : state) {
    std::string agent_id = "agent_" + std::to_string(agent_count++);
    auto agent = std::make_shared<TaskAgent>(agent_id);
    registry.registerAgent(agent_id, agent);
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RegisterAgent_StringBased);

static void BM_RegisterAgent_IntegerBased(benchmark::State& state) {
  IntegerBasedRegistry registry;
  int32_t agent_count = 0;

  for (auto _ : state) {
    std::string agent_id = "agent_" + std::to_string(agent_count++);
    auto agent = std::make_shared<TaskAgent>(agent_id);
    registry.registerAgent(agent_id, agent);
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RegisterAgent_IntegerBased);

// ============================================================================
// Benchmark 2: LookupAgent (hasAgent)
// ============================================================================

static void BM_LookupAgent_StringBased(benchmark::State& state) {
  StringBasedRegistry registry;

  // Pre-register 1000 agents
  for (int32_t i = 0; i < 1000; ++i) {
    std::string agent_id = "agent_" + std::to_string(i);
    auto agent = std::make_shared<TaskAgent>(agent_id);
    registry.registerAgent(agent_id, agent);
  }

  int32_t lookup_idx = 0;
  for (auto _ : state) {
    std::string agent_id = "agent_" + std::to_string(lookup_idx++ % 1000);
    benchmark::DoNotOptimize(registry.hasAgent(agent_id));
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_LookupAgent_StringBased);

static void BM_LookupAgent_IntegerBased(benchmark::State& state) {
  IntegerBasedRegistry registry;

  // Pre-register 1000 agents
  for (int32_t i = 0; i < 1000; ++i) {
    std::string agent_id = "agent_" + std::to_string(i);
    auto agent = std::make_shared<TaskAgent>(agent_id);
    registry.registerAgent(agent_id, agent);
  }

  int32_t lookup_idx = 0;
  for (auto _ : state) {
    std::string agent_id = "agent_" + std::to_string(lookup_idx++ % 1000);
    benchmark::DoNotOptimize(registry.hasAgent(agent_id));
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_LookupAgent_IntegerBased);

// ============================================================================
// Benchmark 3: GetAgent (lookup + retrieval)
// ============================================================================

static void BM_GetAgent_StringBased(benchmark::State& state) {
  StringBasedRegistry registry;

  // Pre-register 1000 agents
  for (int32_t i = 0; i < 1000; ++i) {
    std::string agent_id = "agent_" + std::to_string(i);
    auto agent = std::make_shared<TaskAgent>(agent_id);
    registry.registerAgent(agent_id, agent);
  }

  int32_t lookup_idx = 0;
  for (auto _ : state) {
    std::string agent_id = "agent_" + std::to_string(lookup_idx++ % 1000);
    auto agent = registry.getAgent(agent_id);
    benchmark::DoNotOptimize(agent);
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_GetAgent_StringBased);

static void BM_GetAgent_IntegerBased(benchmark::State& state) {
  IntegerBasedRegistry registry;

  // Pre-register 1000 agents
  for (int32_t i = 0; i < 1000; ++i) {
    std::string agent_id = "agent_" + std::to_string(i);
    auto agent = std::make_shared<TaskAgent>(agent_id);
    registry.registerAgent(agent_id, agent);
  }

  int32_t lookup_idx = 0;
  for (auto _ : state) {
    std::string agent_id = "agent_" + std::to_string(lookup_idx++ % 1000);
    auto agent = registry.getAgent(agent_id);
    benchmark::DoNotOptimize(agent);
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_GetAgent_IntegerBased);

// ============================================================================
// Benchmark 4: RouteMessage (end-to-end with MessageBus)
// ============================================================================

static void BM_RouteMessage_MessageBus(benchmark::State& state) {
  MessageBus bus;

  // Pre-register 1000 agents
  for (int32_t i = 0; i < 1000; ++i) {
    std::string agent_id = "agent_" + std::to_string(i);
    auto agent = std::make_shared<TaskAgent>(agent_id);
    bus.registerAgent(agent_id, agent);
  }

  int32_t msg_idx = 0;
  for (auto _ : state) {
    std::string receiver_id = "agent_" + std::to_string(msg_idx++ % 1000);
    KeystoneMessage msg = KeystoneMessage::create("sender", receiver_id, "test_command");
    benchmark::DoNotOptimize(bus.routeMessage(msg));
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RouteMessage_MessageBus);

// ============================================================================
// Benchmark 5: Scalability with different agent counts
// ============================================================================

static void BM_Lookup_Scalability_StringBased(benchmark::State& state) {
  StringBasedRegistry registry;
  int32_t num_agents = static_cast<int32_t>(state.range(0));

  // Pre-register agents
  for (int32_t i = 0; i < num_agents; ++i) {
    std::string agent_id = "agent_" + std::to_string(i);
    auto agent = std::make_shared<TaskAgent>(agent_id);
    registry.registerAgent(agent_id, agent);
  }

  int32_t lookup_idx = 0;
  for (auto _ : state) {
    std::string agent_id = "agent_" + std::to_string(lookup_idx++ % num_agents);
    benchmark::DoNotOptimize(registry.hasAgent(agent_id));
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Lookup_Scalability_StringBased)->Range(100, 10000);

static void BM_Lookup_Scalability_IntegerBased(benchmark::State& state) {
  IntegerBasedRegistry registry;
  int32_t num_agents = static_cast<int32_t>(state.range(0));

  // Pre-register agents
  for (int32_t i = 0; i < num_agents; ++i) {
    std::string agent_id = "agent_" + std::to_string(i);
    auto agent = std::make_shared<TaskAgent>(agent_id);
    registry.registerAgent(agent_id, agent);
  }

  int32_t lookup_idx = 0;
  for (auto _ : state) {
    std::string agent_id = "agent_" + std::to_string(lookup_idx++ % num_agents);
    benchmark::DoNotOptimize(registry.hasAgent(agent_id));
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Lookup_Scalability_IntegerBased)->Range(100, 10000);

BENCHMARK_MAIN();
