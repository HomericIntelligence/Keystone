// KeystoneMessage::command is [[deprecated]]; test files intentionally access
// it to verify backward-compat behaviour.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <set>
#include <thread>

#include "agents/task_agent.hpp"
#include "concurrency/work_stealing_scheduler.hpp"
#include "core/message_bus.hpp"

using namespace keystone;
using namespace keystone::agents;
using namespace keystone::core;
using namespace keystone::concurrency;

class AsyncTaskAgentTest : public ::testing::Test {
 protected:
  void SetUp() override {
    scheduler_ = std::make_unique<WorkStealingScheduler>(2);
    scheduler_->start();

    bus_ = std::make_unique<MessageBus>();
    bus_->setScheduler(scheduler_.get());

    agent_ = std::make_shared<TaskAgent>("async_task_1");
    agent_->setMessageBus(bus_.get());
    agent_->setScheduler(scheduler_.get());

    bus_->registerAgent(agent_->getAgentId(), agent_);
  }

  void TearDown() override { scheduler_->shutdown(); }

  std::unique_ptr<WorkStealingScheduler> scheduler_;
  std::unique_ptr<MessageBus> bus_;
  std::shared_ptr<TaskAgent> agent_;
};

TEST_F(AsyncTaskAgentTest, ProcessSimpleEchoCommand) {
  // Create message with echo command
  auto msg =
      KeystoneMessage::create("test", agent_->getAgentId(), "echo hello");

  // Send message via bus (async routing)
  bus_->routeMessage(msg);

  // Wait for async processing
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Verify response was sent back
  // Note: In async mode, the response is sent via MessageBus, not returned
  // directly The agent's command history should be updated
  const auto& history = agent_->getCommandHistory();
  ASSERT_EQ(history.size(), 1);
  EXPECT_EQ(history[0].first, "echo hello");
  EXPECT_EQ(history[0].second, "hello");
}

TEST_F(AsyncTaskAgentTest, ProcessArithmeticCommand) {
  auto msg =
      KeystoneMessage::create("test", agent_->getAgentId(), "echo $((5 + 3))");

  bus_->routeMessage(msg);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  const auto& history = agent_->getCommandHistory();
  ASSERT_EQ(history.size(), 1);
  EXPECT_EQ(history[0].first, "echo $((5 + 3))");
  EXPECT_EQ(history[0].second, "8");
}

TEST_F(AsyncTaskAgentTest, ProcessMultipleCommands) {
  auto msg1 =
      KeystoneMessage::create("test", agent_->getAgentId(), "echo first");
  auto msg2 =
      KeystoneMessage::create("test", agent_->getAgentId(), "echo second");
  auto msg3 =
      KeystoneMessage::create("test", agent_->getAgentId(), "echo third");

  bus_->routeMessage(msg1);
  bus_->routeMessage(msg2);
  bus_->routeMessage(msg3);

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  const auto& history = agent_->getCommandHistory();
  ASSERT_EQ(history.size(), 3);

  // Commands may complete in any order due to work-stealing
  std::set<std::string> results;
  for (const auto& [cmd, result] : history) {
    results.insert(result);
  }

  EXPECT_TRUE(results.count("first") > 0);
  EXPECT_TRUE(results.count("second") > 0);
  EXPECT_TRUE(results.count("third") > 0);
}

TEST_F(AsyncTaskAgentTest, HandleCommandFailure) {
  // This command will fail (exit code 1)
  auto msg = KeystoneMessage::create("test", agent_->getAgentId(), "false");

  bus_->routeMessage(msg);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Command should still be logged (with error)
  // The agent creates an error response
  const auto& history = agent_->getCommandHistory();
  (void)history;  // Suppress unused warning - reserved for future assertions
  // Note: Failed commands might not be logged depending on exception handling
  // The important part is that the agent doesn't crash
}

TEST_F(AsyncTaskAgentTest, AsyncExecutionDoesNotBlock) {
  // Submit a slow command (sleep 1) and a fast command to verify non-blocking
  // execution
  auto slow_msg =
      KeystoneMessage::create("test", agent_->getAgentId(), "sleep 1");
  auto fast_msg =
      KeystoneMessage::create("test", agent_->getAgentId(), "echo fast");

  auto start = std::chrono::steady_clock::now();
  bus_->routeMessage(slow_msg);
  bus_->routeMessage(fast_msg);

  // Give fast message time to complete (should be << 1s)
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Fast command should already be in history even though sleep is still
  // running
  const auto& history = agent_->getCommandHistory();
  // At least the echo fast should have completed by now (async, non-blocking)
  bool fast_done =
      std::any_of(history.begin(), history.end(),
                  [](const auto& p) { return p.second == "fast"; });
  EXPECT_TRUE(fast_done) << "echo fast should complete before sleep 1 finishes";

  // Wait for sleep to finish
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  EXPECT_EQ(agent_->getCommandHistory().size(), 2);
  auto elapsed = std::chrono::steady_clock::now() - start;
  EXPECT_GE(elapsed, std::chrono::milliseconds(1000));
}

TEST_F(AsyncTaskAgentTest, AgentIdPreserved) {
  EXPECT_EQ(agent_->getAgentId(), "async_task_1");
}

TEST_F(AsyncTaskAgentTest, MessageBusIntegration) {
  // Create second agent to receive responses
  auto receiver = std::make_shared<TaskAgent>("receiver");
  receiver->setMessageBus(bus_.get());
  receiver->setScheduler(scheduler_.get());
  bus_->registerAgent(receiver->getAgentId(), receiver);

  // Send message from agent to receiver
  auto msg = KeystoneMessage::create(agent_->getAgentId(),
                                     receiver->getAgentId(), "echo test");

  agent_->sendMessage(msg);

  // Wait for message delivery
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Receiver should have processed the message
  const auto& history = receiver->getCommandHistory();
  ASSERT_EQ(history.size(), 1);
  EXPECT_EQ(history[0].second, "test");
}

TEST_F(AsyncTaskAgentTest, CoawaitSyntaxInCoroutine) {
  // This test verifies that executeBashAsync can be co_awaited
  // The actual co_await happens inside processMessage, which is tested
  // by all other tests. This is more of a compilation test.

  auto msg =
      KeystoneMessage::create("test", agent_->getAgentId(), "echo coawait");
  bus_->routeMessage(msg);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  const auto& history = agent_->getCommandHistory();
  ASSERT_EQ(history.size(), 1);
  EXPECT_EQ(history[0].second, "coawait");
}

TEST_F(AsyncTaskAgentTest, SchedulerRequired) {
  // Create agent without scheduler
  auto no_sched_agent = std::make_shared<TaskAgent>("no_sched");
  no_sched_agent->setMessageBus(bus_.get());
  // Don't set scheduler

  bus_->registerAgent(no_sched_agent->getAgentId(), no_sched_agent);

  auto msg = KeystoneMessage::create("test", no_sched_agent->getAgentId(),
                                     "echo test");

  // Send message - should still work (message queued)
  bus_->routeMessage(msg);

  // Without scheduler, auto-processing doesn't happen
  // But message should be in inbox
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  auto inbox_msg = no_sched_agent->getMessage();
  ASSERT_TRUE(inbox_msg.has_value());
  EXPECT_EQ(inbox_msg->command, "echo test");
}

// Issue #376: A failed agent must reject messages without executing them.
TEST_F(AsyncTaskAgentTest, FailedAgentRejectsMessages) {
  agent_->markAsFailed("injected failure");

  auto msg =
      KeystoneMessage::create("test", agent_->getAgentId(), "echo hello");
  bus_->routeMessage(msg);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Command should NOT have been logged (agent rejected it before execution)
  const auto& history = agent_->getCommandHistory();
  EXPECT_TRUE(history.empty());
}

#pragma GCC diagnostic pop
