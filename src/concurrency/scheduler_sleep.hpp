#pragma once

#include <chrono>
#include <mutex>

namespace keystone::concurrency::detail {

// The SLEEP phase uses this boundary both for timed waits and notifications.
// SPIN and YIELD remain in WorkStealingScheduler.
template <typename ConditionVariable, typename TryWork, typename Stopping>
auto sleepUntilWork(ConditionVariable& condition, std::mutex& mutex,
                    std::chrono::milliseconds timeout, TryWork try_work,
                    Stopping stopping) -> decltype(try_work()) {
  // Submission takes the same mutex before notification. Keep the final queue
  // check and entering the wait together so a notification cannot be lost.
  std::unique_lock<std::mutex> lock(mutex);
  while (true) {
    if (auto work = try_work()) {
      return work;
    }
    if (stopping()) {
      return {};
    }
    // A shutdown-only predicate would ignore notifications for new work.
    // Every notification, spurious wake, or timeout must recheck the queues.
    condition.wait_for(lock, timeout);
    if (stopping()) {
      return {};
    }
  }
}

template <typename ConditionVariable>
void notifySchedulerWork(ConditionVariable& condition, std::mutex& mutex) {
  std::lock_guard<std::mutex> lock(mutex);
  condition.notify_all();
}

}  // namespace keystone::concurrency::detail
