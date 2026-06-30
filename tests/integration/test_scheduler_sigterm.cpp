/**
 * @file test_scheduler_sigterm.cpp
 * @brief Integration test: SIGTERM mid-flight causes graceful shutdown
 *
 * Validates that WorkStealingScheduler drains all in-flight and queued tasks
 * before worker threads exit when a SIGTERM signal is received.
 *
 * Issue #303: Verify scheduler does NOT drop tasks on SIGTERM; all submitted
 * tasks must complete (atomic counter must equal the number submitted).
 *
 * Test design:
 *  1. Install a SIGTERM handler that calls scheduler.shutdown() instead of
 *     terminating the process.
 *  2. Start a WorkStealingScheduler with N workers.
 *  3. Submit M slow tasks (each sleeps briefly to ensure some are mid-flight
 *     when SIGTERM is raised).
 *  4. Raise SIGTERM via std::raise(SIGTERM).
 *  5. Wait for shutdown() to complete (all workers join).
 *  6. Assert that the atomic counter equals M.
 */

#include "concurrency/work_stealing_scheduler.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <functional>
#include <thread>

#include <gtest/gtest.h>

using namespace keystone::concurrency;

namespace {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<bool> g_sigterm_received{false};

/**
 * @brief SIGTERM handler: delegate shutdown to the scheduler rather than
 * killing the process.
 *
 * Signal-handler constraints (POSIX async-signal-safety):
 * - Only async-signal-safe functions may be called inside a signal handler.
 * - scheduler.shutdown() acquires mutexes and joins threads — it is NOT
 *   async-signal-safe and must NOT be called directly from the handler.
 * - Instead we set an atomic flag and let the test thread observe it and
 *   call shutdown() on behalf of the signal.
 */
extern "C" void sigtermHandler(int /*sig*/) {
  g_sigterm_received.store(true, std::memory_order_release);
}

}  // namespace

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class SchedulerSigtermTest : public ::testing::Test {
 protected:
  void SetUp() override {
    g_sigterm_received.store(false, std::memory_order_relaxed);

    // Capture the previous SIGTERM handler so we can restore it in TearDown.
    prev_handler_ = std::signal(SIGTERM, sigtermHandler);
    ASSERT_NE(prev_handler_, SIG_ERR) << "Failed to install SIGTERM handler";
  }

  void TearDown() override {
    // Restore the previous signal handler unconditionally.
    std::signal(SIGTERM, prev_handler_);
    g_sigterm_received.store(false, std::memory_order_relaxed);
  }

  // Helper: spin until the signal flag is observed, then call shutdown().
  // Returns after shutdown() returns (all workers have joined).
  static void driveShutdownFromSignal(WorkStealingScheduler& scheduler) {
    while (!g_sigterm_received.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    scheduler.shutdown();
  }

  void (*prev_handler_)(int){SIG_DFL};
};

// ---------------------------------------------------------------------------
// Test 1: All tasks complete when SIGTERM fires after all tasks are submitted
//          but while some are still mid-flight (actively executing).
// ---------------------------------------------------------------------------

/**
 * @brief InflightTasksCompleteOnSigterm
 *
 * Scenario:
 *  - 4 workers, 20 tasks that each sleep 30 ms.
 *  - Tasks are submitted, then SIGTERM is raised on the test thread.
 *  - The signal handler sets g_sigterm_received; a helper thread observes it
 *    and calls scheduler.shutdown().
 *  - shutdown() blocks until all workers drain and exit.
 *  - After shutdown(), counter must equal 20.
 *
 * The 30 ms sleep per task (with 4 workers) means at least some tasks are
 * actively executing when shutdown is requested, validating the "mid-flight"
 * drain behaviour.
 */
TEST_F(SchedulerSigtermTest, InflightTasksCompleteOnSigterm) {
  constexpr size_t num_workers = 4;
  constexpr int32_t num_tasks = 20;
  constexpr auto task_duration = std::chrono::milliseconds(30);

  WorkStealingScheduler scheduler(num_workers);
  scheduler.start();

  std::atomic<int32_t> counter{0};

  // Submit all tasks before raising SIGTERM.
  for (int32_t i = 0; i < num_tasks; ++i) {
    scheduler.submit([&counter, task_duration]() {
      std::this_thread::sleep_for(task_duration);
      counter.fetch_add(1, std::memory_order_relaxed);
    });
  }

  // Spawn a helper thread that watches for the signal flag and drives shutdown.
  // This is necessary because calling scheduler.shutdown() inside a signal
  // handler violates POSIX async-signal-safety requirements.
  std::thread shutdown_driver([&scheduler]() { driveShutdownFromSignal(scheduler); });

  // Raise SIGTERM on this thread. The handler sets g_sigterm_received = true;
  // shutdown_driver wakes up and calls scheduler.shutdown().
  std::raise(SIGTERM);

  // Wait for the driver thread (which internally waits for all workers).
  shutdown_driver.join();

  // All 20 tasks must have completed — none may be dropped.
  EXPECT_EQ(counter.load(std::memory_order_acquire), num_tasks)
      << "Scheduler dropped tasks on SIGTERM: expected " << num_tasks << " completions, got "
      << counter.load(std::memory_order_acquire);

  EXPECT_FALSE(scheduler.isRunning()) << "Scheduler should not be running after shutdown";
}

// ---------------------------------------------------------------------------
// Test 2: Tasks submitted before SIGTERM via submitTo() (targeting specific
//          workers) all complete — validates per-worker drain.
// ---------------------------------------------------------------------------

/**
 * @brief PerWorkerDrainOnSigterm
 *
 * Submits a batch of tasks directly to each individual worker queue via
 * submitTo(), ensuring every worker's own queue is populated. Raises SIGTERM
 * after all submissions. Verifies every task completes (drain is per-worker
 * and exhaustive).
 */
TEST_F(SchedulerSigtermTest, PerWorkerDrainOnSigterm) {
  constexpr size_t num_workers = 3;
  constexpr int32_t tasks_per_worker = 8;
  constexpr int32_t num_tasks = static_cast<int32_t>(num_workers) * tasks_per_worker;
  constexpr auto task_duration = std::chrono::milliseconds(20);

  WorkStealingScheduler scheduler(num_workers);
  scheduler.start();

  std::atomic<int32_t> counter{0};

  // Submit tasks directly to each worker so each queue is non-empty.
  for (size_t w = 0; w < num_workers; ++w) {
    for (int32_t t = 0; t < tasks_per_worker; ++t) {
      scheduler.submitTo(w, [&counter, task_duration]() {
        std::this_thread::sleep_for(task_duration);
        counter.fetch_add(1, std::memory_order_relaxed);
      });
    }
  }

  std::thread shutdown_driver([&scheduler]() { driveShutdownFromSignal(scheduler); });

  std::raise(SIGTERM);

  shutdown_driver.join();

  EXPECT_EQ(counter.load(std::memory_order_acquire), num_tasks)
      << "Per-worker drain incomplete: expected " << num_tasks << " completions, got "
      << counter.load(std::memory_order_acquire);

  EXPECT_FALSE(scheduler.isRunning());
}

// ---------------------------------------------------------------------------
// Test 3: Large workload — stress test the drain path.
// ---------------------------------------------------------------------------

/**
 * @brief LargeWorkloadDrainsCompletely
 *
 * Submits a large number of very-short tasks (counter increment only, no
 * sleep) before raising SIGTERM. Most tasks will still be sitting in queues
 * when shutdown is requested. Validates that the drain-after-shutdown-flag
 * path in workerLoop() handles arbitrary queue depth correctly.
 */
TEST_F(SchedulerSigtermTest, LargeWorkloadDrainsCompletely) {
  constexpr size_t num_workers = 2;
  constexpr int32_t num_tasks = 500;

  WorkStealingScheduler scheduler(num_workers);
  scheduler.start();

  std::atomic<int32_t> counter{0};

  // Submit all tasks immediately (no sleep — most land in queues unprocessed).
  for (int32_t i = 0; i < num_tasks; ++i) {
    scheduler.submit([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });
  }

  std::thread shutdown_driver([&scheduler]() { driveShutdownFromSignal(scheduler); });

  // Small delay to allow a few tasks to start executing before SIGTERM.
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  std::raise(SIGTERM);

  shutdown_driver.join();

  EXPECT_EQ(counter.load(std::memory_order_acquire), num_tasks)
      << "Large-workload drain incomplete: expected " << num_tasks << " completions, got "
      << counter.load(std::memory_order_acquire);

  EXPECT_FALSE(scheduler.isRunning());
}

// ---------------------------------------------------------------------------
// Test 4: SIGTERM raised before any tasks are submitted — clean shutdown.
// ---------------------------------------------------------------------------

/**
 * @brief SigtermWithEmptyQueueShutdownsCleanly
 *
 * Edge case: SIGTERM arrives when the scheduler is idle (no tasks submitted).
 * shutdown() must return without deadlocking or crashing.
 */
TEST_F(SchedulerSigtermTest, SigtermWithEmptyQueueShutdownsCleanly) {
  constexpr size_t num_workers = 2;

  WorkStealingScheduler scheduler(num_workers);
  scheduler.start();

  EXPECT_TRUE(scheduler.isRunning());

  std::thread shutdown_driver([&scheduler]() { driveShutdownFromSignal(scheduler); });

  std::raise(SIGTERM);

  shutdown_driver.join();

  EXPECT_FALSE(scheduler.isRunning())
      << "Scheduler should have shut down cleanly even with an empty queue";
}
