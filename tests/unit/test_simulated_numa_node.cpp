#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "simulation/simulated_numa_node.hpp"

using namespace keystone::simulation;

class SimulatedNUMANodeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Tests will create nodes as needed
  }
};

TEST_F(SimulatedNUMANodeTest, CreateAndDestroy) {
  SimulatedNUMANode node(0, 4);
  EXPECT_EQ(node.getNodeId(), 0);
  EXPECT_EQ(node.getNumWorkers(), 4);
}

TEST_F(SimulatedNUMANodeTest, StartAndShutdown) {
  SimulatedNUMANode node(0, 2);

  node.start();
  // Should be able to submit work after starting
  std::atomic<bool> executed{false};
  node.submit([&]() { executed = true; });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_TRUE(executed);

  node.shutdown();
}

TEST_F(SimulatedNUMANodeTest, SubmitWork) {
  SimulatedNUMANode node(0, 4);
  node.start();

  std::atomic<int32_t> counter{0};

  // Submit multiple work items
  for (int32_t i = 0; i < 10; ++i) {
    node.submit([&]() { counter++; });
  }

  // Wait for completion
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_EQ(counter.load(), 10);
  node.shutdown();
}

TEST_F(SimulatedNUMANodeTest, SubmitToSpecificWorker) {
  SimulatedNUMANode node(0, 4);
  node.start();

  std::atomic<int32_t> counter{0};

  // Submit to worker 0
  for (int32_t i = 0; i < 5; ++i) {
    node.submitToWorker(0, [&]() { counter++; });
  }

  // Submit to worker 1
  for (int32_t i = 0; i < 5; ++i) {
    node.submitToWorker(1, [&]() { counter++; });
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(counter.load(), 10);

  node.shutdown();
}

TEST_F(SimulatedNUMANodeTest, AgentRegistration) {
  SimulatedNUMANode node(0, 4);

  EXPECT_FALSE(node.hasAgent("agent_A"));

  node.registerAgent("agent_A");
  EXPECT_TRUE(node.hasAgent("agent_A"));

  node.registerAgent("agent_B");
  EXPECT_TRUE(node.hasAgent("agent_B"));

  auto agents = node.getLocalAgents();
  EXPECT_EQ(agents.size(), 2u);
  EXPECT_TRUE(agents.count("agent_A") > 0);
  EXPECT_TRUE(agents.count("agent_B") > 0);
}

TEST_F(SimulatedNUMANodeTest, AgentUnregistration) {
  SimulatedNUMANode node(0, 4);

  node.registerAgent("agent_A");
  node.registerAgent("agent_B");
  EXPECT_EQ(node.getLocalAgents().size(), 2u);

  node.unregisterAgent("agent_A");
  EXPECT_FALSE(node.hasAgent("agent_A"));
  EXPECT_TRUE(node.hasAgent("agent_B"));
  EXPECT_EQ(node.getLocalAgents().size(), 1u);

  node.unregisterAgent("agent_B");
  EXPECT_EQ(node.getLocalAgents().size(), 0u);
}

TEST_F(SimulatedNUMANodeTest, LocalStealTracking) {
  SimulatedNUMANode node(0, 4);

  EXPECT_EQ(node.getLocalSteals(), 0);

  node.recordLocalSteal();
  EXPECT_EQ(node.getLocalSteals(), 1);

  node.recordLocalSteal();
  node.recordLocalSteal();
  EXPECT_EQ(node.getLocalSteals(), 3);
}

TEST_F(SimulatedNUMANodeTest, RemoteStealTracking) {
  SimulatedNUMANode node(0, 4);

  EXPECT_EQ(node.getRemoteSteals(), 0);

  // Attempt steal (will increment counter even if no work)
  node.stealWork();
  EXPECT_EQ(node.getRemoteSteals(), 1);

  node.stealWork();
  node.stealWork();
  EXPECT_EQ(node.getRemoteSteals(), 3);
}

TEST_F(SimulatedNUMANodeTest, QueueDepthTracking) {
  SimulatedNUMANode node(0, 4);
  node.start();

  // Initially empty
  EXPECT_EQ(node.getQueueDepth(), 0);

  // Submit work that blocks briefly
  for (int32_t i = 0; i < 20; ++i) {
    node.submit(
        [&]() { std::this_thread::sleep_for(std::chrono::milliseconds(50)); });
  }

  // Should have pending work
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  size_t depth = node.getQueueDepth();
  EXPECT_GT(depth, 0);

  // Wait for completion
  std::this_thread::sleep_for(std::chrono::seconds(2));
  EXPECT_EQ(node.getQueueDepth(), 0);

  node.shutdown();
}

TEST_F(SimulatedNUMANodeTest, ResetStats) {
  SimulatedNUMANode node(0, 4);

  node.recordLocalSteal();
  node.recordLocalSteal();
  node.stealWork();

  EXPECT_EQ(node.getLocalSteals(), 2);
  EXPECT_EQ(node.getRemoteSteals(), 1);

  node.resetStats();

  EXPECT_EQ(node.getLocalSteals(), 0);
  EXPECT_EQ(node.getRemoteSteals(), 0);
}

TEST_F(SimulatedNUMANodeTest, MultipleNodes) {
  SimulatedNUMANode node0(0, 2);
  SimulatedNUMANode node1(1, 2);
  SimulatedNUMANode node2(2, 2);

  EXPECT_EQ(node0.getNodeId(), 0);
  EXPECT_EQ(node1.getNodeId(), 1);
  EXPECT_EQ(node2.getNodeId(), 2);

  node0.start();
  node1.start();
  node2.start();

  std::atomic<int32_t> counter0{0}, counter1{0}, counter2{0};

  node0.submit([&]() { counter0++; });
  node1.submit([&]() { counter1++; });
  node2.submit([&]() { counter2++; });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_EQ(counter0.load(), 1);
  EXPECT_EQ(counter1.load(), 1);
  EXPECT_EQ(counter2.load(), 1);

  node0.shutdown();
  node1.shutdown();
  node2.shutdown();
}

TEST_F(SimulatedNUMANodeTest, SuccessfulWorkStealing) {
  SimulatedNUMANode node(0, 4);
  node.start();

  // Submit work that will remain in queue
  std::atomic<int32_t> counter{0};
  for (int32_t i = 0; i < 10; ++i) {
    node.submitToWorker(0, [&]() { counter++; });
  }

  // Give workers minimal time to process, but keep work in queue
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Attempt to steal work
  auto stolen_work = node.stealWork();

  // Check if we stole something (may or may not succeed depending on timing)
  if (stolen_work.has_value()) {
    // Execute the stolen work
    stolen_work.value()();
    EXPECT_EQ(counter.load(), 1);
  }

  node.shutdown();
}

TEST_F(SimulatedNUMANodeTest, StealFromEmptyQueue) {
  SimulatedNUMANode node(0, 4);
  node.start();

  // Try to steal from empty queue
  auto stolen_work = node.stealWork();

  // Should return nullopt when queue is empty
  EXPECT_FALSE(stolen_work.has_value());

  node.shutdown();
}

TEST_F(SimulatedNUMANodeTest, RemoteStealCounterIncrementsOnAttempt) {
  SimulatedNUMANode node(0, 4);

  // Counter should increment on every steal attempt
  EXPECT_EQ(node.getRemoteSteals(), 0);

  // Try stealing multiple times (all will fail on empty queue)
  for (int32_t i = 0; i < 5; ++i) {
    node.stealWork();
  }

  // Counter increments on each attempt, not just successes
  EXPECT_EQ(node.getRemoteSteals(), 5);
}

TEST_F(SimulatedNUMANodeTest, CrossNodeWorkStealing) {
  // Simulate cross-node stealing scenario
  SimulatedNUMANode node0(0, 2);
  SimulatedNUMANode node1(1, 2);

  node0.start();
  node1.start();

  // Submit work to node1
  std::atomic<int32_t> counter{0};
  for (int32_t i = 0; i < 5; ++i) {
    node1.submitToWorker(0, [&]() { counter++; });
  }

  // Give workers a moment but not enough to complete all work
  std::this_thread::sleep_for(std::chrono::milliseconds(5));

  // Node0 attempts to steal from node1
  size_t initial_steals = node1.getRemoteSteals();
  auto stolen_work = node1.stealWork();
  size_t new_steals = node1.getRemoteSteals();

  // Steal counter should have incremented
  EXPECT_EQ(new_steals, initial_steals + 1);

  // If we got work, execute it
  if (stolen_work.has_value()) {
    stolen_work.value()();
    // Counter should increase
    EXPECT_GT(counter.load(), 0);
  }

  node0.shutdown();
  node1.shutdown();
}

TEST_F(SimulatedNUMANodeTest, MultipleSuccessfulSteals) {
  SimulatedNUMANode node(0, 4);
  node.start();

  // Submit many work items that are slow to execute
  std::atomic<int32_t> counter{0};
  for (int32_t i = 0; i < 20; ++i) {
    node.submit([&]() {
      counter++;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    });
  }

  // Steal multiple work items
  int32_t stolen_count = 0;
  for (int32_t i = 0; i < 10; ++i) {
    auto work = node.stealWork();
    if (work.has_value()) {
      work.value()();
      stolen_count++;
    }
  }

  // At least some steals should have been successful
  EXPECT_GT(stolen_count, 0);

  // Wait for remaining work to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // All work should eventually execute
  EXPECT_EQ(counter.load(), 20);

  node.shutdown();
}
