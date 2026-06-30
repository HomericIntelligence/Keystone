/**
 * @file heartbeat_monitor.cpp
 * @brief Implementation of heartbeat monitoring system
 */

#include "core/heartbeat_monitor.hpp"

#include "concurrency/logger.hpp"

#include <algorithm>

namespace keystone {
namespace core {

using namespace concurrency;

HeartbeatMonitor::HeartbeatMonitor() : HeartbeatMonitor(Config{}) {}

HeartbeatMonitor::HeartbeatMonitor(Config config) : config_(config) {
  Logger::info("HeartbeatMonitor: Created (interval={}ms, timeout={}ms)",
               config_.heartbeat_interval.count(),
               config_.timeout_threshold.count());
}

void HeartbeatMonitor::recordHeartbeat(const std::string& agent_id) {
  std::lock_guard<std::mutex> lock(agents_mutex_);

  auto now = std::chrono::steady_clock::now();

  auto it = agents_.find(agent_id);
  if (it == agents_.end()) {
    // First heartbeat from this agent
    agents_[agent_id] = AgentStatus{.agent_id = agent_id,
                                    .last_heartbeat = now,
                                    .first_heartbeat = now,
                                    .total_heartbeats = 1,
                                    .is_alive = true};

    Logger::debug("HeartbeatMonitor: Agent {} registered", agent_id);
  } else {
    // Subsequent heartbeat
    bool was_dead = !it->second.is_alive;

    it->second.last_heartbeat = now;
    it->second.total_heartbeats++;
    it->second.is_alive = true;

    if (was_dead) {
      Logger::info("HeartbeatMonitor: Agent {} recovered", agent_id);
    } else {
      Logger::trace("HeartbeatMonitor: Heartbeat from {} (total={})",
                    agent_id,
                    it->second.total_heartbeats);
    }
  }
}

bool HeartbeatMonitor::isAlive(const std::string& agent_id) const {
  std::lock_guard<std::mutex> lock(agents_mutex_);

  auto it = agents_.find(agent_id);
  if (it == agents_.end()) {
    return false;  // Unknown agent = not alive
  }

  // Check if heartbeat is within timeout threshold
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                                       it->second.last_heartbeat);

  return elapsed < config_.timeout_threshold;
}

size_t HeartbeatMonitor::checkAgents() {
  std::lock_guard<std::mutex> lock(agents_mutex_);

  size_t newly_failed = 0;
  auto now = std::chrono::steady_clock::now();

  std::vector<std::string> to_remove;

  for (auto& [agent_id, status] : agents_) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                                         status.last_heartbeat);

    bool currently_alive = (elapsed < config_.timeout_threshold);

    if (status.is_alive && !currently_alive) {
      // Agent just failed
      status.is_alive = false;
      newly_failed++;
      total_failures_++;

      Logger::warn("HeartbeatMonitor: Agent {} failed (last heartbeat {}ms ago)",
                   agent_id,
                   elapsed.count());

      // Invoke failure callback
      {
        std::lock_guard<std::mutex> cb_lock(callback_mutex_);
        if (failure_callback_) {
          failure_callback_(agent_id);
        }
      }

      // Mark for removal if auto-remove is enabled
      if (config_.auto_remove_dead) {
        to_remove.push_back(agent_id);
      }
    }
  }

  // Remove dead agents if configured
  for (const auto& agent_id : to_remove) {
    agents_.erase(agent_id);
    Logger::debug("HeartbeatMonitor: Removed dead agent {}", agent_id);
  }

  return newly_failed;
}

std::optional<HeartbeatMonitor::AgentStatus> HeartbeatMonitor::getStatus(
    const std::string& agent_id) const {
  std::lock_guard<std::mutex> lock(agents_mutex_);

  auto it = agents_.find(agent_id);
  if (it == agents_.end()) {
    return std::nullopt;
  }

  return it->second;
}

std::vector<std::string> HeartbeatMonitor::getRegisteredAgents() const {
  std::lock_guard<std::mutex> lock(agents_mutex_);

  std::vector<std::string> agents;
  agents.reserve(agents_.size());

  for (const auto& [agent_id, _] : agents_) {
    agents.push_back(agent_id);
  }

  return agents;
}

std::vector<std::string> HeartbeatMonitor::getAliveAgents() const {
  std::lock_guard<std::mutex> lock(agents_mutex_);

  std::vector<std::string> alive;
  auto now = std::chrono::steady_clock::now();

  for (const auto& [agent_id, status] : agents_) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                                         status.last_heartbeat);

    if (elapsed < config_.timeout_threshold) {
      alive.push_back(agent_id);
    }
  }

  return alive;
}

std::vector<std::string> HeartbeatMonitor::getDeadAgents() const {
  std::lock_guard<std::mutex> lock(agents_mutex_);

  std::vector<std::string> dead;
  auto now = std::chrono::steady_clock::now();

  for (const auto& [agent_id, status] : agents_) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                                         status.last_heartbeat);

    if (elapsed >= config_.timeout_threshold) {
      dead.push_back(agent_id);
    }
  }

  return dead;
}

void HeartbeatMonitor::setFailureCallback(FailureCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  failure_callback_ = std::move(callback);
}

void HeartbeatMonitor::removeAgent(const std::string& agent_id) {
  std::lock_guard<std::mutex> lock(agents_mutex_);

  auto it = agents_.find(agent_id);
  if (it != agents_.end()) {
    agents_.erase(it);
    Logger::debug("HeartbeatMonitor: Agent {} removed", agent_id);
  }
}

void HeartbeatMonitor::reset() {
  std::lock_guard<std::mutex> lock(agents_mutex_);

  agents_.clear();
  total_failures_.store(0);

  Logger::debug("HeartbeatMonitor: Reset complete");
}

}  // namespace core
}  // namespace keystone
