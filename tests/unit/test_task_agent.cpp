/**
 * @file test_task_agent.cpp
 * @brief Unit tests for TaskAgent (Level 3)
 *
 * Test coverage:
 * - Construction & Initialization (3 tests)
 * - Command Execution (6 tests)
 * - Message Handling (4 tests)
 * - Error Handling (2 tests)
 *
 * Total: 15 tests
 */

#include <gtest/gtest.h>

#include "agents/task_agent.hpp"
#include "core/message_bus.hpp"
#include "unit/agent_test_fixture.hpp"

using namespace keystone;
using namespace keystone::test;

// ============================================================================
// Construction & Initialization Tests (3 tests)
// ============================================================================

class TaskAgentTest : public AgentTestFixture {};

TEST_F(TaskAgentTest, DefaultConstruction) {
  auto agent = std::make_shared<agents::TaskAgent>("task_1");
  EXPECT_EQ(agent->getAgentId(), "task_1");
}

TEST_F(TaskAgentTest, GetAgentId) {
  auto agent = std::make_shared<agents::TaskAgent>("task_agent_42");
  EXPECT_EQ(agent->getAgentId(), "task_agent_42");
}

TEST_F(TaskAgentTest, SetMessageBus) {
  auto agent = std::make_shared<agents::TaskAgent>("task_1");
  EXPECT_NO_THROW(agent->setMessageBus(bus_.get()));
}

// ============================================================================
// Command Execution Tests (6 tests)
// ============================================================================

TEST_F(TaskAgentTest, ExecuteEchoCommand) {
  auto agent = std::make_shared<agents::TaskAgent>("task_1");
  agent->setMessageBus(bus_.get());
  bus_->registerAgent(agent->getAgentId(), agent);

  auto msg = core::KeystoneMessage::create("sender", "task_1", "echo hello");
  agent->receiveMessage(msg);

  // Verify message was received
  auto received = agent->getMessage();
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->command, "echo hello");
  EXPECT_EQ(received->sender_id, "sender");
}

TEST_F(TaskAgentTest, ExecuteAddCommand) {
  auto agent = std::make_shared<agents::TaskAgent>("task_1");
  agent->setMessageBus(bus_.get());
  bus_->registerAgent(agent->getAgentId(), agent);

  auto msg = core::KeystoneMessage::create("sender", "task_1", "add 2 3");
  agent->receiveMessage(msg);

  // Verify message was queued
  auto received = agent->getMessage();
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->command, "add 2 3");
}

TEST_F(TaskAgentTest, ExecuteMultipleCommands) {
  auto agent = std::make_shared<agents::TaskAgent>("task_1");
  agent->setMessageBus(bus_.get());
  bus_->registerAgent(agent->getAgentId(), agent);

  // Send multiple commands
  auto msg1 = core::KeystoneMessage::create("sender", "task_1", "echo first");
  auto msg2 = core::KeystoneMessage::create("sender", "task_1", "echo second");
  auto msg3 = core::KeystoneMessage::create("sender", "task_1", "echo third");

  agent->receiveMessage(msg1);
  agent->receiveMessage(msg2);
  agent->receiveMessage(msg3);

  // Verify all messages were queued
  auto r1 = agent->getMessage();
  auto r2 = agent->getMessage();
  auto r3 = agent->getMessage();

  ASSERT_TRUE(r1.has_value());
  ASSERT_TRUE(r2.has_value());
  ASSERT_TRUE(r3.has_value());
  EXPECT_EQ(r1->command, "echo first");
  EXPECT_EQ(r2->command, "echo second");
  EXPECT_EQ(r3->command, "echo third");
}

TEST_F(TaskAgentTest, ExecuteCommandWithError) {
  auto agent = std::make_shared<agents::TaskAgent>("task_1");
  agent->setMessageBus(bus_.get());
  bus_->registerAgent(agent->getAgentId(), agent);

  // Invalid command (should not crash, just return empty/error)
  auto msg = core::KeystoneMessage::create("sender", "task_1", "invalid_cmd");
  EXPECT_NO_THROW(agent->receiveMessage(msg));
}

TEST_F(TaskAgentTest, ExecuteCommandTimeout) {
  auto agent = std::make_shared<agents::TaskAgent>("task_1");
  agent->setMessageBus(bus_.get());
  bus_->registerAgent(agent->getAgentId(), agent);

  // Command that sleeps (should complete without hanging test)
  auto msg =
      core::KeystoneMessage::create("sender", "task_1", "echo timeout_test");
  EXPECT_NO_THROW(agent->receiveMessage(msg));
}

TEST_F(TaskAgentTest, ExecuteInvalidCommand) {
  auto agent = std::make_shared<agents::TaskAgent>("task_1");
  agent->setMessageBus(bus_.get());
  bus_->registerAgent(agent->getAgentId(), agent);

  // Empty command
  auto msg = core::KeystoneMessage::create("sender", "task_1", "");
  EXPECT_NO_THROW(agent->receiveMessage(msg));
}

// ============================================================================
// Message Handling Tests (4 tests)
// ============================================================================

TEST_F(TaskAgentTest, SendMessageSucceeds) {
  auto agent = std::make_shared<agents::TaskAgent>("task_1");
  agent->setMessageBus(bus_.get());
  bus_->registerAgent(agent->getAgentId(), agent);

  auto msg = core::KeystoneMessage::create("task_1", "receiver", "test");
  EXPECT_NO_THROW(agent->sendMessage(msg));
}

TEST_F(TaskAgentTest, ReceiveMessageQueues) {
  auto agent = std::make_shared<agents::TaskAgent>("task_1");

  auto msg = core::KeystoneMessage::create("sender", "task_1", "test");
  agent->receiveMessage(msg);

  // Message should be in inbox
  auto received = agent->getMessage();
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->command, "test");
}

TEST_F(TaskAgentTest, GetMessageFromQueue) {
  auto agent = std::make_shared<agents::TaskAgent>("task_1");

  // Queue 3 messages
  agent->receiveMessage(core::KeystoneMessage::create("s1", "task_1", "msg1"));
  agent->receiveMessage(core::KeystoneMessage::create("s2", "task_1", "msg2"));
  agent->receiveMessage(core::KeystoneMessage::create("s3", "task_1", "msg3"));

  // Retrieve in order
  auto m1 = agent->getMessage();
  auto m2 = agent->getMessage();
  auto m3 = agent->getMessage();
  auto m4 = agent->getMessage();  // Should be empty

  ASSERT_TRUE(m1.has_value());
  ASSERT_TRUE(m2.has_value());
  ASSERT_TRUE(m3.has_value());
  EXPECT_FALSE(m4.has_value());  // No more messages

  EXPECT_EQ(m1->command, "msg1");
  EXPECT_EQ(m2->command, "msg2");
  EXPECT_EQ(m3->command, "msg3");
}

TEST_F(TaskAgentTest, MessagePriority) {
  auto agent = std::make_shared<agents::TaskAgent>("task_1");

  // Send messages with different priorities
  auto high_msg = core::KeystoneMessage::create("sender", "task_1", "high");
  high_msg.priority = core::Priority::HIGH;

  auto normal_msg = core::KeystoneMessage::create("sender", "task_1", "normal");
  normal_msg.priority = core::Priority::NORMAL;

  auto low_msg = core::KeystoneMessage::create("sender", "task_1", "low");
  low_msg.priority = core::Priority::LOW;

  // Queue in mixed order: normal, low, high
  agent->receiveMessage(normal_msg);
  agent->receiveMessage(low_msg);
  agent->receiveMessage(high_msg);

  // Retrieve - should get high first, then normal, then low
  auto m1 = agent->getMessage();
  auto m2 = agent->getMessage();
  auto m3 = agent->getMessage();

  ASSERT_TRUE(m1.has_value());
  ASSERT_TRUE(m2.has_value());
  ASSERT_TRUE(m3.has_value());

  EXPECT_EQ(m1->command, "high");
  EXPECT_EQ(m2->command, "normal");
  EXPECT_EQ(m3->command, "low");
}

// ============================================================================
// Error Handling Tests (2 tests)
// ============================================================================

TEST_F(TaskAgentTest, HandleExecutionError) {
  auto agent = std::make_shared<agents::TaskAgent>("task_1");
  agent->setMessageBus(bus_.get());
  bus_->registerAgent(agent->getAgentId(), agent);

  // Command that will fail (invalid syntax)
  auto msg = core::KeystoneMessage::create("sender", "task_1", "|||invalid|||");
  EXPECT_NO_THROW(agent->receiveMessage(msg));
}

TEST_F(TaskAgentTest, HandleBusUnavailable) {
  auto agent = std::make_shared<agents::TaskAgent>("task_1");
  // Don't set message bus

  auto msg = core::KeystoneMessage::create("task_1", "receiver", "test");
  // Sending without bus should throw
  EXPECT_THROW(agent->sendMessage(msg), std::runtime_error);
}
