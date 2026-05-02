/**
 * @file test_heartbeat_monitor.cpp
 * @brief Unit tests for HeartbeatMonitor
 */

#include <gtest/gtest.h>

#include <thread>

#include "core/heartbeat_monitor.hpp"

using namespace keystone::core;

class HeartbeatMonitorTest : public ::testing::Test {
 protected:
  HeartbeatMonitor::Config default_config_{
      .heartbeat_interval = std::chrono::milliseconds(100),
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
  monitor.setFailureCallback([&failed_agent](const std::string& agent_id) {
    failed_agent = agent_id;
  });

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
