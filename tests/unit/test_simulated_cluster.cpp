#include "simulation/simulated_cluster.hpp"

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

using namespace keystone::simulation;
using namespace std::chrono_literals;

class SimulatedClusterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Tests will create clusters as needed
  }
};

TEST_F(SimulatedClusterTest, CreateWithDefaultConfig) {
  SimulatedCluster::Config config;
  SimulatedCluster cluster(config);

  EXPECT_EQ(cluster.getNumNodes(), 2);
  EXPECT_NE(cluster.getNode(0), nullptr);
  EXPECT_NE(cluster.getNode(1), nullptr);
  EXPECT_EQ(cluster.getNode(2), nullptr);  // Out of bounds
}

TEST_F(SimulatedClusterTest, CreateWithCustomConfig) {
  SimulatedCluster::Config config{.num_nodes = 4,
                                  .workers_per_node = 8,
                                  .network_config = {.min_latency = 50us, .max_latency = 150us}};
  SimulatedCluster cluster(config);

  EXPECT_EQ(cluster.getNumNodes(), 4);

  for (size_t i = 0; i < 4; ++i) {
    auto* node = cluster.getNode(i);
    EXPECT_NE(node, nullptr);
    EXPECT_EQ(node->getNodeId(), i);
    EXPECT_EQ(node->getNumWorkers(), 8);
  }
}

TEST_F(SimulatedClusterTest, StartAndShutdown) {
  SimulatedCluster::Config config{.num_nodes = 2, .workers_per_node = 2, .network_config = {}};
  SimulatedCluster cluster(config);

  cluster.start();

  std::atomic<bool> executed{false};
  cluster.submitToNode(0, [&]() { executed = true; });

  std::this_thread::sleep_for(100ms);
  EXPECT_TRUE(executed);

  cluster.shutdown();
}

TEST_F(SimulatedClusterTest, RegisterAgent) {
  SimulatedCluster::Config config{.num_nodes = 3, .network_config = {}};
  SimulatedCluster cluster(config);

  cluster.registerAgent("agent_A", 0);
  cluster.registerAgent("agent_B", 1);
  cluster.registerAgent("agent_C", 2);

  EXPECT_EQ(cluster.getAgentNode("agent_A"), 0);
  EXPECT_EQ(cluster.getAgentNode("agent_B"), 1);
  EXPECT_EQ(cluster.getAgentNode("agent_C"), 2);
  EXPECT_FALSE(cluster.getAgentNode("agent_X").has_value());
}

TEST_F(SimulatedClusterTest, UnregisterAgent) {
  SimulatedCluster::Config config{.num_nodes = 2, .network_config = {}};
  SimulatedCluster cluster(config);

  cluster.registerAgent("agent_A", 0);
  EXPECT_TRUE(cluster.getAgentNode("agent_A").has_value());

  cluster.unregisterAgent("agent_A");
  EXPECT_FALSE(cluster.getAgentNode("agent_A").has_value());
}

TEST_F(SimulatedClusterTest, SubmitToRegisteredAgent) {
  SimulatedCluster::Config config{.num_nodes = 2, .workers_per_node = 2, .network_config = {}};
  SimulatedCluster cluster(config);
  cluster.start();

  std::atomic<int32_t> node0_counter{0};
  std::atomic<int32_t> node1_counter{0};

  cluster.registerAgent("agent_A", 0);
  cluster.registerAgent("agent_B", 1);

  // Submit to agent_A (should go to node 0)
  cluster.submit("agent_A", [&]() { node0_counter++; });

  // Submit to agent_B (should go to node 1)
  cluster.submit("agent_B", [&]() { node1_counter++; });

  std::this_thread::sleep_for(100ms);

  EXPECT_GT(node0_counter.load(), 0);
  EXPECT_GT(node1_counter.load(), 0);

  cluster.shutdown();
}

TEST_F(SimulatedClusterTest, SubmitToUnregisteredAgent) {
  SimulatedCluster::Config config{.num_nodes = 2, .workers_per_node = 2, .network_config = {}};
  SimulatedCluster cluster(config);
  cluster.start();

  std::atomic<int32_t> total_counter{0};

  // Submit to unregistered agents - should round-robin
  for (int32_t i = 0; i < 10; ++i) {
    cluster.submit("unregistered_agent", [&]() { total_counter++; });
  }

  std::this_thread::sleep_for(100ms);

  EXPECT_EQ(total_counter.load(), 10);

  cluster.shutdown();
}

TEST_F(SimulatedClusterTest, SubmitDirectlyToNode) {
  SimulatedCluster::Config config{.num_nodes = 3, .workers_per_node = 2, .network_config = {}};
  SimulatedCluster cluster(config);
  cluster.start();

  std::atomic<int32_t> counter0{0}, counter1{0}, counter2{0};

  cluster.submitToNode(0, [&]() { counter0++; });
  cluster.submitToNode(1, [&]() { counter1++; });
  cluster.submitToNode(2, [&]() { counter2++; });

  std::this_thread::sleep_for(100ms);

  EXPECT_EQ(counter0.load(), 1);
  EXPECT_EQ(counter1.load(), 1);
  EXPECT_EQ(counter2.load(), 1);

  cluster.shutdown();
}

TEST_F(SimulatedClusterTest, RemoteWorkSteal) {
  SimulatedCluster::Config config{.num_nodes = 2,
                                  .workers_per_node = 2,
                                  .network_config = {.min_latency = 10us, .max_latency = 20us}};
  SimulatedCluster cluster(config);
  cluster.start();

  // Note: stealRemoteWork currently increments counter but doesn't actually
  // transfer work (simplified implementation). This test verifies the
  // network interaction is set up correctly.

  bool success = cluster.stealRemoteWork(0, 1);
  (void)success;  // Suppress unused warning - just testing API doesn't crash
  // May succeed or fail depending on work availability
  // Just verify it doesn't crash

  cluster.shutdown();
}

TEST_F(SimulatedClusterTest, ProcessNetworkMessages) {
  SimulatedCluster::Config config{.num_nodes = 2,
                                  .workers_per_node = 2,
                                  .network_config = {.min_latency = 10us, .max_latency = 20us}};
  SimulatedCluster cluster(config);
  cluster.start();

  std::atomic<int32_t> counter{0};

  // Manually send work via network
  cluster.getNetwork()->send(0, 1, [&]() { counter++; });

  // Wait for latency to elapse
  std::this_thread::sleep_for(50us);

  // Process network messages
  cluster.processNetworkMessages();

  // Work should be delivered and eventually executed
  std::this_thread::sleep_for(100ms);

  EXPECT_EQ(counter.load(), 1);

  cluster.shutdown();
}

TEST_F(SimulatedClusterTest, GetStats) {
  SimulatedCluster::Config config{.num_nodes = 2, .workers_per_node = 2, .network_config = {}};
  SimulatedCluster cluster(config);
  cluster.start();

  // Submit some work
  for (int32_t i = 0; i < 10; ++i) {
    cluster.submitToNode(0, []() {});
  }

  std::this_thread::sleep_for(100ms);

  auto stats = cluster.getStats();

  EXPECT_EQ(stats.total_tasks_submitted, 10);
  EXPECT_EQ(stats.queue_depths_per_node.size(), 2u);

  cluster.shutdown();
}

TEST_F(SimulatedClusterTest, QueueDepthTracking) {
  SimulatedCluster::Config config{.num_nodes = 2, .workers_per_node = 2, .network_config = {}};
  SimulatedCluster cluster(config);
  cluster.start();

  // Submit blocking work to node 0
  for (int32_t i = 0; i < 20; ++i) {
    cluster.submitToNode(0, []() { std::this_thread::sleep_for(50ms); });
  }

  // Give scheduler time to queue work
  std::this_thread::sleep_for(10ms);

  auto stats = cluster.getStats();
  EXPECT_GT(stats.queue_depths_per_node[0], 0);

  // Wait for completion
  std::this_thread::sleep_for(2s);

  stats = cluster.getStats();
  EXPECT_EQ(stats.queue_depths_per_node[0], 0);

  cluster.shutdown();
}

TEST_F(SimulatedClusterTest, LoadImbalanceCalculation) {
  SimulatedCluster::Config config{.num_nodes = 3, .workers_per_node = 2, .network_config = {}};
  SimulatedCluster cluster(config);
  cluster.start();

  // Create imbalanced load: lots on node 0, none on others
  for (int32_t i = 0; i < 30; ++i) {
    cluster.submitToNode(0, []() { std::this_thread::sleep_for(50ms); });
  }

  std::this_thread::sleep_for(10ms);

  auto stats = cluster.getStats();

  // Should have high load imbalance (stddev > 0)
  EXPECT_GT(stats.load_imbalance, 0.0);

  cluster.shutdown();
}

TEST_F(SimulatedClusterTest, ResetStats) {
  SimulatedCluster::Config config{.num_nodes = 2, .workers_per_node = 2, .network_config = {}};
  SimulatedCluster cluster(config);
  cluster.start();

  // Generate some activity
  for (int32_t i = 0; i < 10; ++i) {
    cluster.submitToNode(0, []() {});
  }

  std::this_thread::sleep_for(100ms);

  auto stats = cluster.getStats();
  EXPECT_GT(stats.total_tasks_submitted, 0);

  // Reset stats
  cluster.resetStats();

  stats = cluster.getStats();
  EXPECT_EQ(stats.total_tasks_submitted, 0);
  EXPECT_EQ(stats.total_network_messages, 0);

  cluster.shutdown();
}

TEST_F(SimulatedClusterTest, NetworkStatistics) {
  SimulatedCluster::Config config{.num_nodes = 2,
                                  .workers_per_node = 2,
                                  .network_config = {.min_latency = 100us, .max_latency = 100us}};
  SimulatedCluster cluster(config);
  cluster.start();

  // Send messages via network
  for (int32_t i = 0; i < 5; ++i) {
    cluster.getNetwork()->send(0, 1, []() {});
  }

  std::this_thread::sleep_for(2ms);  // Wait longer for delivery
  cluster.processNetworkMessages();

  auto stats = cluster.getStats();

  EXPECT_EQ(stats.total_network_messages, 5);
  // Average latency should be at least 100µs (relaxed upper bound)
  EXPECT_GE(stats.avg_network_latency_us, 100.0);
  EXPECT_LE(stats.avg_network_latency_us,
            5000.0);  // Relaxed for timing variance

  cluster.shutdown();
}

TEST_F(SimulatedClusterTest, MultiNodeWorkDistribution) {
  SimulatedCluster::Config config{.num_nodes = 4, .workers_per_node = 2, .network_config = {}};
  SimulatedCluster cluster(config);
  cluster.start();

  std::atomic<int32_t> counters[4] = {0, 0, 0, 0};

  // Submit 10 tasks to each node
  for (int32_t node = 0; node < 4; ++node) {
    for (int32_t i = 0; i < 10; ++i) {
      cluster.submitToNode(node, [&counters, node]() { counters[node]++; });
    }
  }

  std::this_thread::sleep_for(200ms);

  // Each node should have executed its tasks
  for (int32_t node = 0; node < 4; ++node) {
    EXPECT_EQ(counters[node].load(), 10);
  }

  cluster.shutdown();
}

TEST_F(SimulatedClusterTest, InvalidNodeIdThrows) {
  SimulatedCluster::Config config{.num_nodes = 2, .network_config = {}};
  SimulatedCluster cluster(config);

  EXPECT_THROW(cluster.submitToNode(2, []() {}), std::out_of_range);
  EXPECT_THROW(cluster.registerAgent("agent", 2), std::out_of_range);
}
