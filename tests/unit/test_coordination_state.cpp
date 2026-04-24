/**
 * @file test_coordination_state.cpp
 * @brief Unit tests for CoordinationState template
 *
 * Stream B Phase B1: CoordinationState eliminates ~287 lines of duplication
 * between ComponentLeadAgent and ModuleLeadAgent.
 *
 * This test suite validates:
 * - State machine transitions
 * - Result tracking and aggregation
 * - Child agent coordination
 * - Thread safety
 * - gRPC integration (if ENABLE_GRPC=ON)
 *
 * Coverage Target: 100% of CoordinationState public methods
 */

#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "agents/coordination_state.hpp"

#ifdef ENABLE_GRPC
#include "fixtures/grpc_test_fixture.hpp"
#endif

using namespace keystone::agents;

// Test state enum matching ComponentLead/ModuleLead patterns
enum class TestState {
  IDLE,
  PLANNING,
  WAITING,
  SYNTHESIZING,
  AGGREGATING,
  ERROR
};

// ============================================================================
// Test Fixture
// ============================================================================

class CoordinationStateTest : public ::testing::Test {
 protected:
  CoordinationState<TestState, std::string> state_;
};

// ============================================================================
// Category 1: State Machine Tests (8 tests)
// ============================================================================

/**
 * @brief Test 1: Initial state is IDLE
 */
TEST_F(CoordinationStateTest, InitialStateIsIdle) {
  EXPECT_EQ(state_.getCurrentState(), TestState::IDLE);
}

/**
 * @brief Test 2: Transition to valid state
 */
TEST_F(CoordinationStateTest, TransitionToValidState) {
  state_.transitionTo(TestState::PLANNING, "PLANNING");
  EXPECT_EQ(state_.getCurrentState(), TestState::PLANNING);
}

/**
 * @brief Test 3: Full state transition sequence
 */
TEST_F(CoordinationStateTest, TransitionSequence) {
  // IDLE → PLANNING
  state_.transitionTo(TestState::PLANNING, "PLANNING");
  EXPECT_EQ(state_.getCurrentState(), TestState::PLANNING);

  // PLANNING → WAITING
  state_.transitionTo(TestState::WAITING, "WAITING");
  EXPECT_EQ(state_.getCurrentState(), TestState::WAITING);

  // WAITING → SYNTHESIZING
  state_.transitionTo(TestState::SYNTHESIZING, "SYNTHESIZING");
  EXPECT_EQ(state_.getCurrentState(), TestState::SYNTHESIZING);

  // SYNTHESIZING → IDLE (complete cycle)
  state_.transitionTo(TestState::IDLE, "IDLE");
  EXPECT_EQ(state_.getCurrentState(), TestState::IDLE);
}

/**
 * @brief Test 4: getCurrentState returns correct state
 */
TEST_F(CoordinationStateTest, GetCurrentState) {
  state_.transitionTo(TestState::AGGREGATING, "AGGREGATING");
  EXPECT_EQ(state_.getCurrentState(), TestState::AGGREGATING);

  state_.transitionTo(TestState::ERROR, "ERROR");
  EXPECT_EQ(state_.getCurrentState(), TestState::ERROR);
}

/**
 * @brief Test 5: Multiple transitions to same state
 */
TEST_F(CoordinationStateTest, MultipleTransitionsToSameState) {
  state_.transitionTo(TestState::PLANNING, "PLANNING");
  EXPECT_EQ(state_.getCurrentState(), TestState::PLANNING);

  // Transition to PLANNING again
  state_.transitionTo(TestState::PLANNING, "PLANNING");
  EXPECT_EQ(state_.getCurrentState(), TestState::PLANNING);
}

/**
 * @brief Test 6: State persists across other method calls
 */
TEST_F(CoordinationStateTest, StatePersistence) {
  state_.transitionTo(TestState::WAITING, "WAITING");

  // Call other methods
  state_.setCurrentGoal("test_goal");
  state_.setRequesterId("requester_1");
  state_.initializeCoordination(3);

  // State should still be WAITING
  EXPECT_EQ(state_.getCurrentState(), TestState::WAITING);
}

/**
 * @brief Test 7: Thread-safe state transitions
 */
TEST_F(CoordinationStateTest, ThreadSafeStateTransitions) {
  constexpr int NUM_THREADS = 10;
  std::vector<std::thread> threads;

  // 10 threads concurrently transition states
  for (int32_t i = 0; i < NUM_THREADS; ++i) {
    threads.emplace_back([this, i]() {
      TestState target =
          (i % 2 == 0) ? TestState::PLANNING : TestState::WAITING;
      std::string name = (i % 2 == 0) ? "PLANNING" : "WAITING";
      state_.transitionTo(target, name);
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Final state should be valid (no crash = success)
  auto final_state = state_.getCurrentState();
  EXPECT_TRUE(final_state == TestState::PLANNING ||
              final_state == TestState::WAITING);
}

/**
 * @brief Test 8: State name is recorded in execution trace
 */
TEST_F(CoordinationStateTest, StateNameLogging) {
  state_.transitionTo(TestState::PLANNING, "PLANNING");
  state_.transitionTo(TestState::WAITING, "WAITING");
  state_.transitionTo(TestState::SYNTHESIZING, "SYNTHESIZING");

  const auto& trace = state_.getExecutionTrace();
  ASSERT_EQ(trace.size(), 3);
  EXPECT_EQ(trace[0], "PLANNING");
  EXPECT_EQ(trace[1], "WAITING");
  EXPECT_EQ(trace[2], "SYNTHESIZING");
}

// ============================================================================
// Category 2: Result Tracking Tests (10 tests)
// ============================================================================

/**
 * @brief Test 9: Record single result
 */
TEST_F(CoordinationStateTest, RecordSingleResult) {
  state_.initializeCoordination(1);
  EXPECT_TRUE(state_.recordResult("result_1"));

  auto results = state_.getResults();
  ASSERT_EQ(results.size(), 1);
  EXPECT_EQ(results[0], "result_1");
}

/**
 * @brief Test 10: Record multiple results
 */
TEST_F(CoordinationStateTest, RecordMultipleResults) {
  state_.initializeCoordination(3);

  EXPECT_FALSE(state_.recordResult("result_1"));
  EXPECT_FALSE(state_.recordResult("result_2"));
  EXPECT_TRUE(state_.recordResult("result_3"));  // Last result -> isComplete

  auto results = state_.getResults();
  ASSERT_EQ(results.size(), 3);
  EXPECT_EQ(results[0], "result_1");
  EXPECT_EQ(results[1], "result_2");
  EXPECT_EQ(results[2], "result_3");
}

/**
 * @brief Test 11: isComplete with expected count
 */
TEST_F(CoordinationStateTest, IsCompleteWithExpectedCount) {
  state_.initializeCoordination(3);

  EXPECT_FALSE(state_.isComplete());

  state_.recordResult("result_1");
  EXPECT_FALSE(state_.isComplete());

  state_.recordResult("result_2");
  EXPECT_FALSE(state_.isComplete());

  state_.recordResult("result_3");
  EXPECT_TRUE(state_.isComplete());
}

/**
 * @brief Test 12: isComplete with zero expected
 */
TEST_F(CoordinationStateTest, IsCompleteWithZeroExpected) {
  state_.initializeCoordination(0);
  EXPECT_TRUE(state_.isComplete());
}

/**
 * @brief Test 13: Record more results than expected
 */
TEST_F(CoordinationStateTest, RecordMoreThanExpected) {
  state_.initializeCoordination(2);

  state_.recordResult("result_1");
  state_.recordResult("result_2");
  EXPECT_TRUE(state_.isComplete());

  // Record extra result
  state_.recordResult("result_3");
  EXPECT_TRUE(state_.isComplete());

  auto results = state_.getResults();
  EXPECT_EQ(results.size(), 3u);
}

/**
 * @brief Test 14: getResults before recording
 */
TEST_F(CoordinationStateTest, GetResultsBeforeRecording) {
  auto results = state_.getResults();
  EXPECT_TRUE(results.empty());
}

/**
 * @brief Test 15: Thread-safe result recording
 */
TEST_F(CoordinationStateTest, RecordResultThreadSafety) {
  constexpr int NUM_THREADS = 10;
  state_.initializeCoordination(NUM_THREADS);

  std::vector<std::thread> threads;
  for (int32_t i = 0; i < NUM_THREADS; ++i) {
    threads.emplace_back(
        [this, i]() { state_.recordResult("result_" + std::to_string(i)); });
  }

  for (auto& t : threads) {
    t.join();
  }

  // All results should be recorded
  auto results = state_.getResults();
  EXPECT_EQ(results.size(), NUM_THREADS);
  EXPECT_TRUE(state_.isComplete());
}

/**
 * @brief Test 16: Custom result type (integers)
 */
TEST_F(CoordinationStateTest, ResultTypeCustom) {
  CoordinationState<TestState, int> int_state;
  int_state.initializeCoordination(3);

  int_state.recordResult(42);
  int_state.recordResult(100);
  int_state.recordResult(200);

  auto results = int_state.getResults();
  ASSERT_EQ(results.size(), 3);
  EXPECT_EQ(results[0], 42);
  EXPECT_EQ(results[1], 100);
  EXPECT_EQ(results[2], 200);
}

/**
 * @brief Test 17: initializeCoordination clears previous results
 */
TEST_F(CoordinationStateTest, InitializeClearsPreviousResults) {
  state_.initializeCoordination(2);
  state_.recordResult("old_1");
  state_.recordResult("old_2");

  EXPECT_EQ(state_.getResults().size(), 2u);

  // Re-initialize
  state_.initializeCoordination(1);
  EXPECT_EQ(state_.getResults().size(), 0u);
  EXPECT_EQ(state_.getReceivedCount(), 0);

  state_.recordResult("new_1");
  auto results = state_.getResults();
  ASSERT_EQ(results.size(), 1);
  EXPECT_EQ(results[0], "new_1");
}

/**
 * @brief Test 18: Result order is preserved
 */
TEST_F(CoordinationStateTest, ResultOrderPreserved) {
  state_.initializeCoordination(5);

  state_.recordResult("A");
  state_.recordResult("B");
  state_.recordResult("C");
  state_.recordResult("D");
  state_.recordResult("E");

  auto results = state_.getResults();
  ASSERT_EQ(results.size(), 5);
  EXPECT_EQ(results[0], "A");
  EXPECT_EQ(results[1], "B");
  EXPECT_EQ(results[2], "C");
  EXPECT_EQ(results[3], "D");
  EXPECT_EQ(results[4], "E");
}

// ============================================================================
// Category 3: Child Tracking Tests (6 tests)
// ============================================================================

/**
 * @brief Test 19: getExpectedCount after initialization
 */
TEST_F(CoordinationStateTest, GetExpectedChildCount) {
  state_.initializeCoordination(5);
  EXPECT_EQ(state_.getExpectedCount(), 5);
}

/**
 * @brief Test 20: Default child count is zero (before initialization)
 */
TEST_F(CoordinationStateTest, DefaultChildCountIsZero) {
  EXPECT_EQ(state_.getExpectedCount(), 0);
  EXPECT_EQ(state_.getReceivedCount(), 0);
}

/**
 * @brief Test 21: Update expected child count
 */
TEST_F(CoordinationStateTest, UpdateExpectedChildCount) {
  state_.initializeCoordination(3);
  EXPECT_EQ(state_.getExpectedCount(), 3);

  state_.initializeCoordination(5);
  EXPECT_EQ(state_.getExpectedCount(), 5);
}

/**
 * @brief Test 22: isComplete matches child count
 */
TEST_F(CoordinationStateTest, IsCompleteMatchesChildCount) {
  state_.initializeCoordination(3);

  state_.recordResult("child_1");
  state_.recordResult("child_2");
  EXPECT_FALSE(state_.isComplete());

  state_.recordResult("child_3");
  EXPECT_TRUE(state_.isComplete());
}

/**
 * @brief Test 23: Child count with no results
 */
TEST_F(CoordinationStateTest, ChildCountWithNoResults) {
  state_.initializeCoordination(5);
  EXPECT_EQ(state_.getExpectedCount(), 5);
  EXPECT_FALSE(state_.isComplete());
}

/**
 * @brief Test 24: Track pending subordinates
 */
TEST_F(CoordinationStateTest, TrackPendingSubordinates) {
  state_.trackPendingSubordinate("msg_1", "subordinate_1");
  state_.trackPendingSubordinate("msg_2", "subordinate_2");
  state_.trackPendingSubordinate("msg_3", "subordinate_3");

  const auto& pending = state_.getPendingSubordinates();
  EXPECT_EQ(pending.size(), 3u);
  EXPECT_EQ(pending.at("msg_1"), "subordinate_1");
  EXPECT_EQ(pending.at("msg_2"), "subordinate_2");
  EXPECT_EQ(pending.at("msg_3"), "subordinate_3");
}

// ============================================================================
// Category 4: Integration Tests (4 tests)
// ============================================================================

/**
 * @brief Test 25: Complete ComponentLead workflow
 */
TEST_F(CoordinationStateTest, CompleteComponentLeadWorkflow) {
  // IDLE → PLANNING
  state_.transitionTo(TestState::PLANNING, "PLANNING");
  EXPECT_EQ(state_.getCurrentState(), TestState::PLANNING);

  // Set up coordination for 2 modules
  state_.initializeCoordination(2);
  state_.setCurrentGoal("build_component_X");
  state_.setRequesterId("chief_architect");

  // PLANNING → WAITING
  state_.transitionTo(TestState::WAITING, "WAITING_FOR_MODULES");
  EXPECT_FALSE(state_.isComplete());

  // Receive module results
  state_.recordResult("module_1_result");
  EXPECT_FALSE(state_.isComplete());

  state_.recordResult("module_2_result");
  EXPECT_TRUE(state_.isComplete());

  // WAITING → AGGREGATING
  state_.transitionTo(TestState::AGGREGATING, "AGGREGATING");

  // AGGREGATING → IDLE (complete)
  state_.transitionTo(TestState::IDLE, "IDLE");
  EXPECT_EQ(state_.getCurrentState(), TestState::IDLE);

  // Verify execution trace
  const auto& trace = state_.getExecutionTrace();
  EXPECT_EQ(trace.size(), 4u);
}

/**
 * @brief Test 26: Complete ModuleLead workflow
 */
TEST_F(CoordinationStateTest, CompleteModuleLeadWorkflow) {
  // IDLE → PLANNING
  state_.transitionTo(TestState::PLANNING, "PLANNING");

  // Set up coordination for 3 tasks
  state_.initializeCoordination(3);
  state_.setCurrentGoal("implement_module_Y");

  // PLANNING → WAITING
  state_.transitionTo(TestState::WAITING, "WAITING_FOR_TASKS");

  // Receive task results
  state_.recordResult("task_1_result");
  state_.recordResult("task_2_result");
  state_.recordResult("task_3_result");
  EXPECT_TRUE(state_.isComplete());

  // WAITING → SYNTHESIZING
  state_.transitionTo(TestState::SYNTHESIZING, "SYNTHESIZING");

  // SYNTHESIZING → IDLE
  state_.transitionTo(TestState::IDLE, "IDLE");
  EXPECT_EQ(state_.getCurrentState(), TestState::IDLE);
}

/**
 * @brief Test 27: Multiple workflow cycles
 */
TEST_F(CoordinationStateTest, MultipleWorkflowCycles) {
  // Cycle 1
  state_.transitionTo(TestState::PLANNING, "PLANNING");
  state_.initializeCoordination(2);
  state_.recordResult("result_1");
  state_.recordResult("result_2");
  EXPECT_TRUE(state_.isComplete());
  state_.transitionTo(TestState::IDLE, "IDLE");

  // Cycle 2 (re-initialize)
  state_.transitionTo(TestState::PLANNING, "PLANNING");
  state_.initializeCoordination(3);
  EXPECT_FALSE(state_.isComplete());
  state_.recordResult("result_3");
  state_.recordResult("result_4");
  state_.recordResult("result_5");
  EXPECT_TRUE(state_.isComplete());
  state_.transitionTo(TestState::IDLE, "IDLE");

  // Cycle 3
  state_.transitionTo(TestState::PLANNING, "PLANNING");
  state_.initializeCoordination(1);
  state_.recordResult("result_6");
  EXPECT_TRUE(state_.isComplete());
  state_.transitionTo(TestState::IDLE, "IDLE");

  EXPECT_EQ(state_.getCurrentState(), TestState::IDLE);
}

/**
 * @brief Test 28: Concurrent workflows (stress test)
 */
TEST_F(CoordinationStateTest, ConcurrentWorkflows) {
  constexpr int NUM_THREADS = 5;
  std::vector<std::thread> threads;

  for (int32_t i = 0; i < NUM_THREADS; ++i) {
    threads.emplace_back([this, i]() {
      // Each thread runs a mini-workflow
      state_.transitionTo(TestState::PLANNING, "PLANNING_" + std::to_string(i));
      state_.transitionTo(TestState::WAITING, "WAITING_" + std::to_string(i));
      state_.transitionTo(TestState::IDLE, "IDLE_" + std::to_string(i));
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Verify execution trace contains all transitions
  const auto& trace = state_.getExecutionTrace();
  EXPECT_EQ(trace.size(), NUM_THREADS * 3);
}

// ============================================================================
// Category 5: gRPC Tests (Optional - if ENABLE_GRPC=ON)
// ============================================================================

#ifdef ENABLE_GRPC

/**
 * @brief Test 29: Initialize gRPC with valid config (uses in-process test
 * servers)
 */
class GrpcCoordinationStateTest : public test::GrpcTestFixture {
 protected:
  CoordinationState<TestState, std::string> state_;
};

TEST_F(GrpcCoordinationStateTest, InitializeGrpcWithValidConfig) {
  // Use in-process test servers from fixture
  auto coord_addr = getCoordinatorAddress();
  auto registry_addr = getRegistryAddress();

  EXPECT_NO_THROW(state_.initializeGrpc("test_agent", coord_addr, registry_addr,
                                        "TestAgent", 2,
                                        {"capability1", "capability2"}, 10));
}

/**
 * @brief Test 30: Initialize gRPC with invalid config (empty addresses)
 */
TEST_F(CoordinationStateTest, InitializeGrpcWithInvalidConfig) {
  // Empty addresses should eventually cause connection failure
  // We test that initialization doesn't crash immediately
  EXPECT_NO_THROW(state_.initializeGrpc("test_agent",
                                        "",  // Empty coordinator address
                                        "",  // Empty registry address
                                        "TestAgent", 2, {}, 10));
}

/**
 * @brief Test 31: Query available children returns empty without initialization
 */
TEST_F(CoordinationStateTest, QueryAvailableChildrenWithoutInit) {
  auto children = state_.queryAvailableChildren("ModuleLead");
  EXPECT_TRUE(children.empty());
}

/**
 * @brief Test 32: Query available children by type (uses in-process test
 * servers)
 */
TEST_F(GrpcCoordinationStateTest, QueryAvailableChildrenByType) {
  // Use in-process test servers from fixture
  auto coord_addr = getCoordinatorAddress();
  auto registry_addr = getRegistryAddress();

  // Initialize gRPC with in-process servers
  state_.initializeGrpc("test_component", coord_addr, registry_addr,
                        "ComponentLead", 1, {"coordination"}, 5);

  // Register some test agents in the ServiceRegistry
  auto registry = getRegistry();
  registry->registerAgent("module_lead_1", "ModuleLead", 2, "localhost:60001",
                          {"task_coordination"});
  registry->registerAgent("module_lead_2", "ModuleLead", 2, "localhost:60002",
                          {"task_coordination"});

  auto children = state_.queryAvailableChildren("ModuleLead");
  // Should find the 2 registered ModuleLead agents
  EXPECT_EQ(children.size(), 2u);
  EXPECT_EQ(children[0], "module_lead_1");
  EXPECT_EQ(children[1], "module_lead_2");
}

/**
 * @brief Test 33: Shutdown gRPC connections
 */
TEST_F(CoordinationStateTest, ShutdownGrpc) {
  EXPECT_NO_THROW(state_.shutdownGrpc());
}

/**
 * @brief Test 34: Get/Set current task ID (gRPC YAML processing)
 */
TEST_F(CoordinationStateTest, CurrentTaskId) {
  state_.setCurrentTaskId("task_12345");
  EXPECT_EQ(state_.getCurrentTaskId(), "task_12345");

  state_.setCurrentTaskId("task_67890");
  EXPECT_EQ(state_.getCurrentTaskId(), "task_67890");
}

#endif  // ENABLE_GRPC

// ============================================================================
// Category 6: Context Management Tests (2 tests)
// ============================================================================

/**
 * @brief Test: Get/Set current goal
 */
TEST_F(CoordinationStateTest, CurrentGoal) {
  state_.setCurrentGoal("implement_feature_X");
  EXPECT_EQ(state_.getCurrentGoal(), "implement_feature_X");

  state_.setCurrentGoal("refactor_module_Y");
  EXPECT_EQ(state_.getCurrentGoal(), "refactor_module_Y");
}

/**
 * @brief Test: Get/Set requester ID
 */
TEST_F(CoordinationStateTest, RequesterId) {
  state_.setRequesterId("chief_architect_001");
  EXPECT_EQ(state_.getRequesterId(), "chief_architect_001");

  state_.setRequesterId("component_lead_002");
  EXPECT_EQ(state_.getRequesterId(), "component_lead_002");
}

// ============================================================================
// Category 7: Failure Tracking Tests — Issue #87 (DAG deadlock prevention)
// ============================================================================

TEST_F(CoordinationStateTest, HasNoFailuresInitially) {
  state_.initializeCoordination(3);
  EXPECT_FALSE(state_.hasFailures());
  EXPECT_EQ(state_.getFailureCount(), 0u);
  EXPECT_TRUE(state_.getFailureMessages().empty());
}

TEST_F(CoordinationStateTest, RecordFailureCountsAsTerminal) {
  state_.initializeCoordination(1);
  bool all_done = state_.recordFailure("task execution error");
  EXPECT_TRUE(all_done);
  EXPECT_TRUE(state_.hasFailures());
  EXPECT_EQ(state_.getFailureCount(), 1u);
}

TEST_F(CoordinationStateTest, RecordFailurePreservesMessage) {
  state_.initializeCoordination(2);
  state_.recordFailure("first error");
  state_.recordFailure("second error");

  auto msgs = state_.getFailureMessages();
  ASSERT_EQ(msgs.size(), 2u);
  EXPECT_EQ(msgs[0], "first error");
  EXPECT_EQ(msgs[1], "second error");
}

TEST_F(CoordinationStateTest, MixedSuccessAndFailureReachesCompletion) {
  state_.initializeCoordination(3);
  EXPECT_FALSE(state_.recordResult("ok_result"));
  EXPECT_FALSE(state_.recordFailure("subtask failed"));
  EXPECT_TRUE(state_.recordResult("another_ok"));

  EXPECT_TRUE(state_.isComplete());
  EXPECT_TRUE(state_.hasFailures());
  EXPECT_EQ(state_.getFailureCount(), 1u);
  EXPECT_EQ(state_.getResults().size(), 2u);
}

TEST_F(CoordinationStateTest, AllFailuresReachCompletion) {
  state_.initializeCoordination(3);
  EXPECT_FALSE(state_.recordFailure("err1"));
  EXPECT_FALSE(state_.recordFailure("err2"));
  EXPECT_TRUE(state_.recordFailure("err3"));

  EXPECT_TRUE(state_.isComplete());
  EXPECT_EQ(state_.getFailureCount(), 3u);
  EXPECT_EQ(state_.getResults().size(), 0u);
}

TEST_F(CoordinationStateTest, InitializeClearsPreviousFailures) {
  state_.initializeCoordination(1);
  state_.recordFailure("old error");

  EXPECT_TRUE(state_.hasFailures());

  state_.initializeCoordination(2);
  EXPECT_FALSE(state_.hasFailures());
  EXPECT_EQ(state_.getFailureCount(), 0u);
  EXPECT_TRUE(state_.getFailureMessages().empty());
}

TEST_F(CoordinationStateTest, RecordFailureThreadSafety) {
  constexpr int NUM_THREADS = 10;
  state_.initializeCoordination(NUM_THREADS);

  std::vector<std::thread> threads;
  for (int32_t i = 0; i < NUM_THREADS; ++i) {
    threads.emplace_back(
        [this, i]() { state_.recordFailure("error_" + std::to_string(i)); });
  }
  for (auto& t : threads) {
    t.join();
  }

  EXPECT_TRUE(state_.isComplete());
  EXPECT_EQ(state_.getFailureCount(), NUM_THREADS);
}

// ============================================================================
// Test Summary
// ============================================================================

/**
 * Total Tests:
 * - Category 1: State Machine (8 tests)
 * - Category 2: Result Tracking (10 tests)
 * - Category 3: Child Tracking (6 tests)
 * - Category 4: Integration (4 tests)
 * - Category 5: gRPC (6 tests - optional, 2 disabled)
 * - Category 6: Context (2 tests)
 * - Category 7: Failure Tracking — Issue #87 (7 tests)
 *
 * Total: 43 tests (39 always enabled, 4 gRPC-conditional)
 */
