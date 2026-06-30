/**
 * @file circuit_breaker.hpp
 * @brief Circuit breaker pattern for fault tolerance
 *
 * Phase 5.4: Recovery Mechanisms - Circuit Breaker Pattern
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace keystone {
namespace core {

/**
 * @brief Circuit breaker for protecting against cascading failures
 *
 * Implements the circuit breaker pattern to prevent repeated failures.
 * States: CLOSED (normal) → OPEN (failing) → HALF_OPEN (testing) → CLOSED
 *
 * Example:
 * @code
 * CircuitBreaker::Config config{
 *     .failure_threshold = 5,
 *     .timeout_ms = 10000,
 *     .success_threshold = 2
 * };
 * CircuitBreaker breaker(config);
 *
 * if (breaker.allowRequest("agent1")) {
 *     // Try sending message
 *     if (success) {
 *         breaker.recordSuccess("agent1");
 *     } else {
 *         breaker.recordFailure("agent1");
 *     }
 * } else {
 *     // Circuit open - don't send
 * }
 * @endcode
 */
class CircuitBreaker {
 public:
  /**
   * @brief Circuit breaker states
   */
  enum class State {
    CLOSED,    ///< Normal operation
    OPEN,      ///< Circuit tripped (blocking requests)
    HALF_OPEN  ///< Testing if service recovered
  };

  /**
   * @brief Circuit breaker configuration
   */
  struct Config {
    uint32_t failure_threshold{5};                ///< Failures before opening circuit
    std::chrono::milliseconds timeout_ms{10000};  ///< Time before trying half-open
    uint32_t success_threshold{2};                ///< Successes to close circuit
  };

  /**
   * @brief Per-target circuit status
   */
  struct CircuitStatus {
    std::string target_id;
    State state{State::CLOSED};
    uint32_t consecutive_failures{0};
    uint32_t consecutive_successes{0};
    std::chrono::steady_clock::time_point last_failure_time;
    std::chrono::steady_clock::time_point circuit_opened_time;
    uint32_t total_failures{0};
    uint32_t total_successes{0};
  };

  /**
   * @brief Construct with default configuration
   */
  CircuitBreaker();

  /**
   * @brief Construct with custom configuration
   * @param config Circuit breaker configuration
   */
  explicit CircuitBreaker(Config config);

  /**
   * @brief Check if request should be allowed
   *
   * Returns true if circuit is CLOSED or HALF_OPEN.
   * Returns false if circuit is OPEN.
   *
   * @param target_id Target identifier
   * @return true if request allowed
   */
  bool allowRequest(const std::string& target_id);

  /**
   * @brief Record a successful request
   *
   * Resets consecutive failures.
   * In HALF_OPEN state, may close the circuit.
   *
   * @param target_id Target identifier
   */
  void recordSuccess(const std::string& target_id);

  /**
   * @brief Record a failed request
   *
   * Increments consecutive failures.
   * May open the circuit if threshold exceeded.
   *
   * @param target_id Target identifier
   */
  void recordFailure(const std::string& target_id);

  /**
   * @brief Get circuit state
   *
   * @param target_id Target identifier
   * @return Circuit state
   */
  State getState(const std::string& target_id) const;

  /**
   * @brief Get circuit status
   *
   * @param target_id Target identifier
   * @return Circuit status, or nullopt if not tracked
   */
  std::optional<CircuitStatus> getStatus(const std::string& target_id) const;

  /**
   * @brief Manually reset circuit to CLOSED state
   *
   * @param target_id Target identifier
   */
  void reset(const std::string& target_id);

  /**
   * @brief Reset all circuits
   */
  void resetAll();

  /**
   * @brief Get all targets being tracked
   * @return List of target IDs
   */
  std::vector<std::string> getTrackedTargets() const;

  /**
   * @brief Get count of open circuits
   * @return Number of open circuits
   */
  size_t getOpenCircuitCount() const;

 private:
  Config config_;  ///< Circuit breaker configuration

  // Per-target circuit status
  std::map<std::string, CircuitStatus> circuits_;
  mutable std::mutex circuits_mutex_;

  /**
   * @brief Transition to OPEN state
   *
   * @param status Circuit status to update
   */
  void transitionToOpen(CircuitStatus& status);

  /**
   * @brief Transition to HALF_OPEN state
   *
   * @param status Circuit status to update
   */
  void transitionToHalfOpen(CircuitStatus& status);

  /**
   * @brief Transition to CLOSED state
   *
   * @param status Circuit status to update
   */
  void transitionToClosed(CircuitStatus& status);

  /**
   * @brief Check if timeout has elapsed for OPEN circuit
   *
   * @param status Circuit status
   * @return true if timeout elapsed
   */
  bool isTimeoutElapsed(const CircuitStatus& status) const;
};

}  // namespace core
}  // namespace keystone
