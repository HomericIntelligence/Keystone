/**
 * @file work_stealing_queue.cpp
 * @brief Implementation of WorkStealingQueue
 */

#include "concurrency/work_stealing_queue.hpp"

namespace keystone {
namespace concurrency {

WorkStealingQueue::WorkStealingQueue()
    : queue_(1024)  // Initial capacity
{}

void WorkStealingQueue::push(WorkItem item) {
  queue_.enqueue(std::move(item));
}

std::optional<WorkItem> WorkStealingQueue::pop() {
  WorkItem item = WorkItem::makeFunction(nullptr);
  if (queue_.try_dequeue(item)) {
    return item;
  }
  return std::nullopt;
}

std::optional<WorkItem> WorkStealingQueue::steal() {
  // ConcurrentQueue is MPMC; pop() and steal() have identical semantics.
  return pop();
}

size_t WorkStealingQueue::size_approx() const {
  return queue_.size_approx();
}

bool WorkStealingQueue::empty() const {
  return queue_.size_approx() == 0;
}

}  // namespace concurrency
}  // namespace keystone
