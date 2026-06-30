/**
 * @file retry_policy.hpp
 * @brief Retry policy for handling message delivery failures
 *
 * Phase 5.3: Message Loss Scenarios with Retry Logic
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace keystone {
namespace core {

/**
 * @brief Retry policy for message delivery with exponential backoff
 *
 * Tracks retry attempts per message and calculates backoff delays.
 * Supports configurable max attempts and backoff strategy.
 *
 * Example:
 * @code
 * RetryPolicy::Config config{
 *     .max_attempts = 3,
 *     .initial_delay_ms = 100,
 *     .max_delay_ms = 5000,
 *     .backoff_multiplier = 2.0
 * };
 * RetryPolicy policy(config);
 *
 * if (policy.shouldRetry(msg_id)) {
 *     auto delay = policy.getNextDelay(msg_id);
 *     std::this_thread::sleep_for(delay);
 *     // Retry sending message
 *     policy.recordAttempt(msg_id);
 * }
 * @endcode
 */
class RetryPolicy {
 public:
  /**
   * @brief Retry policy configuration
   */
  struct Config {
    uint32_t max_attempts{3};                         ///< Maximum retry attempts
    std::chrono::milliseconds initial_delay_ms{100};  ///< Initial backoff delay
    std::chrono::milliseconds max_delay_ms{5000};     ///< Maximum backoff delay
    double backoff_multiplier{2.0};                   ///< Exponential backoff multiplier
  };

  /**
   * @brief Retry statistics for a message
   */
  struct RetryStats {
    uint32_t attempts{0};                                 ///< Number of attempts made
    std::chrono::steady_clock::time_point first_attempt;  ///< Time of first attempt
    std::chrono::steady_clock::time_point last_attempt;   ///< Time of last attempt
    std::chrono::milliseconds total_delay{0};             ///< Total delay accumulated
  };

  /**
   * @brief Construct with default configuration
   */
  RetryPolicy();

  /**
   * @brief Construct with custom configuration
   * @param config Retry policy configuration
   */
  explicit RetryPolicy(Config config);

  /**
   * @brief Check if message should be retried
   *
   * Returns true if the message has not exceeded max attempts.
   *
   * @param message_id Unique message identifier
   * @return true if message can be retried
   */
  bool shouldRetry(const std::string& message_id) const;

  /**
   * @brief Get next retry delay for a message
   *
   * Calculates exponential backoff delay based on attempt count.
   *
   * @param message_id Unique message identifier
   * @return Delay before next retry attempt
   */
  std::chrono::milliseconds getNextDelay(const std::string& message_id);

  /**
   * @brief Record a retry attempt for a message
   *
   * Increments attempt counter and updates statistics.
   *
   * @param message_id Unique message identifier
   */
  void recordAttempt(const std::string& message_id);

  /**
   * @brief Mark message as successfully delivered
   *
   * Removes message from retry tracking.
   *
   * @param message_id Unique message identifier
   */
  void recordSuccess(const std::string& message_id);

  /**
   * @brief Mark message as permanently failed
   *
   * Removes message from retry tracking and increments failure count.
   *
   * @param message_id Unique message identifier
   */
  void recordFailure(const std::string& message_id);

  /**
   * @brief Get retry statistics for a message
   *
   * @param message_id Unique message identifier
   * @return Retry statistics, or nullopt if message not tracked
   */
  std::optional<RetryStats> getStats(const std::string& message_id) const;

  /**
   * @brief Get total number of retries across all messages
   * @return Total retry count
   */
  uint32_t getTotalRetries() const { return total_retries_.load(); }

  /**
   * @brief Get total number of successful deliveries
   * @return Success count
   */
  uint32_t getTotalSuccesses() const { return total_successes_.load(); }

  /**
   * @brief Get total number of permanent failures
   * @return Failure count
   */
  uint32_t getTotalFailures() const { return total_failures_.load(); }

  /**
   * @brief Get number of messages currently being retried
   * @return Active retry count
   */
  size_t getActiveRetries() const;

  /**
   * @brief Reset all statistics and tracked messages
   */
  void reset();

 private:
  Config config_;  ///< Retry policy configuration

  // Per-message retry statistics
  std::map<std::string, RetryStats> retry_stats_;
  mutable std::mutex stats_mutex_;

  // Global statistics
  std::atomic<uint32_t> total_retries_{0};
  std::atomic<uint32_t> total_successes_{0};
  std::atomic<uint32_t> total_failures_{0};

  /**
   * @brief Calculate exponential backoff delay
   *
   * delay = initial_delay * (multiplier ^ attempts)
   * Capped at max_delay_ms.
   *
   * @param attempts Number of attempts made
   * @return Backoff delay
   */
  std::chrono::milliseconds calculateBackoff(uint32_t attempts) const;
};

}  // namespace core
}  // namespace keystone
