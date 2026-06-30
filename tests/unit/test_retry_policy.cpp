/**
 * @file test_retry_policy.cpp
 * @brief Unit tests for RetryPolicy
 */

#include "core/retry_policy.hpp"

#include <thread>

#include <gtest/gtest.h>

using namespace keystone::core;

/**
 * RetryPolicy Unit Tests
 */
class RetryPolicyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Default configuration
    default_config_ = RetryPolicy::Config{.max_attempts = 3,
                                          .initial_delay_ms = std::chrono::milliseconds(100),
                                          .max_delay_ms = std::chrono::milliseconds(5000),
                                          .backoff_multiplier = 2.0};
  }

  RetryPolicy::Config default_config_;
};

TEST_F(RetryPolicyTest, DefaultConstruction) {
  RetryPolicy policy;

  EXPECT_EQ(policy.getTotalRetries(), 0);
  EXPECT_EQ(policy.getTotalSuccesses(), 0);
  EXPECT_EQ(policy.getTotalFailures(), 0);
  EXPECT_EQ(policy.getActiveRetries(), 0);
}

TEST_F(RetryPolicyTest, CustomConfiguration) {
  RetryPolicy::Config config{.max_attempts = 5,
                             .initial_delay_ms = std::chrono::milliseconds(50),
                             .max_delay_ms = std::chrono::milliseconds(10000),
                             .backoff_multiplier = 3.0};

  RetryPolicy policy(config);

  EXPECT_EQ(policy.getTotalRetries(), 0);
  EXPECT_EQ(policy.getActiveRetries(), 0);
}

TEST_F(RetryPolicyTest, ShouldRetryFirstAttempt) {
  RetryPolicy policy(default_config_);

  // First attempt should always be allowed
  EXPECT_TRUE(policy.shouldRetry("msg1"));
}

TEST_F(RetryPolicyTest, ShouldRetryWithinMaxAttempts) {
  RetryPolicy policy(default_config_);

  // Record 2 attempts (within max_attempts=3)
  policy.recordAttempt("msg1");
  policy.recordAttempt("msg1");

  EXPECT_TRUE(policy.shouldRetry("msg1"));
  EXPECT_EQ(policy.getTotalRetries(),
            1);  // First attempt doesn't count as retry
}

TEST_F(RetryPolicyTest, ShouldNotRetryAfterMaxAttempts) {
  RetryPolicy policy(default_config_);

  // Record 3 attempts (max_attempts=3)
  policy.recordAttempt("msg1");
  policy.recordAttempt("msg1");
  policy.recordAttempt("msg1");

  EXPECT_FALSE(policy.shouldRetry("msg1"));
  EXPECT_EQ(policy.getTotalRetries(), 2);  // 2nd and 3rd attempts are retries
}

TEST_F(RetryPolicyTest, GetNextDelayFirstAttempt) {
  RetryPolicy policy(default_config_);

  // First attempt should have no delay
  auto delay = policy.getNextDelay("msg1");
  EXPECT_EQ(delay, std::chrono::milliseconds(0));
}

TEST_F(RetryPolicyTest, GetNextDelayExponentialBackoff) {
  RetryPolicy policy(default_config_);

  // Record first attempt
  policy.recordAttempt("msg1");

  // Second attempt: 100ms * 2^1 = 200ms
  auto delay1 = policy.getNextDelay("msg1");
  EXPECT_EQ(delay1, std::chrono::milliseconds(200));

  // Record second attempt
  policy.recordAttempt("msg1");

  // Third attempt: 100ms * 2^2 = 400ms
  auto delay2 = policy.getNextDelay("msg1");
  EXPECT_EQ(delay2, std::chrono::milliseconds(400));
}

TEST_F(RetryPolicyTest, GetNextDelayMaxCap) {
  RetryPolicy::Config config{.max_attempts = 10,
                             .initial_delay_ms = std::chrono::milliseconds(1000),
                             .max_delay_ms = std::chrono::milliseconds(5000),
                             .backoff_multiplier = 2.0};
  RetryPolicy policy(config);

  // Record many attempts
  policy.recordAttempt("msg1");
  policy.recordAttempt("msg1");
  policy.recordAttempt("msg1");
  policy.recordAttempt("msg1");

  // Delay should be capped at max_delay_ms
  auto delay = policy.getNextDelay("msg1");
  EXPECT_LE(delay, config.max_delay_ms);
}

TEST_F(RetryPolicyTest, RecordSuccess) {
  RetryPolicy policy(default_config_);

  policy.recordAttempt("msg1");
  policy.recordSuccess("msg1");

  EXPECT_EQ(policy.getTotalSuccesses(), 1);
  EXPECT_EQ(policy.getActiveRetries(), 0);

  // Message should be removed from tracking
  EXPECT_TRUE(policy.shouldRetry("msg1"));  // Starts fresh
}

TEST_F(RetryPolicyTest, RecordSuccessAfterRetries) {
  RetryPolicy policy(default_config_);

  policy.recordAttempt("msg1");
  policy.recordAttempt("msg1");
  policy.recordSuccess("msg1");

  EXPECT_EQ(policy.getTotalSuccesses(), 1);
  EXPECT_EQ(policy.getTotalRetries(), 1);
  EXPECT_EQ(policy.getActiveRetries(), 0);
}

TEST_F(RetryPolicyTest, RecordFailure) {
  RetryPolicy policy(default_config_);

  policy.recordAttempt("msg1");
  policy.recordAttempt("msg1");
  policy.recordFailure("msg1");

  EXPECT_EQ(policy.getTotalFailures(), 1);
  EXPECT_EQ(policy.getTotalRetries(), 1);
  EXPECT_EQ(policy.getActiveRetries(), 0);
}

TEST_F(RetryPolicyTest, GetStatsExistingMessage) {
  RetryPolicy policy(default_config_);

  policy.recordAttempt("msg1");
  policy.recordAttempt("msg1");

  auto stats = policy.getStats("msg1");
  ASSERT_TRUE(stats.has_value());
  EXPECT_EQ(stats->attempts, 2);
  EXPECT_GT(stats->total_delay, std::chrono::milliseconds(0));
}

TEST_F(RetryPolicyTest, GetStatsNonexistentMessage) {
  RetryPolicy policy(default_config_);

  auto stats = policy.getStats("nonexistent");
  EXPECT_FALSE(stats.has_value());
}

TEST_F(RetryPolicyTest, MultipleMessages) {
  RetryPolicy policy(default_config_);

  // Message 1: 2 attempts
  policy.recordAttempt("msg1");
  policy.recordAttempt("msg1");

  // Message 2: 1 attempt
  policy.recordAttempt("msg2");

  // Message 3: 3 attempts, then fail
  policy.recordAttempt("msg3");
  policy.recordAttempt("msg3");
  policy.recordAttempt("msg3");
  policy.recordFailure("msg3");

  EXPECT_EQ(policy.getActiveRetries(), 2);  // msg1 and msg2
  EXPECT_EQ(policy.getTotalRetries(), 3);   // msg1:1, msg3:2
  EXPECT_EQ(policy.getTotalFailures(), 1);
}

TEST_F(RetryPolicyTest, ResetStatistics) {
  RetryPolicy policy(default_config_);

  // Need at least 2 attempts for a retry to be counted
  policy.recordAttempt("msg1");
  policy.recordAttempt("msg1");  // This is a retry
  policy.recordAttempt("msg2");
  policy.recordSuccess("msg2");

  EXPECT_GT(policy.getTotalRetries(), 0);
  EXPECT_GT(policy.getTotalSuccesses(), 0);

  policy.reset();

  EXPECT_EQ(policy.getTotalRetries(), 0);
  EXPECT_EQ(policy.getTotalSuccesses(), 0);
  EXPECT_EQ(policy.getTotalFailures(), 0);
  EXPECT_EQ(policy.getActiveRetries(), 0);
}

TEST_F(RetryPolicyTest, ConcurrentAccess) {
  RetryPolicy policy(default_config_);

  // Launch multiple threads accessing retry policy
  std::vector<std::thread> threads;
  for (int32_t i = 0; i < 10; ++i) {
    threads.emplace_back([&policy, i]() {
      std::string msg_id = "msg" + std::to_string(i);

      for (int32_t attempt = 0; attempt < 3; ++attempt) {
        if (policy.shouldRetry(msg_id)) {
          policy.recordAttempt(msg_id);
          auto delay = policy.getNextDelay(msg_id);
          (void)delay;  // Suppress unused warning
        }
      }

      policy.recordSuccess(msg_id);
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(policy.getTotalSuccesses(), 10);
  EXPECT_EQ(policy.getActiveRetries(), 0);
}
