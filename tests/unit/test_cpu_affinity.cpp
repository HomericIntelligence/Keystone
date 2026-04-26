#include "concurrency/work_stealing_scheduler.hpp"

#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

using namespace keystone::concurrency;

// Test CPU affinity feature
TEST(CPUAffinityTest, EnableCPUAffinity) {
  // Create scheduler with CPU affinity enabled
  WorkStealingScheduler scheduler(4, true);  // 4 workers, affinity enabled
  scheduler.start();

  // Submit some work to ensure workers are running
  std::atomic<int32_t> counter{0};
  for (int32_t i = 0; i < 100; ++i) {
    scheduler.submit([&counter]() {
      counter.fetch_add(1, std::memory_order_relaxed);
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    });
  }

  // Wait for work to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  scheduler.shutdown();

  // Verify all work was executed
  EXPECT_EQ(counter.load(), 100);
}

// Test that CPU affinity can be disabled (default behavior)
TEST(CPUAffinityTest, DisabledByDefault) {
  // Create scheduler without CPU affinity (default)
  WorkStealingScheduler scheduler(2);  // affinity disabled by default
  scheduler.start();

  std::atomic<int32_t> counter{0};
  for (int32_t i = 0; i < 50; ++i) {
    scheduler.submit([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  scheduler.shutdown();

  EXPECT_EQ(counter.load(), 50);
}

// Test with more workers than CPU cores
TEST(CPUAffinityTest, MoreWorkersThanCores) {
  size_t num_cores = std::thread::hardware_concurrency();
  size_t num_workers = num_cores * 2;  // 2x cores

  WorkStealingScheduler scheduler(num_workers, true);
  scheduler.start();

  std::atomic<int32_t> counter{0};
  for (size_t i = 0; i < 100; ++i) {
    scheduler.submit([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  scheduler.shutdown();

  // All work should complete despite worker/core mismatch
  EXPECT_EQ(counter.load(), 100);
}
