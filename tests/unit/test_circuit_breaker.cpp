/**
 * @file test_circuit_breaker.cpp
 * @brief Unit tests for CircuitBreaker
 */

#include "core/circuit_breaker.hpp"

#include <thread>

#include <gtest/gtest.h>

using namespace keystone::core;

class CircuitBreakerTest : public ::testing::Test {
 protected:
  CircuitBreaker::Config default_config_{.failure_threshold = 3,
                                         .timeout_ms = std::chrono::milliseconds(500),
                                         .success_threshold = 2};
};

TEST_F(CircuitBreakerTest, DefaultConstruction) {
  CircuitBreaker breaker;

  EXPECT_EQ(breaker.getOpenCircuitCount(), 0);
  EXPECT_TRUE(breaker.getTrackedTargets().empty());
}

TEST_F(CircuitBreakerTest, InitialStateClosed) {
  CircuitBreaker breaker(default_config_);

  EXPECT_TRUE(breaker.allowRequest("target1"));
  EXPECT_EQ(breaker.getState("target1"), CircuitBreaker::State::CLOSED);
}

TEST_F(CircuitBreakerTest, OpenCircuitAfterFailures) {
  CircuitBreaker breaker(default_config_);

  // Record failures
  for (int32_t i = 0; i < 3; ++i) {
    breaker.recordFailure("target1");
  }

  EXPECT_EQ(breaker.getState("target1"), CircuitBreaker::State::OPEN);
  EXPECT_FALSE(breaker.allowRequest("target1"));
  EXPECT_EQ(breaker.getOpenCircuitCount(), 1);
}

TEST_F(CircuitBreakerTest, HalfOpenAfterTimeout) {
  CircuitBreaker breaker(default_config_);

  // Open circuit
  for (int32_t i = 0; i < 3; ++i) {
    breaker.recordFailure("target1");
  }

  EXPECT_EQ(breaker.getState("target1"), CircuitBreaker::State::OPEN);

  // Wait for timeout
  std::this_thread::sleep_for(std::chrono::milliseconds(550));

  // Next request should be allowed (HALF_OPEN)
  EXPECT_TRUE(breaker.allowRequest("target1"));
  EXPECT_EQ(breaker.getState("target1"), CircuitBreaker::State::HALF_OPEN);
}

TEST_F(CircuitBreakerTest, CloseCircuitAfterSuccesses) {
  CircuitBreaker breaker(default_config_);

  // Open circuit
  for (int32_t i = 0; i < 3; ++i) {
    breaker.recordFailure("target1");
  }

  // Wait and transition to HALF_OPEN
  std::this_thread::sleep_for(std::chrono::milliseconds(550));
  breaker.allowRequest("target1");

  // Record successes
  for (int32_t i = 0; i < 2; ++i) {
    breaker.recordSuccess("target1");
  }

  EXPECT_EQ(breaker.getState("target1"), CircuitBreaker::State::CLOSED);
}

TEST_F(CircuitBreakerTest, ReopenOnHalfOpenFailure) {
  CircuitBreaker breaker(default_config_);

  // Open circuit
  for (int32_t i = 0; i < 3; ++i) {
    breaker.recordFailure("target1");
  }

  // Wait and transition to HALF_OPEN
  std::this_thread::sleep_for(std::chrono::milliseconds(550));
  breaker.allowRequest("target1");

  // Record failure in HALF_OPEN
  breaker.recordFailure("target1");

  EXPECT_EQ(breaker.getState("target1"), CircuitBreaker::State::OPEN);
}

TEST_F(CircuitBreakerTest, MultipleTargets) {
  CircuitBreaker breaker(default_config_);

  // Fail target1
  for (int32_t i = 0; i < 3; ++i) {
    breaker.recordFailure("target1");
  }

  // target2 remains healthy
  breaker.recordSuccess("target2");

  EXPECT_EQ(breaker.getState("target1"), CircuitBreaker::State::OPEN);
  EXPECT_EQ(breaker.getState("target2"), CircuitBreaker::State::CLOSED);
  EXPECT_EQ(breaker.getOpenCircuitCount(), 1);
}

TEST_F(CircuitBreakerTest, ManualReset) {
  CircuitBreaker breaker(default_config_);

  // Open circuit
  for (int32_t i = 0; i < 3; ++i) {
    breaker.recordFailure("target1");
  }

  EXPECT_EQ(breaker.getState("target1"), CircuitBreaker::State::OPEN);

  // Manually reset
  breaker.reset("target1");

  EXPECT_EQ(breaker.getState("target1"), CircuitBreaker::State::CLOSED);
  EXPECT_TRUE(breaker.allowRequest("target1"));
}
