/**
 * @file work_stealing_queue.cpp
 * @brief Implementation of WorkStealingQueue (Chase-Lev deque)
 *
 * References:
 * - Chase and Lev, "Dynamic Circular Work-Stealing Deque", 2005
 * - Hesam Jahanjou's C++ implementation notes
 */

#include "concurrency/work_stealing_queue.hpp"

#include "concurrency/logger.hpp"

#include <memory>

namespace keystone {
namespace concurrency {

WorkStealingQueue::WorkStealingQueue() {
  auto arr = std::make_shared<Array>(INITIAL_CAPACITY);
  auto arr_ptr = new std::shared_ptr<Array>(arr);
  array_.store(arr_ptr, std::memory_order_release);
}

WorkStealingQueue::~WorkStealingQueue() {
  auto arr_ptr = array_.load(std::memory_order_acquire);
  if (arr_ptr) {
    delete arr_ptr;
  }
}

WorkStealingQueue::WorkStealingQueue(WorkStealingQueue&& other) noexcept {
  auto other_arr = other.array_.load(std::memory_order_acquire);
  if (other_arr) {
    auto arr_ptr = new std::shared_ptr<Array>(*other_arr);
    array_.store(arr_ptr, std::memory_order_release);

    bottom_.store(other.bottom_.load(std::memory_order_acquire), std::memory_order_release);
    top_.store(other.top_.load(std::memory_order_acquire), std::memory_order_release);

    // Clear other
    other.array_.store(nullptr, std::memory_order_release);
    other.bottom_.store(0, std::memory_order_release);
    other.top_.store(0, std::memory_order_release);
  }
}

WorkStealingQueue& WorkStealingQueue::operator=(WorkStealingQueue&& other) noexcept {
  if (this != &other) {
    // Clean up our current array
    auto old_arr = array_.load(std::memory_order_acquire);
    if (old_arr) {
      delete old_arr;
    }

    // Move from other
    auto other_arr = other.array_.load(std::memory_order_acquire);
    if (other_arr) {
      auto arr_ptr = new std::shared_ptr<Array>(*other_arr);
      array_.store(arr_ptr, std::memory_order_release);

      bottom_.store(other.bottom_.load(std::memory_order_acquire), std::memory_order_release);
      top_.store(other.top_.load(std::memory_order_acquire), std::memory_order_release);

      // Clear other
      other.array_.store(nullptr, std::memory_order_release);
      other.bottom_.store(0, std::memory_order_release);
      other.top_.store(0, std::memory_order_release);
    }
  }
  return *this;
}

std::shared_ptr<WorkStealingQueue::Array> WorkStealingQueue::loadArray() {
  auto arr_ptr = array_.load(std::memory_order_acquire);
  return arr_ptr ? *arr_ptr : nullptr;
}

void WorkStealingQueue::growArray() {
  auto old_arr = loadArray();
  if (!old_arr) {
    return;
  }

  size_t new_capacity = old_arr->capacity * 2;
  if (new_capacity > MAX_CAPACITY) {
    return;  // Don't grow beyond max
  }

  auto new_arr = std::make_shared<Array>(new_capacity);

  // Copy items from old array to new array
  // Items are in positions [top, bottom)
  size_t top_val = top_.load(std::memory_order_acquire);
  size_t bottom_val = bottom_.load(std::memory_order_acquire);

  for (size_t i = top_val; i < bottom_val; ++i) {
    new_arr->items[i % new_capacity] = std::move(old_arr->items[i % old_arr->capacity]);
  }

  // Replace array atomically
  auto new_arr_ptr = new std::shared_ptr<Array>(new_arr);
  auto old_arr_ptr = array_.exchange(new_arr_ptr, std::memory_order_acq_rel);
  if (old_arr_ptr) {
    delete old_arr_ptr;
  }
}

void WorkStealingQueue::push(WorkItem item) {
  // FIX #284: Capture correlation ID on submission thread
  if (item.correlation_id.empty()) {
    item.correlation_id = LogContext::getCorrelationId();
  }

  size_t b = bottom_.load(std::memory_order_relaxed);
  auto arr = loadArray();

  if (!arr) {
    return;  // Should not happen
  }

  // Check if we need to grow
  size_t t = top_.load(std::memory_order_acquire);
  if (b - t >= arr->capacity) {
    growArray();
    arr = loadArray();
  }

  // Push to back
  arr->items[b % arr->capacity] = std::move(item);
  bottom_.store(b + 1, std::memory_order_release);
}

std::optional<WorkItem> WorkStealingQueue::pop() {
  // Owner-only: pops from the back (LIFO)
  // No synchronization needed because only owner calls this

  size_t b = bottom_.load(std::memory_order_relaxed);
  auto arr = loadArray();

  if (!arr) {
    return std::nullopt;
  }

  if (b == 0) {
    return std::nullopt;
  }

  b = b - 1;
  bottom_.store(b, std::memory_order_relaxed);

  std::atomic_thread_fence(std::memory_order_seq_cst);

  size_t t = top_.load(std::memory_order_relaxed);

  if (b >= t) {
    // Non-empty
    WorkItem item = std::move(arr->items[b % arr->capacity]);
    return item;
  } else {
    // Empty, restore bottom
    bottom_.store(b + 1, std::memory_order_relaxed);
    return std::nullopt;
  }
}

std::optional<WorkItem> WorkStealingQueue::steal() {
  // Thief threads: steal from front (FIFO)
  // Uses CAS for synchronization with pop()

  size_t t = top_.load(std::memory_order_acquire);
  std::atomic_thread_fence(std::memory_order_seq_cst);
  size_t b = bottom_.load(std::memory_order_acquire);

  auto arr = loadArray();
  if (!arr) {
    return std::nullopt;
  }

  if (t >= b) {
    return std::nullopt;  // Empty
  }

  WorkItem item = std::move(arr->items[t % arr->capacity]);

  // Try to atomically increment top
  if (top_.compare_exchange_strong(
          t, t + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
    // FIX #284: Restore correlation ID on worker thread before execution
    if (!item.correlation_id.empty()) {
      LogContext::setCorrelationId(item.correlation_id);
    }
    return item;
  }

  return std::nullopt;
}

size_t WorkStealingQueue::size_approx() const {
  size_t b = bottom_.load(std::memory_order_acquire);
  size_t t = top_.load(std::memory_order_acquire);
  return (b > t) ? (b - t) : 0;
}

bool WorkStealingQueue::empty() const {
  return size_approx() == 0;
}

}  // namespace concurrency
}  // namespace keystone
