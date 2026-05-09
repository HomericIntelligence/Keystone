#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "core/message.hpp"

using namespace keystone::core;
using namespace std::chrono_literals;

/**
 * @brief Test deadline field initialization
 */
TEST(DeadlineSchedulingTest, NoDeadlineByDefault) {
  auto msg = KeystoneMessage::create("sender", "receiver", "cmd", "data");

  // No deadline by default
  EXPECT_FALSE(msg.deadline.has_value());
  EXPECT_FALSE(msg.hasDeadlinePassed());
  EXPECT_FALSE(msg.getTimeUntilDeadline().has_value());
}

/**
 * @brief Test setting deadline from now
 */
TEST(DeadlineSchedulingTest, SetDeadlineFromNow) {
  auto msg = KeystoneMessage::create("sender", "receiver", "cmd", "data");

  msg.setDeadlineFromNow(100ms);

  EXPECT_TRUE(msg.deadline.has_value());
  EXPECT_FALSE(msg.hasDeadlinePassed());

  auto time_remaining = msg.getTimeUntilDeadline();
  ASSERT_TRUE(time_remaining.has_value());
  EXPECT_GT(*time_remaining, 50ms);   // At least 50ms remaining
  EXPECT_LE(*time_remaining, 100ms);  // At most 100ms remaining
}

/**
 * @brief Test deadline expiration
 */
TEST(DeadlineSchedulingTest, DeadlineExpiration) {
  auto msg = KeystoneMessage::create("sender", "receiver", "cmd", "data");

  // Set very short deadline
  msg.setDeadlineFromNow(10ms);

  EXPECT_FALSE(msg.hasDeadlinePassed());

  // Wait for deadline to pass
  std::this_thread::sleep_for(20ms);

  EXPECT_TRUE(msg.hasDeadlinePassed());

  auto time_remaining = msg.getTimeUntilDeadline();
  ASSERT_TRUE(time_remaining.has_value());
  EXPECT_EQ(*time_remaining, 0ms);  // Deadline passed, returns 0
}

/**
 * @brief Test time until deadline calculation
 */
TEST(DeadlineSchedulingTest, TimeUntilDeadlineCalculation) {
  auto msg = KeystoneMessage::create("sender", "receiver", "cmd", "data");

  msg.setDeadlineFromNow(200ms);

  // Check time remaining
  auto remaining1 = msg.getTimeUntilDeadline();
  ASSERT_TRUE(remaining1.has_value());
  EXPECT_GT(*remaining1, 150ms);

  // Wait a bit
  std::this_thread::sleep_for(50ms);

  // Time should have decreased
  auto remaining2 = msg.getTimeUntilDeadline();
  ASSERT_TRUE(remaining2.has_value());
  EXPECT_LT(*remaining2, *remaining1);
  EXPECT_GT(*remaining2, 100ms);
}

/**
 * @brief Test explicit deadline setting
 */
TEST(DeadlineSchedulingTest, ExplicitDeadlineSetting) {
  auto msg = KeystoneMessage::create("sender", "receiver", "cmd", "data");

  // Set explicit deadline
  auto future_time = std::chrono::system_clock::now() + 500ms;
  msg.deadline = future_time;

  EXPECT_TRUE(msg.deadline.has_value());
  EXPECT_FALSE(msg.hasDeadlinePassed());

  auto time_remaining = msg.getTimeUntilDeadline();
  ASSERT_TRUE(time_remaining.has_value());
  EXPECT_GT(*time_remaining, 400ms);
}

/**
 * @brief Test deadline with priority messages
 */
TEST(DeadlineSchedulingTest, DeadlineWithPriority) {
  auto msg = KeystoneMessage::create("sender", "receiver", "cmd", "data");

  // HIGH priority with tight deadline
  msg.priority = Priority::HIGH;
  msg.setDeadlineFromNow(50ms);

  EXPECT_EQ(msg.priority, Priority::HIGH);
  EXPECT_TRUE(msg.deadline.has_value());
  EXPECT_FALSE(msg.hasDeadlinePassed());
}

/**
 * @brief Test multiple messages with different deadlines
 */
TEST(DeadlineSchedulingTest, MultipleMessagesWithDeadlines) {
  auto msg1 = KeystoneMessage::create("sender", "receiver", "cmd1", "data1");
  auto msg2 = KeystoneMessage::create("sender", "receiver", "cmd2", "data2");
  auto msg3 = KeystoneMessage::create("sender", "receiver", "cmd3", "data3");

  msg1.setDeadlineFromNow(50ms);
  msg2.setDeadlineFromNow(100ms);
  msg3.setDeadlineFromNow(150ms);

  // All should have time remaining
  EXPECT_TRUE(msg1.getTimeUntilDeadline().has_value());
  EXPECT_TRUE(msg2.getTimeUntilDeadline().has_value());
  EXPECT_TRUE(msg3.getTimeUntilDeadline().has_value());

  // msg1 should have shortest time
  EXPECT_LT(*msg1.getTimeUntilDeadline(), *msg2.getTimeUntilDeadline());
  EXPECT_LT(*msg2.getTimeUntilDeadline(), *msg3.getTimeUntilDeadline());

  // Wait for first deadline to pass
  std::this_thread::sleep_for(60ms);

  EXPECT_TRUE(msg1.hasDeadlinePassed());
  EXPECT_FALSE(msg2.hasDeadlinePassed());
  EXPECT_FALSE(msg3.hasDeadlinePassed());
}

/**
 * @brief Test deadline with enhanced message creation
 */
TEST(DeadlineSchedulingTest, DeadlineWithEnhancedMessage) {
  auto msg = KeystoneMessage::create("sender", "receiver", ActionType::EXECUTE,
                                     "session123", "payload data");

  msg.setDeadlineFromNow(100ms);

  EXPECT_EQ(msg.action_type, ActionType::EXECUTE);
  EXPECT_EQ(msg.session_id, "session123");
  EXPECT_TRUE(msg.deadline.has_value());
  EXPECT_FALSE(msg.hasDeadlinePassed());
}

/**
 * @brief Test clearing deadline
 */
TEST(DeadlineSchedulingTest, ClearDeadline) {
  auto msg = KeystoneMessage::create("sender", "receiver", "cmd", "data");

  msg.setDeadlineFromNow(100ms);
  EXPECT_TRUE(msg.deadline.has_value());

  // Clear deadline
  msg.deadline = std::nullopt;
  EXPECT_FALSE(msg.deadline.has_value());
  EXPECT_FALSE(msg.hasDeadlinePassed());
  EXPECT_FALSE(msg.getTimeUntilDeadline().has_value());
}

/**
 * @brief Test deadline comparison for scheduling
 */
TEST(DeadlineSchedulingTest, DeadlineComparison) {
  auto msg1 = KeystoneMessage::create("sender", "receiver", "urgent", "data");
  auto msg2 = KeystoneMessage::create("sender", "receiver", "normal", "data");

  msg1.setDeadlineFromNow(50ms);
  msg2.setDeadlineFromNow(200ms);

  // msg1 should be processed first (shorter deadline)
  EXPECT_LT(*msg1.getTimeUntilDeadline(), *msg2.getTimeUntilDeadline());
  EXPECT_TRUE(msg1.deadline < msg2.deadline);
}

/**
 * @brief Test deadline-based urgency
 */
TEST(DeadlineSchedulingTest, DeadlineBasedUrgency) {
  auto msg = KeystoneMessage::create("sender", "receiver", "cmd", "data");

  // Message with 10ms deadline is very urgent
  msg.setDeadlineFromNow(10ms);
  auto remaining = msg.getTimeUntilDeadline();
  ASSERT_TRUE(remaining.has_value());
  EXPECT_LE(*remaining, 10ms);

  // Wait 5ms
  std::this_thread::sleep_for(5ms);

  // Should be even more urgent now
  auto remaining_after = msg.getTimeUntilDeadline();
  ASSERT_TRUE(remaining_after.has_value());
  EXPECT_LT(*remaining_after, *remaining);
}
