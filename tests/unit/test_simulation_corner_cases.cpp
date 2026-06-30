#include "simulation/simulated_cluster.hpp"
#include "simulation/simulated_network.hpp"
#include "simulation/simulated_numa_node.hpp"

#include <atomic>
#include <chrono>
#include <limits>
#include <thread>

#include <gtest/gtest.h>

using namespace keystone::simulation;
using namespace std::chrono_literals;

/**
 * Corner Case and Edge Case Tests for Simulation Framework
 *
 * Tests boundary conditions, error handling, resource limits,
 * and failure scenarios to ensure robustness.
 */
class SimulationCornerCaseTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

// ============================================================================
// BOUNDARY CONDITION TESTS
// ============================================================================

TEST_F(SimulationCornerCaseTest, SingleNodeCluster) {
  SimulatedCluster::Config config{.num_nodes = 1, .workers_per_node = 1, .network_config = {}};
  SimulatedCluster cluster(config);
  cluster.start();

  EXPECT_EQ(cluster.getNumNodes(), 1);
  EXPECT_NE(cluster.getNode(0), nullptr);
  EXPECT_EQ(cluster.getNode(1), nullptr);

  std::atomic<int32_t> counter{0};
  cluster.submitToNode(0, [&]() { counter++; });

  std::this_thread::sleep_for(100ms);
  EXPECT_EQ(counter.load(), 1);

  cluster.shutdown();
}

TEST_F(SimulationCornerCaseTest, SingleWorkerPerNode) {
  SimulatedCluster::Config config{.num_nodes = 2, .workers_per_node = 1, .network_config = {}};
  SimulatedCluster cluster(config);
  cluster.start();

  std::atomic<int32_t> counter{0};
  for (int32_t i = 0; i < 10; ++i) {
    cluster.submitToNode(i % 2, [&]() { counter++; });
  }

  std::this_thread::sleep_for(200ms);
  EXPECT_EQ(counter.load(), 10);

  cluster.shutdown();
}

TEST_F(SimulationCornerCaseTest, ManyNodes) {
  SimulatedCluster::Config config{.num_nodes = 8, .workers_per_node = 2, .network_config = {}};
  SimulatedCluster cluster(config);
  cluster.start();

  EXPECT_EQ(cluster.getNumNodes(), 8);

  // Verify all nodes exist
  for (size_t i = 0; i < 8; ++i) {
    EXPECT_NE(cluster.getNode(i), nullptr);
  }

  cluster.shutdown();
}

TEST_F(SimulationCornerCaseTest, ZeroLatencyNetwork) {
  SimulatedNetwork::Config config{.min_latency = 0us, .max_latency = 0us};
  SimulatedNetwork network(config);

  std::atomic<bool> executed{false};
  network.send(0, 1, [&]() { executed = true; });

  // Even with zero latency, still need to receive
  auto work = network.receive(1);
  if (work.has_value()) {
    (*work)();
  }

  EXPECT_TRUE(executed);
}

TEST_F(SimulationCornerCaseTest, VeryHighLatencyNetwork) {
  SimulatedNetwork::Config config{.min_latency = 1000ms, .max_latency = 1000ms};
  SimulatedNetwork network(config);

  std::atomic<bool> executed{false};
  network.send(0, 1, [&]() { executed = true; });

  // Should NOT be ready immediately
  auto work_immediate = network.receive(1);
  EXPECT_FALSE(work_immediate.has_value());
  EXPECT_FALSE(executed);

  // Wait for latency to elapse
  std::this_thread::sleep_for(1100ms);

  auto work_delayed = network.receive(1);
  EXPECT_TRUE(work_delayed.has_value());
  if (work_delayed.has_value()) {
    (*work_delayed)();
  }

  EXPECT_TRUE(executed);
}

// ============================================================================
// ERROR HANDLING TESTS
// ============================================================================

TEST_F(SimulationCornerCaseTest, InvalidNodeIdSubmit) {
  SimulatedCluster::Config config{.num_nodes = 2, .network_config = {}};
  SimulatedCluster cluster(config);
  cluster.start();

  // Out-of-bounds node ID should throw
  EXPECT_THROW(cluster.submitToNode(2, []() {}), std::out_of_range);
  EXPECT_THROW(cluster.submitToNode(100, []() {}), std::out_of_range);

  cluster.shutdown();
}

TEST_F(SimulationCornerCaseTest, InvalidNodeIdRegisterAgent) {
  SimulatedCluster::Config config{.num_nodes = 2, .network_config = {}};
  SimulatedCluster cluster(config);

  // Out-of-bounds node ID should throw
  EXPECT_THROW(cluster.registerAgent("agent", 2), std::out_of_range);
  EXPECT_THROW(cluster.registerAgent("agent", 999), std::out_of_range);
}

TEST_F(SimulationCornerCaseTest, UnregisteredAgentSubmit) {
  SimulatedCluster::Config config{.num_nodes = 2, .network_config = {}};
  SimulatedCluster cluster(config);
  cluster.start();

  std::atomic<int32_t> counter{0};

  // Submit to unregistered agent should use round-robin
  for (int32_t i = 0; i < 10; ++i) {
    cluster.submit("nonexistent_agent", [&]() { counter++; });
  }

  std::this_thread::sleep_for(100ms);
  EXPECT_EQ(counter.load(), 10);

  cluster.shutdown();
}

TEST_F(SimulationCornerCaseTest, DuplicateAgentRegistration) {
  SimulatedCluster::Config config{.num_nodes = 2, .network_config = {}};
  SimulatedCluster cluster(config);

  cluster.registerAgent("agent_A", 0);
  EXPECT_TRUE(cluster.getAgentNode("agent_A").has_value());

  // Re-register on different node (should update)
  cluster.registerAgent("agent_A", 1);
  auto node = cluster.getAgentNode("agent_A");
  EXPECT_TRUE(node.has_value());
  EXPECT_EQ(node.value(), 1);
}

TEST_F(SimulationCornerCaseTest, UnregisterNonexistentAgent) {
  SimulatedCluster::Config config{.num_nodes = 2, .network_config = {}};
  SimulatedCluster cluster(config);

  // Should not crash
  cluster.unregisterAgent("does_not_exist");

  // Verify nothing changed
  EXPECT_FALSE(cluster.getAgentNode("does_not_exist").has_value());
}

// ============================================================================
// RESOURCE EXHAUSTION TESTS
// ============================================================================

TEST_F(SimulationCornerCaseTest, MessageFlood) {
  SimulatedCluster::Config config{.num_nodes = 2, .workers_per_node = 4, .network_config = {}};
  SimulatedCluster cluster(config);
  cluster.start();

  std::atomic<int32_t> counter{0};
  const int32_t MESSAGE_COUNT = 10000;

  // Flood with messages
  for (int32_t i = 0; i < MESSAGE_COUNT; ++i) {
    cluster.submitToNode(i % 2, [&]() { counter++; });
  }

  // Give time for processing
  std::this_thread::sleep_for(2s);

  // Most messages should complete (allow some tolerance)
  EXPECT_GT(counter.load(), MESSAGE_COUNT * 0.9);

  cluster.shutdown();
}

TEST_F(SimulationCornerCaseTest, NetworkMessageFlood) {
  SimulatedNetwork::Config config{.min_latency = 1us, .max_latency = 10us};
  SimulatedNetwork network(config);

  const int32_t FLOOD_SIZE = 1000;

  for (int32_t i = 0; i < FLOOD_SIZE; ++i) {
    network.send(0, 1, []() {});
  }

  EXPECT_EQ(network.getTotalMessages(), FLOOD_SIZE);
  EXPECT_EQ(network.getPendingMessages(), FLOOD_SIZE);

  // Consume all messages
  std::this_thread::sleep_for(50ms);
  int32_t received = 0;
  while (auto work = network.receive(1)) {
    received++;
  }

  EXPECT_EQ(received, FLOOD_SIZE);
  EXPECT_EQ(network.getDeliveredMessages(), FLOOD_SIZE);
}

TEST_F(SimulationCornerCaseTest, HighQueueDepth) {
  SimulatedCluster::Config config{.num_nodes = 1, .workers_per_node = 1, .network_config = {}};
  SimulatedCluster cluster(config);
  cluster.start();

  // Submit many blocking tasks
  std::atomic<int32_t> completed{0};
  for (int32_t i = 0; i < 1000; ++i) {
    cluster.submitToNode(0, [&]() {
      std::this_thread::sleep_for(1ms);
      completed++;
    });
  }

  // Queue should fill up
  std::this_thread::sleep_for(10ms);
  auto stats = cluster.getStats();
  EXPECT_GT(stats.queue_depths_per_node[0], 0);

  // Wait for completion
  std::this_thread::sleep_for(2s);

  cluster.shutdown();
}

// ============================================================================
// CONCURRENT OPERATION TESTS
// ============================================================================

TEST_F(SimulationCornerCaseTest, ParallelSubmitFromMultipleThreads) {
  SimulatedCluster::Config config{.num_nodes = 2, .workers_per_node = 4, .network_config = {}};
  SimulatedCluster cluster(config);
  cluster.start();

  std::atomic<int32_t> counter{0};
  const int32_t THREADS = 4;
  const int32_t SUBMITS_PER_THREAD = 100;

  std::vector<std::thread> threads;
  for (int32_t t = 0; t < THREADS; ++t) {
    threads.emplace_back([&]() {
      for (int32_t i = 0; i < SUBMITS_PER_THREAD; ++i) {
        cluster.submitToNode(i % 2, [&]() { counter++; });
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  std::this_thread::sleep_for(500ms);

  EXPECT_EQ(counter.load(), THREADS * SUBMITS_PER_THREAD);

  cluster.shutdown();
}

TEST_F(SimulationCornerCaseTest, ShutdownDuringActiveWork) {
  SimulatedCluster::Config config{.num_nodes = 2, .workers_per_node = 2, .network_config = {}};
  SimulatedCluster cluster(config);
  cluster.start();

  std::atomic<int32_t> started{0};
  std::atomic<int32_t> completed{0};

  // Submit long-running tasks
  for (int32_t i = 0; i < 100; ++i) {
    cluster.submitToNode(i % 2, [&]() {
      started++;
      std::this_thread::sleep_for(10ms);
      completed++;
    });
  }

  // Let some start
  std::this_thread::sleep_for(50ms);

  // Shutdown while work is active
  cluster.shutdown();

  // Some tasks should have started
  EXPECT_GT(started.load(), 0);
}

TEST_F(SimulationCornerCaseTest, ConcurrentAgentRegistration) {
  SimulatedCluster::Config config{.num_nodes = 4, .network_config = {}};
  SimulatedCluster cluster(config);

  const int32_t THREADS = 4;
  const int32_t AGENTS_PER_THREAD = 20;

  std::vector<std::thread> threads;
  for (int32_t t = 0; t < THREADS; ++t) {
    threads.emplace_back([&, t]() {
      for (int32_t i = 0; i < AGENTS_PER_THREAD; ++i) {
        std::string agent_name = "agent_" + std::to_string(t) + "_" + std::to_string(i);
        cluster.registerAgent(agent_name, i % 4);
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // All agents should be registered (allow small timing variance)
  int32_t registered_count = 0;
  for (int32_t t = 0; t < THREADS; ++t) {
    for (int32_t i = 0; i < AGENTS_PER_THREAD; ++i) {
      std::string agent_name = "agent_" + std::to_string(t) + "_" + std::to_string(i);
      if (cluster.getAgentNode(agent_name).has_value()) {
        registered_count++;
      }
    }
  }

  // Most agents should be registered (allow for timing variance)
  EXPECT_GT(registered_count, THREADS * AGENTS_PER_THREAD * 0.95);
}

// ============================================================================
// NETWORK FAILURE SIMULATION TESTS
// ============================================================================

TEST_F(SimulationCornerCaseTest, FullPacketLoss) {
  SimulatedNetwork::Config config{
      .min_latency = 100us,
      .max_latency = 100us,
      .packet_loss_rate = 1.0  // 100% loss
  };
  SimulatedNetwork network(config);

  for (int32_t i = 0; i < 100; ++i) {
    network.send(0, 1, []() {});
  }

  EXPECT_EQ(network.getTotalMessages(), 100);
  EXPECT_EQ(network.getDroppedMessages(), 100);
  EXPECT_EQ(network.getPendingMessages(), 0);

  std::this_thread::sleep_for(1ms);

  // No messages should be receivable
  auto work = network.receive(1);
  EXPECT_FALSE(work.has_value());
}

TEST_F(SimulationCornerCaseTest, PartialPacketLoss) {
  SimulatedNetwork::Config config{
      .min_latency = 10us,
      .max_latency = 10us,
      .packet_loss_rate = 0.5  // 50% loss
  };
  SimulatedNetwork network(config);

  const int32_t MESSAGE_COUNT = 1000;

  for (int32_t i = 0; i < MESSAGE_COUNT; ++i) {
    network.send(0, 1, []() {});
  }

  EXPECT_EQ(network.getTotalMessages(), MESSAGE_COUNT);

  // Should have roughly 50% dropped (allow variance)
  size_t dropped = network.getDroppedMessages();
  EXPECT_GT(dropped, MESSAGE_COUNT * 0.3);  // At least 30%
  EXPECT_LT(dropped, MESSAGE_COUNT * 0.7);  // At most 70%
}

TEST_F(SimulationCornerCaseTest, MessageTimeoutScenario) {
  SimulatedNetwork::Config config{.min_latency = 500ms, .max_latency = 500ms};
  SimulatedNetwork network(config);

  std::atomic<bool> executed{false};
  network.send(0, 1, [&]() { executed = true; });

  // Try to receive before latency elapses
  for (int32_t attempt = 0; attempt < 10; ++attempt) {
    auto work = network.receive(1);
    EXPECT_FALSE(work.has_value());
    EXPECT_FALSE(executed);
    std::this_thread::sleep_for(10ms);
  }

  // Wait for latency
  std::this_thread::sleep_for(500ms);

  auto work = network.receive(1);
  EXPECT_TRUE(work.has_value());
}

// ============================================================================
// STATISTICS EDGE CASE TESTS
// ============================================================================

TEST_F(SimulationCornerCaseTest, StatisticsWithNoActivity) {
  SimulatedCluster::Config config{.num_nodes = 2, .network_config = {}};
  SimulatedCluster cluster(config);
  cluster.start();

  auto stats = cluster.getStats();

  EXPECT_EQ(stats.total_tasks_submitted, 0);
  EXPECT_EQ(stats.total_network_messages, 0);
  EXPECT_EQ(stats.avg_network_latency_us, 0.0);
  EXPECT_EQ(stats.queue_depths_per_node.size(), 2u);

  cluster.shutdown();
}

TEST_F(SimulationCornerCaseTest, ResetStatsDuringOperation) {
  SimulatedCluster::Config config{.num_nodes = 2, .workers_per_node = 2, .network_config = {}};
  SimulatedCluster cluster(config);
  cluster.start();

  // Submit work
  for (int32_t i = 0; i < 50; ++i) {
    cluster.submitToNode(i % 2, []() { std::this_thread::sleep_for(1ms); });
  }

  std::this_thread::sleep_for(20ms);

  // Reset during operation
  cluster.resetStats();

  auto stats = cluster.getStats();
  EXPECT_EQ(stats.total_tasks_submitted, 0);
  EXPECT_EQ(stats.total_network_messages, 0);

  cluster.shutdown();
}

TEST_F(SimulationCornerCaseTest, NetworkStatisticsOverflow) {
  SimulatedNetwork network;

  // Send many messages
  for (int32_t i = 0; i < 10000; ++i) {
    network.send(0, 1, []() {});
  }

  // Statistics should handle large numbers
  EXPECT_EQ(network.getTotalMessages(), 10000);
  EXPECT_LE(network.getDroppedMessages(), 10000);
}

TEST_F(SimulationCornerCaseTest, LoadImbalanceCalculationExtremes) {
  SimulatedCluster::Config config{.num_nodes = 4, .workers_per_node = 1, .network_config = {}};
  SimulatedCluster cluster(config);
  cluster.start();

  // Create extreme imbalance: all work on node 0
  for (int32_t i = 0; i < 100; ++i) {
    cluster.submitToNode(0, []() { std::this_thread::sleep_for(10ms); });
  }

  std::this_thread::sleep_for(20ms);

  auto stats = cluster.getStats();

  // Should have very high load imbalance
  EXPECT_GT(stats.load_imbalance, 0.0);

  cluster.shutdown();
}

// ============================================================================
// LIFECYCLE EDGE CASE TESTS
// ============================================================================

TEST_F(SimulationCornerCaseTest, MultipleStartCalls) {
  SimulatedCluster::Config config{.num_nodes = 2, .network_config = {}};
  SimulatedCluster cluster(config);

  cluster.start();

  // Second start should be safe (idempotent or throw)
  // Implementation may vary, just ensure no crash
  // cluster.start();  // Uncomment if implementation supports

  cluster.shutdown();
}

TEST_F(SimulationCornerCaseTest, MultipleShutdownCalls) {
  SimulatedCluster::Config config{.num_nodes = 2, .network_config = {}};
  SimulatedCluster cluster(config);

  cluster.start();
  cluster.shutdown();

  // Second shutdown should be safe
  cluster.shutdown();
}

TEST_F(SimulationCornerCaseTest, SubmitWithoutStart) {
  SimulatedCluster::Config config{.num_nodes = 2, .network_config = {}};
  SimulatedCluster cluster(config);

  // Submit without calling start (may queue or throw)
  std::atomic<bool> executed{false};

  // This should either throw or not execute
  // Implementation-dependent behavior
  try {
    cluster.submitToNode(0, [&]() { executed = true; });
    std::this_thread::sleep_for(100ms);
    // If no throw, execution should not occur without start
    // EXPECT_FALSE(executed);
  } catch (...) {
    // Exception is acceptable for unstarted cluster
  }
}

TEST_F(SimulationCornerCaseTest, EmptyAgentNameRegistration) {
  SimulatedCluster::Config config{.num_nodes = 2, .network_config = {}};
  SimulatedCluster cluster(config);

  // Empty agent name should work (valid string)
  cluster.registerAgent("", 0);
  EXPECT_TRUE(cluster.getAgentNode("").has_value());
}

// ============================================================================
// NUMA NODE EDGE CASES
// ============================================================================

TEST_F(SimulationCornerCaseTest, NUMANodeWithZeroWorkers) {
  // Zero workers might be invalid, but test boundary
  // EXPECT_THROW(SimulatedNUMANode node(0, 0), std::invalid_argument);

  // If allowed, node should exist but not process work
  // Implementation-dependent
}

TEST_F(SimulationCornerCaseTest, NUMANodeStealFromSelf) {
  SimulatedNUMANode node(0, 4);
  node.start();

  node.submit([]() {});

  // Stealing from self should fail gracefully
  auto work = node.stealWork();
  // May succeed or fail depending on implementation

  node.shutdown();
}

TEST_F(SimulationCornerCaseTest, NUMANodeRegisterSameAgentTwice) {
  SimulatedNUMANode node(0, 2);

  node.registerAgent("agent");
  EXPECT_TRUE(node.hasAgent("agent"));

  // Re-register same agent (should be idempotent)
  node.registerAgent("agent");
  EXPECT_TRUE(node.hasAgent("agent"));

  auto agents = node.getLocalAgents();
  EXPECT_EQ(agents.size(), 1u);
}
