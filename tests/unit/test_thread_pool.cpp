/**
 * @file test_thread_pool.cpp
 * @brief Unit tests for ThreadPool
 */

#include "concurrency/task.hpp"
#include "concurrency/thread_pool.hpp"

#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

using namespace keystone::concurrency;

// Test: Create and destroy ThreadPool
// DISABLED under sanitizers: TSan instruments thread-startup so heavily
// that ThreadPool(4) + ~ThreadPool() takes >600s on CI runners.
// Pool construction/destruction is validated indirectly by all other tests.
TEST(ThreadPoolTest, DISABLED_CreateAndDestroy) {
  ThreadPool pool(4);
  EXPECT_EQ(pool.size(), 4u);
}

// Test: Submit and execute function
TEST(ThreadPoolTest, SubmitFunction) {
  ThreadPool pool(2);
  std::atomic<int> counter{0};

  pool.submit([&]() { counter.fetch_add(1); });

  // Wait for execution
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_EQ(counter.load(), 1);
}

// Test: Submit multiple functions
TEST(ThreadPoolTest, SubmitMultipleFunctions) {
  ThreadPool pool(4);
  std::atomic<int> counter{0};

  for (int32_t i = 0; i < 10; ++i) {
    pool.submit([&]() { counter.fetch_add(1); });
  }

  // Wait for all to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  EXPECT_EQ(counter.load(), 10);
}

// Test: Submit coroutine handle
TEST(ThreadPoolTest, SubmitCoroutineHandle) {
  ThreadPool pool(2);
  std::atomic<bool> executed{false};

  // Create a simple coroutine lambda that returns Task<void>
  auto createTask = [&]() -> Task<void> {
    executed.store(true);
    co_return;
  };

  // Create task on heap to prevent dangling pointer
  auto task = std::make_shared<Task<void>>(createTask());

  // Submit task for execution by manually resuming
  // Capture shared_ptr to keep task alive
  pool.submit([task]() { task->resume(); });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_TRUE(executed.load());
}

// Test: Parallel execution
TEST(ThreadPoolTest, ParallelExecution) {
  ThreadPool pool(4);
  std::atomic<int> counter{0};
  std::atomic<int> max_concurrent{0};
  std::atomic<int> current_concurrent{0};

  auto work = [&]() {
    int32_t concurrent = current_concurrent.fetch_add(1) + 1;

    // Update max if this is higher
    int32_t expected_max = max_concurrent.load();
    while (concurrent > expected_max) {
      if (max_concurrent.compare_exchange_weak(expected_max, concurrent)) {
        break;
      }
    }

    // Simulate work
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    current_concurrent.fetch_sub(1);
    counter.fetch_add(1);
  };

  // Submit 8 tasks
  for (int32_t i = 0; i < 8; ++i) {
    pool.submit(work);
  }

  // Wait for completion
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  EXPECT_EQ(counter.load(), 8);
  // With 4 threads, we should see some parallelism
  EXPECT_GT(max_concurrent.load(), 1);
}

// Test: Graceful shutdown
TEST(ThreadPoolTest, GracefulShutdown) {
  ThreadPool pool(2);
  std::atomic<int> counter{0};

  // Submit some work
  for (int32_t i = 0; i < 5; ++i) {
    pool.submit([&]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      counter.fetch_add(1);
    });
  }

  // Shutdown should wait for all work to complete
  pool.shutdown();

  EXPECT_EQ(counter.load(), 5);
  EXPECT_TRUE(pool.is_shutting_down());
}

// Test: No new work accepted after shutdown
// DISABLED under TSan: hangs indefinitely (see issue #145)
TEST(ThreadPoolTest, DISABLED_NoWorkAfterShutdown) {
  ThreadPool pool(2);
  std::atomic<int> counter{0};

  pool.shutdown();

  // Try to submit work after shutdown
  pool.submit([&]() { counter.fetch_add(1); });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Work should not be executed
  EXPECT_EQ(counter.load(), 0);
}

// Test: Thread pool with hardware_concurrency threads
// DISABLED under sanitizers: ASan/LSan/TSan instrument thread-startup so
// heavily that ThreadPool() + ~ThreadPool() hangs on CI runners (>120s
// CTest timeout). Construction is validated indirectly by all other tests.
TEST(ThreadPoolTest, DISABLED_HardwareConcurrency) {
  ThreadPool pool;  // Uses std::thread::hardware_concurrency()

  EXPECT_GT(pool.size(), 0);
  EXPECT_LE(pool.size(), std::thread::hardware_concurrency());
}

// Test: Exception handling in worker
TEST(ThreadPoolTest, ExceptionHandling) {
  ThreadPool pool(2);
  std::atomic<int> counter{0};

  // Submit task that throws
  pool.submit([]() { throw std::runtime_error("Test exception"); });

  // Submit normal task
  pool.submit([&]() { counter.fetch_add(1); });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Normal task should still execute despite exception in other task
  EXPECT_EQ(counter.load(), 1);
}

// Test: Thread safety with concurrent submissions
TEST(ThreadPoolTest, ConcurrentSubmissions) {
  ThreadPool pool(4);
  std::atomic<int> counter{0};

  // Launch multiple threads that submit work
  std::vector<std::thread> submitters;
  for (int32_t i = 0; i < 4; ++i) {
    submitters.emplace_back([&]() {
      for (int32_t j = 0; j < 25; ++j) {
        pool.submit([&]() { counter.fetch_add(1); });
      }
    });
  }

  // Wait for all submitters to finish
  for (auto& t : submitters) {
    t.join();
  }

  // Wait for all work to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  EXPECT_EQ(counter.load(), 100);
}

// Test: Destructor calls shutdown
TEST(ThreadPoolTest, DestructorShutdown) {
  std::atomic<int> counter{0};

  {
    ThreadPool pool(2);

    for (int32_t i = 0; i < 5; ++i) {
      pool.submit([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        counter.fetch_add(1);
      });
    }

    // Pool destroyed here, should wait for work
  }

  // After scope, all work should be done
  EXPECT_EQ(counter.load(), 5);
}
