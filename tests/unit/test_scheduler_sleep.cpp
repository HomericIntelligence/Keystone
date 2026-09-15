#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "../../src/concurrency/scheduler_sleep.hpp"
#include "concurrency/work_stealing_queue.hpp"

using namespace keystone::concurrency;
using namespace std::chrono_literals;

namespace {

// Control only the condition-variable boundary. Notifications are lost unless
// a wait is active. A false predicate consumes the simulated deadline, as the
// standard predicate overload does, even after a notification. No clock sleeps.
class NotificationBoundary {
 public:
  std::function<void()> during_wait;
  int32_t timeouts = 0;
  int32_t waits = 0;

  void notify_all() {
    if (waiting_.load()) {
      notified_.store(true);
    }
  }

  std::cv_status wait_for(std::unique_lock<std::mutex>& lock,
                          std::chrono::milliseconds timeout) {
    if (++waits > 2) {
      throw std::runtime_error("unexpected repeated scheduler wait");
    }
    EXPECT_EQ(timeout, 1ms);
    EXPECT_TRUE(lock.owns_lock());
    notified_.store(false);
    waiting_.store(true);
    lock.unlock();
    if (during_wait) {
      during_wait();
    }
    lock.lock();
    waiting_.store(false);
    if (!notified_.load()) {
      ++timeouts;
      return std::cv_status::timeout;
    }
    return std::cv_status::no_timeout;
  }

  template <typename Predicate>
  bool wait_for(std::unique_lock<std::mutex>& lock,
                std::chrono::milliseconds timeout, Predicate predicate) {
    if (predicate()) {
      return true;
    }
    const auto status = wait_for(lock, timeout);
    if (predicate()) {
      return true;
    }
    if (status != std::cv_status::timeout) {
      ++timeouts;
    }
    return false;
  }

 private:
  std::atomic<bool> waiting_{false};
  std::atomic<bool> notified_{false};
};

TEST(SchedulerSleepTest, WorkNotificationDoesNotWaitForTimeout) {
  WorkStealingQueue queue;
  std::mutex mutex;
  NotificationBoundary condition;
  bool executed = false;
  condition.during_wait = [&]() {
    queue.push(WorkItem::makeFunction([&]() { executed = true; }));
    detail::notifySchedulerWork(condition, mutex);
  };

  auto work = detail::sleepUntilWork(
      condition, mutex, 1ms, [&]() { return queue.pop(); },
      []() { return false; });

  ASSERT_TRUE(work.has_value());
  work->execute();
  EXPECT_TRUE(executed);
  EXPECT_EQ(condition.waits, 1);
  EXPECT_EQ(condition.timeouts, 0);
}

TEST(SchedulerSleepTest, SubmissionBetweenEmptyCheckAndWaitIsNotLost) {
  WorkStealingQueue queue;
  std::mutex mutex;
  NotificationBoundary condition;
  bool executed = false;
  std::promise<void> producer_checked;
  auto checked = producer_checked.get_future();
  std::jthread producer;
  condition.during_wait = [&]() { producer.join(); };

  auto try_work = [&]() {
    auto work = queue.pop();
    if (!work && !producer.joinable()) {
      producer = std::jthread([&]() {
        const bool checked_before_wait = mutex.try_lock();
        if (checked_before_wait) {
          mutex.unlock();
        } else {
          // The consumer holds the wait mutex. Let it enter the wait before
          // the notification can take the same mutex.
          producer_checked.set_value();
        }
        queue.push(WorkItem::makeFunction([&]() { executed = true; }));
        detail::notifySchedulerWork(condition, mutex);
        if (checked_before_wait) {
          producer_checked.set_value();
        }
      });
      if (checked.wait_for(5s) != std::future_status::ready) {
        throw std::runtime_error("producer did not reach the wait boundary");
      }
    }
    return work;
  };

  auto work = detail::sleepUntilWork(condition, mutex, 1ms, try_work,
                                     []() { return false; });

  ASSERT_TRUE(work.has_value());
  work->execute();
  EXPECT_TRUE(executed);
  EXPECT_EQ(condition.timeouts, 0);
}

TEST(SchedulerSleepTest, QueuedWorkDoesNotWait) {
  WorkStealingQueue queue;
  std::mutex mutex;
  NotificationBoundary condition;
  queue.push(WorkItem::makeFunction([]() {}));

  auto work = detail::sleepUntilWork(
      condition, mutex, 1ms, [&]() { return queue.pop(); },
      []() { return false; });

  EXPECT_TRUE(work.has_value());
  EXPECT_EQ(condition.waits, 0);
}

TEST(SchedulerSleepTest, ShutdownNotificationReturnsNoWork) {
  WorkStealingQueue queue;
  std::mutex mutex;
  NotificationBoundary condition;
  bool stopped = false;
  condition.during_wait = [&]() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      stopped = true;
    }
    condition.notify_all();
  };

  auto work = detail::sleepUntilWork(
      condition, mutex, 1ms, [&]() { return queue.pop(); },
      [&]() { return stopped; });

  EXPECT_FALSE(work.has_value());
  EXPECT_EQ(condition.waits, 1);
  EXPECT_EQ(condition.timeouts, 0);
}

TEST(SchedulerSleepTest, ShutdownBeforeWaitReturnsWithoutWaiting) {
  WorkStealingQueue queue;
  std::mutex mutex;
  NotificationBoundary condition;

  auto work = detail::sleepUntilWork(
      condition, mutex, 1ms, [&]() { return queue.pop(); },
      []() { return true; });

  EXPECT_FALSE(work.has_value());
  EXPECT_EQ(condition.waits, 0);
}

TEST(SchedulerSleepTest, RepeatedNotificationRechecksQueueWithoutTimeout) {
  WorkStealingQueue queue;
  std::mutex mutex;
  NotificationBoundary condition;
  condition.during_wait = [&]() {
    if (condition.waits == 2) {
      queue.push(WorkItem::makeFunction([]() {}));
    }
    detail::notifySchedulerWork(condition, mutex);
  };

  auto work = detail::sleepUntilWork(
      condition, mutex, 1ms, [&]() { return queue.pop(); },
      []() { return false; });

  EXPECT_TRUE(work.has_value());
  EXPECT_EQ(condition.waits, 2);
  EXPECT_EQ(condition.timeouts, 0);
}

TEST(SchedulerSleepTest, ShutdownAfterNotificationLeavesQueuedWorkForDrain) {
  WorkStealingQueue queue;
  std::mutex mutex;
  NotificationBoundary condition;
  bool stopped = false;
  condition.during_wait = [&]() {
    queue.push(WorkItem::makeFunction([]() {}));
    {
      std::lock_guard<std::mutex> lock(mutex);
      stopped = true;
    }
    condition.notify_all();
  };

  auto work = detail::sleepUntilWork(
      condition, mutex, 1ms, [&]() { return queue.pop(); },
      [&]() { return stopped; });

  EXPECT_FALSE(work.has_value());
  EXPECT_TRUE(queue.pop().has_value());
  EXPECT_EQ(condition.timeouts, 0);
}

}  // namespace
