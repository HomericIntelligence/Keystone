#include "core/metrics.hpp"

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

using namespace keystone::core;

class MetricsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Reset metrics before each test
    Metrics::getInstance().reset();
  }
};

/**
 * @brief Test singleton pattern
 */
TEST_F(MetricsTest, SingletonPattern) {
  auto& metrics1 = Metrics::getInstance();
  auto& metrics2 = Metrics::getInstance();

  // Same instance
  EXPECT_EQ(&metrics1, &metrics2);
}

/**
 * @brief Test message counting
 */
TEST_F(MetricsTest, MessageCounting) {
  auto& metrics = Metrics::getInstance();

  EXPECT_EQ(metrics.getTotalMessagesSent(), 0);
  EXPECT_EQ(metrics.getTotalMessagesProcessed(), 0);

  metrics.recordMessageSent("msg1", Priority::NORMAL);
  metrics.recordMessageSent("msg2", Priority::NORMAL);
  metrics.recordMessageSent("msg3", Priority::NORMAL);

  EXPECT_EQ(metrics.getTotalMessagesSent(), 3);
  EXPECT_EQ(metrics.getTotalMessagesProcessed(), 0);

  metrics.recordMessageProcessed("msg1");
  metrics.recordMessageProcessed("msg2");

  EXPECT_EQ(metrics.getTotalMessagesSent(), 3);
  EXPECT_EQ(metrics.getTotalMessagesProcessed(), 2);
}

/**
 * @brief Test priority distribution tracking
 */
TEST_F(MetricsTest, PriorityDistribution) {
  auto& metrics = Metrics::getInstance();

  // Send messages with different priorities
  metrics.recordMessageSent("msg_high1", Priority::HIGH);
  metrics.recordMessageSent("msg_high2", Priority::HIGH);
  metrics.recordMessageSent("msg_normal1", Priority::NORMAL);
  metrics.recordMessageSent("msg_normal2", Priority::NORMAL);
  metrics.recordMessageSent("msg_normal3", Priority::NORMAL);
  metrics.recordMessageSent("msg_low1", Priority::LOW);

  auto stats = metrics.getPriorityStats();
  EXPECT_EQ(stats.high_count, 2);
  EXPECT_EQ(stats.normal_count, 3);
  EXPECT_EQ(stats.low_count, 1);
}

/**
 * @brief Test latency tracking
 */
TEST_F(MetricsTest, LatencyTracking) {
  auto& metrics = Metrics::getInstance();

  // Initially no latency data
  EXPECT_FALSE(metrics.getAverageLatencyUs().has_value());

  // Send a message
  metrics.recordMessageSent("msg1", Priority::NORMAL);

  // Simulate some processing time
  std::this_thread::sleep_for(std::chrono::microseconds(100));

  // Process the message
  metrics.recordMessageProcessed("msg1");

  // Should have latency data now
  auto latency = metrics.getAverageLatencyUs();
  ASSERT_TRUE(latency.has_value());
  EXPECT_GT(*latency, 50.0);  // At least 50μs (we slept for 100μs)
}

/**
 * @brief Test queue depth tracking
 */
TEST_F(MetricsTest, QueueDepthTracking) {
  auto& metrics = Metrics::getInstance();

  EXPECT_EQ(metrics.getMaxQueueDepth(), 0);

  metrics.recordQueueDepth("agent1", 5);
  EXPECT_EQ(metrics.getMaxQueueDepth(), 5);

  metrics.recordQueueDepth("agent2", 10);
  EXPECT_EQ(metrics.getMaxQueueDepth(), 10);

  metrics.recordQueueDepth("agent1", 3);  // Decrease doesn't change max
  EXPECT_EQ(metrics.getMaxQueueDepth(), 10);

  metrics.recordQueueDepth("agent3", 15);
  EXPECT_EQ(metrics.getMaxQueueDepth(), 15);
}

/**
 * @brief Test worker utilization tracking
 */
TEST_F(MetricsTest, WorkerUtilization) {
  auto& metrics = Metrics::getInstance();

  EXPECT_EQ(metrics.getWorkerUtilization(), 0.0);

  // Simulate worker activity
  metrics.recordWorkerActivity(0, true);   // busy
  metrics.recordWorkerActivity(1, true);   // busy
  metrics.recordWorkerActivity(2, false);  // idle
  metrics.recordWorkerActivity(3, true);   // busy

  // 3 busy out of 4 samples = 75%
  EXPECT_NEAR(metrics.getWorkerUtilization(), 75.0, 0.1);
}

/**
 * @brief Test throughput calculation
 */
TEST_F(MetricsTest, ThroughputCalculation) {
  auto& metrics = Metrics::getInstance();

  // Process some messages
  for (int32_t i = 0; i < 100; ++i) {
    metrics.recordMessageSent("msg" + std::to_string(i), Priority::NORMAL);
    metrics.recordMessageProcessed("msg" + std::to_string(i));
  }

  // Wait a bit
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  double throughput = metrics.getMessagesPerSecond();
  EXPECT_GT(throughput, 0.0);
  EXPECT_LT(throughput, 1000000.0);  // Sanity check
}

/**
 * @brief Test metrics reset
 */
TEST_F(MetricsTest, MetricsReset) {
  auto& metrics = Metrics::getInstance();

  // Add some data
  metrics.recordMessageSent("msg1", Priority::HIGH);
  metrics.recordMessageSent("msg2", Priority::NORMAL);
  metrics.recordMessageProcessed("msg1");
  metrics.recordQueueDepth("agent1", 10);
  metrics.recordWorkerActivity(0, true);

  EXPECT_GT(metrics.getTotalMessagesSent(), 0);

  // Reset
  metrics.reset();

  // Everything should be zero
  EXPECT_EQ(metrics.getTotalMessagesSent(), 0);
  EXPECT_EQ(metrics.getTotalMessagesProcessed(), 0);
  EXPECT_EQ(metrics.getMaxQueueDepth(), 0);
  EXPECT_FALSE(metrics.getAverageLatencyUs().has_value());

  auto stats = metrics.getPriorityStats();
  EXPECT_EQ(stats.high_count, 0);
  EXPECT_EQ(stats.normal_count, 0);
  EXPECT_EQ(stats.low_count, 0);
}

/**
 * @brief Test report generation
 */
TEST_F(MetricsTest, ReportGeneration) {
  auto& metrics = Metrics::getInstance();

  // Add some data
  metrics.recordMessageSent("msg1", Priority::HIGH);
  metrics.recordMessageSent("msg2", Priority::NORMAL);
  metrics.recordMessageProcessed("msg1");
  metrics.recordQueueDepth("agent1", 5);

  std::string report = metrics.generateReport();

  // Report should contain key information
  EXPECT_NE(report.find("Messages Sent"), std::string::npos);
  EXPECT_NE(report.find("Messages Processed"), std::string::npos);
  EXPECT_NE(report.find("Priority Distribution"), std::string::npos);
  EXPECT_NE(report.find("Max Queue Depth"), std::string::npos);
}

/**
 * @brief Test thread safety with concurrent access
 */
TEST_F(MetricsTest, ThreadSafety) {
  auto& metrics = Metrics::getInstance();

  const int32_t num_threads = 10;
  const int32_t msgs_per_thread = 100;

  std::vector<std::thread> threads;

  for (int32_t t = 0; t < num_threads; ++t) {
    threads.emplace_back([&metrics, t]() {
      for (int32_t i = 0; i < msgs_per_thread; ++i) {
        std::string msg_id = "thread" + std::to_string(t) + "_msg" + std::to_string(i);
        // Cycle through priorities: HIGH, NORMAL, LOW
        Priority priority = static_cast<Priority>(i % 3);
        metrics.recordMessageSent(msg_id, priority);
        metrics.recordMessageProcessed(msg_id);
        metrics.recordQueueDepth("agent" + std::to_string(t), i);
        metrics.recordWorkerActivity(t, i % 2 == 0);
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // All messages should be counted
  EXPECT_EQ(metrics.getTotalMessagesSent(), num_threads * msgs_per_thread);
  EXPECT_EQ(metrics.getTotalMessagesProcessed(), num_threads * msgs_per_thread);

  // Priority counts should add up
  auto stats = metrics.getPriorityStats();
  EXPECT_EQ(stats.high_count + stats.normal_count + stats.low_count,
            static_cast<uint64_t>(num_threads * msgs_per_thread));
}
