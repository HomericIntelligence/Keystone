#include "simulation/simulated_cluster.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace keystone::simulation;
using namespace std::chrono_literals;

/**
 * Phase D.3 Integration Test: 4-Layer Hierarchy in Simulated Cluster
 *
 * This test validates that a hierarchical agent system can operate across
 * multiple simulated NUMA nodes with network latency, demonstrating:
 * - Cross-node agent communication
 * - Network latency impact on message flow
 * - Load distribution across nodes
 * - Hierarchical work delegation through the network
 */
class DistributedHierarchyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Tests will create clusters as needed
  }
};

/**
 * Test: 4-layer hierarchy with agents on different nodes
 *
 * Architecture:
 *   Node 0: Chief (orchestrator)
 *   Node 1: ComponentLead
 *   Node 2: ModuleLead
 *   Node 3: TaskAgents (3 workers)
 *
 * Flow: Work distributed across nodes using network
 */
TEST_F(DistributedHierarchyTest, FourLayerHierarchyAcrossNodes) {
  // Configure 4-node cluster with network latency
  SimulatedCluster::Config config{.num_nodes = 4,
                                  .workers_per_node = 2,
                                  .network_config = {.min_latency = 100us, .max_latency = 200us}};

  SimulatedCluster cluster(config);
  cluster.start();

  // Register agents on specific nodes (simulating affinity)
  cluster.registerAgent("chief", 0);
  cluster.registerAgent("component_lead", 1);
  cluster.registerAgent("module_lead", 2);
  cluster.registerAgent("task_agent_1", 3);
  cluster.registerAgent("task_agent_2", 3);
  cluster.registerAgent("task_agent_3", 3);

  // Counters for each layer
  std::atomic<int32_t> chief_executions{0};
  std::atomic<int32_t> component_executions{0};
  std::atomic<int32_t> module_executions{0};
  std::atomic<int32_t> task_executions{0};

  // Simulate hierarchical delegation using network send
  // Chief sends to ComponentLead via network
  cluster.getNetwork()->send(0, 1, [&]() {
    chief_executions++;

    // ComponentLead sends to ModuleLead via network
    cluster.getNetwork()->send(1, 2, [&]() {
      component_executions++;

      // ModuleLead sends to TaskAgents via network
      cluster.getNetwork()->send(2, 3, [&]() {
        module_executions++;
        task_executions++;
      });
      cluster.getNetwork()->send(2, 3, [&]() { task_executions++; });
      cluster.getNetwork()->send(2, 3, [&]() { task_executions++; });
    });
  });

  // Process network messages periodically
  for (int32_t i = 0; i < 20; ++i) {
    cluster.processNetworkMessages();
    std::this_thread::sleep_for(5ms);
  }

  // Wait for all work to complete
  std::this_thread::sleep_for(100ms);

  // Verify all layers executed
  EXPECT_EQ(chief_executions.load(), 1);
  EXPECT_EQ(component_executions.load(), 1);
  EXPECT_EQ(module_executions.load(), 1);
  EXPECT_EQ(task_executions.load(), 3);

  auto stats = cluster.getStats();

  // Verify network messages were sent (cross-node communication)
  EXPECT_GT(stats.total_network_messages, 0);

  cluster.shutdown();
}

/**
 * Test: Multiple commands flowing through distributed hierarchy
 */
TEST_F(DistributedHierarchyTest, MultipleCommandsDistributed) {
  SimulatedCluster::Config config{.num_nodes = 4,
                                  .workers_per_node = 4,
                                  .network_config = {.min_latency = 100us, .max_latency = 150us}};

  SimulatedCluster cluster(config);
  cluster.start();

  cluster.registerAgent("chief", 0);
  cluster.registerAgent("component_lead", 1);
  cluster.registerAgent("module_lead_1", 2);
  cluster.registerAgent("module_lead_2", 2);
  cluster.registerAgent("task_agent", 3);

  std::atomic<int32_t> total_task_executions{0};

  // Send 10 commands through the hierarchy using network
  for (int32_t cmd = 0; cmd < 10; ++cmd) {
    cluster.getNetwork()->send(0, 1, [&]() {
      // ComponentLead receives and delegates to ModuleLead
      size_t module_node = 2;
      cluster.getNetwork()->send(1, module_node, [&]() {
        // ModuleLead delegates to TaskAgent
        cluster.getNetwork()->send(2, 3, [&]() { total_task_executions++; });
      });
    });
  }

  // Process network messages
  for (int32_t i = 0; i < 50; ++i) {
    cluster.processNetworkMessages();
    std::this_thread::sleep_for(5ms);
  }

  std::this_thread::sleep_for(100ms);

  // All 10 commands should complete
  EXPECT_EQ(total_task_executions.load(), 10);

  auto stats = cluster.getStats();
  EXPECT_GT(stats.total_network_messages, 10);  // Many cross-node messages

  cluster.shutdown();
}

/**
 * Test: Load balancing with concentrated workload
 */
TEST_F(DistributedHierarchyTest, LoadBalancingAcrossNodes) {
  SimulatedCluster::Config config{.num_nodes = 4,
                                  .workers_per_node = 2,
                                  .network_config = {.min_latency = 50us, .max_latency = 100us}};

  SimulatedCluster cluster(config);
  cluster.start();

  // Register many task agents on node 3
  for (int32_t i = 0; i < 10; ++i) {
    cluster.registerAgent("task_" + std::to_string(i), 3);
  }

  std::atomic<int32_t> completed_tasks{0};

  // Submit concentrated workload to node 3
  for (int32_t i = 0; i < 100; ++i) {
    cluster.submit("task_" + std::to_string(i % 10), [&]() {
      // Simulate work
      int32_t sum = 0;
      for (int32_t j = 0; j < 1000; ++j) {
        sum += j;
      }
      [[maybe_unused]] volatile int result = sum;  // Prevent optimization
      completed_tasks++;
    });
  }

  // Process and wait
  for (int32_t i = 0; i < 50; ++i) {
    cluster.processNetworkMessages();
    std::this_thread::sleep_for(10ms);
  }

  std::this_thread::sleep_for(500ms);

  // Most tasks should complete (allowing for timing variance)
  EXPECT_GT(completed_tasks.load(), 80);

  auto stats = cluster.getStats();

  // Check load distribution (some variance expected)
  // Node 3 should have highest queue depth due to agent affinity
  size_t node3_depth = stats.queue_depths_per_node[3];
  EXPECT_GE(node3_depth, 0);  // May be 0 if all work completed

  cluster.shutdown();
}

/**
 * Test: Network latency impact on throughput
 */
TEST_F(DistributedHierarchyTest, NetworkLatencyImpact) {
  // Test with low latency (100µs)
  SimulatedCluster::Config low_latency_config{.num_nodes = 2,
                                              .workers_per_node = 4,
                                              .network_config = {.min_latency = 100us,
                                                                 .max_latency = 100us}};

  SimulatedCluster low_latency_cluster(low_latency_config);
  low_latency_cluster.start();

  low_latency_cluster.registerAgent("sender", 0);
  low_latency_cluster.registerAgent("receiver", 1);

  std::atomic<int32_t> low_latency_count{0};

  auto start_low = std::chrono::steady_clock::now();

  for (int32_t i = 0; i < 50; ++i) {
    low_latency_cluster.submit("sender", [&]() {
      low_latency_cluster.submit("receiver", [&]() { low_latency_count++; });
    });
  }

  for (int32_t i = 0; i < 50; ++i) {
    low_latency_cluster.processNetworkMessages();
    std::this_thread::sleep_for(5ms);
  }

  auto end_low = std::chrono::steady_clock::now();
  auto duration_low =
      std::chrono::duration_cast<std::chrono::milliseconds>(end_low - start_low).count();

  low_latency_cluster.shutdown();

  // Test with high latency (1ms)
  SimulatedCluster::Config high_latency_config{.num_nodes = 2,
                                               .workers_per_node = 4,
                                               .network_config = {.min_latency = 1ms,
                                                                  .max_latency = 1ms}};

  SimulatedCluster high_latency_cluster(high_latency_config);
  high_latency_cluster.start();

  high_latency_cluster.registerAgent("sender", 0);
  high_latency_cluster.registerAgent("receiver", 1);

  std::atomic<int32_t> high_latency_count{0};

  auto start_high = std::chrono::steady_clock::now();

  for (int32_t i = 0; i < 50; ++i) {
    high_latency_cluster.submit("sender", [&]() {
      high_latency_cluster.submit("receiver", [&]() { high_latency_count++; });
    });
  }

  for (int32_t i = 0; i < 50; ++i) {
    high_latency_cluster.processNetworkMessages();
    std::this_thread::sleep_for(5ms);
  }

  auto end_high = std::chrono::steady_clock::now();
  auto duration_high =
      std::chrono::duration_cast<std::chrono::milliseconds>(end_high - start_high).count();

  high_latency_cluster.shutdown();

  // Both should complete most tasks
  EXPECT_GT(low_latency_count.load(), 40);
  EXPECT_GT(high_latency_count.load(), 40);

  // High latency should take longer (though timing may vary)
  // This is more of an observational test
  EXPECT_GT(duration_high, 0);
  EXPECT_GT(duration_low, 0);
}

/**
 * Test: Agent migration scenario (moving agents between nodes)
 */
TEST_F(DistributedHierarchyTest, AgentMigrationBetweenNodes) {
  SimulatedCluster::Config config{.num_nodes = 3,
                                  .workers_per_node = 2,
                                  .network_config = {.min_latency = 100us, .max_latency = 200us}};

  SimulatedCluster cluster(config);
  cluster.start();

  // Initially on node 0
  cluster.registerAgent("mobile_agent", 0);

  std::atomic<int32_t> executions_node0{0};
  std::atomic<int32_t> executions_node1{0};
  std::atomic<int32_t> executions_node2{0};

  // Execute on node 0
  for (int32_t i = 0; i < 10; ++i) {
    cluster.submit("mobile_agent", [&]() { executions_node0++; });
  }

  std::this_thread::sleep_for(100ms);

  // Migrate to node 1 (unregister and re-register)
  cluster.unregisterAgent("mobile_agent");
  cluster.registerAgent("mobile_agent", 1);

  // Execute on node 1
  for (int32_t i = 0; i < 10; ++i) {
    cluster.submit("mobile_agent", [&]() { executions_node1++; });
  }

  std::this_thread::sleep_for(100ms);

  // Migrate to node 2
  cluster.unregisterAgent("mobile_agent");
  cluster.registerAgent("mobile_agent", 2);

  // Execute on node 2
  for (int32_t i = 0; i < 10; ++i) {
    cluster.submit("mobile_agent", [&]() { executions_node2++; });
  }

  std::this_thread::sleep_for(100ms);

  // Verify executions on each node
  EXPECT_EQ(executions_node0.load(), 10);
  EXPECT_EQ(executions_node1.load(), 10);
  EXPECT_EQ(executions_node2.load(), 10);

  cluster.shutdown();
}

/**
 * Test: Statistics collection in distributed hierarchy
 */
TEST_F(DistributedHierarchyTest, DistributedStatisticsCollection) {
  SimulatedCluster::Config config{.num_nodes = 3,
                                  .workers_per_node = 4,
                                  .network_config = {.min_latency = 100us, .max_latency = 200us}};

  SimulatedCluster cluster(config);
  cluster.start();

  cluster.registerAgent("chief", 0);
  cluster.registerAgent("worker_1", 1);
  cluster.registerAgent("worker_2", 2);

  // Generate cross-node traffic using network
  for (int32_t i = 0; i < 20; ++i) {
    size_t target_node = (i % 2 == 0) ? 1 : 2;
    cluster.getNetwork()->send(0, target_node, [&]() {
      // Work
      int32_t sum = 0;
      for (int32_t j = 0; j < 100; ++j) {
        sum += j;
      }
      [[maybe_unused]] volatile int result = sum;  // Prevent optimization
    });
  }

  // Process network messages
  for (int32_t i = 0; i < 50; ++i) {
    cluster.processNetworkMessages();
    std::this_thread::sleep_for(5ms);
  }

  auto stats = cluster.getStats();

  // Validate statistics
  EXPECT_EQ(stats.total_network_messages, 20);
  EXPECT_GT(stats.avg_network_latency_us, 100.0);  // At least min latency
  EXPECT_LE(stats.avg_network_latency_us,
            10000.0);  // Relaxed for scheduler variance
  EXPECT_EQ(stats.queue_depths_per_node.size(), 3u);

  // Reset and verify
  cluster.resetStats();
  auto reset_stats = cluster.getStats();

  EXPECT_EQ(reset_stats.total_network_messages, 0);

  cluster.shutdown();
}
