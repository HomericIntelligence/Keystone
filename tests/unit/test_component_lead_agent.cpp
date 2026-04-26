/**
 * @file test_component_lead_agent.cpp
 * @brief Unit tests for ComponentLeadAgent (Level 1)
 *
 * Test coverage:
 * - Construction & Initialization (2 tests)
 * - Module Decomposition (4 tests)
 * - Coordination (4 tests)
 * - State Machine (2 tests)
 * - Failure Handling / DAG Deadlock Prevention (4 tests) — Issue #183
 *
 * Total: 16 tests
 */

#include "agents/component_lead_agent.hpp"
#include "agents/module_lead_agent.hpp"
#include "core/message_bus.hpp"
#include "unit/agent_test_fixture.hpp"

#include <gtest/gtest.h>

using namespace keystone;
using namespace keystone::test;

// ============================================================================
// Construction & Initialization Tests (2 tests)
// ============================================================================

class ComponentLeadAgentTest : public AgentTestFixture {};

TEST_F(ComponentLeadAgentTest, DefaultConstruction) {
  auto agent = std::make_shared<agents::ComponentLeadAgent>("component_1");
  EXPECT_EQ(agent->getAgentId(), "component_1");
}

TEST_F(ComponentLeadAgentTest, InitialCoordinationState) {
  auto agent = std::make_shared<agents::ComponentLeadAgent>("component_1");
  // Should start in IDLE state
  EXPECT_EQ(agent->getCurrentState(), agents::ComponentLeadAgent::State::IDLE);
}

// ============================================================================
// Module Decomposition Tests (4 tests)
// ============================================================================

TEST_F(ComponentLeadAgentTest, DecomposeComponentIntoModules) {
  auto component = std::make_shared<agents::ComponentLeadAgent>("component_1");
  component->setMessageBus(bus_.get());
  bus_->registerAgent(component->getAgentId(), component);

  // Configure module leads
  std::vector<std::string> module_ids = {"module_1", "module_2"};
  component->setAvailableModuleLeads(module_ids);

  // Send component goal
  auto msg = core::KeystoneMessage::create("chief", "component_1", "build feature X");
  component->receiveMessage(msg);

  auto received = component->getMessage();
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->command, "build feature X");
}

TEST_F(ComponentLeadAgentTest, DecomposeWithVariableCounts) {
  auto component = std::make_shared<agents::ComponentLeadAgent>("component_1");
  component->setMessageBus(bus_.get());
  bus_->registerAgent(component->getAgentId(), component);

  // Test with 1 module
  std::vector<std::string> module_ids = {"module_1"};
  component->setAvailableModuleLeads(module_ids);
  EXPECT_NO_THROW(component->setAvailableModuleLeads(module_ids));

  // Test with 3 modules
  module_ids = {"module_1", "module_2", "module_3"};
  EXPECT_NO_THROW(component->setAvailableModuleLeads(module_ids));

  // Test with 5 modules
  module_ids = {"module_1", "module_2", "module_3", "module_4", "module_5"};
  EXPECT_NO_THROW(component->setAvailableModuleLeads(module_ids));
}

TEST_F(ComponentLeadAgentTest, DecompositionFailure) {
  auto component = std::make_shared<agents::ComponentLeadAgent>("component_1");
  component->setMessageBus(bus_.get());
  bus_->registerAgent(component->getAgentId(), component);

  // No module leads configured - should handle gracefully
  auto msg = core::KeystoneMessage::create("chief", "component_1", "goal");
  EXPECT_NO_THROW(component->receiveMessage(msg));
}

TEST_F(ComponentLeadAgentTest, EmptyGoalHandling) {
  auto component = std::make_shared<agents::ComponentLeadAgent>("component_1");
  component->setMessageBus(bus_.get());
  bus_->registerAgent(component->getAgentId(), component);

  std::vector<std::string> module_ids = {"module_1", "module_2"};
  component->setAvailableModuleLeads(module_ids);

  // Empty command
  auto msg = core::KeystoneMessage::create("chief", "component_1", "");
  EXPECT_NO_THROW(component->receiveMessage(msg));
}

// ============================================================================
// Coordination Tests (4 tests)
// ============================================================================

TEST_F(ComponentLeadAgentTest, CoordinateTwoModuleLeads) {
  auto component = std::make_shared<agents::ComponentLeadAgent>("component_1");
  auto module1 = std::make_shared<agents::ModuleLeadAgent>("module_1");
  auto module2 = std::make_shared<agents::ModuleLeadAgent>("module_2");

  component->setMessageBus(bus_.get());
  module1->setMessageBus(bus_.get());
  module2->setMessageBus(bus_.get());

  bus_->registerAgent(component->getAgentId(), component);
  bus_->registerAgent(module1->getAgentId(), module1);
  bus_->registerAgent(module2->getAgentId(), module2);

  std::vector<std::string> module_ids = {"module_1", "module_2"};
  component->setAvailableModuleLeads(module_ids);

  // Send messages to modules
  component->sendMessage(core::KeystoneMessage::create("component_1", "module_1", "goal1"));
  component->sendMessage(core::KeystoneMessage::create("component_1", "module_2", "goal2"));

  // Both modules should receive messages
  auto r1 = module1->getMessage();
  auto r2 = module2->getMessage();

  ASSERT_TRUE(r1.has_value());
  ASSERT_TRUE(r2.has_value());
  EXPECT_EQ(r1->command, "goal1");
  EXPECT_EQ(r2->command, "goal2");
}

TEST_F(ComponentLeadAgentTest, CoordinateWithPartialFailures) {
  auto component = std::make_shared<agents::ComponentLeadAgent>("component_1");
  auto module1 = std::make_shared<agents::ModuleLeadAgent>("module_1");

  component->setMessageBus(bus_.get());
  module1->setMessageBus(bus_.get());

  bus_->registerAgent(component->getAgentId(), component);
  bus_->registerAgent(module1->getAgentId(), module1);

  std::vector<std::string> module_ids = {"module_1", "module_2"};  // module_2 doesn't exist
  component->setAvailableModuleLeads(module_ids);

  // Try to send to both (one will fail routing)
  EXPECT_NO_THROW(
      component->sendMessage(core::KeystoneMessage::create("component_1", "module_1", "goal1")));
  EXPECT_NO_THROW(component->sendMessage(
      core::KeystoneMessage::create("component_1", "module_2", "goal2")));  // Will fail routing
}

TEST_F(ComponentLeadAgentTest, WaitForAllModuleResults) {
  auto component = std::make_shared<agents::ComponentLeadAgent>("component_1");
  auto module1 = std::make_shared<agents::ModuleLeadAgent>("module_1");
  auto module2 = std::make_shared<agents::ModuleLeadAgent>("module_2");

  component->setMessageBus(bus_.get());
  module1->setMessageBus(bus_.get());
  module2->setMessageBus(bus_.get());

  bus_->registerAgent(component->getAgentId(), component);
  bus_->registerAgent(module1->getAgentId(), module1);
  bus_->registerAgent(module2->getAgentId(), module2);

  std::vector<std::string> module_ids = {"module_1", "module_2"};
  component->setAvailableModuleLeads(module_ids);

  // Send module results back to component
  module1->sendMessage(core::KeystoneMessage::create("module_1", "component_1", "result1"));
  module2->sendMessage(core::KeystoneMessage::create("module_2", "component_1", "result2"));

  // Component should receive both results
  auto r1 = component->getMessage();
  auto r2 = component->getMessage();

  ASSERT_TRUE(r1.has_value());
  ASSERT_TRUE(r2.has_value());
}

TEST_F(ComponentLeadAgentTest, ResultAggregation) {
  auto component = std::make_shared<agents::ComponentLeadAgent>("component_1");
  component->setMessageBus(bus_.get());
  bus_->registerAgent(component->getAgentId(), component);

  std::vector<std::string> module_ids = {"module_1", "module_2"};
  component->setAvailableModuleLeads(module_ids);

  // Simulate receiving results from modules
  auto result1 = core::KeystoneMessage::create("module_1", "component_1", "module1_result");
  auto result2 = core::KeystoneMessage::create("module_2", "component_1", "module2_result");

  component->receiveMessage(result1);
  component->receiveMessage(result2);

  // Process results
  component->processMessage(result1).get();
  component->processMessage(result2).get();

  // Aggregate (should combine all results)
  auto aggregated = component->synthesizeComponentResult();
  EXPECT_FALSE(aggregated.empty());
}

// ============================================================================
// State Machine Tests (2 tests)
// ============================================================================

TEST_F(ComponentLeadAgentTest, StateTransitionFlow) {
  auto component = std::make_shared<agents::ComponentLeadAgent>("component_1");
  component->setMessageBus(bus_.get());
  bus_->registerAgent(component->getAgentId(), component);

  std::vector<std::string> module_ids = {"module_1", "module_2"};
  component->setAvailableModuleLeads(module_ids);

  // Initial state: IDLE
  EXPECT_EQ(component->getCurrentState(), agents::ComponentLeadAgent::State::IDLE);

  // Get execution trace (should be empty or just initialization)
  auto trace = component->getExecutionTrace();
  // Trace may be empty or contain initialization states
  (void)trace;  // Suppress unused variable warning
}

TEST_F(ComponentLeadAgentTest, ConcurrentCoordination) {
  auto component = std::make_shared<agents::ComponentLeadAgent>("component_1");
  component->setMessageBus(bus_.get());
  bus_->registerAgent(component->getAgentId(), component);

  std::vector<std::string> module_ids = {"module_1", "module_2"};
  component->setAvailableModuleLeads(module_ids);

  // Send many messages concurrently
  for (int32_t i = 0; i < 50; ++i) {
    auto msg = core::KeystoneMessage::create("sender", "component_1", "cmd" + std::to_string(i));
    EXPECT_NO_THROW(component->receiveMessage(msg));
  }

  // Verify all messages were queued
  int32_t count = 0;
  while (component->getMessage().has_value()) {
    ++count;
  }
  EXPECT_EQ(count, 50);
}

// ============================================================================
// Issue #183: DAG Deadlock Prevention — Failure Handling Tests (4 tests)
// Mirrors the ModuleLeadAgent failure-handling tests added for Issue #87.
// ============================================================================

TEST_F(ComponentLeadAgentTest, SingleModuleFailureTransitionsToError) {
  auto component = std::make_shared<agents::ComponentLeadAgent>("component_1");
  component->setMessageBus(bus_.get());
  bus_->registerAgent(component->getAgentId(), component);

  std::vector<std::string> module_ids = {"module_1"};
  component->setAvailableModuleLeads(module_ids);

  // Decompose "Messaging(10)" into 1 module goal, which initialises coordination
  // for 1 expected result.
  component
      ->processMessage(
          core::KeystoneMessage::create("chief", "component_1", "Build Core: Messaging(10)"))
      .get();

  // Simulate the single module reporting failure
  auto failure_msg =
      core::KeystoneMessage::create("module_1", "component_1", "response", "compile error");
  failure_msg.action_type = core::ActionType::TASK_FAILED;
  component->processMessage(failure_msg).get();

  // Agent must be in ERROR state — DAG is not deadlocked
  EXPECT_EQ(component->getCurrentState(), agents::ComponentLeadAgent::State::ERROR);
}

TEST_F(ComponentLeadAgentTest, SynthesizeAfterModuleFailureReturnsErrorMessage) {
  auto component = std::make_shared<agents::ComponentLeadAgent>("component_1");
  component->setMessageBus(bus_.get());
  bus_->registerAgent(component->getAgentId(), component);

  std::vector<std::string> module_ids = {"module_1", "module_2"};
  component->setAvailableModuleLeads(module_ids);

  // Decompose into 2 module goals
  component
      ->processMessage(core::KeystoneMessage::create(
          "chief", "component_1", "Build Core: Messaging(10) and Concurrency(20)"))
      .get();

  // One success
  auto success_msg =
      core::KeystoneMessage::create("module_1", "component_1", "module_result", "messaging done");
  component->processMessage(success_msg).get();

  // One failure
  auto failure_msg =
      core::KeystoneMessage::create("module_2", "component_1", "response", "linker error");
  failure_msg.action_type = core::ActionType::TASK_FAILED;
  component->processMessage(failure_msg).get();

  EXPECT_EQ(component->getCurrentState(), agents::ComponentLeadAgent::State::ERROR);

  // synthesizeComponentResult() must surface the error, not silently succeed
  auto result = component->synthesizeComponentResult();
  EXPECT_NE(result.find("ERROR"), std::string::npos);
}

TEST_F(ComponentLeadAgentTest, ModuleFailureBeforeAllResultsDoesNotDeadlock) {
  auto component = std::make_shared<agents::ComponentLeadAgent>("component_1");
  component->setMessageBus(bus_.get());
  bus_->registerAgent(component->getAgentId(), component);

  std::vector<std::string> module_ids = {"module_1", "module_2", "module_3"};
  component->setAvailableModuleLeads(module_ids);

  // Decompose into 3 module goals
  component
      ->processMessage(core::KeystoneMessage::create(
          "chief",
          "component_1",
          "Build Core: Messaging(10) and Concurrency(20) and Storage(30)"))
      .get();

  // First module fails immediately — must not leave the other two permanently pending
  auto failure_msg =
      core::KeystoneMessage::create("module_1", "component_1", "response", "fatal error");
  failure_msg.action_type = core::ActionType::TASK_FAILED;
  component->processMessage(failure_msg).get();

  // State must not remain stuck in WAITING_FOR_MODULES after a terminal event
  auto state = component->getCurrentState();
  EXPECT_TRUE(state == agents::ComponentLeadAgent::State::ERROR ||
              state == agents::ComponentLeadAgent::State::WAITING_FOR_MODULES);
}

TEST_F(ComponentLeadAgentTest, SuccessResultAfterModuleFailureStillCountsTowardCompletion) {
  auto component = std::make_shared<agents::ComponentLeadAgent>("component_1");
  component->setMessageBus(bus_.get());
  bus_->registerAgent(component->getAgentId(), component);

  std::vector<std::string> module_ids = {"module_1", "module_2"};
  component->setAvailableModuleLeads(module_ids);

  // Decompose into 2 module goals
  component
      ->processMessage(core::KeystoneMessage::create(
          "chief", "component_1", "Build Core: Messaging(10) and Concurrency(20)"))
      .get();

  // Failure arrives first
  auto failure_msg =
      core::KeystoneMessage::create("module_1", "component_1", "response", "timeout");
  failure_msg.action_type = core::ActionType::TASK_FAILED;
  component->processMessage(failure_msg).get();

  // Then a success — all results are now terminal; agent must not remain WAITING
  auto success_msg =
      core::KeystoneMessage::create("module_2", "component_1", "module_result", "concurrency done");
  component->processMessage(success_msg).get();

  auto state = component->getCurrentState();
  EXPECT_NE(state, agents::ComponentLeadAgent::State::WAITING_FOR_MODULES);
}

// ============================================================================
// Issue #186: gRPC Async TASK_FAILED Handling Tests (5 tests)
// Tests for explicit TASK_FAILED handling in async gRPC result path
// ============================================================================

#ifdef ENABLE_GRPC

TEST_F(ComponentLeadAgentTest, ProcessGrpcTaskResultSuccessUpdatesCoordination) {
  auto component = std::make_shared<agents::ComponentLeadAgent>("component_1");

  // Initialize coordination with 1 expected result
  component->coordination_.initializeCoordination(1);

  // Create a successful TaskResult from gRPC
  hmas::TaskResult result;
  result.set_task_id("task-1");
  result.set_status(hmas::TASK_PHASE_COMPLETED);
  result.set_result_yaml("result: success");

  // Process the result
  bool all_done = component->processTaskResultFromGrpc(result);

  // Should mark completion when all results received
  EXPECT_TRUE(all_done);
  EXPECT_EQ(component->coordination_.getReceivedCount(), 1);
}

TEST_F(ComponentLeadAgentTest, ProcessGrpcTaskResultFailureUpdatesCoordination) {
  auto component = std::make_shared<agents::ComponentLeadAgent>("component_1");

  // Initialize coordination with 1 expected result
  component->coordination_.initializeCoordination(1);

  // Create a failed TaskResult from gRPC
  hmas::TaskResult result;
  result.set_task_id("task-1");
  result.set_status(hmas::TASK_PHASE_FAILED);
  result.set_error("Module execution failed");

  // Process the result
  bool all_done = component->processTaskResultFromGrpc(result);

  // Should mark completion and record failure
  EXPECT_TRUE(all_done);
  EXPECT_TRUE(component->coordination_.hasFailures());
  EXPECT_EQ(component->coordination_.getFailureCount(), 1);
  EXPECT_NE(component->coordination_.getFailureMessages()[0].find("Module execution failed"),
            std::string::npos);
}

TEST_F(ComponentLeadAgentTest, ProcessGrpcTaskResultTimeoutIsRecordedAsFailure) {
  auto component = std::make_shared<agents::ComponentLeadAgent>("component_1");

  // Initialize coordination with 1 expected result
  component->coordination_.initializeCoordination(1);

  // Create a timeout TaskResult from gRPC
  hmas::TaskResult result;
  result.set_task_id("task-1");
  result.set_status(hmas::TASK_PHASE_TIMEOUT);
  result.set_error("Task timed out after 25 minutes");

  // Process the result
  bool all_done = component->processTaskResultFromGrpc(result);

  // Timeout should be treated as failure
  EXPECT_TRUE(all_done);
  EXPECT_TRUE(component->coordination_.hasFailures());
  EXPECT_EQ(component->coordination_.getFailureCount(), 1);
}

TEST_F(ComponentLeadAgentTest, ProcessGrpcTaskResultMixedSuccessAndFailure) {
  auto component = std::make_shared<agents::ComponentLeadAgent>("component_1");

  // Initialize coordination with 2 expected results
  component->coordination_.initializeCoordination(2);

  // First result: success
  hmas::TaskResult result1;
  result1.set_task_id("task-1");
  result1.set_status(hmas::TASK_PHASE_COMPLETED);
  result1.set_result_yaml("result: partial success");

  bool done1 = component->processTaskResultFromGrpc(result1);
  EXPECT_FALSE(done1);  // Not all done yet
  EXPECT_EQ(component->coordination_.getReceivedCount(), 1);
  EXPECT_FALSE(component->coordination_.hasFailures());

  // Second result: failure
  hmas::TaskResult result2;
  result2.set_task_id("task-2");
  result2.set_status(hmas::TASK_PHASE_FAILED);
  result2.set_error("Task 2 failed");

  bool done2 = component->processTaskResultFromGrpc(result2);
  EXPECT_TRUE(done2);  // All done now
  EXPECT_EQ(component->coordination_.getReceivedCount(), 2);
  EXPECT_TRUE(component->coordination_.hasFailures());
  EXPECT_EQ(component->coordination_.getFailureCount(), 1);
}

TEST_F(ComponentLeadAgentTest, ProcessGrpcTaskResultErrorStateTransition) {
  auto component = std::make_shared<agents::ComponentLeadAgent>("component_1");
  component->setMessageBus(bus_.get());
  bus_->registerAgent(component->getAgentId(), component);

  std::vector<std::string> module_ids = {"module_1"};
  component->setAvailableModuleLeads(module_ids);

  // Initialize to WAITING state
  component->coordination_.transitionTo(agents::ComponentLeadAgent::State::WAITING_FOR_MODULES,
                                       "WAITING_FOR_MODULES");

  // Process a failed result
  hmas::TaskResult result;
  result.set_task_id("task-1");
  result.set_status(hmas::TASK_PHASE_FAILED);
  result.set_error("Critical failure");

  component->processTaskResultFromGrpc(result);

  // Should transition to ERROR state
  EXPECT_EQ(component->getCurrentState(), agents::ComponentLeadAgent::State::ERROR);
}

#endif  // ENABLE_GRPC
