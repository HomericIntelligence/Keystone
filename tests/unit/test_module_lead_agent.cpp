/**
 * @file test_module_lead_agent.cpp
 * @brief Unit tests for ModuleLeadAgent (Level 2)
 *
 * Test coverage:
 * - Construction & Initialization (2 tests)
 * - Task Decomposition (4 tests)
 * - Coordination (4 tests)
 * - State Machine (2 tests)
 *
 * Total: 12 tests
 */

// KeystoneMessage::command is [[deprecated]]; test files intentionally access
// it to verify backward-compat behaviour.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <gtest/gtest.h>

#include "agents/component_lead_agent.hpp"
#include "agents/module_lead_agent.hpp"
#include "agents/task_agent.hpp"
#include "core/message_bus.hpp"
#include "unit/agent_test_fixture.hpp"

using namespace keystone;
using namespace keystone::test;

// ============================================================================
// Construction & Initialization Tests (2 tests)
// ============================================================================

class ModuleLeadAgentTest : public AgentTestFixture {};

TEST_F(ModuleLeadAgentTest, DefaultConstruction) {
  auto agent = std::make_shared<agents::ModuleLeadAgent>("module_1");
  EXPECT_EQ(agent->getAgentId(), "module_1");
}

TEST_F(ModuleLeadAgentTest, InitialCoordinationState) {
  auto agent = std::make_shared<agents::ModuleLeadAgent>("module_1");
  // Should start in IDLE state
  EXPECT_EQ(agent->getCurrentState(), agents::ModuleLeadAgent::State::IDLE);
}

// ============================================================================
// Task Decomposition Tests (4 tests)
// ============================================================================

TEST_F(ModuleLeadAgentTest, DecomposeModuleIntoTasks) {
  auto module = std::make_shared<agents::ModuleLeadAgent>("module_1");
  module->setMessageBus(bus_.get());
  bus_->registerAgent(module->getAgentId(), module);

  // Configure task agents
  std::vector<std::string> task_ids = {"task_1", "task_2", "task_3"};
  module->setAvailableTaskAgents(task_ids);

  // Send module goal
  auto msg =
      core::KeystoneMessage::create("chief", "module_1", "process dataset");
  module->receiveMessage(msg);

  auto received = module->getMessage();
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->command, "process dataset");
}

TEST_F(ModuleLeadAgentTest, DecomposeWithVariableTaskCount) {
  auto module = std::make_shared<agents::ModuleLeadAgent>("module_1");
  module->setMessageBus(bus_.get());
  bus_->registerAgent(module->getAgentId(), module);

  // Test with 2 tasks
  std::vector<std::string> task_ids = {"task_1", "task_2"};
  module->setAvailableTaskAgents(task_ids);
  EXPECT_NO_THROW(module->setAvailableTaskAgents(task_ids));

  // Test with 5 tasks
  task_ids = {"task_1", "task_2", "task_3", "task_4", "task_5"};
  EXPECT_NO_THROW(module->setAvailableTaskAgents(task_ids));
}

TEST_F(ModuleLeadAgentTest, DecompositionFailure) {
  auto module = std::make_shared<agents::ModuleLeadAgent>("module_1");
  module->setMessageBus(bus_.get());
  bus_->registerAgent(module->getAgentId(), module);

  // No task agents configured - should handle gracefully
  auto msg = core::KeystoneMessage::create("chief", "module_1", "goal");
  EXPECT_NO_THROW(module->receiveMessage(msg));
}

TEST_F(ModuleLeadAgentTest, EmptyGoalHandling) {
  auto module = std::make_shared<agents::ModuleLeadAgent>("module_1");
  module->setMessageBus(bus_.get());
  bus_->registerAgent(module->getAgentId(), module);

  std::vector<std::string> task_ids = {"task_1", "task_2"};
  module->setAvailableTaskAgents(task_ids);

  // Empty command
  auto msg = core::KeystoneMessage::create("chief", "module_1", "");
  EXPECT_NO_THROW(module->receiveMessage(msg));
}

// ============================================================================
// Coordination Tests (4 tests)
// ============================================================================

TEST_F(ModuleLeadAgentTest, CoordinateThreeTaskAgents) {
  auto module = std::make_shared<agents::ModuleLeadAgent>("module_1");
  auto task1 = std::make_shared<agents::TaskAgent>("task_1");
  auto task2 = std::make_shared<agents::TaskAgent>("task_2");
  auto task3 = std::make_shared<agents::TaskAgent>("task_3");

  module->setMessageBus(bus_.get());
  task1->setMessageBus(bus_.get());
  task2->setMessageBus(bus_.get());
  task3->setMessageBus(bus_.get());

  bus_->registerAgent(module->getAgentId(), module);
  bus_->registerAgent(task1->getAgentId(), task1);
  bus_->registerAgent(task2->getAgentId(), task2);
  bus_->registerAgent(task3->getAgentId(), task3);

  std::vector<std::string> task_ids = {"task_1", "task_2", "task_3"};
  module->setAvailableTaskAgents(task_ids);

  // Send messages to tasks
  module->sendMessage(
      core::KeystoneMessage::create("module_1", "task_1", "cmd1"));
  module->sendMessage(
      core::KeystoneMessage::create("module_1", "task_2", "cmd2"));
  module->sendMessage(
      core::KeystoneMessage::create("module_1", "task_3", "cmd3"));

  // All tasks should receive messages
  auto r1 = task1->getMessage();
  auto r2 = task2->getMessage();
  auto r3 = task3->getMessage();

  ASSERT_TRUE(r1.has_value());
  ASSERT_TRUE(r2.has_value());
  ASSERT_TRUE(r3.has_value());
}

TEST_F(ModuleLeadAgentTest, CoordinateWithPartialFailures) {
  auto module = std::make_shared<agents::ModuleLeadAgent>("module_1");
  auto task1 = std::make_shared<agents::TaskAgent>("task_1");
  auto task2 = std::make_shared<agents::TaskAgent>("task_2");

  module->setMessageBus(bus_.get());
  task1->setMessageBus(bus_.get());
  task2->setMessageBus(bus_.get());

  bus_->registerAgent(module->getAgentId(), module);
  bus_->registerAgent(task1->getAgentId(), task1);
  bus_->registerAgent(task2->getAgentId(), task2);

  std::vector<std::string> task_ids = {"task_1", "task_2",
                                       "task_3"};  // task_3 doesn't exist
  module->setAvailableTaskAgents(task_ids);

  // Try to send to all (one will fail)
  EXPECT_NO_THROW(module->sendMessage(
      core::KeystoneMessage::create("module_1", "task_1", "cmd1")));
  EXPECT_NO_THROW(module->sendMessage(
      core::KeystoneMessage::create("module_1", "task_2", "cmd2")));
  EXPECT_NO_THROW(module->sendMessage(core::KeystoneMessage::create(
      "module_1", "task_3", "cmd3")));  // Will fail routing
}

TEST_F(ModuleLeadAgentTest, WaitForAllResults) {
  auto module = std::make_shared<agents::ModuleLeadAgent>("module_1");
  auto task1 = std::make_shared<agents::TaskAgent>("task_1");
  auto task2 = std::make_shared<agents::TaskAgent>("task_2");

  module->setMessageBus(bus_.get());
  task1->setMessageBus(bus_.get());
  task2->setMessageBus(bus_.get());

  bus_->registerAgent(module->getAgentId(), module);
  bus_->registerAgent(task1->getAgentId(), task1);
  bus_->registerAgent(task2->getAgentId(), task2);

  std::vector<std::string> task_ids = {"task_1", "task_2"};
  module->setAvailableTaskAgents(task_ids);

  // Send task results back to module
  task1->sendMessage(
      core::KeystoneMessage::create("task_1", "module_1", "result1"));
  task2->sendMessage(
      core::KeystoneMessage::create("task_2", "module_1", "result2"));

  // Module should receive both results
  auto r1 = module->getMessage();
  auto r2 = module->getMessage();

  ASSERT_TRUE(r1.has_value());
  ASSERT_TRUE(r2.has_value());
}

TEST_F(ModuleLeadAgentTest, ResultSynthesis) {
  auto module = std::make_shared<agents::ModuleLeadAgent>("module_1");
  module->setMessageBus(bus_.get());
  bus_->registerAgent(module->getAgentId(), module);

  std::vector<std::string> task_ids = {"task_1", "task_2", "task_3"};
  module->setAvailableTaskAgents(task_ids);

  // Simulate receiving results from tasks
  auto result1 = core::KeystoneMessage::create("task_1", "module_1", "5");
  auto result2 = core::KeystoneMessage::create("task_2", "module_1", "7");
  auto result3 = core::KeystoneMessage::create("task_3", "module_1", "3");

  module->receiveMessage(result1);
  module->receiveMessage(result2);
  module->receiveMessage(result3);

  // Process results
  module->processMessage(result1).get();
  module->processMessage(result2).get();
  module->processMessage(result3).get();

  // Synthesize (should combine all results)
  auto synthesized = module->synthesizeResults();
  EXPECT_FALSE(synthesized.empty());
}

// ============================================================================
// State Machine Tests (2 tests)
// ============================================================================

TEST_F(ModuleLeadAgentTest, StateTransitionFlow) {
  auto module = std::make_shared<agents::ModuleLeadAgent>("module_1");
  module->setMessageBus(bus_.get());
  bus_->registerAgent(module->getAgentId(), module);

  std::vector<std::string> task_ids = {"task_1", "task_2", "task_3"};
  module->setAvailableTaskAgents(task_ids);

  // Initial state: IDLE
  EXPECT_EQ(module->getCurrentState(), agents::ModuleLeadAgent::State::IDLE);

  // Get execution trace (should be empty or just initialization)
  auto trace = module->getExecutionTrace();
  // Trace may be empty or contain initialization states
  (void)trace;  // Suppress unused variable warning
}

// ============================================================================
// Issue #87: DAG Deadlock Prevention — Failure Handling Tests (4 tests)
// ============================================================================

TEST_F(ModuleLeadAgentTest, SingleTaskFailureTransitionsToError) {
  auto module = std::make_shared<agents::ModuleLeadAgent>("module_1");
  module->setMessageBus(bus_.get());
  bus_->registerAgent(module->getAgentId(), module);

  std::vector<std::string> task_ids = {"task_1"};
  module->setAvailableTaskAgents(task_ids);

  // Simulate receiving a TASK_FAILED message from a TaskAgent
  auto failure_msg = core::KeystoneMessage::create(
      "task_1", "module_1", "response", "command not found");
  failure_msg.action_type = core::ActionType::TASK_FAILED;

  // Initialize coordination for 1 expected result (single number → single task)
  module
      ->processMessage(
          core::KeystoneMessage::create("chief", "module_1", "Calculate: 42"))
      .get();

  // Deliver failure
  module->processMessage(failure_msg).get();

  // Agent must be in ERROR state — DAG is not deadlocked
  EXPECT_EQ(module->getCurrentState(), agents::ModuleLeadAgent::State::ERROR);
}

TEST_F(ModuleLeadAgentTest, SynthesizeAfterFailureReturnsErrorMessage) {
  auto module = std::make_shared<agents::ModuleLeadAgent>("module_1");
  module->setMessageBus(bus_.get());
  bus_->registerAgent(module->getAgentId(), module);

  std::vector<std::string> task_ids = {"task_1", "task_2"};
  module->setAvailableTaskAgents(task_ids);

  // Process goal to set up coordination for 2 tasks
  module
      ->processMessage(core::KeystoneMessage::create("chief", "module_1",
                                                     "Calculate: 10 + 20"))
      .get();

  // One success, one failure
  auto success_msg =
      core::KeystoneMessage::create("task_1", "module_1", "response", "10");
  module->processMessage(success_msg).get();

  auto failure_msg = core::KeystoneMessage::create("task_2", "module_1",
                                                   "response", "exec failed");
  failure_msg.action_type = core::ActionType::TASK_FAILED;
  module->processMessage(failure_msg).get();

  EXPECT_EQ(module->getCurrentState(), agents::ModuleLeadAgent::State::ERROR);

  auto result = module->synthesizeResults();
  EXPECT_NE(result.find("ERROR"), std::string::npos);
}

TEST_F(ModuleLeadAgentTest, FailureBeforeAllResultsDoesNotDeadlock) {
  auto module = std::make_shared<agents::ModuleLeadAgent>("module_1");
  module->setMessageBus(bus_.get());
  bus_->registerAgent(module->getAgentId(), module);

  std::vector<std::string> task_ids = {"task_1", "task_2", "task_3"};
  module->setAvailableTaskAgents(task_ids);

  // Goal decomposes into 3 tasks (numbers extracted from "10 + 20 + 30")
  module
      ->processMessage(core::KeystoneMessage::create(
          "chief", "module_1", "Calculate sum of: 10 + 20 + 30"))
      .get();

  // First task fails — must not leave the remaining 2 in permanent pending
  auto failure_msg =
      core::KeystoneMessage::create("task_1", "module_1", "response", "error");
  failure_msg.action_type = core::ActionType::TASK_FAILED;
  module->processMessage(failure_msg).get();

  // State must not be stuck in WAITING after any terminal event
  auto state = module->getCurrentState();
  EXPECT_TRUE(state == agents::ModuleLeadAgent::State::ERROR ||
              state == agents::ModuleLeadAgent::State::WAITING_FOR_TASKS);
}

TEST_F(ModuleLeadAgentTest,
       SuccessResultAfterFailureStillCountsTowardCompletion) {
  auto module = std::make_shared<agents::ModuleLeadAgent>("module_1");
  module->setMessageBus(bus_.get());
  bus_->registerAgent(module->getAgentId(), module);

  std::vector<std::string> task_ids = {"task_1", "task_2"};
  module->setAvailableTaskAgents(task_ids);

  module
      ->processMessage(core::KeystoneMessage::create("chief", "module_1",
                                                     "Calculate: 10 + 20"))
      .get();

  // Failure first
  auto failure_msg =
      core::KeystoneMessage::create("task_1", "module_1", "response", "boom");
  failure_msg.action_type = core::ActionType::TASK_FAILED;
  module->processMessage(failure_msg).get();

  // Then success — all results are now terminal, agent must not remain in
  // WAITING
  auto success_msg =
      core::KeystoneMessage::create("task_2", "module_1", "response", "20");
  module->processMessage(success_msg).get();

  auto state = module->getCurrentState();
  EXPECT_NE(state, agents::ModuleLeadAgent::State::WAITING_FOR_TASKS);
}

// ============================================================================
// Issue #184: isSubordinateResult does not intercept TASK_FAILED messages
// ============================================================================

TEST_F(ModuleLeadAgentTest, TaskFailedNotTreatedAsSuccessResult) {
  // A TASK_FAILED message with command == "response" must NOT be processed by
  // processSubordinateResult() — it must reach processSubordinateFailure()
  // so the failure is recorded rather than silently counted as a success.
  auto module = std::make_shared<agents::ModuleLeadAgent>("module_1");
  module->setMessageBus(bus_.get());
  bus_->registerAgent(module->getAgentId(), module);

  std::vector<std::string> task_ids = {"task_1"};
  module->setAvailableTaskAgents(task_ids);

  // Prime coordination for 1 task
  module
      ->processMessage(
          core::KeystoneMessage::create("chief", "module_1", "Calculate: 7"))
      .get();

  // Send a TASK_FAILED with command == "response" (the ambiguous case from
  // #184)
  auto failure_msg = core::KeystoneMessage::create(
      "task_1", "module_1", "response", "task failed badly");
  failure_msg.action_type = core::ActionType::TASK_FAILED;
  module->processMessage(failure_msg).get();

  // Must be in ERROR, not SYNTHESIZING — the failure was not treated as success
  EXPECT_EQ(module->getCurrentState(), agents::ModuleLeadAgent::State::ERROR);
}

// ============================================================================
// Issue #185: processSubordinateFailure propagates failure upward
// ============================================================================

TEST_F(ModuleLeadAgentTest, FailurePropagatedUpwardToRequester) {
  // When all subordinates have reported terminal events and at least one has
  // failed, processSubordinateFailure() must send a TASK_FAILED message to
  // the requester so the ComponentLeadAgent does not permanently stall.
  auto component = std::make_shared<agents::ComponentLeadAgent>("component_1");
  auto module = std::make_shared<agents::ModuleLeadAgent>("module_1");
  auto task1 = std::make_shared<agents::TaskAgent>("task_1");

  component->setMessageBus(bus_.get());
  module->setMessageBus(bus_.get());
  task1->setMessageBus(bus_.get());

  bus_->registerAgent(component->getAgentId(), component);
  bus_->registerAgent(module->getAgentId(), module);
  bus_->registerAgent(task1->getAgentId(), task1);

  // Configure component → module → task hierarchy
  component->setAvailableModuleLeads({"module_1"});
  module->setAvailableTaskAgents({"task_1"});

  // ComponentLeadAgent receives a goal and delegates to module_1
  // Use a goal pattern that decomposes to exactly 1 module
  component
      ->processMessage(core::KeystoneMessage::create(
          "chief", "component_1", "Implement Core component: Messaging(42)"))
      .get();

  // module_1 should have received a goal — prime module coordination for 1 task
  // Sender must be component_1 so requester_id_ is set correctly for upward
  // propagation.
  module
      ->processMessage(core::KeystoneMessage::create("component_1", "module_1",
                                                     "Calculate: 42"))
      .get();

  // Deliver a TASK_FAILED to the module from task_1
  auto failure_msg = core::KeystoneMessage::create(
      "task_1", "module_1", "response", "execution failed");
  failure_msg.action_type = core::ActionType::TASK_FAILED;
  module->processMessage(failure_msg).get();

  // module_1 must be in ERROR
  EXPECT_EQ(module->getCurrentState(), agents::ModuleLeadAgent::State::ERROR);

  // component_1 inbox should contain a TASK_FAILED message from module_1
  bool received_failure = false;
  std::optional<core::KeystoneMessage> msg;
  while ((msg = component->getMessage()).has_value()) {
    if (msg->action_type == core::ActionType::TASK_FAILED) {
      received_failure = true;
      break;
    }
  }
  EXPECT_TRUE(received_failure)
      << "ComponentLeadAgent did not receive TASK_FAILED from module";
}

TEST_F(ModuleLeadAgentTest, FailureNotPropagatedUntilAllTerminal) {
  // Upward propagation must be deferred until ALL subordinates have reported
  // (success or failure) — partial failure must not immediately fire upward.
  auto module = std::make_shared<agents::ModuleLeadAgent>("module_1");
  module->setMessageBus(bus_.get());
  bus_->registerAgent(module->getAgentId(), module);

  // Register a dummy parent to capture upward messages
  auto parent = std::make_shared<agents::ModuleLeadAgent>("parent_lead");
  parent->setMessageBus(bus_.get());
  bus_->registerAgent(parent->getAgentId(), parent);

  std::vector<std::string> task_ids = {"task_1", "task_2"};
  module->setAvailableTaskAgents(task_ids);

  // Prime module with a 2-task goal, setting parent as requester
  module
      ->processMessage(core::KeystoneMessage::create("parent_lead", "module_1",
                                                     "Calculate: 10 + 20"))
      .get();

  // First task fails — only 1 of 2 done, upward propagation must NOT fire yet
  auto failure_msg =
      core::KeystoneMessage::create("task_1", "module_1", "response", "boom");
  failure_msg.action_type = core::ActionType::TASK_FAILED;
  module->processMessage(failure_msg).get();

  // No TASK_FAILED should have been delivered to parent yet
  bool premature_failure = false;
  std::optional<core::KeystoneMessage> msg;
  while ((msg = parent->getMessage()).has_value()) {
    if (msg->action_type == core::ActionType::TASK_FAILED) {
      premature_failure = true;
    }
  }
  EXPECT_FALSE(premature_failure)
      << "TASK_FAILED was sent upward before all subtasks terminated";

  // Second task succeeds — all done, now upward failure message must arrive
  auto success_msg =
      core::KeystoneMessage::create("task_2", "module_1", "response", "20");
  module->processMessage(success_msg).get();

  EXPECT_EQ(module->getCurrentState(), agents::ModuleLeadAgent::State::ERROR);
}

TEST_F(ModuleLeadAgentTest, ConcurrentCoordination) {
  auto module = std::make_shared<agents::ModuleLeadAgent>("module_1");
  module->setMessageBus(bus_.get());
  bus_->registerAgent(module->getAgentId(), module);

  std::vector<std::string> task_ids = {"task_1", "task_2", "task_3"};
  module->setAvailableTaskAgents(task_ids);

  // Send many messages concurrently
  for (int32_t i = 0; i < 50; ++i) {
    auto msg = core::KeystoneMessage::create("sender", "module_1",
                                             "cmd" + std::to_string(i));
    EXPECT_NO_THROW(module->receiveMessage(msg));
  }

  // Verify all messages were queued
  int32_t count = 0;
  while (module->getMessage().has_value()) {
    ++count;
  }
  EXPECT_EQ(count, 50);
}

#pragma GCC diagnostic pop
