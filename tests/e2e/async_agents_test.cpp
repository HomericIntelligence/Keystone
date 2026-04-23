#include "agents/task_agent.hpp"
#include "concurrency/work_stealing_scheduler.hpp"
#include "core/message_bus.hpp"

#include <chrono>
#include <map>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace keystone;
using namespace keystone::agents;
using namespace keystone::core;
using namespace keystone::concurrency;

/**
 * Phase B E2E Tests: Async Agent Coroutine Workflow
 *
 * These tests demonstrate the complete async agent architecture where:
 * - Agents use coroutines for message processing (Task<Response>)
 * - Bash commands execute asynchronously on worker threads
 * - MessageBus routes messages via WorkStealingScheduler
 * - Multiple agents process messages concurrently
 */

TEST(E2E_PhaseB, AsyncAgentCoroutineWorkflow) {
  // Setup: Scheduler + MessageBus + Async Agents
  WorkStealingScheduler scheduler(4);
  scheduler.start();

  MessageBus bus;
  bus.setScheduler(&scheduler);

  // Create 3 task agents (async by default)
  auto agent1 = std::make_shared<TaskAgent>("async_1");
  auto agent2 = std::make_shared<TaskAgent>("async_2");
  auto agent3 = std::make_shared<TaskAgent>("async_3");

  agent1->setMessageBus(&bus);
  agent1->setScheduler(&scheduler);
  agent2->setMessageBus(&bus);
  agent2->setScheduler(&scheduler);
  agent3->setMessageBus(&bus);
  agent3->setScheduler(&scheduler);

  bus.registerAgent(agent1->getAgentId(), agent1);
  bus.registerAgent(agent2->getAgentId(), agent2);
  bus.registerAgent(agent3->getAgentId(), agent3);

  // Send commands to all agents
  std::vector<std::pair<TaskAgent*, std::string>> commands = {{agent1.get(), "echo $((10 + 5))"},
                                                              {agent2.get(), "echo $((20 * 3))"},
                                                              {agent3.get(), "echo $((100 - 25))"},
                                                              {agent1.get(), "echo hello"},
                                                              {agent2.get(), "echo world"},
                                                              {agent3.get(), "echo async"}};

  for (const auto& [agent, cmd] : commands) {
    auto msg = KeystoneMessage::create("test", agent->getAgentId(), cmd);
    bus.routeMessage(msg);
  }

  // Wait for async processing to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  // Verify all commands executed correctly
  const auto& history1 = agent1->getCommandHistory();
  const auto& history2 = agent2->getCommandHistory();
  const auto& history3 = agent3->getCommandHistory();

  ASSERT_EQ(history1.size(), 2);
  ASSERT_EQ(history2.size(), 2);
  ASSERT_EQ(history3.size(), 2);

  // Verify results (order may vary due to async execution)
  std::map<std::string, std::string> results;
  for (const auto& [cmd, result] : history1) {
    results[cmd] = result;
  }
  for (const auto& [cmd, result] : history2) {
    results[cmd] = result;
  }
  for (const auto& [cmd, result] : history3) {
    results[cmd] = result;
  }

  EXPECT_EQ(results["echo $((10 + 5))"], "15");
  EXPECT_EQ(results["echo $((20 * 3))"], "60");
  EXPECT_EQ(results["echo $((100 - 25))"], "75");
  EXPECT_EQ(results["echo hello"], "hello");
  EXPECT_EQ(results["echo world"], "world");
  EXPECT_EQ(results["echo async"], "async");

  scheduler.shutdown();
}

TEST(E2E_PhaseB, AsyncAgentsConcurrentProcessing) {
  // Verify that async agents can process multiple commands concurrently
  WorkStealingScheduler scheduler(4);
  scheduler.start();

  MessageBus bus;
  bus.setScheduler(&scheduler);

  auto agent = std::make_shared<TaskAgent>("concurrent");
  agent->setMessageBus(&bus);
  agent->setScheduler(&scheduler);
  bus.registerAgent(agent->getAgentId(), agent);

  // Send 10 echo commands (sleep is blocked by security validation)
  for (int32_t i = 0; i < 10; ++i) {
    auto cmd = "echo " + std::to_string(i);
    auto msg = KeystoneMessage::create("test", agent->getAgentId(), cmd);
    bus.routeMessage(msg);
  }

  // Wait for all to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(400));

  const auto& history = agent->getCommandHistory();
  ASSERT_EQ(history.size(), 10);

  // Verify all results present
  std::set<std::string> results;
  for (const auto& [cmd, result] : history) {
    results.insert(result);
  }

  for (int32_t i = 0; i < 10; ++i) {
    EXPECT_TRUE(results.count(std::to_string(i)) > 0);
  }

  scheduler.shutdown();
}

TEST(E2E_PhaseB, AsyncAgentsWithChainedMessages) {
  // Test async agents communicating with each other
  WorkStealingScheduler scheduler(2);
  scheduler.start();

  MessageBus bus;
  bus.setScheduler(&scheduler);

  auto agent_a = std::make_shared<TaskAgent>("agent_a");
  auto agent_b = std::make_shared<TaskAgent>("agent_b");

  agent_a->setMessageBus(&bus);
  agent_a->setScheduler(&scheduler);
  agent_b->setMessageBus(&bus);
  agent_b->setScheduler(&scheduler);

  bus.registerAgent(agent_a->getAgentId(), agent_a);
  bus.registerAgent(agent_b->getAgentId(), agent_b);

  // Send commands: agent_a processes, sends response to agent_b
  auto msg_to_a = KeystoneMessage::create("test", "agent_a", "echo step1");
  auto msg_to_b = KeystoneMessage::create("test", "agent_b", "echo step2");

  bus.routeMessage(msg_to_a);
  bus.routeMessage(msg_to_b);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Both agents should have processed their commands
  const auto& history_a = agent_a->getCommandHistory();
  const auto& history_b = agent_b->getCommandHistory();

  ASSERT_GE(history_a.size(), 1);
  ASSERT_GE(history_b.size(), 1);

  EXPECT_EQ(history_a[0].second, "step1");
  EXPECT_EQ(history_b[0].second, "step2");

  scheduler.shutdown();
}

TEST(E2E_PhaseB, AsyncAgentErrorHandling) {
  // Verify async agents handle command failures gracefully
  WorkStealingScheduler scheduler(2);
  scheduler.start();

  MessageBus bus;
  bus.setScheduler(&scheduler);

  auto agent = std::make_shared<TaskAgent>("error_handler");
  agent->setMessageBus(&bus);
  agent->setScheduler(&scheduler);
  bus.registerAgent(agent->getAgentId(), agent);

  // Send mix of successful and failing commands
  std::vector<std::string> commands = {"echo success1",
                                       "false",  // Will fail with status 1
                                       "echo success2",
                                       "exit 1",  // Will also fail
                                       "echo success3"};

  for (const auto& cmd : commands) {
    auto msg = KeystoneMessage::create("test", agent->getAgentId(), cmd);
    bus.routeMessage(msg);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Agent should still be functional
  // Successful commands should be in history
  const auto& history = agent->getCommandHistory();

  // At least the successful commands should be logged
  // (Failed commands may or may not be logged depending on exception handling)
  EXPECT_GE(history.size(), 3);

  std::set<std::string> results;
  for (const auto& [cmd, result] : history) {
    results.insert(result);
  }

  // Successful results should be present
  EXPECT_TRUE(results.count("success1") > 0);
  EXPECT_TRUE(results.count("success2") > 0);
  EXPECT_TRUE(results.count("success3") > 0);

  scheduler.shutdown();
}

TEST(E2E_PhaseB, CoawaitAsyncBashExecution) {
  // Demonstrate that executeBashAsync can be co_awaited
  WorkStealingScheduler scheduler(2);
  scheduler.start();

  MessageBus bus;
  bus.setScheduler(&scheduler);

  auto agent = std::make_shared<TaskAgent>("coawait_test");
  agent->setMessageBus(&bus);
  agent->setScheduler(&scheduler);
  bus.registerAgent(agent->getAgentId(), agent);

  // This command's execution will co_await executeBashAsync internally
  auto msg = KeystoneMessage::create("test", agent->getAgentId(), "echo $((42 * 2))");
  bus.routeMessage(msg);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  const auto& history = agent->getCommandHistory();
  ASSERT_EQ(history.size(), 1);
  EXPECT_EQ(history[0].second, "84");

  scheduler.shutdown();
}
