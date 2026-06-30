/**
 * @file circuit_breaker.cpp
 * @brief Implementation of circuit breaker pattern
 */

#include "core/circuit_breaker.hpp"

#include "concurrency/logger.hpp"

namespace keystone {
namespace core {

using namespace concurrency;

CircuitBreaker::CircuitBreaker() : CircuitBreaker(Config{}) {}

CircuitBreaker::CircuitBreaker(Config config) : config_(config) {
  Logger::info(
      "CircuitBreaker: Created (failure_threshold={}, timeout={}ms, "
      "success_threshold={})",
      config_.failure_threshold,
      config_.timeout_ms.count(),
      config_.success_threshold);
}

bool CircuitBreaker::allowRequest(const std::string& target_id) {
  std::lock_guard<std::mutex> lock(circuits_mutex_);

  auto it = circuits_.find(target_id);
  if (it == circuits_.end()) {
    // First request to this target - create circuit in CLOSED state
    circuits_[target_id] =
        CircuitStatus{.target_id = target_id,
                      .state = State::CLOSED,
                      .last_failure_time = std::chrono::steady_clock::time_point{},
                      .circuit_opened_time = std::chrono::steady_clock::time_point{}};
    return true;
  }

  auto& status = it->second;

  switch (status.state) {
    case State::CLOSED:
      // Normal operation - allow all requests
      return true;

    case State::OPEN:
      // Circuit is open - check if timeout has elapsed
      if (isTimeoutElapsed(status)) {
        // Timeout elapsed - transition to HALF_OPEN and allow one request
        transitionToHalfOpen(status);
        return true;
      }
      // Still in timeout - reject request
      Logger::trace("CircuitBreaker: Request to {} rejected (circuit OPEN)", target_id);
      return false;

    case State::HALF_OPEN:
      // Allow limited requests to test if service recovered
      return true;
  }

  return false;
}

void CircuitBreaker::recordSuccess(const std::string& target_id) {
  std::lock_guard<std::mutex> lock(circuits_mutex_);

  auto it = circuits_.find(target_id);
  if (it == circuits_.end()) {
    return;  // Unknown target
  }

  auto& status = it->second;
  status.total_successes++;
  status.consecutive_successes++;
  status.consecutive_failures = 0;  // Reset failure counter

  Logger::trace("CircuitBreaker: Success for {} (consecutive={})",
                target_id,
                status.consecutive_successes);

  if (status.state == State::HALF_OPEN) {
    // Check if we have enough successes to close the circuit
    if (status.consecutive_successes >= config_.success_threshold) {
      transitionToClosed(status);
    }
  }
}

void CircuitBreaker::recordFailure(const std::string& target_id) {
  std::lock_guard<std::mutex> lock(circuits_mutex_);

  auto it = circuits_.find(target_id);
  if (it == circuits_.end()) {
    // Create circuit if it doesn't exist
    circuits_[target_id] =
        CircuitStatus{.target_id = target_id,
                      .state = State::CLOSED,
                      .last_failure_time = std::chrono::steady_clock::time_point{},
                      .circuit_opened_time = std::chrono::steady_clock::time_point{}};
    it = circuits_.find(target_id);
  }

  auto& status = it->second;
  status.total_failures++;
  status.consecutive_failures++;
  status.consecutive_successes = 0;  // Reset success counter
  status.last_failure_time = std::chrono::steady_clock::now();

  Logger::debug("CircuitBreaker: Failure for {} (consecutive={})",
                target_id,
                status.consecutive_failures);

  if (status.state == State::CLOSED) {
    // Check if we've exceeded failure threshold
    if (status.consecutive_failures >= config_.failure_threshold) {
      transitionToOpen(status);
    }
  } else if (status.state == State::HALF_OPEN) {
    // Any failure in HALF_OPEN state immediately opens circuit
    transitionToOpen(status);
  }
}

CircuitBreaker::State CircuitBreaker::getState(const std::string& target_id) const {
  std::lock_guard<std::mutex> lock(circuits_mutex_);

  auto it = circuits_.find(target_id);
  if (it == circuits_.end()) {
    return State::CLOSED;  // Default state
  }

  return it->second.state;
}

std::optional<CircuitBreaker::CircuitStatus> CircuitBreaker::getStatus(
    const std::string& target_id) const {
  std::lock_guard<std::mutex> lock(circuits_mutex_);

  auto it = circuits_.find(target_id);
  if (it == circuits_.end()) {
    return std::nullopt;
  }

  return it->second;
}

void CircuitBreaker::reset(const std::string& target_id) {
  std::lock_guard<std::mutex> lock(circuits_mutex_);

  auto it = circuits_.find(target_id);
  if (it != circuits_.end()) {
    transitionToClosed(it->second);
    Logger::info("CircuitBreaker: Circuit for {} manually reset", target_id);
  }
}

void CircuitBreaker::resetAll() {
  std::lock_guard<std::mutex> lock(circuits_mutex_);

  for (auto& [target_id, status] : circuits_) {
    transitionToClosed(status);
  }

  Logger::info("CircuitBreaker: All circuits reset");
}

std::vector<std::string> CircuitBreaker::getTrackedTargets() const {
  std::lock_guard<std::mutex> lock(circuits_mutex_);

  std::vector<std::string> targets;
  targets.reserve(circuits_.size());

  for (const auto& [target_id, _] : circuits_) {
    targets.push_back(target_id);
  }

  return targets;
}

size_t CircuitBreaker::getOpenCircuitCount() const {
  std::lock_guard<std::mutex> lock(circuits_mutex_);

  size_t count = 0;
  for (const auto& [_, status] : circuits_) {
    if (status.state == State::OPEN) {
      count++;
    }
  }

  return count;
}

void CircuitBreaker::transitionToOpen(CircuitStatus& status) {
  status.state = State::OPEN;
  status.circuit_opened_time = std::chrono::steady_clock::now();

  Logger::warn("CircuitBreaker: Circuit OPENED for {} ({} consecutive failures)",
               status.target_id,
               status.consecutive_failures);
}

void CircuitBreaker::transitionToHalfOpen(CircuitStatus& status) {
  status.state = State::HALF_OPEN;
  status.consecutive_successes = 0;

  Logger::info("CircuitBreaker: Circuit HALF_OPEN for {} (testing recovery)", status.target_id);
}

void CircuitBreaker::transitionToClosed(CircuitStatus& status) {
  status.state = State::CLOSED;
  status.consecutive_failures = 0;
  status.consecutive_successes = 0;

  Logger::info("CircuitBreaker: Circuit CLOSED for {} (normal operation)", status.target_id);
}

bool CircuitBreaker::isTimeoutElapsed(const CircuitStatus& status) const {
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                                       status.circuit_opened_time);

  return elapsed >= config_.timeout_ms;
}

}  // namespace core
}  // namespace keystone
