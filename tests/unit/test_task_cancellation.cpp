/**
 * @file test_task_cancellation.cpp
 * @brief Unit tests for task cancellation notification (Issue #52, #515)
 *
 * Tests the cancellation protocol via AgentEnvelope (Issue #515: CANCEL_TASK
 * moved from core::ActionType to agents::AgentActionType):
 * - AgentEnvelope::createCancellation() factory method
 * - AgentCore::requestCancellation() / isCancelled() / clearCancellation()
 * - AsyncAgent::handleCancellation(AgentEnvelope) helper
 * - MessageBus routing of cancellation messages
 */

// KeystoneMessage::command is [[deprecated]]; test files intentionally access
// it to verify backward-compat behaviour.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include "agents/agent_action_type.hpp"
#include "agents/agent_envelope.hpp"
#include "agents/async_agent.hpp"
#include "agents/task_agent.hpp"
#include "core/message.hpp"
#include "core/message_bus.hpp"

#include <gtest/gtest.h>

using namespace keystone::core;
using namespace keystone::agents;

/**
 * @brief Test: Create cancellation envelope with correct fields
 */
TEST(TaskCancellation, CreateCancellationEnvelope) {
  auto env = AgentEnvelope::createCancellation("parent", "child", "task_123");

  EXPECT_EQ(env.transport_msg.sender_id, "parent");
  EXPECT_EQ(env.transport_msg.receiver_id, "child");
  ASSERT_TRUE(env.agent_action.has_value());
  EXPECT_EQ(*env.agent_action, AgentActionType::CANCEL_TASK);
  EXPECT_EQ(env.transport_msg.priority, Priority::HIGH);
  ASSERT_TRUE(env.task_id.has_value());
  EXPECT_EQ(*env.task_id, "task_123");
  EXPECT_EQ(env.session_id, "default");
}

/**
 * @brief Test: Create cancellation envelope with custom session
 */
TEST(TaskCancellation, CreateCancellationEnvelopeWithSession) {
  auto env = AgentEnvelope::createCancellation("parent", "child", "task_456", "session_xyz");

  EXPECT_EQ(env.transport_msg.sender_id, "parent");
  EXPECT_EQ(env.transport_msg.receiver_id, "child");
  ASSERT_TRUE(env.agent_action.has_value());
  EXPECT_EQ(*env.agent_action, AgentActionType::CANCEL_TASK);
  ASSERT_TRUE(env.task_id.has_value());
  EXPECT_EQ(*env.task_id, "task_456");
  EXPECT_EQ(env.session_id, "session_xyz");
}

/**
 * @brief Test: AgentActionType::CANCEL_TASK converts to string correctly
 */
TEST(TaskCancellation, AgentActionTypeToString) {
  EXPECT_EQ(agentActionTypeToString(AgentActionType::CANCEL_TASK), "CANCEL_TASK");
}

/**
 * @brief Test: Agent cancellation tracking (single task)
 */
TEST(TaskCancellation, AgentCancellationTracking) {
  auto agent = std::make_shared<TaskAgent>("test_agent");

  // Initially, task is not cancelled
  EXPECT_FALSE(agent->isCancelled("task_1"));

  // Request cancellation
  agent->requestCancellation("task_1");
  EXPECT_TRUE(agent->isCancelled("task_1"));

  // Clear cancellation
  agent->clearCancellation("task_1");
  EXPECT_FALSE(agent->isCancelled("task_1"));
}

/**
 * @brief Test: Agent cancellation tracking (multiple tasks)
 */
TEST(TaskCancellation, AgentCancellationMultipleTasks) {
  auto agent = std::make_shared<TaskAgent>("test_agent");

  // Request cancellation for multiple tasks
  agent->requestCancellation("task_1");
  agent->requestCancellation("task_2");
  agent->requestCancellation("task_3");

  EXPECT_TRUE(agent->isCancelled("task_1"));
  EXPECT_TRUE(agent->isCancelled("task_2"));
  EXPECT_TRUE(agent->isCancelled("task_3"));
  EXPECT_FALSE(agent->isCancelled("task_4"));

  // Clear one task
  agent->clearCancellation("task_2");

  EXPECT_TRUE(agent->isCancelled("task_1"));
  EXPECT_FALSE(agent->isCancelled("task_2"));
  EXPECT_TRUE(agent->isCancelled("task_3"));
}

/**
 * @brief Test: MessageBus routes cancellation transport messages
 */
TEST(TaskCancellation, MessageBusRoutesCancellation) {
  MessageBus bus;
  auto parent = std::make_shared<TaskAgent>("parent");
  auto child = std::make_shared<TaskAgent>("child");

  bus.registerAgent(parent->getAgentId(), parent);
  bus.registerAgent(child->getAgentId(), child);

  parent->setMessageBus(&bus);
  child->setMessageBus(&bus);

  // Parent sends cancellation to child via AgentEnvelope
  auto env = AgentEnvelope::createCancellation("parent", "child", "task_xyz");
  EXPECT_TRUE(bus.routeMessage(env.transport_msg));

  // Child should receive the transport message; decode via wrap()
  auto received = child->getMessage();
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->sender_id, "parent");

  // Decode the envelope to verify CANCEL_TASK intent survived the round-trip
  auto decoded_env = AgentEnvelope::wrap(*received);
  ASSERT_TRUE(decoded_env.agent_action.has_value());
  EXPECT_EQ(*decoded_env.agent_action, AgentActionType::CANCEL_TASK);
  ASSERT_TRUE(decoded_env.task_id.has_value());
  EXPECT_EQ(*decoded_env.task_id, "task_xyz");
}

/**
 * @brief Test: Cancellation envelope has HIGH priority on transport message
 */
TEST(TaskCancellation, CancellationHasHighPriority) {
  auto env = AgentEnvelope::createCancellation("sender", "receiver", "task_1");
  EXPECT_EQ(env.transport_msg.priority, Priority::HIGH);
}

/**
 * @brief Test: AgentCore cancellation state via direct interface
 */
TEST(TaskCancellation, AsyncAgentHandlesCancellation) {
  auto agent = std::make_shared<TaskAgent>("test_agent");

  // Initially not cancelled
  EXPECT_FALSE(agent->isCancelled("task_abc"));

  // Use the public requestCancellation interface
  agent->requestCancellation("task_abc");

  // Now it should be cancelled
  EXPECT_TRUE(agent->isCancelled("task_abc"));
}

/**
 * @brief Test: Missing task_id in cancellation envelope returns error
 *
 * Construct a raw CANCEL_TASK-prefixed message without a task_id and verify
 * that processMessage returns an error when the envelope has no task_id.
 */
TEST(TaskCancellation, MissingTaskIdReturnsError) {
  MessageBus bus;
  auto agent = std::make_shared<TaskAgent>("test_agent");

  bus.registerAgent(agent->getAgentId(), agent);
  agent->setMessageBus(&bus);

  // Create a transport message whose payload has the CANCEL_TASK prefix
  // but no task_id following it (empty suffix).
  KeystoneMessage msg;
  msg.msg_id = "msg_1";
  msg.sender_id = "parent";
  msg.receiver_id = "test_agent";
  msg.action_type = ActionType::EXECUTE;
  msg.command = "CANCEL_TASK";
  msg.timestamp = std::chrono::system_clock::now();
  msg.priority = Priority::HIGH;
  msg.content_type = ContentType::TEXT_PLAIN;
  // Payload has the CANCEL_TASK prefix but empty task_id suffix
  msg.payload = "CANCEL_TASK:";

  // Process the message - should return error because task_id is empty
  auto response = agent->processMessage(msg).get();
  EXPECT_EQ(response.status, Response::Status::Error);
  EXPECT_TRUE(response.result.find("missing task_id") != std::string::npos);
}

/**
 * @brief Test: Thread-safe cancellation (concurrent access)
 */
TEST(TaskCancellation, ThreadSafeCancellation) {
  auto agent = std::make_shared<TaskAgent>("test_agent");

  // Launch multiple threads requesting cancellation
  std::vector<std::thread> threads;
  for (int32_t i = 0; i < 10; ++i) {
    threads.emplace_back([&agent, i]() {
      std::string task_id = "task_" + std::to_string(i);
      agent->requestCancellation(task_id);
    });
  }

  // Wait for all threads
  for (auto& t : threads) {
    t.join();
  }

  // All tasks should be cancelled
  for (int32_t i = 0; i < 10; ++i) {
    std::string task_id = "task_" + std::to_string(i);
    EXPECT_TRUE(agent->isCancelled(task_id));
  }
}

#pragma GCC diagnostic pop
