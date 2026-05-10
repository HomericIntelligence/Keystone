/**
 * @file task_cancellation_test.cpp
 * @brief E2E tests for task cancellation notification (Issue #52)
 *
 * Tests the complete cancellation flow:
 * 1. Parent agent sends task to child agent
 * 2. Parent decides to cancel the task
 * 3. Parent sends CANCEL_TASK message with task_id
 * 4. Child receives cancellation and marks task as cancelled
 * 5. Child acknowledges cancellation
 * 6. Parent receives acknowledgement
 */

// KeystoneMessage::command is [[deprecated]]; test files intentionally access
// it to verify backward-compat behaviour.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "agents/chief_architect_agent.hpp"
#include "agents/task_agent.hpp"
#include "core/message_bus.hpp"

using namespace keystone::agents;
using namespace keystone::core;

/**
 * @brief E2E Test: Parent cancels task in child agent
 *
 * Flow:
 *   1. ChiefArchitect (parent) sends task to TaskAgent (child)
 *   2. ChiefArchitect sends CANCEL_TASK message with task_id
 *   3. TaskAgent marks task as cancelled
 *   4. TaskAgent sends acknowledgement back
 *   5. ChiefArchitect receives acknowledgement
 */
TEST(E2E_TaskCancellation, ParentCancelsChildTask) {
  // ARRANGE: Create MessageBus and agents
  auto bus = std::make_unique<MessageBus>();
  auto chief = std::make_shared<ChiefArchitectAgent>("chief");
  auto task_agent = std::make_shared<TaskAgent>("task_agent");

  // Register agents with bus
  bus->registerAgent(chief->getAgentId(), chief);
  bus->registerAgent(task_agent->getAgentId(), task_agent);

  // Set message bus for each agent
  chief->setMessageBus(bus.get());
  task_agent->setMessageBus(bus.get());

  // Define a task ID for tracking
  const std::string task_id = "task_12345";

  // ACT: Parent sends cancellation request
  auto cancel_msg = KeystoneMessage::createCancellation(
      chief->getAgentId(), task_agent->getAgentId(), task_id);

  // Chief sends cancellation message
  chief->sendMessage(cancel_msg);

  // TaskAgent receives and processes the cancellation
  auto received_msg = task_agent->getMessage();
  ASSERT_TRUE(received_msg.has_value())
      << "TaskAgent should receive cancellation message";
  EXPECT_EQ(received_msg->action_type, ActionType::CANCEL_TASK);
  ASSERT_TRUE(received_msg->task_id.has_value());
  EXPECT_EQ(*received_msg->task_id, task_id);

  // TaskAgent processes the message
  auto response = task_agent->processMessage(*received_msg).get();

  // ASSERT: Verify cancellation was processed
  EXPECT_EQ(response.status, Response::Status::Success);
  EXPECT_TRUE(response.result.find(task_id) != std::string::npos);
  EXPECT_TRUE(response.result.find("cancellation") != std::string::npos);

  // Verify task is marked as cancelled in TaskAgent
  EXPECT_TRUE(task_agent->isCancelled(task_id));

  // Verify acknowledgement was sent back to Chief
  auto chief_msg = chief->getMessage();
  ASSERT_TRUE(chief_msg.has_value()) << "Chief should receive acknowledgement";
}

/**
 * @brief E2E Test: Multiple tasks, cancel only one
 */
TEST(E2E_TaskCancellation, CancelSpecificTask) {
  // ARRANGE: Create MessageBus and agents
  auto bus = std::make_unique<MessageBus>();
  auto chief = std::make_shared<ChiefArchitectAgent>("chief");
  auto task_agent = std::make_shared<TaskAgent>("task_agent");

  bus->registerAgent(chief->getAgentId(), chief);
  bus->registerAgent(task_agent->getAgentId(), task_agent);

  chief->setMessageBus(bus.get());
  task_agent->setMessageBus(bus.get());

  // Define multiple task IDs
  const std::string task1 = "task_001";
  const std::string task2 = "task_002";
  const std::string task3 = "task_003";

  // ACT: Cancel only task2
  auto cancel_msg = KeystoneMessage::createCancellation(
      chief->getAgentId(), task_agent->getAgentId(), task2);

  chief->sendMessage(cancel_msg);

  // TaskAgent receives and processes
  auto received_msg = task_agent->getMessage();
  ASSERT_TRUE(received_msg.has_value());
  auto response = task_agent->processMessage(*received_msg).get();

  // ASSERT: Only task2 is cancelled
  EXPECT_FALSE(task_agent->isCancelled(task1));
  EXPECT_TRUE(task_agent->isCancelled(task2));
  EXPECT_FALSE(task_agent->isCancelled(task3));
}

/**
 * @brief E2E Test: Cancellation message has HIGH priority
 */
TEST(E2E_TaskCancellation, CancellationHasHighPriority) {
  // ARRANGE
  auto bus = std::make_unique<MessageBus>();
  auto chief = std::make_shared<ChiefArchitectAgent>("chief");
  auto task_agent = std::make_shared<TaskAgent>("task_agent");

  bus->registerAgent(chief->getAgentId(), chief);
  bus->registerAgent(task_agent->getAgentId(), task_agent);

  chief->setMessageBus(bus.get());
  task_agent->setMessageBus(bus.get());

  // Send a LOW priority message first
  auto low_msg = KeystoneMessage::create(
      chief->getAgentId(), task_agent->getAgentId(), "echo 'low priority'");
  low_msg.priority = Priority::LOW;
  chief->sendMessage(low_msg);

  // Send a HIGH priority cancellation message
  auto cancel_msg = KeystoneMessage::createCancellation(
      chief->getAgentId(), task_agent->getAgentId(), "task_urgent");

  chief->sendMessage(cancel_msg);

  // ACT: TaskAgent should process HIGH priority cancellation first
  auto first_msg = task_agent->getMessage();
  ASSERT_TRUE(first_msg.has_value());

  // ASSERT: First message should be the cancellation (HIGH priority)
  EXPECT_EQ(first_msg->priority, Priority::HIGH);
  EXPECT_EQ(first_msg->action_type, ActionType::CANCEL_TASK);

  // Second message should be the LOW priority one
  auto second_msg = task_agent->getMessage();
  ASSERT_TRUE(second_msg.has_value());
  EXPECT_EQ(second_msg->priority, Priority::LOW);
}

/**
 * @brief E2E Test: Agent checks cancellation status during long operation
 *
 * Simulates a long-running task that checks for cancellation periodically.
 */
TEST(E2E_TaskCancellation, AgentChecksForCancellationDuringWork) {
  // ARRANGE
  auto bus = std::make_unique<MessageBus>();
  auto task_agent = std::make_shared<TaskAgent>("task_agent");

  bus->registerAgent(task_agent->getAgentId(), task_agent);
  task_agent->setMessageBus(bus.get());

  const std::string task_id = "long_task";

  // Simulate starting a long-running task
  std::atomic<bool> task_completed{false};
  std::atomic<bool> task_cancelled{false};

  std::thread worker([&]() {
    // Simulate work in chunks, checking for cancellation
    for (int32_t i = 0; i < 100; ++i) {
      // Check if task was cancelled
      if (task_agent->isCancelled(task_id)) {
        task_cancelled = true;
        return;  // Exit early
      }

      // Simulate work
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    task_completed = true;
  });

  // Let task start
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // ACT: Request cancellation
  task_agent->requestCancellation(task_id);

  // Wait for worker to finish
  worker.join();

  // ASSERT: Task should have been cancelled, not completed
  EXPECT_FALSE(task_completed);
  EXPECT_TRUE(task_cancelled);
}

/**
 * @brief E2E Test: Cancel then clear allows new task with same ID
 */
TEST(E2E_TaskCancellation, ClearCancellationAllowsReuse) {
  // ARRANGE
  auto task_agent = std::make_shared<TaskAgent>("task_agent");
  const std::string task_id = "reusable_task";

  // ACT: Cancel task
  task_agent->requestCancellation(task_id);
  EXPECT_TRUE(task_agent->isCancelled(task_id));

  // Clear cancellation
  task_agent->clearCancellation(task_id);

  // ASSERT: Task ID can be reused
  EXPECT_FALSE(task_agent->isCancelled(task_id));

  // New task with same ID should not be cancelled
  task_agent->requestCancellation("other_task");
  EXPECT_FALSE(task_agent->isCancelled(task_id));
  EXPECT_TRUE(task_agent->isCancelled("other_task"));
}

/**
 * @brief E2E Test: Cancellation message missing task_id returns error
 */
TEST(E2E_TaskCancellation, MissingTaskIdReturnsError) {
  // ARRANGE
  auto bus = std::make_unique<MessageBus>();
  auto chief = std::make_shared<ChiefArchitectAgent>("chief");
  auto task_agent = std::make_shared<TaskAgent>("task_agent");

  bus->registerAgent(chief->getAgentId(), chief);
  bus->registerAgent(task_agent->getAgentId(), task_agent);

  chief->setMessageBus(bus.get());
  task_agent->setMessageBus(bus.get());

  // Create malformed CANCEL_TASK message without task_id
  KeystoneMessage bad_msg;
  bad_msg.msg_id = "msg_bad";
  bad_msg.sender_id = chief->getAgentId();
  bad_msg.receiver_id = task_agent->getAgentId();
  bad_msg.action_type = ActionType::CANCEL_TASK;
  bad_msg.command = "CANCEL_TASK";
  bad_msg.timestamp = std::chrono::system_clock::now();
  bad_msg.priority = Priority::HIGH;
  bad_msg.session_id = "default";
  bad_msg.content_type = ContentType::TEXT_PLAIN;
  // task_id is NOT set

  // ACT: Send malformed message
  chief->sendMessage(bad_msg);

  auto received = task_agent->getMessage();
  ASSERT_TRUE(received.has_value());

  auto response = task_agent->processMessage(*received).get();

  // ASSERT: Should return error
  EXPECT_EQ(response.status, Response::Status::Error);
  EXPECT_TRUE(response.result.find("missing task_id") != std::string::npos);
}

#pragma GCC diagnostic pop
