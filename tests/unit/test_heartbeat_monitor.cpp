/**
 * @file test_heartbeat_monitor.cpp
 * @brief Unit tests for HeartbeatMonitor
 */

#include "core/heartbeat_monitor.hpp"

#include <thread>

#include <gtest/gtest.h>

using namespace keystone::core;

class HeartbeatMonitorTest : public ::testing::Test {
 protected:
  HeartbeatMonitor::Config default_config_{.heartbeat_interval = std::chrono::milliseconds(100),
                                           .timeout_threshold = std::chrono::milliseconds(300),
                                           .auto_remove_dead = false};
};

TEST_F(HeartbeatMonitorTest, DefaultConstruction) {
  HeartbeatMonitor monitor;

  EXPECT_EQ(monitor.getTotalFailures(), 0);
  EXPECT_TRUE(monitor.getRegisteredAgents().empty());
}

TEST_F(HeartbeatMonitorTest, RecordHeartbeat) {
  HeartbeatMonitor monitor(default_config_);

  monitor.recordHeartbeat("agent1");

  EXPECT_TRUE(monitor.isAlive("agent1"));
  EXPECT_EQ(monitor.getRegisteredAgents().size(), 1u);
}

TEST_F(HeartbeatMonitorTest, DetectFailure) {
  HeartbeatMonitor monitor(default_config_);

  monitor.recordHeartbeat("agent1");
  EXPECT_TRUE(monitor.isAlive("agent1"));

  // Wait longer than timeout threshold
  std::this_thread::sleep_for(std::chrono::milliseconds(350));

  // Check agents should detect failure
  int32_t failures = monitor.checkAgents();
  EXPECT_EQ(failures, 1);
  EXPECT_FALSE(monitor.isAlive("agent1"));
  EXPECT_EQ(monitor.getTotalFailures(), 1);
}

TEST_F(HeartbeatMonitorTest, AgentRecovery) {
  HeartbeatMonitor monitor(default_config_);

  monitor.recordHeartbeat("agent1");
  std::this_thread::sleep_for(std::chrono::milliseconds(350));
  monitor.checkAgents();

  EXPECT_FALSE(monitor.isAlive("agent1"));

  // Agent sends heartbeat again
  monitor.recordHeartbeat("agent1");
  EXPECT_TRUE(monitor.isAlive("agent1"));
}

TEST_F(HeartbeatMonitorTest, FailureCallback) {
  HeartbeatMonitor monitor(default_config_);

  std::string failed_agent;
  monitor.setFailureCallback(
      [&failed_agent](const std::string& agent_id) { failed_agent = agent_id; });

  monitor.recordHeartbeat("agent1");
  std::this_thread::sleep_for(std::chrono::milliseconds(350));
  monitor.checkAgents();

  EXPECT_EQ(failed_agent, "agent1");
}

TEST_F(HeartbeatMonitorTest, MultipleAgents) {
  HeartbeatMonitor monitor(default_config_);

  monitor.recordHeartbeat("agent1");
  monitor.recordHeartbeat("agent2");
  monitor.recordHeartbeat("agent3");

  EXPECT_EQ(monitor.getAliveAgents().size(), 3u);

  // Let agent2 timeout
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  monitor.recordHeartbeat("agent1");  // Keep agent1 alive
  monitor.recordHeartbeat("agent3");  // Keep agent3 alive

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  monitor.checkAgents();

  EXPECT_EQ(monitor.getAliveAgents().size(), 2u);
  EXPECT_EQ(monitor.getDeadAgents().size(), 1u);
}

// ---------------------------------------------------------------------------
// Extended coverage: getStatus, removeAgent, reset, auto-remove, recovery.
// ---------------------------------------------------------------------------

TEST_F(HeartbeatMonitorTest, GetStatusUnknownReturnsNullopt) {
  HeartbeatMonitor monitor(default_config_);
  EXPECT_FALSE(monitor.getStatus("ghost").has_value());
}

TEST_F(HeartbeatMonitorTest, GetStatusReflectsHeartbeatCount) {
  HeartbeatMonitor monitor(default_config_);
  monitor.recordHeartbeat("agent1");
  monitor.recordHeartbeat("agent1");

  auto status = monitor.getStatus("agent1");
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->agent_id, "agent1");
  EXPECT_EQ(status->total_heartbeats, 2u);
  EXPECT_TRUE(status->is_alive);
}

TEST_F(HeartbeatMonitorTest, RemoveAgentStopsTracking) {
  HeartbeatMonitor monitor(default_config_);
  monitor.recordHeartbeat("agent1");
  ASSERT_EQ(monitor.getRegisteredAgents().size(), 1u);

  monitor.removeAgent("agent1");
  EXPECT_TRUE(monitor.getRegisteredAgents().empty());
  EXPECT_FALSE(monitor.getStatus("agent1").has_value());
}

TEST_F(HeartbeatMonitorTest, RemoveUnknownAgentIsNoOp) {
  HeartbeatMonitor monitor(default_config_);
  monitor.recordHeartbeat("agent1");
  // Removing an agent that isn't tracked must not throw or drop others.
  EXPECT_NO_THROW(monitor.removeAgent("ghost"));
  EXPECT_EQ(monitor.getRegisteredAgents().size(), 1u);
}

TEST_F(HeartbeatMonitorTest, ResetClearsAgentsAndFailures) {
  HeartbeatMonitor monitor(default_config_);
  monitor.recordHeartbeat("agent1");
  std::this_thread::sleep_for(std::chrono::milliseconds(350));
  monitor.checkAgents();
  ASSERT_EQ(monitor.getTotalFailures(), 1u);

  monitor.reset();
  EXPECT_TRUE(monitor.getRegisteredAgents().empty());
  EXPECT_EQ(monitor.getTotalFailures(), 0u);
}

TEST_F(HeartbeatMonitorTest, RecoveryAfterFailureTakesRecoveredBranch) {
  HeartbeatMonitor monitor(default_config_);

  monitor.recordHeartbeat("agent1");
  std::this_thread::sleep_for(std::chrono::milliseconds(350));
  EXPECT_EQ(monitor.checkAgents(), 1u);

  auto before = monitor.getStatus("agent1");
  ASSERT_TRUE(before.has_value());
  EXPECT_FALSE(before->is_alive);

  // Heartbeat while dead exercises the was_dead recovery branch.
  monitor.recordHeartbeat("agent1");
  auto after = monitor.getStatus("agent1");
  ASSERT_TRUE(after.has_value());
  EXPECT_TRUE(after->is_alive);
  EXPECT_EQ(after->total_heartbeats, 2u);
}

TEST_F(HeartbeatMonitorTest, AutoRemoveDeadDropsFailedAgents) {
  HeartbeatMonitor::Config cfg{.heartbeat_interval = std::chrono::milliseconds(100),
                               .timeout_threshold = std::chrono::milliseconds(300),
                               .auto_remove_dead = true};
  HeartbeatMonitor monitor(cfg);

  monitor.recordHeartbeat("agent1");
  std::this_thread::sleep_for(std::chrono::milliseconds(350));

  EXPECT_EQ(monitor.checkAgents(), 1u);
  // auto_remove_dead=true → failed agent is erased from the registry.
  EXPECT_TRUE(monitor.getRegisteredAgents().empty());
  EXPECT_EQ(monitor.getTotalFailures(), 1u);
}

TEST_F(HeartbeatMonitorTest, CheckAgentsNoFailuresWhenAllFresh) {
  HeartbeatMonitor monitor(default_config_);
  monitor.recordHeartbeat("agent1");
  monitor.recordHeartbeat("agent2");
  // Both fresh → no newly-failed agents.
  EXPECT_EQ(monitor.checkAgents(), 0u);
}

TEST_F(HeartbeatMonitorTest, IsAliveUnknownAgentIsFalse) {
  HeartbeatMonitor monitor(default_config_);
  EXPECT_FALSE(monitor.isAlive("ghost"));
}

TEST_F(HeartbeatMonitorTest, GetDeadAgentsListsTimedOut) {
  HeartbeatMonitor monitor(default_config_);
  monitor.recordHeartbeat("agent1");
  std::this_thread::sleep_for(std::chrono::milliseconds(350));

  auto dead = monitor.getDeadAgents();
  ASSERT_EQ(dead.size(), 1u);
  EXPECT_EQ(dead[0], "agent1");
  EXPECT_TRUE(monitor.getAliveAgents().empty());
}
