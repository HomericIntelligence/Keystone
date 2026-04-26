/**
 * @file work_stealing_queue.cpp
 * @brief Implementation of Chase-Lev work-stealing deque
 */

#include "concurrency/work_stealing_queue.hpp"

namespace keystone {
namespace concurrency {

WorkStealingQueue::WorkStealingQueue()
    : bottom_(0), top_(0), elements_(nullptr), capacity_(INITIAL_CAPACITY) {
  elements_ = new WorkItem[capacity_];
}

WorkStealingQueue::~WorkStealingQueue() {
  if (elements_ != nullptr) {
    delete[] elements_;
    elements_ = nullptr;
  }
}

WorkStealingQueue::WorkStealingQueue(WorkStealingQueue&& other) noexcept
    : bottom_(other.bottom_.load()),
      top_(other.top_.load()),
      elements_(other.elements_),
      capacity_(other.capacity_) {
  other.elements_ = nullptr;
  other.capacity_ = 0;
  other.bottom_.store(0);
  other.top_.store(0);
}

WorkStealingQueue& WorkStealingQueue::operator=(
    WorkStealingQueue&& other) noexcept {
  if (this != &other) {
    if (elements_ != nullptr) {
      delete[] elements_;
    }
    bottom_.store(other.bottom_.load());
    top_.store(other.top_.load());
    elements_ = other.elements_;
    capacity_ = other.capacity_;
    other.elements_ = nullptr;
    other.capacity_ = 0;
    other.bottom_.store(0);
    other.top_.store(0);
  }
  return *this;
}

void WorkStealingQueue::push(WorkItem item) {
  int64_t b = bottom_.load(std::memory_order_relaxed);
  int64_t t = top_.load(std::memory_order_acquire);

  if (static_cast<size_t>(b - t) >= capacity_) {
    grow();
    b = bottom_.load(std::memory_order_relaxed);
  }

  get(b) = std::move(item);
  bottom_.store(b + 1, std::memory_order_release);
}

std::optional<WorkItem> WorkStealingQueue::pop() {
  int64_t b = bottom_.load(std::memory_order_relaxed) - 1;
  bottom_.store(b, std::memory_order_relaxed);

  int64_t t = top_.load(std::memory_order_acquire);

  if (t <= b) {
    WorkItem item = std::move(get(b));
    if (t == b) {
      bottom_.store(b + 1, std::memory_order_relaxed);
      top_.store(b + 1, std::memory_order_relaxed);
    }
    return item;
  }

  bottom_.store(b + 1, std::memory_order_relaxed);
  return std::nullopt;
}

std::optional<WorkItem> WorkStealingQueue::steal() {
  int64_t t = top_.load(std::memory_order_acquire);
  std::atomic_thread_fence(std::memory_order_acquire);
  int64_t b = bottom_.load(std::memory_order_acquire);

  if (t < b) {
    WorkItem item = std::move(get(t));
    int64_t new_top = t + 1;
    if (top_.compare_exchange_strong(t, new_top, std::memory_order_release,
                                     std::memory_order_relaxed)) {
      return item;
    }
    return std::nullopt;
  }

  return std::nullopt;
}

size_t WorkStealingQueue::size_approx() const {
  int64_t b = bottom_.load(std::memory_order_acquire);
  int64_t t = top_.load(std::memory_order_acquire);
  int64_t size = b - t;
  return size > 0 ? static_cast<size_t>(size) : 0;
}

bool WorkStealingQueue::empty() const {
  int64_t b = bottom_.load(std::memory_order_acquire);
  int64_t t = top_.load(std::memory_order_acquire);
  return t >= b;
}

void WorkStealingQueue::grow() {
  size_t new_capacity = capacity_ * 2;
  WorkItem* new_elements = new WorkItem[new_capacity];

  int64_t b = bottom_.load(std::memory_order_relaxed);
  int64_t t = top_.load(std::memory_order_relaxed);

  for (int64_t i = t; i < b; ++i) {
    new_elements[i % static_cast<int64_t>(new_capacity)] = std::move(get(i));
  }

  delete[] elements_;
  elements_ = new_elements;
  capacity_ = new_capacity;
}

WorkItem& WorkStealingQueue::get(int64_t index) {
  return elements_[index % static_cast<int64_t>(capacity_)];
}

const WorkItem& WorkStealingQueue::get(int64_t index) const {
  return elements_[index % static_cast<int64_t>(capacity_)];
}

}  // namespace concurrency
}  // namespace keystone
