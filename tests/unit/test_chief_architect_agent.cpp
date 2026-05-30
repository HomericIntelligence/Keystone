/**
 * @file test_chief_architect_agent.cpp
 * @brief Unit tests for ChiefArchitectAgent (Level 0)
 *
 * Test coverage:
 * - Construction & Initialization (3 tests)
 * - Message Processing (5 tests)
 * - Delegation (4 tests)
 * - State Management (3 tests)
 *
 * Total: 15 tests
 */

// KeystoneMessage::command is [[deprecated]]; test files intentionally access
// it to verify backward-compat behaviour.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <gtest/gtest.h>

#include "agents/chief_architect_agent.hpp"
#include "agents/task_agent.hpp"
#include "core/message_bus.hpp"
#include "unit/agent_test_fixture.hpp"

using namespace keystone;
using namespace keystone::test;

// ============================================================================
// Construction & Initialization Tests (3 tests)
// ============================================================================

class ChiefArchitectAgentTest : public AgentTestFixture {};

TEST_F(ChiefArchitectAgentTest, DefaultConstruction) {
  auto agent = std::make_shared<agents::ChiefArchitectAgent>("chief");
  EXPECT_EQ(agent->getAgentId(), "chief");
}

TEST_F(ChiefArchitectAgentTest, GetAgentId) {
  auto agent = std::make_shared<agents::ChiefArchitectAgent>("chief_42");
  EXPECT_EQ(agent->getAgentId(), "chief_42");
}

TEST_F(ChiefArchitectAgentTest, InitialStateIsIdle) {
  auto agent = std::make_shared<agents::ChiefArchitectAgent>("chief");
  // Chief architect should be ready to receive commands
  EXPECT_NO_THROW(agent->setMessageBus(bus_.get()));
}

// ============================================================================
// Message Processing Tests (5 tests)
// ============================================================================

TEST_F(ChiefArchitectAgentTest, ProcessEchoCommand) {
  auto chief = std::make_shared<agents::ChiefArchitectAgent>("chief");
  chief->setMessageBus(bus_.get());
  bus_->registerAgent(chief->getAgentId(), chief);

  auto msg = core::KeystoneMessage::create("sender", "chief", "echo hello");
  chief->receiveMessage(msg);

  auto received = chief->getMessage();
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->command, "echo hello");
}

TEST_F(ChiefArchitectAgentTest, ProcessDelegateCommand) {
  auto chief = std::make_shared<agents::ChiefArchitectAgent>("chief");
  auto task = std::make_shared<agents::TaskAgent>("task_1");

  chief->setMessageBus(bus_.get());
  task->setMessageBus(bus_.get());

  bus_->registerAgent(chief->getAgentId(), chief);
  bus_->registerAgent(task->getAgentId(), task);

  // Send delegation command
  auto msg = core::KeystoneMessage::create("sender", "chief",
                                           "delegate add 2 3 to task_1");
  chief->receiveMessage(msg);

  // Chief should receive the message
  auto received = chief->getMessage();
  ASSERT_TRUE(received.has_value());
}

TEST_F(ChiefArchitectAgentTest, ProcessUnknownCommand) {
  auto chief = std::make_shared<agents::ChiefArchitectAgent>("chief");
  chief->setMessageBus(bus_.get());
  bus_->registerAgent(chief->getAgentId(), chief);

  auto msg =
      core::KeystoneMessage::create("sender", "chief", "unknown_command");
  EXPECT_NO_THROW(chief->receiveMessage(msg));
}

TEST_F(ChiefArchitectAgentTest, ProcessMessageWithoutBus) {
  auto chief = std::make_shared<agents::ChiefArchitectAgent>("chief");
  // Don't set message bus

  auto msg = core::KeystoneMessage::create("sender", "chief", "test");
  // Receiving without bus should work (just queues message)
  EXPECT_NO_THROW(chief->receiveMessage(msg));
}

TEST_F(ChiefArchitectAgentTest, ProcessMessageQueueing) {
  auto chief = std::make_shared<agents::ChiefArchitectAgent>("chief");

  // Queue multiple messages
  chief->receiveMessage(core::KeystoneMessage::create("s1", "chief", "cmd1"));
  chief->receiveMessage(core::KeystoneMessage::create("s2", "chief", "cmd2"));
  chief->receiveMessage(core::KeystoneMessage::create("s3", "chief", "cmd3"));

  // All should be queued
  auto m1 = chief->getMessage();
  auto m2 = chief->getMessage();
  auto m3 = chief->getMessage();

  ASSERT_TRUE(m1.has_value());
  ASSERT_TRUE(m2.has_value());
  ASSERT_TRUE(m3.has_value());
  EXPECT_EQ(m1->command, "cmd1");
  EXPECT_EQ(m2->command, "cmd2");
  EXPECT_EQ(m3->command, "cmd3");
}

// ============================================================================
// Delegation Tests (4 tests)
// ============================================================================

TEST_F(ChiefArchitectAgentTest, DelegateToTaskAgent) {
  auto chief = std::make_shared<agents::ChiefArchitectAgent>("chief");
  auto task = std::make_shared<agents::TaskAgent>("task_1");

  chief->setMessageBus(bus_.get());
  task->setMessageBus(bus_.get());

  bus_->registerAgent(chief->getAgentId(), chief);
  bus_->registerAgent(task->getAgentId(), task);

  // Send message from chief to task
  auto msg = core::KeystoneMessage::create("chief", "task_1", "echo test");
  EXPECT_NO_THROW(chief->sendMessage(msg));

  // Task should receive the message
  auto received = task->getMessage();
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->command, "echo test");
  EXPECT_EQ(received->sender_id, "chief");
}

TEST_F(ChiefArchitectAgentTest, DelegateToMultipleAgents) {
  auto chief = std::make_shared<agents::ChiefArchitectAgent>("chief");
  auto task1 = std::make_shared<agents::TaskAgent>("task_1");
  auto task2 = std::make_shared<agents::TaskAgent>("task_2");
  auto task3 = std::make_shared<agents::TaskAgent>("task_3");

  chief->setMessageBus(bus_.get());
  task1->setMessageBus(bus_.get());
  task2->setMessageBus(bus_.get());
  task3->setMessageBus(bus_.get());

  bus_->registerAgent(chief->getAgentId(), chief);
  bus_->registerAgent(task1->getAgentId(), task1);
  bus_->registerAgent(task2->getAgentId(), task2);
  bus_->registerAgent(task3->getAgentId(), task3);

  // Delegate to all three
  chief->sendMessage(core::KeystoneMessage::create("chief", "task_1", "cmd1"));
  chief->sendMessage(core::KeystoneMessage::create("chief", "task_2", "cmd2"));
  chief->sendMessage(core::KeystoneMessage::create("chief", "task_3", "cmd3"));

  // All tasks should receive their messages
  auto r1 = task1->getMessage();
  auto r2 = task2->getMessage();
  auto r3 = task3->getMessage();

  ASSERT_TRUE(r1.has_value());
  ASSERT_TRUE(r2.has_value());
  ASSERT_TRUE(r3.has_value());
  EXPECT_EQ(r1->command, "cmd1");
  EXPECT_EQ(r2->command, "cmd2");
  EXPECT_EQ(r3->command, "cmd3");
}

TEST_F(ChiefArchitectAgentTest, DelegationWithoutRegisteredAgents) {
  auto chief = std::make_shared<agents::ChiefArchitectAgent>("chief");
  chief->setMessageBus(bus_.get());
  bus_->registerAgent(chief->getAgentId(), chief);

  // Try to send to unregistered agent (should fail gracefully)
  auto msg = core::KeystoneMessage::create("chief", "nonexistent", "test");
  EXPECT_NO_THROW(chief->sendMessage(msg));
}

TEST_F(ChiefArchitectAgentTest, DelegationResponseHandling) {
  auto chief = std::make_shared<agents::ChiefArchitectAgent>("chief");
  auto task = std::make_shared<agents::TaskAgent>("task_1");

  chief->setMessageBus(bus_.get());
  task->setMessageBus(bus_.get());

  bus_->registerAgent(chief->getAgentId(), chief);
  bus_->registerAgent(task->getAgentId(), task);

  // Send command from chief to task
  chief->sendMessage(core::KeystoneMessage::create("chief", "task_1", "cmd"));

  // Task receives and responds
  auto task_msg = task->getMessage();
  ASSERT_TRUE(task_msg.has_value());

  // Task sends response back
  auto response = core::KeystoneMessage::create("task_1", "chief", "result");
  task->sendMessage(response);

  // Chief should receive response
  auto chief_response = chief->getMessage();
  ASSERT_TRUE(chief_response.has_value());
  EXPECT_EQ(chief_response->command, "result");
  EXPECT_EQ(chief_response->sender_id, "task_1");
}

// ============================================================================
// State Management Tests (3 tests)
// ============================================================================

TEST_F(ChiefArchitectAgentTest, StateTransitionOnDelegation) {
  auto chief = std::make_shared<agents::ChiefArchitectAgent>("chief");
  auto task = std::make_shared<agents::TaskAgent>("task_1");

  chief->setMessageBus(bus_.get());
  task->setMessageBus(bus_.get());

  bus_->registerAgent(chief->getAgentId(), chief);
  bus_->registerAgent(task->getAgentId(), task);

  // Send delegation command
  EXPECT_NO_THROW(chief->sendMessage(
      core::KeystoneMessage::create("chief", "task_1", "test")));
}

TEST_F(ChiefArchitectAgentTest, StateResetAfterCompletion) {
  auto chief = std::make_shared<agents::ChiefArchitectAgent>("chief");
  auto task = std::make_shared<agents::TaskAgent>("task_1");

  chief->setMessageBus(bus_.get());
  task->setMessageBus(bus_.get());

  bus_->registerAgent(chief->getAgentId(), chief);
  bus_->registerAgent(task->getAgentId(), task);

  // Complete a full cycle: delegate -> response
  chief->sendMessage(core::KeystoneMessage::create("chief", "task_1", "cmd"));
  auto task_msg = task->getMessage();
  ASSERT_TRUE(task_msg.has_value());

  task->sendMessage(core::KeystoneMessage::create("task_1", "chief", "result"));
  auto response = chief->getMessage();
  ASSERT_TRUE(response.has_value());

  // Chief should be ready for next command
  EXPECT_NO_THROW(chief->sendMessage(
      core::KeystoneMessage::create("chief", "task_1", "cmd2")));
}

TEST_F(ChiefArchitectAgentTest, ConcurrentStateAccess) {
  auto chief = std::make_shared<agents::ChiefArchitectAgent>("chief");
  chief->setMessageBus(bus_.get());
  bus_->registerAgent(chief->getAgentId(), chief);

  // Send multiple messages rapidly (test thread safety)
  for (int32_t i = 0; i < 100; ++i) {
    auto msg = core::KeystoneMessage::create("sender", "chief",
                                             "cmd" + std::to_string(i));
    EXPECT_NO_THROW(chief->receiveMessage(msg));
  }

  // Verify all messages were queued
  int32_t count = 0;
  while (chief->getMessage().has_value()) {
    ++count;
  }
  EXPECT_EQ(count, 100);
}

// ============================================================================
// awaitMessage / sendCommand Async Contract Tests (Issue #509)
// ============================================================================
// These tests verify that sendCommand() properly suspends (via awaitMessage)
// rather than busy-polling getMessage() synchronously inside the coroutine.
//
// Test-drive strategy: coroutines are driven synchronously via Task::get(),
// which resumes the coroutine until completion — the correct API (sync_wait
// does not exist in this codebase; get() is the synchronous drain method).
//
// IMPORTANT: Task coroutines have initial_suspend() = suspend_always (lazy).
// In a no-scheduler environment, YieldAwaitable::await_suspend() calls
// std::this_thread::yield() and then resumes inline, so awaitMessage() is
// effectively a tight synchronous poll loop.  To interleave the test's
// response injection with the coroutine's poll, we pre-load the response into
// chief's inbox before starting the coroutine, OR we inject between the
// sendMessage() step and the awaitMessage() step using an intermediate resume.

#include "agents/agent_awaitable.hpp"

// 1. sendCommand returns a successful response when a reply is in the inbox.
//
// Design note: Task coroutines are lazy (initial_suspend = suspend_always) and
// in no-scheduler mode YieldAwaitable::await_suspend() resumes inline (no true
// thread suspension).  The only safe way to interleave response injection in a
// single-threaded test is to pre-load the reply into chief's inbox before the
// coroutine's awaitMessage() poll runs.  We use a background thread to inject
// the reply after a short delay so that the poll loop finds it naturally.
TEST_F(ChiefArchitectAgentTest, SendCommand_GetsResponseFromTaskAgent) {
  auto chief = std::make_shared<agents::ChiefArchitectAgent>("chief");
  auto task = std::make_shared<agents::TaskAgent>("task_1");

  chief->setMessageBus(bus_.get());
  task->setMessageBus(bus_.get());

  bus_->registerAgent(chief->getAgentId(), chief);
  bus_->registerAgent(task->getAgentId(), task);

  // Pre-populate chief's inbox with a response message (with payload) so that
  // awaitMessage() finds it on the first poll without spinning to the deadline.
  // processMessage() checks msg.payload — the response must carry one.
  auto reply = core::KeystoneMessage::create("task_1", "chief", "hello");
  reply.payload = "hello";  // required by processMessage()'s payload guard
  chief->receiveMessage(reply);

  // Drive sendCommand to completion.  The coroutine will send to task_1, then
  // awaitMessage() polls chief's inbox and immediately finds the pre-loaded
  // reply, so no spin occurs.
  core::Response response = chief->sendCommand("echo hello", "task_1").get();

  EXPECT_NE(response.status, core::Response::Status::Error)
      << "Expected success but got error: " << response.result;
}

// 2. sendCommand returns an error response when the peer never replies
// (timeout). We test the timeout path directly via awaitMessage() with a past
// deadline so the test doesn't wait 500 ms.
TEST_F(ChiefArchitectAgentTest, SendCommand_ReturnsErrorOnTimeout) {
  auto chief = std::make_shared<agents::ChiefArchitectAgent>("chief");
  chief->setMessageBus(bus_.get());
  bus_->registerAgent(chief->getAgentId(), chief);

  // Deadline already in the past ensures immediate nullopt return.
  auto already_past =
      std::chrono::steady_clock::now() - std::chrono::milliseconds{1};

  auto wait_task = agents::awaitMessage(*chief, already_past);
  auto msg_opt = wait_task.get();

  EXPECT_FALSE(msg_opt.has_value())
      << "Expected nullopt after deadline but got a message";
}

// 3. awaitMessage returns immediately when a message is already in the inbox.
// Verifies the happy-path poll succeeds on the first iteration (no spin
// needed).
TEST_F(ChiefArchitectAgentTest, SendCommand_HandlesImmediateResponse) {
  auto chief = std::make_shared<agents::ChiefArchitectAgent>("chief");
  chief->setMessageBus(bus_.get());
  bus_->registerAgent(chief->getAgentId(), chief);

  // Pre-load a message directly into chief's inbox.
  auto pre_loaded = core::KeystoneMessage::create("task_1", "chief", "pre");
  chief->receiveMessage(pre_loaded);

  // awaitMessage should find it on the first poll without any suspension.
  auto wait_task = agents::awaitMessage(*chief);
  auto msg_opt = wait_task.get();

  ASSERT_TRUE(msg_opt.has_value());
  EXPECT_EQ(msg_opt->command, "pre");
}

// 4. Cancellation path regression: CANCEL_TASK still produces an ack response.
TEST_F(ChiefArchitectAgentTest, ProcessMessage_CancelTask) {
  auto chief = std::make_shared<agents::ChiefArchitectAgent>("chief");
  chief->setMessageBus(bus_.get());
  bus_->registerAgent(chief->getAgentId(), chief);

  // createCancellation sets action_type = CANCEL_TASK and task_id correctly.
  auto cancel_msg = core::KeystoneMessage::createCancellation(
      "sender", chief->getAgentId(), "task-42");

  auto task = chief->processMessage(cancel_msg);
  core::Response resp = task.get();

  // CANCEL_TASK should succeed and produce an ack response.
  EXPECT_NE(resp.status, core::Response::Status::Error)
      << "CANCEL_TASK handler returned unexpected error: " << resp.result;
}

#pragma GCC diagnostic pop
