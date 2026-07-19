#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "simulation/simulated_network.hpp"

using namespace keystone::simulation;
using namespace std::chrono_literals;

class SimulatedNetworkTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Tests will create networks as needed
  }
};

TEST_F(SimulatedNetworkTest, CreateWithDefaultConfig) {
  SimulatedNetwork network;

  EXPECT_EQ(network.getTotalMessages(), 0);
  EXPECT_EQ(network.getDeliveredMessages(), 0);
  EXPECT_EQ(network.getDroppedMessages(), 0);
  EXPECT_EQ(network.getAverageLatencyUs(), 0.0);
}

TEST_F(SimulatedNetworkTest, CreateWithCustomConfig) {
  SimulatedNetwork::Config config{.min_latency = 50us,
                                  .max_latency = 200us,
                                  .bandwidth_mbps = 1000,
                                  .packet_loss_rate = 0.1};

  SimulatedNetwork network(config);
  EXPECT_EQ(network.getTotalMessages(), 0);
}

TEST_F(SimulatedNetworkTest, SendAndReceive) {
  SimulatedNetwork::Config config{
      .min_latency = 1000us,  // Increased from 10us to 1ms for reliable timing
      .max_latency = 2000us   // Increased from 20us to 2ms
  };
  SimulatedNetwork network(config);

  std::atomic<bool> work_executed{false};

  // Send work from node 0 to node 1
  network.send(0, 1, [&]() { work_executed = true; });

  EXPECT_EQ(network.getTotalMessages(), 1);
  EXPECT_EQ(network.getPendingMessages(), 1);

  // Immediately try to receive - should not be ready yet
  auto work = network.receive(1);
  EXPECT_FALSE(work.has_value());

  // Wait for latency to elapse (longer than max_latency)
  std::this_thread::sleep_for(3000us);

  // Now should be ready
  work = network.receive(1);
  EXPECT_TRUE(work.has_value());

  if (work.has_value()) {
    (*work)();  // Execute the work
  }

  EXPECT_TRUE(work_executed);
  EXPECT_EQ(network.getDeliveredMessages(), 1);
  EXPECT_EQ(network.getPendingMessages(), 0);
}

TEST_F(SimulatedNetworkTest, MultipleMessages) {
  SimulatedNetwork::Config config{.min_latency = 10us, .max_latency = 20us};
  SimulatedNetwork network(config);

  std::atomic<int32_t> counter{0};

  // Send 10 messages
  for (int32_t i = 0; i < 10; ++i) {
    network.send(0, 1, [&]() { counter++; });
  }

  EXPECT_EQ(network.getTotalMessages(), 10);
  EXPECT_EQ(network.getPendingMessages(), 10);

  // Wait for latency
  std::this_thread::sleep_for(50us);

  // Receive all messages
  int32_t received = 0;
  while (auto work = network.receive(1)) {
    (*work)();
    received++;
  }

  EXPECT_EQ(received, 10);
  EXPECT_EQ(counter.load(), 10);
  EXPECT_EQ(network.getDeliveredMessages(), 10);
  EXPECT_EQ(network.getPendingMessages(), 0);
}

TEST_F(SimulatedNetworkTest, DifferentDestinations) {
  SimulatedNetwork::Config config{.min_latency = 10us, .max_latency = 20us};
  SimulatedNetwork network(config);

  std::atomic<int32_t> counter0{0}, counter1{0}, counter2{0};

  // Send to different nodes
  network.send(0, 0, [&]() { counter0++; });
  network.send(0, 1, [&]() { counter1++; });
  network.send(0, 2, [&]() { counter2++; });

  EXPECT_EQ(network.getTotalMessages(), 3);

  std::this_thread::sleep_for(50us);

  // Receive from each node
  auto work0 = network.receive(0);
  auto work1 = network.receive(1);
  auto work2 = network.receive(2);

  EXPECT_TRUE(work0.has_value());
  EXPECT_TRUE(work1.has_value());
  EXPECT_TRUE(work2.has_value());

  (*work0)();
  (*work1)();
  (*work2)();

  EXPECT_EQ(counter0.load(), 1);
  EXPECT_EQ(counter1.load(), 1);
  EXPECT_EQ(counter2.load(), 1);
}

TEST_F(SimulatedNetworkTest, PacketLoss) {
  SimulatedNetwork::Config config{
      .min_latency = 10us,
      .max_latency = 20us,
      .packet_loss_rate = 1.0  // Drop all packets
  };
  SimulatedNetwork network(config);

  // Send messages - all should be dropped
  for (int32_t i = 0; i < 10; ++i) {
    network.send(0, 1, []() {});
  }

  EXPECT_EQ(network.getTotalMessages(), 10);
  EXPECT_EQ(network.getDroppedMessages(), 10);
  EXPECT_EQ(network.getPendingMessages(), 0);

  std::this_thread::sleep_for(50us);

  // No messages should be receivable
  auto work = network.receive(1);
  EXPECT_FALSE(work.has_value());
}

TEST_F(SimulatedNetworkTest, NoPacketLoss) {
  SimulatedNetwork::Config config{
      .min_latency = 10us,
      .max_latency = 20us,
      .packet_loss_rate = 0.0  // No packet loss
  };
  SimulatedNetwork network(config);

  // Send messages
  for (int32_t i = 0; i < 10; ++i) {
    network.send(0, 1, []() {});
  }

  EXPECT_EQ(network.getTotalMessages(), 10);
  EXPECT_EQ(network.getDroppedMessages(), 0);
  EXPECT_EQ(network.getPendingMessages(), 10);

  std::this_thread::sleep_for(50us);

  // All messages should be receivable
  int32_t received = 0;
  while (auto work = network.receive(1)) {
    received++;
  }

  EXPECT_EQ(received, 10);
  EXPECT_EQ(network.getDeliveredMessages(), 10);
}

TEST_F(SimulatedNetworkTest, LatencyTracking) {
  SimulatedNetwork::Config config{
      .min_latency = 100us,
      .max_latency = 100us  // Fixed latency for predictable testing
  };
  SimulatedNetwork network(config);

  network.send(0, 1, []() {});

  std::this_thread::sleep_for(2ms);  // Wait longer for reliable delivery

  auto work = network.receive(1);
  EXPECT_TRUE(work.has_value());

  // Average latency should be at least 100µs (upper bound relaxed for scheduler
  // variance)
  double avg_latency = network.getAverageLatencyUs();
  EXPECT_GE(avg_latency, 100.0);
  EXPECT_LE(avg_latency, 5000.0);  // Relaxed for timing variance
}

TEST_F(SimulatedNetworkTest, AverageLatencyMultipleMessages) {
  SimulatedNetwork::Config config{.min_latency = 50us, .max_latency = 150us};
  SimulatedNetwork network(config);

  // Send multiple messages
  for (int32_t i = 0; i < 20; ++i) {
    network.send(0, 1, []() {});
  }

  std::this_thread::sleep_for(2ms);  // Wait longer for all to be ready

  // Receive all
  while (auto work = network.receive(1)) {
    // Just consume
  }

  // Average should be in configured range (relaxed for variance)
  double avg_latency = network.getAverageLatencyUs();
  EXPECT_GE(avg_latency, 50.0);
  EXPECT_LE(avg_latency, 5000.0);  // Relaxed for scheduler variance
}

TEST_F(SimulatedNetworkTest, ResetStats) {
  SimulatedNetwork::Config config{.min_latency = 10us, .max_latency = 20us};
  SimulatedNetwork network(config);

  // Send and receive some messages
  for (int32_t i = 0; i < 5; ++i) {
    network.send(0, 1, []() {});
  }

  std::this_thread::sleep_for(50us);

  while (auto work = network.receive(1)) {
    // Consume
  }

  EXPECT_EQ(network.getTotalMessages(), 5);
  EXPECT_EQ(network.getDeliveredMessages(), 5);

  // Reset stats
  network.resetStats();

  EXPECT_EQ(network.getTotalMessages(), 0);
  EXPECT_EQ(network.getDeliveredMessages(), 0);
  EXPECT_EQ(network.getDroppedMessages(), 0);
  EXPECT_EQ(network.getAverageLatencyUs(), 0.0);
}

TEST_F(SimulatedNetworkTest, QueueOrdering) {
  SimulatedNetwork::Config config{.min_latency = 10us, .max_latency = 20us};
  SimulatedNetwork network(config);

  std::vector<int> execution_order;

  // Send messages with identifiable work
  for (int32_t i = 0; i < 5; ++i) {
    network.send(0, 1,
                 [&execution_order, i]() { execution_order.push_back(i); });
  }

  std::this_thread::sleep_for(50us);

  // Receive and execute in order
  while (auto work = network.receive(1)) {
    (*work)();
  }

  // Should be FIFO order
  EXPECT_EQ(execution_order.size(), 5u);
  for (int32_t i = 0; i < 5; ++i) {
    EXPECT_EQ(execution_order[i], i);
  }
}

TEST_F(SimulatedNetworkTest, PendingMessageCount) {
  SimulatedNetwork::Config config{.min_latency = 10us, .max_latency = 20us};
  SimulatedNetwork network(config);

  EXPECT_EQ(network.getPendingMessages(), 0);

  // Send 10 messages
  for (int32_t i = 0; i < 10; ++i) {
    network.send(0, 1, []() {});
  }

  EXPECT_EQ(network.getPendingMessages(), 10);

  std::this_thread::sleep_for(50us);

  // Receive 5 messages
  for (int32_t i = 0; i < 5; ++i) {
    auto work = network.receive(1);
    EXPECT_TRUE(work.has_value());
  }

  EXPECT_EQ(network.getPendingMessages(), 5);

  // Receive remaining 5
  for (int32_t i = 0; i < 5; ++i) {
    auto work = network.receive(1);
    EXPECT_TRUE(work.has_value());
  }

  EXPECT_EQ(network.getPendingMessages(), 0);
}

// ===========================================================================
// Coverage extension tests (test/coverage-scheduler-sim-network)
// Target uncovered regions: default constructor, network partition
// (create/heal/isPartitioned/canCommunicate branches) and the partition-drop
// path in send().
// ===========================================================================

// Test: default constructor delegates to Config{} and starts clean
TEST_F(SimulatedNetworkTest, DefaultConstructor) {
  SimulatedNetwork network;  // SimulatedNetwork() -> SimulatedNetwork(Config{})

  EXPECT_FALSE(network.isPartitioned());
  EXPECT_EQ(network.getTotalMessages(), 0);
  EXPECT_EQ(network.getPartitionDroppedMessages(), 0);

  // Default config has zero packet loss, so a message is queued (not dropped).
  network.send(0, 1, []() {});
  EXPECT_EQ(network.getTotalMessages(), 1);
  EXPECT_EQ(network.getDroppedMessages(), 0);
  EXPECT_EQ(network.getPendingMessages(), 1);
}

// Test: no partition -> canCommunicate always true (early-return branch)
TEST_F(SimulatedNetworkTest, CanCommunicateNoPartition) {
  SimulatedNetwork network;
  EXPECT_FALSE(network.isPartitioned());
  EXPECT_TRUE(network.canCommunicate(0, 1));
  EXPECT_TRUE(network.canCommunicate(5, 5));
}

// Test: createPartition sets state and isPartitioned reports true
TEST_F(SimulatedNetworkTest, CreatePartitionSetsState) {
  SimulatedNetwork network;
  EXPECT_FALSE(network.isPartitioned());

  network.createPartition({0, 1}, {2, 3});
  EXPECT_TRUE(network.isPartitioned());
}

// Test: canCommunicate honours partition membership on every branch
TEST_F(SimulatedNetworkTest, CanCommunicateAcrossPartition) {
  SimulatedNetwork network;
  network.createPartition({0, 1}, {2, 3});

  // Both in A
  EXPECT_TRUE(network.canCommunicate(0, 1));
  // Both in B
  EXPECT_TRUE(network.canCommunicate(2, 3));
  // A -> B (different partitions)
  EXPECT_FALSE(network.canCommunicate(0, 2));
  // B -> A (different partitions)
  EXPECT_FALSE(network.canCommunicate(3, 1));
  // from in A, to in neither -> different partitions
  EXPECT_FALSE(network.canCommunicate(0, 9));
  // from in B, to in neither -> different partitions
  EXPECT_FALSE(network.canCommunicate(2, 9));
  // neither node in any partition -> cannot communicate
  EXPECT_FALSE(network.canCommunicate(8, 9));
}

// Test: send across a partition is dropped and counted separately
TEST_F(SimulatedNetworkTest, SendDroppedByPartition) {
  SimulatedNetwork::Config config{.min_latency = 10us, .max_latency = 20us};
  SimulatedNetwork network(config);
  network.createPartition({0, 1}, {2, 3});

  // Cross-partition send -> dropped by partition.
  network.send(0, 2, []() {});
  EXPECT_EQ(network.getTotalMessages(), 1);
  EXPECT_EQ(network.getPartitionDroppedMessages(), 1);
  EXPECT_EQ(network.getDroppedMessages(), 0);  // not a packet-loss drop
  EXPECT_EQ(network.getPendingMessages(), 0);

  // Same-partition send -> delivered normally.
  network.send(0, 1, []() {});
  EXPECT_EQ(network.getTotalMessages(), 2);
  EXPECT_EQ(network.getPartitionDroppedMessages(), 1);
  EXPECT_EQ(network.getPendingMessages(), 1);
}

// Test: healPartition restores full connectivity
TEST_F(SimulatedNetworkTest, HealPartitionRestoresConnectivity) {
  SimulatedNetwork::Config config{.min_latency = 10us, .max_latency = 20us};
  SimulatedNetwork network(config);

  network.createPartition({0}, {1});
  EXPECT_TRUE(network.isPartitioned());
  EXPECT_FALSE(network.canCommunicate(0, 1));

  network.healPartition();
  EXPECT_FALSE(network.isPartitioned());
  EXPECT_TRUE(network.canCommunicate(0, 1));

  // After healing, a previously-blocked send now goes through.
  network.send(0, 1, []() {});
  EXPECT_EQ(network.getPartitionDroppedMessages(), 0);
  EXPECT_EQ(network.getPendingMessages(), 1);
}
