#pragma once

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace keystone {
namespace concurrency {

/**
 * @brief ThreadPool - Fixed-size thread pool with coroutine support
 *
 * This thread pool manages a fixed number of worker threads (typically one per
 * CPU core) and provides the ability to submit both regular functions and
 * coroutine handles for execution.
 *
 * Features:
 * - Fixed-size worker pool
 * - Coroutine handle submission
 * - Function object submission
 * - Graceful shutdown
 * - Thread-safe operations
 *
 * Usage:
 *   ThreadPool pool(4);  // 4 worker threads
 *   pool.submit([]() { std::cout << "Hello from thread!\n"; });
 *   pool.submit(coroutine_handle);
 *   pool.shutdown();
 */
class ThreadPool {
 public:
  /**
   * @brief Construct a ThreadPool with specified number of threads
   *
   * @param num_threads Number of worker threads (default: hardware concurrency)
   */
  explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency());

  /**
   * @brief Destructor - ensures graceful shutdown
   */
  ~ThreadPool();

  // Non-copyable, non-movable
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;
  ThreadPool(ThreadPool&&) = delete;
  ThreadPool& operator=(ThreadPool&&) = delete;

  /**
   * @brief Submit a function for execution
   *
   * @param func Function to execute on a worker thread
   */
  void submit(std::function<void()> func);

  /**
   * @brief Submit a coroutine handle for execution
   *
   * @param handle Coroutine handle to resume on a worker thread
   */
  void submit(std::coroutine_handle<> handle);

  /**
   * @brief Shutdown the thread pool gracefully
   *
   * Waits for all pending work to complete and joins all threads.
   */
  void shutdown();

  /**
   * @brief Get the number of worker threads
   */
  size_t size() const { return workers_.size(); }

  /**
   * @brief Check if the thread pool is shutting down
   */
  bool is_shutting_down() const { return shutdown_requested_.load(); }

 private:
  /**
   * @brief Work item that can be either a function or coroutine handle
   */
  struct WorkItem {
    enum class Type { Function, Coroutine };

    Type type;
    std::function<void()> func;
    std::coroutine_handle<> handle;

    static WorkItem makeFunction(std::function<void()> f) {
      WorkItem item;
      item.type = Type::Function;
      item.func = std::move(f);
      return item;
    }

    static WorkItem makeCoroutine(std::coroutine_handle<> h) {
      WorkItem item;
      item.type = Type::Coroutine;
      item.handle = h;
      return item;
    }

    void execute() {
      if (type == Type::Function && func) {
        func();
      } else if (type == Type::Coroutine && handle) {
        handle.resume();
      }
    }
  };

  /**
   * @brief Worker thread function
   */
  void worker_loop();

  std::vector<std::thread> workers_;
  std::queue<WorkItem> work_queue_;
  std::mutex queue_mutex_;
  std::condition_variable condition_;
  std::condition_variable drain_cv_;
  std::atomic<bool> shutdown_requested_{false};
};

}  // namespace concurrency
}  // namespace keystone
