/**
 * @file retry_policy.cpp
 * @brief Implementation of retry policy for message delivery
 */

#include "core/retry_policy.hpp"

#include "concurrency/logger.hpp"

#include <algorithm>
#include <cmath>

namespace keystone {
namespace core {

using namespace concurrency;

RetryPolicy::RetryPolicy() : RetryPolicy(Config{}) {}

RetryPolicy::RetryPolicy(Config config) : config_(config) {
  Logger::info("RetryPolicy: Created (max_attempts={}, initial_delay={}ms, backoff={}x)",
               config_.max_attempts,
               config_.initial_delay_ms.count(),
               config_.backoff_multiplier);
}

bool RetryPolicy::shouldRetry(const std::string& message_id) const {
  std::lock_guard<std::mutex> lock(stats_mutex_);

  auto it = retry_stats_.find(message_id);
  if (it == retry_stats_.end()) {
    // First attempt - can retry
    return true;
  }

  // Check if we've exceeded max attempts
  return it->second.attempts < config_.max_attempts;
}

std::chrono::milliseconds RetryPolicy::getNextDelay(const std::string& message_id) {
  std::lock_guard<std::mutex> lock(stats_mutex_);

  auto it = retry_stats_.find(message_id);
  if (it == retry_stats_.end()) {
    // First attempt - no delay
    return std::chrono::milliseconds(0);
  }

  // Calculate exponential backoff based on attempts
  return calculateBackoff(it->second.attempts);
}

void RetryPolicy::recordAttempt(const std::string& message_id) {
  std::lock_guard<std::mutex> lock(stats_mutex_);

  auto now = std::chrono::steady_clock::now();

  auto it = retry_stats_.find(message_id);
  if (it == retry_stats_.end()) {
    // First attempt
    retry_stats_[message_id] = RetryStats{.attempts = 1,
                                          .first_attempt = now,
                                          .last_attempt = now,
                                          .total_delay = std::chrono::milliseconds(0)};

    Logger::trace("RetryPolicy: First attempt for message {}", message_id);
  } else {
    // Subsequent attempt
    it->second.attempts++;
    it->second.last_attempt = now;

    // Calculate delay since last attempt
    auto delay = calculateBackoff(it->second.attempts - 1);
    it->second.total_delay += delay;

    total_retries_++;

    Logger::debug("RetryPolicy: Retry attempt {} for message {} (delay={}ms)",
                  it->second.attempts,
                  message_id,
                  delay.count());
  }
}

void RetryPolicy::recordSuccess(const std::string& message_id) {
  std::lock_guard<std::mutex> lock(stats_mutex_);

  auto it = retry_stats_.find(message_id);
  if (it != retry_stats_.end()) {
    uint32_t attempts = it->second.attempts;
    auto total_delay = it->second.total_delay;

    retry_stats_.erase(it);
    total_successes_++;

    Logger::debug(
        "RetryPolicy: Message {} succeeded after {} attempts "
        "(total_delay={}ms)",
        message_id,
        attempts,
        total_delay.count());
  } else {
    // First attempt succeeded
    total_successes_++;
    Logger::trace("RetryPolicy: Message {} succeeded on first attempt", message_id);
  }
}

void RetryPolicy::recordFailure(const std::string& message_id) {
  std::lock_guard<std::mutex> lock(stats_mutex_);

  auto it = retry_stats_.find(message_id);
  if (it != retry_stats_.end()) {
    uint32_t attempts = it->second.attempts;
    retry_stats_.erase(it);

    total_failures_++;

    Logger::warn("RetryPolicy: Message {} permanently failed after {} attempts",
                 message_id,
                 attempts);
  } else {
    total_failures_++;
    Logger::warn("RetryPolicy: Message {} failed on first attempt", message_id);
  }
}

std::optional<RetryPolicy::RetryStats> RetryPolicy::getStats(const std::string& message_id) const {
  std::lock_guard<std::mutex> lock(stats_mutex_);

  auto it = retry_stats_.find(message_id);
  if (it == retry_stats_.end()) {
    return std::nullopt;
  }

  return it->second;
}

size_t RetryPolicy::getActiveRetries() const {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  return retry_stats_.size();
}

void RetryPolicy::reset() {
  std::lock_guard<std::mutex> lock(stats_mutex_);

  retry_stats_.clear();
  total_retries_.store(0);
  total_successes_.store(0);
  total_failures_.store(0);

  Logger::debug("RetryPolicy: Statistics reset");
}

std::chrono::milliseconds RetryPolicy::calculateBackoff(uint32_t attempts) const {
  if (attempts == 0) {
    return std::chrono::milliseconds(0);
  }

  // Exponential backoff: delay = initial_delay * (multiplier ^ attempts)
  double delay_ms = config_.initial_delay_ms.count() *
                    std::pow(config_.backoff_multiplier, attempts);

  // Cap at max delay
  delay_ms = std::min(delay_ms, static_cast<double>(config_.max_delay_ms.count()));

  return std::chrono::milliseconds(static_cast<int64_t>(delay_ms));
}

}  // namespace core
}  // namespace keystone
