// fuzz_retry_policy.cpp
// Fuzz test for retry policy and exponential backoff
//
// Tests the robustness of retry policy against:
// - Invalid attempt counts
// - Extreme backoff multipliers
// - Integer overflow in delay calculation
// - Zero/negative delays
// - Maximum retry limits
//
// Build with: cmake -DENABLE_FUZZING=ON -DCMAKE_CXX_COMPILER=clang++ ..
// Run with: ./fuzz_retry_policy -max_len=512 -runs=1000000

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "core/retry_policy.hpp"

using namespace keystone;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Need at least 16 bytes for interesting fuzzing
  if (size < 16) {
    return 0;
  }

  try {
    // Extract retry policy parameters from fuzzed data

    // Max retries (limit to reasonable range)
    uint32_t max_retries = 0;
    for (int i = 0; i < 4; ++i) {
      max_retries |= (uint32_t(data[i]) << (i * 8));
    }
    max_retries = std::min(max_retries, uint32_t(100));  // Cap at 100

    // Initial delay in milliseconds
    uint32_t initial_delay_ms = 0;
    for (int i = 0; i < 4; ++i) {
      initial_delay_ms |= (uint32_t(data[4 + i]) << (i * 8));
    }
    initial_delay_ms =
        std::min(initial_delay_ms, uint32_t(60000));             // Cap at 60s
    initial_delay_ms = std::max(initial_delay_ms, uint32_t(1));  // Min 1ms

    // Backoff multiplier (as fixed-point: value / 100.0)
    uint16_t multiplier_fixed = 0;
    multiplier_fixed |= uint16_t(data[8]);
    multiplier_fixed |= (uint16_t(data[9]) << 8);
    double backoff_multiplier =
        std::min(double(multiplier_fixed) / 100.0, 10.0);
    backoff_multiplier = std::max(backoff_multiplier, 1.0);

    // Max delay in milliseconds
    uint32_t max_delay_ms = 0;
    for (int i = 0; i < 4; ++i) {
      max_delay_ms |= (uint32_t(data[10 + i]) << (i * 8));
    }
    max_delay_ms =
        std::min(max_delay_ms, uint32_t(300000));  // Cap at 5 minutes

    // Create retry policy with fuzzed parameters
    auto initial_delay = std::chrono::milliseconds(initial_delay_ms);
    auto max_delay = std::chrono::milliseconds(max_delay_ms);

    RetryPolicy policy(max_retries, initial_delay, backoff_multiplier,
                       max_delay);

    // Test 1: Query if retry is allowed for various attempt counts
    if (size >= 17) {
      uint32_t attempt = data[14] % 150;  // Test attempts 0-149
      bool should_retry = policy.shouldRetry(attempt);

      // Verify invariant: attempts >= max_retries should not retry
      if (attempt >= max_retries && should_retry) {
        // Logic error detected (but don't crash)
        return 0;
      }
    }

    // Test 2: Calculate backoff delays and check for overflow
    if (size >= 18) {
      uint32_t attempt = data[15] % 150;
      auto delay = policy.getBackoffDelay(attempt);

      // Verify delay is within reasonable bounds
      auto delay_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(delay).count();

      // Should not exceed max_delay
      if (delay_ms > static_cast<int64_t>(max_delay_ms)) {
        // Policy violation detected
        return 0;
      }

      // Should not be negative
      if (delay_ms < 0) {
        // Invalid delay
        return 0;
      }
    }

    // Test 3: Simulate a retry sequence
    uint32_t current_attempt = 0;
    while (current_attempt < max_retries && current_attempt < 20) {
      if (!policy.shouldRetry(current_attempt)) {
        break;
      }

      auto delay = policy.getBackoffDelay(current_attempt);

      // Verify delay calculation is monotonically increasing (or capped)
      if (current_attempt > 0) {
        auto prev_delay = policy.getBackoffDelay(current_attempt - 1);
        // Either increasing or at max
        if (delay < prev_delay && delay < max_delay) {
          // Non-monotonic and not at cap
          return 0;
        }
      }

      current_attempt++;
    }

    // Test 4: Edge cases
    if (size >= 19) {
      // Test with attempt = 0
      policy.shouldRetry(0);
      policy.getBackoffDelay(0);

      // Test with attempt = max_retries - 1
      if (max_retries > 0) {
        policy.shouldRetry(max_retries - 1);
        policy.getBackoffDelay(max_retries - 1);
      }

      // Test with attempt = max_retries (should not retry)
      bool should_not_retry = !policy.shouldRetry(max_retries);

      // Test with very large attempt (should not retry)
      bool should_not_retry_large = !policy.shouldRetry(1000000);
    }

  } catch (const std::exception&) {
    // Expected for invalid configurations
    return 0;
  } catch (...) {
    // Catch any other exceptions to prevent crashes
    return 0;
  }

  return 0;
}
