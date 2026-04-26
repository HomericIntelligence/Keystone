#pragma once

#include <atomic>
#include <cassert>
#include <coroutine>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>

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
  std::string correlation_id;  // FIX #284: Capture and propagate correlation ID

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
  WorkItem() : type(Type::Function), func(nullptr), handle(nullptr), correlation_id("") {}
};

/**
 * @brief WorkStealingQueue - Chase-Lev lock-free deque for work-stealing scheduler
 *
 * Implements the classic Chase-Lev deque semantics for work-stealing thread pools:
 * - The owner thread pushes and pops from the back (LIFO for cache locality)
 * - Thief threads steal from the front (FIFO for fairness)
 *
 * Architecture:
 * - Dynamic array of WorkItems managed with atomic bottom and top pointers
 * - bottom_ (atomic): points to the last pushed item (owner-only)
 * - top_ (atomic): points to the front for stealing (CAS-based)
 * - Array grows dynamically when capacity is exceeded
 *
 * Thread Safety:
 * - push() and pop() are owner-only, no synchronization needed
 * - steal() uses atomic CAS to grab from front
 * - All operations are wait-free or lock-free
 *
 * Usage:
 *   WorkStealingQueue queue;
 *
 *   // Owner thread
 *   queue.push(WorkItem::makeFunction([]() { }));
 *   auto item = queue.pop();  // LIFO from back
 *
 *   // Thief thread
 *   auto stolen = queue.steal();  // FIFO from front
 *
 * References:
 * - Chase, D. and Lev, Y., "Dynamic circular work-stealing deque", 2005
 * - Leiserson and Plego, "Cache-oblivious B-trees", 2010
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
  ~WorkStealingQueue();

  // Non-copyable, movable
  WorkStealingQueue(const WorkStealingQueue&) = delete;
  WorkStealingQueue& operator=(const WorkStealingQueue&) = delete;

  WorkStealingQueue(WorkStealingQueue&&) noexcept;
  WorkStealingQueue& operator=(WorkStealingQueue&&) noexcept;

  /**
   * @brief Push a work item onto the back of the deque (owner thread only)
   *
   * @param item Work item to push
   */
  void push(WorkItem item);

  /**
   * @brief Pop a work item from the back of the deque (owner thread, LIFO)
   *
   * Should only be called by the owner thread.
   *
   * @return Work item if available, std::nullopt otherwise
   */
  std::optional<WorkItem> pop();

  /**
   * @brief Steal a work item from the front of the deque (thief thread, FIFO)
   *
   * Can be called by any thread (thief). Uses atomic CAS for synchronization
   * with pop() at the back.
   *
   * @return Work item if available, std::nullopt otherwise
   */
  std::optional<WorkItem> steal();

  /**
   * @brief Get approximate size of the deque
   *
   * Note: This is an approximate count due to concurrent access
   *
   * @return Approximate number of items in deque
   */
  size_t size_approx() const;

  /**
   * @brief Check if deque is (approximately) empty
   *
   * @return true if deque appears empty
   */
  bool empty() const;

 private:
  static constexpr size_t INITIAL_CAPACITY = 32;
  static constexpr size_t MAX_CAPACITY = 1024 * 1024;  // 1M items

  struct Array {
    std::unique_ptr<WorkItem[]> items;
    size_t capacity;

    Array(size_t cap) : capacity(cap) { items = std::make_unique<WorkItem[]>(cap); }
  };

  std::atomic<size_t> bottom_{0};  // Owner-only, points to next push position
  std::atomic<size_t> top_{0};     // Atomic, points to next steal position
  std::atomic<std::shared_ptr<Array>*> array_{nullptr};

  void growArray();
  std::shared_ptr<Array> loadArray();
};

}  // namespace concurrency
}  // namespace keystone
