#include "simulation/simulated_cluster.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include <benchmark/benchmark.h>

using namespace keystone::simulation;
using namespace std::chrono_literals;

// Benchmark: Local-only work stealing (single node baseline)
static void BM_WorkStealing_LocalOnly(benchmark::State& state) {
  const size_t num_tasks = state.range(0);

  for (auto _ : state) {
    SimulatedCluster::Config config{.num_nodes = 1, .workers_per_node = 4, .network_config = {}};
    SimulatedCluster cluster(config);
    cluster.start();

    std::atomic<size_t> completed{0};

    // Submit all tasks to single node
    for (size_t i = 0; i < num_tasks; ++i) {
      cluster.submitToNode(0, [&completed]() {
        // Simulate work
        volatile int32_t sum = 0;
        for (int32_t j = 0; j < 100; ++j) {
          sum += j;
        }
        completed++;
      });
    }

    // Wait for completion
    while (completed.load() < num_tasks) {
      std::this_thread::sleep_for(1ms);
    }

    cluster.shutdown();
  }

  state.SetItemsProcessed(state.iterations() * num_tasks);
}
BENCHMARK(BM_WorkStealing_LocalOnly)->Arg(100)->Arg(500)->Arg(1000);

// Benchmark: Two nodes with 100µs network latency
static void BM_WorkStealing_TwoNodes_100us(benchmark::State& state) {
  const size_t num_tasks = state.range(0);

  for (auto _ : state) {
    SimulatedCluster::Config config{.num_nodes = 2,
                                    .workers_per_node = 4,
                                    .network_config = {.min_latency = 100us,
                                                       .max_latency = 100us,
                                                       .packet_loss_rate = 0.0}};
    SimulatedCluster cluster(config);
    cluster.start();

    std::atomic<size_t> completed{0};

    // Submit tasks to both nodes (round-robin)
    for (size_t i = 0; i < num_tasks; ++i) {
      cluster.submitToNode(i % 2, [&completed]() {
        volatile int32_t sum = 0;
        for (int32_t j = 0; j < 100; ++j) {
          sum += j;
        }
        completed++;
      });
    }

    // Wait for completion
    while (completed.load() < num_tasks) {
      std::this_thread::sleep_for(1ms);
    }

    cluster.shutdown();
  }

  state.SetItemsProcessed(state.iterations() * num_tasks);
}
BENCHMARK(BM_WorkStealing_TwoNodes_100us)->Arg(100)->Arg(500)->Arg(1000);

// Benchmark: Two nodes with 500µs network latency
static void BM_WorkStealing_TwoNodes_500us(benchmark::State& state) {
  const size_t num_tasks = state.range(0);

  for (auto _ : state) {
    SimulatedCluster::Config config{.num_nodes = 2,
                                    .workers_per_node = 4,
                                    .network_config = {.min_latency = 500us,
                                                       .max_latency = 500us,
                                                       .packet_loss_rate = 0.0}};
    SimulatedCluster cluster(config);
    cluster.start();

    std::atomic<size_t> completed{0};

    for (size_t i = 0; i < num_tasks; ++i) {
      cluster.submitToNode(i % 2, [&completed]() {
        volatile int32_t sum = 0;
        for (int32_t j = 0; j < 100; ++j) {
          sum += j;
        }
        completed++;
      });
    }

    while (completed.load() < num_tasks) {
      std::this_thread::sleep_for(1ms);
    }

    cluster.shutdown();
  }

  state.SetItemsProcessed(state.iterations() * num_tasks);
}
BENCHMARK(BM_WorkStealing_TwoNodes_500us)->Arg(100)->Arg(500)->Arg(1000);

// Benchmark: Two nodes with 1ms network latency
static void BM_WorkStealing_TwoNodes_1ms(benchmark::State& state) {
  const size_t num_tasks = state.range(0);

  for (auto _ : state) {
    SimulatedCluster::Config config{.num_nodes = 2,
                                    .workers_per_node = 4,
                                    .network_config = {.min_latency = 1ms,
                                                       .max_latency = 1ms,
                                                       .packet_loss_rate = 0.0}};
    SimulatedCluster cluster(config);
    cluster.start();

    std::atomic<size_t> completed{0};

    for (size_t i = 0; i < num_tasks; ++i) {
      cluster.submitToNode(i % 2, [&completed]() {
        volatile int32_t sum = 0;
        for (int32_t j = 0; j < 100; ++j) {
          sum += j;
        }
        completed++;
      });
    }

    while (completed.load() < num_tasks) {
      std::this_thread::sleep_for(1ms);
    }

    cluster.shutdown();
  }

  state.SetItemsProcessed(state.iterations() * num_tasks);
}
BENCHMARK(BM_WorkStealing_TwoNodes_1ms)->Arg(100)->Arg(500)->Arg(1000);

// Benchmark: Load balancing effectiveness (imbalanced submission)
static void BM_LoadBalancing_Imbalanced(benchmark::State& state) {
  const size_t num_tasks = state.range(0);

  for (auto _ : state) {
    SimulatedCluster::Config config{.num_nodes = 4,
                                    .workers_per_node = 2,
                                    .network_config = {.min_latency = 100us, .max_latency = 200us}};
    SimulatedCluster cluster(config);
    cluster.start();

    std::atomic<size_t> completed{0};

    // Submit all tasks to node 0 (creates imbalance)
    for (size_t i = 0; i < num_tasks; ++i) {
      cluster.submitToNode(0, [&completed]() {
        volatile int32_t sum = 0;
        for (int32_t j = 0; j < 100; ++j) {
          sum += j;
        }
        completed++;
      });
    }

    // Periodically process network messages to allow work stealing
    auto start = std::chrono::steady_clock::now();
    while (completed.load() < num_tasks) {
      cluster.processNetworkMessages();
      std::this_thread::sleep_for(1ms);

      // Timeout after 10 seconds
      auto elapsed = std::chrono::steady_clock::now() - start;
      if (elapsed > 10s) {
        break;
      }
    }

    auto stats = cluster.getStats();
    state.counters["LoadImbalance"] = stats.load_imbalance;
    state.counters["NetworkMessages"] = static_cast<double>(stats.total_network_messages);

    cluster.shutdown();
  }

  state.SetItemsProcessed(state.iterations() * num_tasks);
}
BENCHMARK(BM_LoadBalancing_Imbalanced)->Arg(100)->Arg(500)->Arg(1000);

// Benchmark: Network overhead measurement (message-only)
static void BM_NetworkOverhead_MessageOnly(benchmark::State& state) {
  const size_t num_messages = state.range(0);

  for (auto _ : state) {
    SimulatedCluster::Config config{.num_nodes = 2,
                                    .workers_per_node = 2,
                                    .network_config = {.min_latency = 100us, .max_latency = 100us}};
    SimulatedCluster cluster(config);
    cluster.start();

    std::atomic<size_t> received{0};

    // Send messages via network
    for (size_t i = 0; i < num_messages; ++i) {
      cluster.getNetwork()->send(0, 1, [&received]() { received++; });
    }

    // Process and wait for delivery
    auto start = std::chrono::steady_clock::now();
    while (received.load() < num_messages) {
      cluster.processNetworkMessages();
      std::this_thread::sleep_for(100us);

      // Timeout after 5 seconds
      auto elapsed = std::chrono::steady_clock::now() - start;
      if (elapsed > 5s) {
        break;
      }
    }

    auto stats = cluster.getStats();
    state.counters["AvgLatencyUs"] = stats.avg_network_latency_us;
    state.counters["MessagesDelivered"] = static_cast<double>(received.load());

    cluster.shutdown();
  }

  state.SetItemsProcessed(state.iterations() * num_messages);
}
BENCHMARK(BM_NetworkOverhead_MessageOnly)->Arg(50)->Arg(100)->Arg(200);

// Benchmark: Agent affinity (registered agents)
static void BM_AgentAffinity_Registered(benchmark::State& state) {
  const size_t num_tasks = state.range(0);

  for (auto _ : state) {
    SimulatedCluster::Config config{.num_nodes = 4, .workers_per_node = 4, .network_config = {}};
    SimulatedCluster cluster(config);
    cluster.start();

    // Register agents on different nodes
    cluster.registerAgent("agent_A", 0);
    cluster.registerAgent("agent_B", 1);
    cluster.registerAgent("agent_C", 2);
    cluster.registerAgent("agent_D", 3);

    std::atomic<size_t> completed{0};

    // Submit tasks to agents (should route to home nodes)
    std::vector<std::string> agents = {"agent_A", "agent_B", "agent_C", "agent_D"};
    for (size_t i = 0; i < num_tasks; ++i) {
      cluster.submit(agents[i % 4], [&completed]() {
        volatile int32_t sum = 0;
        for (int32_t j = 0; j < 100; ++j) {
          sum += j;
        }
        completed++;
      });
    }

    while (completed.load() < num_tasks) {
      std::this_thread::sleep_for(1ms);
    }

    cluster.shutdown();
  }

  state.SetItemsProcessed(state.iterations() * num_tasks);
}
BENCHMARK(BM_AgentAffinity_Registered)->Arg(100)->Arg(500)->Arg(1000);

// Benchmark: Packet loss impact
static void BM_PacketLoss_Impact(benchmark::State& state) {
  const double packet_loss = static_cast<double>(state.range(0)) / 100.0;
  const size_t num_messages = 100;

  for (auto _ : state) {
    SimulatedCluster::Config config{.num_nodes = 2,
                                    .workers_per_node = 2,
                                    .network_config = {.min_latency = 100us,
                                                       .max_latency = 100us,
                                                       .packet_loss_rate = packet_loss}};
    SimulatedCluster cluster(config);
    cluster.start();

    std::atomic<size_t> received{0};

    for (size_t i = 0; i < num_messages; ++i) {
      cluster.getNetwork()->send(0, 1, [&received]() { received++; });
    }

    // Wait for delivery (with timeout)
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < 2s) {
      cluster.processNetworkMessages();
      std::this_thread::sleep_for(100us);
    }

    state.counters["PacketLoss%"] = packet_loss * 100.0;
    state.counters["Delivered"] = static_cast<double>(received.load());
    state.counters["DeliveryRate%"] = (static_cast<double>(received.load()) / num_messages) * 100.0;

    cluster.shutdown();
  }

  state.SetItemsProcessed(state.iterations() * num_messages);
}
BENCHMARK(BM_PacketLoss_Impact)->Arg(0)->Arg(10)->Arg(25)->Arg(50);

BENCHMARK_MAIN();
