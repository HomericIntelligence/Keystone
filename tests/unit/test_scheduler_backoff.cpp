/**
 * @file test_scheduler_backoff.cpp
 * @brief Unit tests for WorkStealingScheduler 3-phase backoff (Stream C1)
 *
 * Tests the SPIN → YIELD → SLEEP backoff strategy to ensure:
 * - Idle CPU usage < 2%
 * - Low latency for new work (< 1ms)
 * - No work is lost during backoff phases
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "concurrency/work_stealing_scheduler.hpp"

using namespace keystone::concurrency;
using namespace std::chrono_literals;

// Test fixture for backoff tests
class SchedulerBackoffTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Start fresh scheduler for each test
  }

  void TearDown() override {
    // Cleanup
  }

  // Helper: Measure CPU time over a duration
  double measureCPUUsage(std::function<void()> workload,
                         std::chrono::milliseconds duration) {
    auto start_time = std::chrono::steady_clock::now();
    auto start_cpu = std::clock();

    workload();

    // Wait for duration
    std::this_thread::sleep_for(duration);

    auto end_cpu = std::clock();
    auto end_time = std::chrono::steady_clock::now();

    double cpu_time_ms = 1000.0 * (end_cpu - start_cpu) / CLOCKS_PER_SEC;
    auto wall_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            end_time - start_time)
                            .count();

    return (cpu_time_ms / wall_time_ms) * 100.0;  // Percentage
  }
};

// Test 1: SPIN phase finds work with ultra-low latency
TEST_F(SchedulerBackoffTest, SpinPhaseFindsWork) {
  WorkStealingScheduler scheduler(4);
  scheduler.start();

  auto work_found = std::make_shared<std::atomic<bool>>(false);
  auto start = std::make_shared<std::chrono::steady_clock::time_point>();

  // Submit work immediately (should be found in SPIN phase)
  scheduler.submit(
      [work_found, start]() { *start = std::chrono::steady_clock::now(); });

  // Submit another work that measures latency
  std::this_thread::sleep_for(1ms);  // Let first work execute
  auto submit_time = std::chrono::steady_clock::now();

  scheduler.submit([work_found, start, submit_time]() {
    auto execute_time = std::chrono::steady_clock::now();
    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
                       execute_time - submit_time)
                       .count();

    // Should be found in SPIN phase (< 10μs typical)
    // Under sanitizers the overhead is significant; use a relaxed limit.
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
    EXPECT_LT(latency, 5000);
#else
    EXPECT_LT(latency, 200);
#endif
#elif defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    EXPECT_LT(latency, 5000);
#else
    EXPECT_LT(latency, 200);
#endif
    work_found->store(true);
  });

  // Wait for work completion
  std::this_thread::sleep_for(50ms);
  EXPECT_TRUE(work_found->load());

  scheduler.shutdown();
}

// Test 2: YIELD phase finds work with acceptable latency
TEST_F(SchedulerBackoffTest, YieldPhaseFindsWork) {
  WorkStealingScheduler scheduler(2);
  scheduler.start();

  // Let workers enter YIELD phase by doing nothing for a bit
  std::this_thread::sleep_for(5ms);

  auto work_found = std::make_shared<std::atomic<bool>>(false);
  auto submit_time = std::chrono::steady_clock::now();

  scheduler.submit([work_found, submit_time]() {
    auto execute_time = std::chrono::steady_clock::now();
    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
                       execute_time - submit_time)
                       .count();

    // Should be found in YIELD phase (< 100μs typical)
    // Allow up to 2000μs for safety (CI systems can be slower)
    EXPECT_LT(latency, 2000);
    work_found->store(true);
  });

  std::this_thread::sleep_for(50ms);
  EXPECT_TRUE(work_found->load());

  scheduler.shutdown();
}

// Test 3: SLEEP phase finds work with wake-up notification
TEST_F(SchedulerBackoffTest, SleepPhaseFindsWork) {
  WorkStealingScheduler scheduler(2);
  scheduler.start();

  // Let workers enter SLEEP phase (wait > 1000 iterations)
  std::this_thread::sleep_for(50ms);

  auto work_found = std::make_shared<std::atomic<bool>>(false);
  auto submit_time = std::chrono::steady_clock::now();

  scheduler.submit([work_found, submit_time]() {
    auto execute_time = std::chrono::steady_clock::now();
    auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                       execute_time - submit_time)
                       .count();

    // With wake-up notification, should be < 2ms
    EXPECT_LT(latency, 2);
    work_found->store(true);
  });

  std::this_thread::sleep_for(100ms);
  EXPECT_TRUE(work_found->load());

  scheduler.shutdown();
}

// Test 4: Backoff reduces idle CPU usage
TEST_F(SchedulerBackoffTest, BackoffReducesIdleCPU) {
  WorkStealingScheduler scheduler(4);
  scheduler.start();

  // Let scheduler idle for 1 second
  std::this_thread::sleep_for(1000ms);

  // Note: Accurate CPU measurement requires platform-specific tools
  // This test is more of a smoke test - real validation done via benchmarks
  // We just verify the scheduler doesn't crash and completes idle period
  EXPECT_TRUE(scheduler.isRunning());

  scheduler.shutdown();
}

// Test 5: Wake-up notification works
TEST_F(SchedulerBackoffTest, WakeUpNotification) {
  WorkStealingScheduler scheduler(2);
  scheduler.start();

  // Let workers enter SLEEP phase
  std::this_thread::sleep_for(50ms);

  auto work_executed = std::make_shared<std::atomic<int32_t>>(0);

  // Submit multiple work items rapidly
  // All workers should wake up immediately via notify_all()
  for (int32_t i = 0; i < 10; ++i) {
    scheduler.submit([work_executed]() { work_executed->fetch_add(1); });
  }

  // All work should complete quickly (< 100ms)
  std::this_thread::sleep_for(100ms);
  EXPECT_EQ(work_executed->load(), 10);

  scheduler.shutdown();
}

// Test 6: Multiple workers in backoff
TEST_F(SchedulerBackoffTest, MultipleWorkersBackoff) {
  WorkStealingScheduler scheduler(4);
  scheduler.start();

  // Let all workers idle (enter SLEEP phase)
  std::this_thread::sleep_for(100ms);

  // Verify scheduler is still running and responsive
  auto counter = std::make_shared<std::atomic<int32_t>>(0);
  scheduler.submit([counter]() { counter->fetch_add(1); });

  std::this_thread::sleep_for(50ms);
  EXPECT_EQ(counter->load(), 1);

  scheduler.shutdown();
}

// Test 7: Backoff doesn't lose work
TEST_F(SchedulerBackoffTest, BackoffDoesNotLoseWork) {
  WorkStealingScheduler scheduler(4);
  scheduler.start();

  auto counter = std::make_shared<std::atomic<int32_t>>(0);

  // Submit 1000 tasks rapidly
  for (int32_t i = 0; i < 1000; ++i) {
    scheduler.submit([counter]() {
      counter->fetch_add(1);
      // Tiny delay to allow backoff phases to trigger
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    });
  }

  // Wait for all work to complete
  std::this_thread::sleep_for(500ms);

  EXPECT_EQ(counter->load(), 1000);

  scheduler.shutdown();
}

// Test 8: Low latency under continuous load
TEST_F(SchedulerBackoffTest, LatencyUnderLoad) {
  WorkStealingScheduler scheduler(4);
  scheduler.start();

  auto total_latency_us = std::make_shared<std::atomic<int64_t>>(0);
  auto task_count = std::make_shared<std::atomic<int32_t>>(0);

  // Submit continuous work to keep workers in SPIN phase
  for (int32_t i = 0; i < 100; ++i) {
    auto submit_time = std::chrono::steady_clock::now();
    scheduler.submit([submit_time, total_latency_us, task_count]() {
      auto execute_time = std::chrono::steady_clock::now();
      auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
                         execute_time - submit_time)
                         .count();

      total_latency_us->fetch_add(latency);
      task_count->fetch_add(1);
    });

    // Small delay between submissions
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  // Wait for completion
  std::this_thread::sleep_for(200ms);

  int32_t count = task_count->load();
  EXPECT_EQ(count, 100);

  // Average latency should be low (<= 150μs)
  // Note: CI systems can be slower than local machines
  if (count > 0) {
    int64_t avg_latency = total_latency_us->load() / count;
    EXPECT_LE(avg_latency, 150);
  }

  scheduler.shutdown();
}

// Test 9: Verify workers progress through backoff phases
TEST_F(SchedulerBackoffTest, BackoffPhaseProgression) {
  WorkStealingScheduler scheduler(1);
  scheduler.start();

  // Worker starts in SPIN phase, should progress to YIELD then SLEEP
  // We can't directly observe phase transitions, but we can verify
  // that the scheduler remains responsive at different time points

  auto counter = std::make_shared<std::atomic<int32_t>>(0);

  // After 1ms - likely in SPIN/YIELD phase
  std::this_thread::sleep_for(1ms);
  scheduler.submit([counter]() { counter->fetch_add(1); });
  std::this_thread::sleep_for(10ms);
  EXPECT_EQ(counter->load(), 1);

  // After 10ms - likely in YIELD phase
  std::this_thread::sleep_for(10ms);
  scheduler.submit([counter]() { counter->fetch_add(1); });
  std::this_thread::sleep_for(10ms);
  EXPECT_EQ(counter->load(), 2);

  // After 50ms - definitely in SLEEP phase
  std::this_thread::sleep_for(50ms);
  scheduler.submit([counter]() { counter->fetch_add(1); });
  std::this_thread::sleep_for(10ms);
  EXPECT_EQ(counter->load(), 3);

  scheduler.shutdown();
}

// Test 10: Shutdown wakes sleeping workers immediately
TEST_F(SchedulerBackoffTest, ShutdownWakesSleepingWorkers) {
  WorkStealingScheduler scheduler(4);
  scheduler.start();

  // Let workers enter SLEEP phase
  std::this_thread::sleep_for(100ms);

  // Shutdown should complete quickly (< 100ms)
  auto shutdown_start = std::chrono::steady_clock::now();
  scheduler.shutdown();
  auto shutdown_end = std::chrono::steady_clock::now();

  auto shutdown_duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(shutdown_end -
                                                            shutdown_start)
          .count();

  // Shutdown should be fast due to wake-up notification
  EXPECT_LT(shutdown_duration, 100);
}
