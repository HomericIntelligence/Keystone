/**
 * @file test_work_stealing_queue.cpp
 * @brief Unit tests for WorkStealingQueue
 */

#include "concurrency/task.hpp"
#include "concurrency/work_stealing_queue.hpp"

#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace keystone::concurrency;

// Test: Create and destroy queue
TEST(WorkStealingQueueTest, CreateAndDestroy) {
  WorkStealingQueue queue;
  EXPECT_TRUE(queue.empty());
}

// Test: Push and pop function work item
TEST(WorkStealingQueueTest, PushPopFunction) {
  WorkStealingQueue queue;
  bool executed = false;

  auto work = WorkItem::makeFunction([&]() { executed = true; });

  queue.push(std::move(work));
  EXPECT_FALSE(queue.empty());

  auto item = queue.pop();
  ASSERT_TRUE(item.has_value());

  item->execute();
  EXPECT_TRUE(executed);
  EXPECT_TRUE(queue.empty());
}

// Test: Pop from empty queue returns nullopt
TEST(WorkStealingQueueTest, PopEmpty) {
  WorkStealingQueue queue;

  auto item = queue.pop();
  EXPECT_FALSE(item.has_value());
}

// Test: Steal from queue
TEST(WorkStealingQueueTest, Steal) {
  WorkStealingQueue queue;
  std::atomic<int32_t> counter{0};

  queue.push(WorkItem::makeFunction([&]() { counter.fetch_add(1); }));

  auto item = queue.steal();
  ASSERT_TRUE(item.has_value());

  item->execute();
  EXPECT_EQ(counter.load(), 1);
}

// Test: Steal from empty queue returns nullopt
TEST(WorkStealingQueueTest, StealEmpty) {
  WorkStealingQueue queue;

  auto item = queue.steal();
  EXPECT_FALSE(item.has_value());
}

// Test: Multiple push and pop
TEST(WorkStealingQueueTest, MultiplePushPop) {
  WorkStealingQueue queue;
  std::atomic<int32_t> counter{0};

  // Push 10 items
  for (int32_t i = 0; i < 10; ++i) {
    queue.push(WorkItem::makeFunction([&]() { counter.fetch_add(1); }));
  }

  EXPECT_EQ(queue.size_approx(), size_t{10});

  // Pop all items
  int32_t popped = 0;
  while (auto item = queue.pop()) {
    item->execute();
    popped++;
  }

  EXPECT_EQ(popped, 10);
  EXPECT_EQ(counter.load(), 10);
  EXPECT_TRUE(queue.empty());
}

// Test: Concurrent push from multiple threads
TEST(WorkStealingQueueTest, ConcurrentPush) {
  WorkStealingQueue queue;
  std::atomic<int32_t> push_count{0};
  constexpr int32_t num_threads = 4;
  constexpr int32_t items_per_thread = 25;

  std::vector<std::thread> threads;
  for (int32_t t = 0; t < num_threads; ++t) {
    threads.emplace_back([&]() {
      for (int32_t i = 0; i < items_per_thread; ++i) {
        queue.push(WorkItem::makeFunction([&]() { push_count.fetch_add(1); }));
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(queue.size_approx(), num_threads * items_per_thread);
}

// Test: Work stealing from multiple threads
TEST(WorkStealingQueueTest, WorkStealingMultipleThreads) {
  WorkStealingQueue queue;
  std::atomic<int32_t> executed_count{0};
  constexpr int32_t total_items = 100;

  // Push work items
  for (int32_t i = 0; i < total_items; ++i) {
    queue.push(WorkItem::makeFunction([&]() { executed_count.fetch_add(1); }));
  }

  // Multiple threads steal and execute
  std::vector<std::thread> thieves;
  for (int32_t t = 0; t < 4; ++t) {
    thieves.emplace_back([&]() {
      while (auto item = queue.steal()) {
        item->execute();
      }
    });
  }

  for (auto& thief : thieves) {
    thief.join();
  }

  EXPECT_EQ(executed_count.load(), total_items);
  EXPECT_TRUE(queue.empty());
}

// Test: WorkItem validity
TEST(WorkStealingQueueTest, WorkItemValidity) {
  // FIX: WorkItem default constructor is private (FIX P3-02)
  // Always use factory methods: makeFunction() or makeCoroutine()

  WorkItem func_item = WorkItem::makeFunction([]() {});
  EXPECT_TRUE(func_item.valid());
}

// Test: Size approximation
TEST(WorkStealingQueueTest, SizeApproximation) {
  WorkStealingQueue queue;

  EXPECT_EQ(queue.size_approx(), size_t{0});

  for (int32_t i = 0; i < 10; ++i) {
    queue.push(WorkItem::makeFunction([]() {}));
  }

  EXPECT_GE(queue.size_approx(), size_t{10});

  for (int32_t i = 0; i < 5; ++i) {
    queue.pop();
  }

  EXPECT_LE(queue.size_approx(), size_t{5});
}

// Test: Move semantics
TEST(WorkStealingQueueTest, MoveSemantics) {
  WorkStealingQueue queue1;

  queue1.push(WorkItem::makeFunction([]() {}));
  EXPECT_FALSE(queue1.empty());

  WorkStealingQueue queue2 = std::move(queue1);
  EXPECT_FALSE(queue2.empty());

  auto item = queue2.pop();
  EXPECT_TRUE(item.has_value());
}
