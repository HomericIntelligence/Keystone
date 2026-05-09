// message_bus_performance.cpp
// Comprehensive benchmarks for MessageBus routing and delivery
//
// Phase 3.1: Updated for unified async API
//
// Measures:
// - Message routing latency (p50, p95, p99, p99.9)
// - Registration/unregistration overhead
// - Concurrent message throughput
// - Agent discovery performance

#include <benchmark/benchmark.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "agents/task_agent.hpp"
#include "core/message.hpp"
#include "core/message_bus.hpp"

using namespace keystone;
using namespace keystone::core;
using namespace keystone::agents;

// Benchmark: Simple message routing latency
static void BM_MessageRouting_SingleAgent(benchmark::State& state) {
  MessageBus bus;
  auto agent = std::make_shared<TaskAgent>("test-agent");

  bus.registerAgent(agent->getAgentId(), agent);
  agent->setMessageBus(&bus);

  auto msg = KeystoneMessage::create("sender", "test-agent", "test");

  for (auto _ : state) {
    bus.routeMessage(msg);
  }

  state.SetItemsProcessed(state.iterations());
  state.counters["messages/sec"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_MessageRouting_SingleAgent);

// Benchmark: Routing to many agents (fan-out)
static void BM_MessageRouting_FanOut(benchmark::State& state) {
  int32_t num_agents = static_cast<int32_t>(state.range(0));

  MessageBus bus;
  std::vector<std::shared_ptr<TaskAgent>> agents;

  for (int32_t i = 0; i < num_agents; ++i) {
    auto agent = std::make_shared<TaskAgent>("agent-" + std::to_string(i));
    bus.registerAgent(agent->getAgentId(), agent);
    agent->setMessageBus(&bus);
    agents.push_back(agent);
  }

  size_t agent_idx = 0;
  for (auto _ : state) {
    auto receiver = "agent-" + std::to_string(agent_idx);
    auto msg = KeystoneMessage::create("sender", receiver, "test");
    bus.routeMessage(msg);
    agent_idx = (agent_idx + 1) % num_agents;
  }

  state.SetItemsProcessed(state.iterations());
  state.counters["agents"] = num_agents;
}
BENCHMARK(BM_MessageRouting_FanOut)->Range(8, 512);

// Benchmark: Agent registration overhead
static void BM_AgentRegistration(benchmark::State& state) {
  MessageBus bus;
  std::vector<std::shared_ptr<TaskAgent>> agents;

  size_t count = 0;
  for (auto _ : state) {
    auto agent =
        std::make_shared<TaskAgent>("agent-" + std::to_string(count++));
    bus.registerAgent(agent->getAgentId(), agent);
    agents.push_back(agent);
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AgentRegistration);

// Benchmark: Agent unregistration overhead
static void BM_AgentUnregistration(benchmark::State& state) {
  int32_t num_agents = 1000;

  for (auto _ : state) {
    state.PauseTiming();
    MessageBus bus;
    std::vector<std::shared_ptr<TaskAgent>> agents;

    for (int32_t i = 0; i < num_agents; ++i) {
      auto agent = std::make_shared<TaskAgent>("agent-" + std::to_string(i));
      bus.registerAgent(agent->getAgentId(), agent);
      agents.push_back(agent);
    }
    state.ResumeTiming();

    for (int32_t i = 0; i < num_agents; ++i) {
      bus.unregisterAgent("agent-" + std::to_string(i));
    }
  }

  state.SetItemsProcessed(state.iterations() * num_agents);
}
BENCHMARK(BM_AgentUnregistration);

// Benchmark: Agent lookup (hasAgent)
static void BM_AgentLookup(benchmark::State& state) {
  int32_t num_agents = static_cast<int32_t>(state.range(0));

  MessageBus bus;
  std::vector<std::shared_ptr<TaskAgent>> agents;

  for (int32_t i = 0; i < num_agents; ++i) {
    auto agent = std::make_shared<TaskAgent>("agent-" + std::to_string(i));
    bus.registerAgent(agent->getAgentId(), agent);
    agents.push_back(agent);
  }

  size_t idx = 0;
  for (auto _ : state) {
    auto agent_id = "agent-" + std::to_string(idx % num_agents);
    benchmark::DoNotOptimize(bus.hasAgent(agent_id));
    idx++;
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AgentLookup)->Range(8, 1024);

// Benchmark: List all agents
static void BM_ListAgents(benchmark::State& state) {
  int32_t num_agents = static_cast<int32_t>(state.range(0));

  MessageBus bus;
  std::vector<std::shared_ptr<TaskAgent>> agents;

  for (int32_t i = 0; i < num_agents; ++i) {
    auto agent = std::make_shared<TaskAgent>("agent-" + std::to_string(i));
    bus.registerAgent(agent->getAgentId(), agent);
    agents.push_back(agent);
  }

  for (auto _ : state) {
    auto list = bus.listAgents();
    benchmark::DoNotOptimize(list);
  }

  state.counters["agents"] = num_agents;
}
BENCHMARK(BM_ListAgents)->Range(8, 1024);

// Benchmark: Concurrent message routing (multi-threaded)
static void BM_ConcurrentRouting(benchmark::State& state) {
  static MessageBus bus;
  static auto agent = std::make_shared<TaskAgent>("test-agent");
  static std::once_flag init_flag;

  std::call_once(init_flag, [&]() {
    bus.registerAgent(agent->getAgentId(), agent);
    agent->setMessageBus(&bus);
  });

  auto msg = KeystoneMessage::create("sender", "test-agent", "test");

  for (auto _ : state) {
    bus.routeMessage(msg);
  }
}
BENCHMARK(BM_ConcurrentRouting)->ThreadRange(1, 8);

// Benchmark: Message routing with payload (varying sizes)
static void BM_MessageRouting_WithPayload(benchmark::State& state) {
  int32_t payload_size = static_cast<int32_t>(state.range(0));  // Size in bytes

  MessageBus bus;
  auto agent = std::make_shared<TaskAgent>("test-agent");
  bus.registerAgent(agent->getAgentId(), agent);
  agent->setMessageBus(&bus);

  // Create payload of specified size
  std::string payload_data(payload_size, 'x');

  for (auto _ : state) {
    auto msg = KeystoneMessage::create("sender", "test-agent", "test");
    msg.payload = payload_data;
    bus.routeMessage(msg);
  }

  state.SetBytesProcessed(state.iterations() * payload_size);
  state.counters["payload_bytes"] = payload_size;
}
BENCHMARK(BM_MessageRouting_WithPayload)->Range(64, 64 << 10);  // 64B to 64KB

// Benchmark: Round-trip message latency
static void BM_MessageRoundTrip(benchmark::State& state) {
  MessageBus bus;

  auto agent1 = std::make_shared<TaskAgent>("agent1");
  auto agent2 = std::make_shared<TaskAgent>("agent2");

  bus.registerAgent(agent1->getAgentId(), agent1);
  bus.registerAgent(agent2->getAgentId(), agent2);
  agent1->setMessageBus(&bus);
  agent2->setMessageBus(&bus);

  for (auto _ : state) {
    auto msg1 = KeystoneMessage::create("agent1", "agent2", "ping");
    bus.routeMessage(msg1);

    auto msg2 = KeystoneMessage::create("agent2", "agent1", "pong");
    bus.routeMessage(msg2);
  }

  state.SetItemsProcessed(state.iterations() * 2);  // 2 messages per iteration
}
BENCHMARK(BM_MessageRoundTrip);

// Benchmark: Broadcast to multiple agents
static void BM_MessageBroadcast(benchmark::State& state) {
  int32_t num_agents = static_cast<int32_t>(state.range(0));

  MessageBus bus;
  std::vector<std::shared_ptr<TaskAgent>> agents;

  for (int32_t i = 0; i < num_agents; ++i) {
    auto agent = std::make_shared<TaskAgent>("agent-" + std::to_string(i));
    bus.registerAgent(agent->getAgentId(), agent);
    agent->setMessageBus(&bus);
    agents.push_back(agent);
  }

  for (auto _ : state) {
    // Broadcast: send message to all agents
    for (int32_t i = 0; i < num_agents; ++i) {
      auto msg = KeystoneMessage::create(
          "broadcaster", "agent-" + std::to_string(i), "broadcast");
      bus.routeMessage(msg);
    }
  }

  state.SetItemsProcessed(state.iterations() * num_agents);
  state.counters["agents"] = num_agents;
}
BENCHMARK(BM_MessageBroadcast)->Range(8, 256);

BENCHMARK_MAIN();
