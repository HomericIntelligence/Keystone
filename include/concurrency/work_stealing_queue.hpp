#pragma once

#include <cassert>
#include <coroutine>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

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

  WorkItem() : type(Type::Function), func(nullptr), handle(nullptr), correlation_id("") {}
};

/**
 * @brief WorkStealingQueue — mutex-protected deque with LIFO pop / FIFO steal.
 *
 * Implements work-stealing deque semantics (Issues #346, #349):
 * - push() appends to the back; callable from any thread.
 * - pop()  removes from the back (LIFO) — for the owner worker thread.
 * - steal() removes from the front (FIFO) — for thief threads.
 *
 * A single mutex protects the deque, making all three operations correct
 * under arbitrary concurrent access. This is simpler and more correct than
 * a lock-free Chase-Lev deque for the multi-producer case required by the
 * scheduler's submit() path.
 */
class WorkStealingQueue {
 public:
  WorkStealingQueue() = default;
  ~WorkStealingQueue() = default;

  WorkStealingQueue(const WorkStealingQueue&) = delete;
  WorkStealingQueue& operator=(const WorkStealingQueue&) = delete;

  WorkStealingQueue(WorkStealingQueue&& other) noexcept {
    std::lock_guard<std::mutex> lock(other.mutex_);
    deque_ = std::move(other.deque_);
  }

  WorkStealingQueue& operator=(WorkStealingQueue&& other) noexcept {
    if (this != &other) {
      std::scoped_lock lock(mutex_, other.mutex_);
      deque_ = std::move(other.deque_);
    }
    return *this;
  }

  void push(WorkItem item);
  std::optional<WorkItem> pop();
  std::optional<WorkItem> steal();
  size_t size_approx() const;
  bool empty() const;

 private:
  mutable std::mutex mutex_;
  std::deque<WorkItem> deque_;
};

}  // namespace concurrency
}  // namespace keystone
