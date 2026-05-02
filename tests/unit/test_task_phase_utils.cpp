/**
 * @file test_task_phase_utils.cpp
 * @brief Tests for task phase utilities including isTerminalPhase() and the
 *        new TASK_PHASE_ERROR variant (issue #95).
 */

#include <gtest/gtest.h>

#include "network/hmas_coordinator_service.hpp"
#include "network/task_phase_utils.hpp"

using namespace keystone::network;

// ============================================================================
// isTerminalPhase()
// ============================================================================

TEST(TaskPhaseUtilsTest, NonTerminalPhasesReturnFalse) {
  EXPECT_FALSE(isTerminalPhase(hmas::TASK_PHASE_UNSPECIFIED));
  EXPECT_FALSE(isTerminalPhase(hmas::TASK_PHASE_PENDING));
  EXPECT_FALSE(isTerminalPhase(hmas::TASK_PHASE_PLANNING));
  EXPECT_FALSE(isTerminalPhase(hmas::TASK_PHASE_WAITING));
  EXPECT_FALSE(isTerminalPhase(hmas::TASK_PHASE_EXECUTING));
  EXPECT_FALSE(isTerminalPhase(hmas::TASK_PHASE_SYNTHESIZING));
}

TEST(TaskPhaseUtilsTest, TerminalPhasesReturnTrue) {
  EXPECT_TRUE(isTerminalPhase(hmas::TASK_PHASE_COMPLETED));
  EXPECT_TRUE(isTerminalPhase(hmas::TASK_PHASE_FAILED));
  EXPECT_TRUE(isTerminalPhase(hmas::TASK_PHASE_ERROR));
  EXPECT_TRUE(isTerminalPhase(hmas::TASK_PHASE_TIMEOUT));
  EXPECT_TRUE(isTerminalPhase(hmas::TASK_PHASE_CANCELLED));
}

// ============================================================================
// Coordinator: CancelTask blocks on all terminal states (including
// ERROR/TIMEOUT)
// ============================================================================

class CoordinatorTerminalStateTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto registry = std::make_shared<ServiceRegistry>();
    auto router = std::make_shared<TaskRouter>(registry);
    coordinator_ =
        std::make_shared<HMASCoordinatorServiceImpl>(registry, router);

    // Pre-populate tasks in various terminal states
    for (const auto& [id, phase] :
         std::initializer_list<std::pair<std::string, hmas::TaskPhase>>{
             {"task-completed", hmas::TASK_PHASE_COMPLETED},
             {"task-failed", hmas::TASK_PHASE_FAILED},
             {"task-error", hmas::TASK_PHASE_ERROR},
             {"task-timeout", hmas::TASK_PHASE_TIMEOUT},
             {"task-cancelled", hmas::TASK_PHASE_CANCELLED},
         }) {
      coordinator_->updateTaskStatus(id, phase);
    }
  }

  std::shared_ptr<HMASCoordinatorServiceImpl> coordinator_;
};

TEST_F(CoordinatorTerminalStateTest, CancelReturnsFalseForAllTerminalStates) {
  grpc::ServerContext ctx;
  hmas::CancelRequest req;
  hmas::CancelResponse resp;

  for (const auto& task_id : {"task-completed", "task-failed", "task-error",
                              "task-timeout", "task-cancelled"}) {
    resp.Clear();
    req.set_task_id(task_id);
    auto status = coordinator_->CancelTask(&ctx, &req, &resp);

    EXPECT_TRUE(status.ok()) << "CancelTask RPC failed for " << task_id;
    EXPECT_FALSE(resp.cancelled())
        << "Expected cancelled=false for terminal task " << task_id;
    EXPECT_EQ(resp.message(), "Task already in terminal state")
        << "Wrong message for " << task_id;
  }
}

TEST_F(CoordinatorTerminalStateTest,
       GetTaskProgressIsCompleteForAllTerminalStates) {
  grpc::ServerContext ctx;
  hmas::TaskProgressRequest req;
  hmas::TaskProgress resp;

  for (const auto& task_id : {"task-completed", "task-failed", "task-error",
                              "task-timeout", "task-cancelled"}) {
    resp.Clear();
    req.set_task_id(task_id);
    auto status = coordinator_->GetTaskProgress(&ctx, &req, &resp);

    EXPECT_TRUE(status.ok()) << "GetTaskProgress failed for " << task_id;
    EXPECT_TRUE(resp.is_complete())
        << "Expected is_complete=true for terminal task " << task_id;
  }
}

TEST_F(CoordinatorTerminalStateTest, CleanupRemovesAllTerminalStates) {
  // All 5 terminal tasks were set at SetUp; pass age_threshold_ms=0 to force
  // cleanup
  int32_t removed = coordinator_->cleanupOldTasks(0);
  EXPECT_EQ(removed, 5);

  for (const auto& task_id : {"task-completed", "task-failed", "task-error",
                              "task-timeout", "task-cancelled"}) {
    EXPECT_FALSE(coordinator_->hasTask(task_id))
        << "Task should have been cleaned: " << task_id;
  }
}

// ============================================================================
// phaseToString / stringToPhase round-trips (via coordinator private methods;
// tested indirectly through public updateTaskStatus + getTaskState)
// ============================================================================

TEST_F(CoordinatorTerminalStateTest, ErrorPhaseIsTrackedCorrectly) {
  auto state = coordinator_->getTaskState("task-error");
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(state->phase, hmas::TASK_PHASE_ERROR);
}
