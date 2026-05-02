/**
 * @file thread_pool.cpp
 * @brief Implementation of ThreadPool
 */

#include "concurrency/thread_pool.hpp"

#include "concurrency/logger.hpp"

namespace keystone {
namespace concurrency {

ThreadPool::ThreadPool(size_t num_threads) {
  // Create worker threads
  workers_.reserve(num_threads);
  for (size_t i = 0; i < num_threads; ++i) {
    workers_.emplace_back([this]() { worker_loop(); });
  }
}

ThreadPool::~ThreadPool() { shutdown(); }

void ThreadPool::submit(std::function<void()> func) {
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (shutdown_requested_.load()) {
      return;  // Don't accept new work during shutdown
    }
    work_queue_.push(WorkItem::makeFunction(std::move(func)));
  }
  condition_.notify_one();
}

void ThreadPool::submit(std::coroutine_handle<> handle) {
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (shutdown_requested_.load()) {
      return;  // Don't accept new work during shutdown
    }
    work_queue_.push(WorkItem::makeCoroutine(handle));
  }
  condition_.notify_one();
}

void ThreadPool::shutdown() {
  {
    // Hold the lock while setting the flag so workers can't miss the wakeup:
    // a worker either sees shutdown_requested_==true in its predicate check
    // (which it evaluates under the same lock), or it is already sleeping and
    // will be woken by notify_all() below.
    std::lock_guard<std::mutex> lock(queue_mutex_);
    shutdown_requested_.store(true);
  }

  // Wake workers so they process remaining items (drain phase).
  condition_.notify_all();

  // Explicitly wait for the queue to drain before signalling workers to exit.
  // This makes the "all pending work completes before shutdown" guarantee
  // explicit and by-design, not merely incidental.
  {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    drain_cv_.wait(lock, [this]() { return work_queue_.empty(); });
  }

  // Queue is now empty — wake all workers so they see the exit condition.
  condition_.notify_all();

  // Join all worker threads
  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }

  workers_.clear();
}

void ThreadPool::worker_loop() {
  while (true) {
    WorkItem work_item;

    {
      std::unique_lock<std::mutex> lock(queue_mutex_);

      // Wait for work or shutdown
      condition_.wait(lock, [this]() {
        return shutdown_requested_.load() || !work_queue_.empty();
      });

      // Exit if shutdown and no more work
      if (shutdown_requested_.load() && work_queue_.empty()) {
        return;
      }

      // Get work item
      if (!work_queue_.empty()) {
        work_item = std::move(work_queue_.front());
        work_queue_.pop();
      } else {
        continue;  // Spurious wakeup
      }

      // Notify drain waiters when the queue becomes empty after dequeueing.
      if (work_queue_.empty()) {
        drain_cv_.notify_all();
      }
    }

    // Execute work item outside the lock
    try {
      work_item.execute();
    } catch (const std::exception& e) {
      // Log error (for now, just print to stderr)
      Logger::error("ThreadPool: Exception in worker: {}", e.what());
    } catch (...) {
      Logger::error("ThreadPool: Unknown exception in worker");
    }
  }
}

}  // namespace concurrency
}  // namespace keystone
