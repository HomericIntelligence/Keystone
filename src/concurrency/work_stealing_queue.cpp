/**
 * @file work_stealing_queue.cpp
 * @brief WorkStealingQueue — mutex-protected deque with LIFO pop / FIFO steal.
 *
 * Implements Issues #346 and #349: pop() is LIFO (owner removes from back),
 * steal() is FIFO (thief removes from front). push() is multi-thread safe.
 */

#include "concurrency/work_stealing_queue.hpp"

#include "concurrency/logger.hpp"

namespace keystone {
namespace concurrency {

void WorkStealingQueue::push(WorkItem item) {
  if (item.correlation_id.empty()) {
    item.correlation_id = LogContext::getCorrelationId();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  deque_.push_back(std::move(item));
}

std::optional<WorkItem> WorkStealingQueue::pop() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (deque_.empty()) {
    return std::nullopt;
  }
  WorkItem item = std::move(deque_.back());
  deque_.pop_back();
  return item;
}

std::optional<WorkItem> WorkStealingQueue::steal() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (deque_.empty()) {
    return std::nullopt;
  }
  WorkItem item = std::move(deque_.front());
  deque_.pop_front();
  // FIX #284: Restore correlation ID on worker thread before execution
  if (!item.correlation_id.empty()) {
    LogContext::setCorrelationId(item.correlation_id);
  }
  return item;
}

size_t WorkStealingQueue::size_approx() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return deque_.size();
}

bool WorkStealingQueue::empty() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return deque_.empty();
}

}  // namespace concurrency
}  // namespace keystone
