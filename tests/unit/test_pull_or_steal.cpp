/**
 * @file test_pull_or_steal.cpp
 * @brief Unit tests for PullOrSteal awaitable
 */

#include <gtest/gtest.h>

#include <atomic>
#include <vector>

#include "concurrency/pull_or_steal.hpp"
#include "concurrency/task.hpp"

using namespace keystone::concurrency;

// Test: PullOrSteal from own queue (immediate)
TEST(PullOrStealTest, PopFromOwnQueue) {
  std::atomic<bool> shutdown{false};
  WorkStealingQueue own_queue;
  std::vector<WorkStealingQueue*> all_queues = {&own_queue};

  // Add work to own queue
  own_queue.push(WorkItem::makeFunction([]() {}));

  // Keep lambda alive until get() completes to avoid stack-use-after-scope
  auto testLambda = [&]() -> Task<std::optional<WorkItem>> {
    auto work = co_await PullOrSteal(own_queue, all_queues, 0, shutdown);
    co_return work;
  };
  auto testTask = testLambda();

  auto result = testTask.get();
  EXPECT_TRUE(result.has_value());
  EXPECT_TRUE(result->valid());
}

// Test: PullOrSteal steals from another queue
TEST(PullOrStealTest, StealFromOtherQueue) {
  std::atomic<bool> shutdown{false};
  WorkStealingQueue own_queue;
  WorkStealingQueue victim_queue;
  std::vector<WorkStealingQueue*> all_queues = {&own_queue, &victim_queue};

  // Add work to victim queue only
  victim_queue.push(WorkItem::makeFunction([]() {}));

  // Keep lambda alive until get() completes to avoid stack-use-after-scope
  auto testLambda = [&]() -> Task<std::optional<WorkItem>> {
    auto work = co_await PullOrSteal(own_queue, all_queues, 0, shutdown);
    co_return work;
  };
  auto testTask = testLambda();

  auto result = testTask.get();
  EXPECT_TRUE(result.has_value());
  EXPECT_TRUE(result->valid());

  // Victim queue should now be empty
  EXPECT_TRUE(victim_queue.empty());
}

// Test: PullOrSteal with multiple victim queues
TEST(PullOrStealTest, StealFromMultipleQueues) {
  std::atomic<bool> shutdown{false};
  WorkStealingQueue own_queue;
  WorkStealingQueue victim1;
  WorkStealingQueue victim2;
  WorkStealingQueue victim3;
  std::vector<WorkStealingQueue*> all_queues = {&own_queue, &victim1, &victim2,
                                                &victim3};

  // Add work to victim2 only
  victim2.push(WorkItem::makeFunction([]() {}));

  // Keep lambda alive until get() completes to avoid stack-use-after-scope
  auto testLambda = [&]() -> Task<std::optional<WorkItem>> {
    auto work = co_await PullOrSteal(own_queue, all_queues, 0, shutdown);
    co_return work;
  };
  auto testTask = testLambda();

  auto result = testTask.get();
  EXPECT_TRUE(result.has_value());
  EXPECT_TRUE(result->valid());
}

// Test: PullOrSteal with shutdown flag
TEST(PullOrStealTest, ShutdownFlag) {
  std::atomic<bool> shutdown{true};  // Already shutdown
  WorkStealingQueue own_queue;
  std::vector<WorkStealingQueue*> all_queues = {&own_queue};

  // Keep lambda alive until get() completes to avoid stack-use-after-scope
  auto testLambda = [&]() -> Task<std::optional<WorkItem>> {
    auto work = co_await PullOrSteal(own_queue, all_queues, 0, shutdown);
    co_return work;
  };
  auto testTask = testLambda();

  auto result = testTask.get();
  EXPECT_FALSE(result.has_value());  // Should return nullopt due to shutdown
}

// Test: PullOrSteal prefers own queue (LIFO)
TEST(PullOrStealTest, PrefersOwnQueue) {
  std::atomic<bool> shutdown{false};
  WorkStealingQueue own_queue;
  WorkStealingQueue victim_queue;
  std::vector<WorkStealingQueue*> all_queues = {&own_queue, &victim_queue};

  // Add work to both queues
  own_queue.push(WorkItem::makeFunction([]() {}));
  victim_queue.push(WorkItem::makeFunction([]() {}));

  // Keep lambda alive until get() completes to avoid stack-use-after-scope
  auto testLambda = [&]() -> Task<std::optional<WorkItem>> {
    auto work = co_await PullOrSteal(own_queue, all_queues, 0, shutdown);
    co_return work;
  };
  auto testTask = testLambda();

  auto result = testTask.get();
  EXPECT_TRUE(result.has_value());

  // Victim queue should still have work (own queue was preferred)
  EXPECT_FALSE(victim_queue.empty());
}

// Test: PullOrSteal round-robin stealing
TEST(PullOrStealTest, RoundRobinStealing) {
  std::atomic<bool> shutdown{false};
  WorkStealingQueue queue0;
  WorkStealingQueue queue1;
  WorkStealingQueue queue2;
  std::vector<WorkStealingQueue*> all_queues = {&queue0, &queue1, &queue2};

  // Worker 0 steals from queue1 first (round-robin from worker_index + 1)
  queue1.push(WorkItem::makeFunction([]() {}));
  queue2.push(WorkItem::makeFunction([]() {}));

  // Keep lambda alive until get() completes to avoid stack-use-after-scope
  auto testLambda = [&]() -> Task<std::optional<WorkItem>> {
    auto work = co_await PullOrSteal(queue0, all_queues, 0, shutdown);
    co_return work;
  };
  auto testTask = testLambda();

  auto result = testTask.get();
  EXPECT_TRUE(result.has_value());

  // Should have stolen from queue1 (first in round-robin order)
  EXPECT_TRUE(queue1.empty());
  EXPECT_FALSE(queue2.empty());  // queue2 still has work
}

// Test: PullOrStealWithTimeout returns work immediately if available
TEST(PullOrStealTest, TimeoutImmediateSuccess) {
  std::atomic<bool> shutdown{false};
  WorkStealingQueue own_queue;
  std::vector<WorkStealingQueue*> all_queues = {&own_queue};

  own_queue.push(WorkItem::makeFunction([]() {}));

  // Keep lambda alive until get() completes to avoid stack-use-after-scope
  auto testLambda = [&]() -> Task<std::optional<WorkItem>> {
    auto work = co_await PullOrStealWithTimeout(
        own_queue, all_queues, 0, shutdown, std::chrono::milliseconds(100));
    co_return work;
  };
  auto testTask = testLambda();

  auto result = testTask.get();
  EXPECT_TRUE(result.has_value());
}

// Test: PullOrStealWithTimeout with empty queues
TEST(PullOrStealTest, TimeoutWithEmptyQueues) {
  std::atomic<bool> shutdown{false};
  WorkStealingQueue own_queue;
  std::vector<WorkStealingQueue*> all_queues = {&own_queue};

  // Keep lambda alive until get() completes to avoid stack-use-after-scope
  auto testLambda = [&]() -> Task<std::optional<WorkItem>> {
    auto work = co_await PullOrStealWithTimeout(
        own_queue, all_queues, 0, shutdown, std::chrono::milliseconds(50));
    co_return work;
  };
  auto testTask = testLambda();

  auto result = testTask.get();

  // Result should be nullopt (no work found)
  EXPECT_FALSE(result.has_value());

  // Note: Timeout behavior is simplified in current implementation
  // Full timeout semantics will be implemented with WorkStealingScheduler
}

// Test: Multiple workers using PullOrSteal
TEST(PullOrStealTest, MultipleWorkers) {
  std::atomic<bool> shutdown{false};
  WorkStealingQueue queue0;
  WorkStealingQueue queue1;
  std::vector<WorkStealingQueue*> all_queues = {&queue0, &queue1};

  // Add work to queue0
  queue0.push(WorkItem::makeFunction([]() {}));
  queue0.push(WorkItem::makeFunction([]() {}));

  // Keep lambdas alive until get() completes to avoid stack-use-after-scope
  // Worker 0 gets first item (sequential execution for determinism)
  auto lambda0 = [&]() -> Task<std::optional<WorkItem>> {
    auto work = co_await PullOrSteal(queue0, all_queues, 0, shutdown);
    co_return work;
  };
  auto task0 = lambda0();
  auto result0 = task0.get();

  // Worker 1 steals second item
  auto lambda1 = [&]() -> Task<std::optional<WorkItem>> {
    auto work = co_await PullOrSteal(queue1, all_queues, 1, shutdown);
    co_return work;
  };
  auto task1 = lambda1();
  auto result1 = task1.get();

  // At least one worker should have gotten work
  EXPECT_TRUE(result0.has_value() || result1.has_value());
}

// Test: PullOrSteal with coroutine work items
TEST(PullOrStealTest, CoroutineWorkItem) {
  std::atomic<bool> shutdown{false};
  WorkStealingQueue own_queue;
  std::vector<WorkStealingQueue*> all_queues = {&own_queue};

  // Create a simple coroutine work item
  auto simpleCoroutine = []() -> Task<void> { co_return; }();

  own_queue.push(WorkItem::makeCoroutine(simpleCoroutine.get_handle()));

  // Keep lambda alive until get() completes to avoid stack-use-after-scope
  auto testLambda = [&]() -> Task<std::optional<WorkItem>> {
    auto work = co_await PullOrSteal(own_queue, all_queues, 0, shutdown);
    co_return work;
  };
  auto testTask = testLambda();

  auto result = testTask.get();
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(result->type, WorkItem::Type::Coroutine);
  EXPECT_TRUE(result->valid());
}

// Test: await_ready returns true when work is available
TEST(PullOrStealTest, AwaitReadyTrue) {
  std::atomic<bool> shutdown{false};
  WorkStealingQueue own_queue;
  std::vector<WorkStealingQueue*> all_queues = {&own_queue};

  own_queue.push(WorkItem::makeFunction([]() {}));

  PullOrSteal awaitable(own_queue, all_queues, 0, shutdown);
  EXPECT_TRUE(awaitable.await_ready());
}

// Test: await_ready returns false when no work available
TEST(PullOrStealTest, AwaitReadyFalse) {
  std::atomic<bool> shutdown{false};
  WorkStealingQueue own_queue;
  std::vector<WorkStealingQueue*> all_queues = {&own_queue};

  // No work in queue
  PullOrSteal awaitable(own_queue, all_queues, 0, shutdown);
  EXPECT_FALSE(awaitable.await_ready());
}
