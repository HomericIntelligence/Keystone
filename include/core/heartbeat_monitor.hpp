/**
 * @file heartbeat_monitor.hpp
 * @brief Heartbeat monitoring system for agent health tracking
 *
 * Phase 5.4: Recovery Mechanisms - Heartbeat Monitoring
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace keystone {
namespace core {

/**
 * @brief Heartbeat monitor for detecting failed agents
 *
 * Tracks periodic heartbeats from agents and detects when agents
 * stop sending heartbeats (indicating failure or network issues).
 *
 * Example:
 * @code
 * HeartbeatMonitor::Config config{
 *     .heartbeat_interval = 1s,
 *     .timeout_threshold = 3s
 * };
 * HeartbeatMonitor monitor(config);
 *
 * // Agent sends heartbeat
 * monitor.recordHeartbeat("agent1");
 *
 * // Check if agent is alive
 * if (!monitor.isAlive("agent1")) {
 *     // Agent failed - take recovery action
 * }
 * @endcode
 */
class HeartbeatMonitor {
 public:
  /**
   * @brief Heartbeat monitoring configuration
   */
  struct Config {
    std::chrono::milliseconds heartbeat_interval{1000};  ///< Expected heartbeat interval
    std::chrono::milliseconds timeout_threshold{3000};   ///< Timeout before marking as dead
    bool auto_remove_dead{false};                        ///< Automatically remove dead agents
  };

  /**
   * @brief Agent heartbeat status
   */
  struct AgentStatus {
    std::string agent_id;
    std::chrono::steady_clock::time_point last_heartbeat;
    std::chrono::steady_clock::time_point first_heartbeat;
    uint32_t total_heartbeats{0};
    bool is_alive{true};
  };

  /**
   * @brief Callback for agent failure detection
   */
  using FailureCallback = std::function<void(const std::string& agent_id)>;

  /**
   * @brief Construct with default configuration
   */
  HeartbeatMonitor();

  /**
   * @brief Construct with custom configuration
   * @param config Heartbeat monitoring configuration
   */
  explicit HeartbeatMonitor(Config config);

  /**
   * @brief Record a heartbeat from an agent
   *
   * Updates the last heartbeat timestamp and marks agent as alive.
   *
   * @param agent_id Agent identifier
   */
  void recordHeartbeat(const std::string& agent_id);

  /**
   * @brief Check if agent is alive
   *
   * An agent is considered alive if it sent a heartbeat within
   * the timeout threshold.
   *
   * @param agent_id Agent identifier
   * @return true if agent is alive
   */
  bool isAlive(const std::string& agent_id) const;

  /**
   * @brief Check all agents and update their status
   *
   * Checks all registered agents against timeout threshold.
   * Marks agents as dead if they haven't sent heartbeat in time.
   *
   * @return Number of newly detected failures
   */
  size_t checkAgents();

  /**
   * @brief Get agent status
   *
   * @param agent_id Agent identifier
   * @return Agent status, or nullopt if not registered
   */
  std::optional<AgentStatus> getStatus(const std::string& agent_id) const;

  /**
   * @brief Get all registered agents
   * @return List of agent IDs
   */
  std::vector<std::string> getRegisteredAgents() const;

  /**
   * @brief Get all alive agents
   * @return List of alive agent IDs
   */
  std::vector<std::string> getAliveAgents() const;

  /**
   * @brief Get all dead agents
   * @return List of dead agent IDs
   */
  std::vector<std::string> getDeadAgents() const;

  /**
   * @brief Register failure callback
   *
   * Callback is invoked when an agent is first detected as failed.
   *
   * @param callback Function to call on agent failure
   */
  void setFailureCallback(FailureCallback callback);

  /**
   * @brief Remove agent from monitoring
   *
   * @param agent_id Agent identifier
   */
  void removeAgent(const std::string& agent_id);

  /**
   * @brief Reset all statistics
   */
  void reset();

  /**
   * @brief Get total number of failures detected
   * @return Failure count
   */
  uint32_t getTotalFailures() const { return total_failures_.load(); }

 private:
  Config config_;  ///< Heartbeat monitoring configuration

  // Agent status tracking
  std::map<std::string, AgentStatus> agents_;
  mutable std::mutex agents_mutex_;

  // Failure callback
  FailureCallback failure_callback_;
  std::mutex callback_mutex_;

  // Statistics
  std::atomic<uint32_t> total_failures_{0};
};

}  // namespace core
}  // namespace keystone
