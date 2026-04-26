#pragma once

#include <cassert>
#include <coroutine>
#include <functional>
#include <memory>
#include <optional>
#include <variant>

#include <concurrentqueue.h>

namespace keystone {
namespace concurrency {

/**
 * @brief WorkItem - A unit of work (function or coroutine)
 *
 * FIX P3-02: Default constructor is private to prevent invalid WorkItems.
 * Always use makeFunction() or makeCoroutine() factory methods.
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
    // FIX P3-02: Assert valid state before execution
    assert(valid() && "Cannot execute invalid WorkItem");

    if (type == Type::Function && func) {
      func();
    } else if (type == Type::Coroutine && handle) {
      handle.resume();
    }
  }

  bool valid() const {
    return (type == Type::Function && func != nullptr) ||
           (type == Type::Coroutine && handle != nullptr);
  }

 private:
  // FIX P3-02: Private default constructor prevents accidental creation of invalid WorkItems
  WorkItem() : type(Type::Function), func(nullptr), handle(nullptr) {}
};

/**
 * @brief WorkStealingQueue - Lock-free queue for work-stealing scheduler
 *
 * This queue is designed for work-stealing thread pools where:
 * - The owner thread pushes and pops work items from one end (LIFO for
 * locality)
 * - Other threads can steal work items from the other end (FIFO)
 *
 * Uses moodycamel::ConcurrentQueue for lock-free MPMC operations.
 *
 * Thread Safety:
 * - push() and pop() are called by the owner thread
 * - steal() is called by other threads
 * - All operations are lock-free and thread-safe
 *
 * Usage:
 *   WorkStealingQueue queue;
 *
 *   // Owner thread
 *   queue.push(WorkItem::makeFunction([]() { }));
 *   auto item = queue.pop();  // LIFO
 *
 *   // Thief thread
 *   auto stolen = queue.steal();  // FIFO
 */
class WorkStealingQueue {
 public:
  /**
   * @brief Construct a WorkStealingQueue
   */
  WorkStealingQueue();

  /**
   * @brief Destructor
   */
  ~WorkStealingQueue() = default;

  // Non-copyable, movable
  WorkStealingQueue(const WorkStealingQueue&) = delete;
  WorkStealingQueue& operator=(const WorkStealingQueue&) = delete;

  WorkStealingQueue(WorkStealingQueue&&) noexcept = default;
  WorkStealingQueue& operator=(WorkStealingQueue&&) noexcept = default;

  /**
   * @brief Push a work item onto the queue (owner thread)
   *
   * @param item Work item to push
   */
  void push(WorkItem item);

  /**
   * @brief Pop a work item from the queue (owner thread, LIFO)
   *
   * @return Work item if available, std::nullopt otherwise
   */
  std::optional<WorkItem> pop();

  /**
   * @brief Steal a work item from the queue (thief thread, FIFO)
   *
   * @return Work item if available, std::nullopt otherwise
   */
  std::optional<WorkItem> steal();

  /**
   * @brief Get approximate size of the queue
   *
   * Note: This is an approximate count due to concurrent access
   *
   * @return Approximate number of items in queue
   */
  size_t size_approx() const;

  /**
   * @brief Check if queue is (approximately) empty
   *
   * @return true if queue appears empty
   */
  bool empty() const;

 private:
  moodycamel::ConcurrentQueue<WorkItem> queue_;
};

}  // namespace concurrency
}  // namespace keystone
