#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "agents/agent_core.hpp"
#include "core/message.hpp"
#include "core/message_bus.hpp"

using namespace keystone;
using namespace keystone::core;
using namespace keystone::agents;

/**
 * @brief Test agent to verify priority-based message processing
 */
class TestPriorityAgent : public AgentCore {
 public:
  explicit TestPriorityAgent(const std::string& agent_id)
      : AgentCore(agent_id) {}

  std::vector<std::string> processed_order;

  void processAllMessages() {
    while (auto msg = getMessage()) {
      // Record message content in order processed
      if (msg->payload) {
        processed_order.push_back(*msg->payload);
      }
    }
  }
};

/**
 * @brief Test that HIGH priority messages are processed first
 */
TEST(MessagePriorityTest, HighPriorityProcessedFirst) {
  auto agent = std::make_shared<TestPriorityAgent>("test_agent");

  // Send messages in NORMAL, HIGH, LOW order
  auto normal_msg =
      KeystoneMessage::create("sender", "test_agent", "cmd", "NORMAL");
  normal_msg.priority = Priority::NORMAL;

  auto high_msg =
      KeystoneMessage::create("sender", "test_agent", "cmd", "HIGH");
  high_msg.priority = Priority::HIGH;

  auto low_msg = KeystoneMessage::create("sender", "test_agent", "cmd", "LOW");
  low_msg.priority = Priority::LOW;

  // Receive in order: NORMAL, HIGH, LOW
  agent->receiveMessage(normal_msg);
  agent->receiveMessage(high_msg);
  agent->receiveMessage(low_msg);

  // Process all messages
  agent->processAllMessages();

  // Verify they were processed in priority order: HIGH, NORMAL, LOW
  ASSERT_EQ(agent->processed_order.size(), 3);
  EXPECT_EQ(agent->processed_order[0], "HIGH");
  EXPECT_EQ(agent->processed_order[1], "NORMAL");
  EXPECT_EQ(agent->processed_order[2], "LOW");
}

/**
 * @brief Test multiple messages of same priority maintain FIFO order
 */
TEST(MessagePriorityTest, SamePriorityFIFO) {
  auto agent = std::make_shared<TestPriorityAgent>("test_agent");

  // Send multiple NORMAL priority messages
  auto msg1 = KeystoneMessage::create("sender", "test_agent", "cmd", "FIRST");
  msg1.priority = Priority::NORMAL;

  auto msg2 = KeystoneMessage::create("sender", "test_agent", "cmd", "SECOND");
  msg2.priority = Priority::NORMAL;

  auto msg3 = KeystoneMessage::create("sender", "test_agent", "cmd", "THIRD");
  msg3.priority = Priority::NORMAL;

  agent->receiveMessage(msg1);
  agent->receiveMessage(msg2);
  agent->receiveMessage(msg3);

  agent->processAllMessages();

  // Same priority maintains FIFO order
  ASSERT_EQ(agent->processed_order.size(), 3);
  EXPECT_EQ(agent->processed_order[0], "FIRST");
  EXPECT_EQ(agent->processed_order[1], "SECOND");
  EXPECT_EQ(agent->processed_order[2], "THIRD");
}

/**
 * @brief Test complex mixed priority scenario
 */
TEST(MessagePriorityTest, MixedPriorityOrdering) {
  auto agent = std::make_shared<TestPriorityAgent>("test_agent");

  // Send messages in mixed order
  auto low1 = KeystoneMessage::create("sender", "test_agent", "cmd", "LOW1");
  low1.priority = Priority::LOW;

  auto normal1 =
      KeystoneMessage::create("sender", "test_agent", "cmd", "NORMAL1");
  normal1.priority = Priority::NORMAL;

  auto high1 = KeystoneMessage::create("sender", "test_agent", "cmd", "HIGH1");
  high1.priority = Priority::HIGH;

  auto normal2 =
      KeystoneMessage::create("sender", "test_agent", "cmd", "NORMAL2");
  normal2.priority = Priority::NORMAL;

  auto high2 = KeystoneMessage::create("sender", "test_agent", "cmd", "HIGH2");
  high2.priority = Priority::HIGH;

  auto low2 = KeystoneMessage::create("sender", "test_agent", "cmd", "LOW2");
  low2.priority = Priority::LOW;

  // Receive in mixed order
  agent->receiveMessage(low1);
  agent->receiveMessage(normal1);
  agent->receiveMessage(high1);
  agent->receiveMessage(normal2);
  agent->receiveMessage(high2);
  agent->receiveMessage(low2);

  agent->processAllMessages();

  // Expected order: HIGH1, HIGH2, NORMAL1, NORMAL2, LOW1, LOW2
  ASSERT_EQ(agent->processed_order.size(), 6);
  EXPECT_EQ(agent->processed_order[0], "HIGH1");
  EXPECT_EQ(agent->processed_order[1], "HIGH2");
  EXPECT_EQ(agent->processed_order[2], "NORMAL1");
  EXPECT_EQ(agent->processed_order[3], "NORMAL2");
  EXPECT_EQ(agent->processed_order[4], "LOW1");
  EXPECT_EQ(agent->processed_order[5], "LOW2");
}

/**
 * @brief Test that default priority is NORMAL
 */
TEST(MessagePriorityTest, DefaultPriorityIsNormal) {
  auto msg = KeystoneMessage::create("sender", "receiver", "cmd", "data");

  // Default priority should be NORMAL
  EXPECT_EQ(msg.priority, Priority::NORMAL);
}

/**
 * @brief Test priority with getMessage() one at a time
 */
TEST(MessagePriorityTest, GetMessageRespectsPriority) {
  auto agent = std::make_shared<TestPriorityAgent>("test_agent");

  // Send LOW, NORMAL, HIGH
  auto low_msg = KeystoneMessage::create("sender", "test_agent", "cmd", "LOW");
  low_msg.priority = Priority::LOW;

  auto normal_msg =
      KeystoneMessage::create("sender", "test_agent", "cmd", "NORMAL");
  normal_msg.priority = Priority::NORMAL;

  auto high_msg =
      KeystoneMessage::create("sender", "test_agent", "cmd", "HIGH");
  high_msg.priority = Priority::HIGH;

  agent->receiveMessage(low_msg);
  agent->receiveMessage(normal_msg);
  agent->receiveMessage(high_msg);

  // Get messages one at a time
  auto msg1 = agent->getMessage();
  ASSERT_TRUE(msg1.has_value());
  EXPECT_EQ(*msg1->payload, "HIGH");

  auto msg2 = agent->getMessage();
  ASSERT_TRUE(msg2.has_value());
  EXPECT_EQ(*msg2->payload, "NORMAL");

  auto msg3 = agent->getMessage();
  ASSERT_TRUE(msg3.has_value());
  EXPECT_EQ(*msg3->payload, "LOW");

  // No more messages
  auto msg4 = agent->getMessage();
  EXPECT_FALSE(msg4.has_value());
}

// ========================================================================
// Issue #23: Configurable Fairness Check Interval Tests
// ========================================================================

/**
 * @brief Test that default fairness interval is set from Config
 */
TEST(MessagePriorityTest, DefaultFairnessIntervalFromConfig) {
  auto agent = std::make_shared<TestPriorityAgent>("test_agent");

  // Default should be 100ms from Config::AGENT_LOW_PRIORITY_CHECK_INTERVAL
  auto interval = agent->getLowPriorityCheckInterval();
  EXPECT_EQ(interval, std::chrono::milliseconds{100});
}

/**
 * @brief Test setting custom fairness interval
 */
TEST(MessagePriorityTest, SetCustomFairnessInterval) {
  auto agent = std::make_shared<TestPriorityAgent>("test_agent");

  // Set to 50ms (low-latency agent)
  agent->setLowPriorityCheckInterval(std::chrono::milliseconds{50});
  EXPECT_EQ(agent->getLowPriorityCheckInterval(),
            std::chrono::milliseconds{50});

  // Set to 500ms (high-throughput agent)
  agent->setLowPriorityCheckInterval(std::chrono::milliseconds{500});
  EXPECT_EQ(agent->getLowPriorityCheckInterval(),
            std::chrono::milliseconds{500});

  // Set to 10ms (ultra-low-latency)
  agent->setLowPriorityCheckInterval(std::chrono::milliseconds{10});
  EXPECT_EQ(agent->getLowPriorityCheckInterval(),
            std::chrono::milliseconds{10});
}

/**
 * @brief Test different agents can have different intervals
 */
TEST(MessagePriorityTest, DifferentAgentsDifferentIntervals) {
  auto low_latency_agent = std::make_shared<TestPriorityAgent>("low_latency");
  auto high_throughput_agent =
      std::make_shared<TestPriorityAgent>("high_throughput");

  // Configure different intervals
  low_latency_agent->setLowPriorityCheckInterval(std::chrono::milliseconds{10});
  high_throughput_agent->setLowPriorityCheckInterval(
      std::chrono::milliseconds{500});

  // Verify they retain separate configurations
  EXPECT_EQ(low_latency_agent->getLowPriorityCheckInterval(),
            std::chrono::milliseconds{10});
  EXPECT_EQ(high_throughput_agent->getLowPriorityCheckInterval(),
            std::chrono::milliseconds{500});
}

/**
 * @brief Test that fairness mechanism uses configured interval
 *
 * This test verifies that the fairness check actually honors the configured
 * interval by testing with a very short interval (1ms) and ensuring LOW
 * priority messages get processed even with continuous HIGH priority load.
 */
TEST(MessagePriorityTest, FairnessMechanismUsesConfiguredInterval) {
  auto agent = std::make_shared<TestPriorityAgent>("test_agent");

  // Set very short interval (1ms) to make fairness trigger quickly
  agent->setLowPriorityCheckInterval(std::chrono::milliseconds{1});

  // Send one LOW priority message
  auto low_msg = KeystoneMessage::create("sender", "test_agent", "cmd", "LOW");
  low_msg.priority = Priority::LOW;
  agent->receiveMessage(low_msg);

  // Send many HIGH priority messages after
  for (int32_t i = 0; i < 5; ++i) {
    auto high_msg = KeystoneMessage::create("sender", "test_agent", "cmd",
                                            "HIGH" + std::to_string(i));
    high_msg.priority = Priority::HIGH;
    agent->receiveMessage(high_msg);
  }

  // Wait for fairness interval to elapse (1ms + safety margin)
  std::this_thread::sleep_for(std::chrono::milliseconds{5});

  // First getMessage should process HIGH (normal priority order)
  auto msg1 = agent->getMessage();
  ASSERT_TRUE(msg1.has_value());
  EXPECT_EQ(*msg1->payload, "HIGH0");

  // Continue processing HIGH messages
  auto msg2 = agent->getMessage();
  ASSERT_TRUE(msg2.has_value());
  EXPECT_EQ(*msg2->payload, "HIGH1");

  // Wait for another fairness interval
  std::this_thread::sleep_for(std::chrono::milliseconds{5});

  // Now the fairness mechanism should kick in and process LOW
  // (even though HIGH messages are still available)
  auto msg3 = agent->getMessage();
  ASSERT_TRUE(msg3.has_value());
  EXPECT_EQ(*msg3->payload, "LOW");
}

/**
 * @brief Test thread safety of setting interval concurrently
 *
 * Verifies that setLowPriorityCheckInterval is thread-safe using atomics
 */
TEST(MessagePriorityTest, ConcurrentIntervalSettingThreadSafe) {
  auto agent = std::make_shared<TestPriorityAgent>("test_agent");

  // Launch multiple threads setting different intervals
  std::vector<std::thread> threads;
  for (int32_t i = 0; i < 10; ++i) {
    threads.emplace_back([&agent, i]() {
      agent->setLowPriorityCheckInterval(
          std::chrono::milliseconds{10 + i * 10});
    });
  }

  // Wait for all threads
  for (auto& t : threads) {
    t.join();
  }

  // Should have one of the set values (exact value is non-deterministic due to
  // race)
  auto final_interval = agent->getLowPriorityCheckInterval();
  EXPECT_GE(final_interval.count(), 10);
  EXPECT_LE(final_interval.count(), 100);
}

/**
 * @brief Test extreme interval values
 */
TEST(MessagePriorityTest, ExtremeIntervalValues) {
  auto agent = std::make_shared<TestPriorityAgent>("test_agent");

  // Very short interval (1ms, minimum practical value)
  agent->setLowPriorityCheckInterval(std::chrono::milliseconds{1});
  EXPECT_EQ(agent->getLowPriorityCheckInterval(), std::chrono::milliseconds{1});

  // Very long interval (10 seconds)
  agent->setLowPriorityCheckInterval(std::chrono::seconds{10});
  EXPECT_EQ(agent->getLowPriorityCheckInterval(), std::chrono::seconds{10});

  // Zero interval (should work but may cause constant fairness checks)
  agent->setLowPriorityCheckInterval(std::chrono::milliseconds{0});
  EXPECT_EQ(agent->getLowPriorityCheckInterval(), std::chrono::milliseconds{0});
}
