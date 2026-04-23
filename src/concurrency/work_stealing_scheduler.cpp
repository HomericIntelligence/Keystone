/**
 * @file work_stealing_scheduler.cpp
 * @brief Implementation of WorkStealingScheduler
 */

#include "concurrency/work_stealing_scheduler.hpp"

#include "concurrency/scheduler_accessor.hpp"

#include <random>
#include <sstream>

// Phase D: CPU affinity support (Linux-specific)
#ifdef __linux__
#  include <pthread.h>
#  include <sched.h>
#endif

namespace keystone {
namespace concurrency {

WorkStealingScheduler::WorkStealingScheduler(size_t num_workers, bool enable_cpu_affinity)
    : num_workers_(num_workers), enable_cpu_affinity_(enable_cpu_affinity) {
  // FIX P2-10: Enforce maximum worker thread limit to prevent DoS
  if (num_workers_ > MAX_WORKER_THREADS) {
    throw std::invalid_argument(
        "Too many worker threads requested: " + std::to_string(num_workers_) +
        " (max: " + std::to_string(MAX_WORKER_THREADS) + ")");
  }

  if (num_workers_ == 0) {
    num_workers_ = 1;  // At least one worker
  }

  // Create worker queues
  worker_queues_.reserve(num_workers_);
  queue_pointers_.reserve(num_workers_);

  for (size_t i = 0; i < num_workers_; ++i) {
    worker_queues_.push_back(std::make_unique<WorkStealingQueue>());
    queue_pointers_.push_back(worker_queues_[i].get());
  }
}

WorkStealingScheduler::~WorkStealingScheduler() {
  if (running_.load()) {
    shutdown();
  }
}

void WorkStealingScheduler::start() {
  if (running_.exchange(true)) {
    // Already running
    return;
  }

  shutdown_requested_.store(false);
  workers_.reserve(num_workers_);

  Logger::info("WorkStealingScheduler starting with {} workers", num_workers_);

  // Launch worker threads
  for (size_t i = 0; i < num_workers_; ++i) {
    workers_.emplace_back([this, i]() { workerLoop(i); });
  }

  Logger::info("WorkStealingScheduler started");
}

void WorkStealingScheduler::submit(std::function<void()> func) {
  size_t worker_idx = getNextWorkerIndex();
  submitTo(worker_idx, std::move(func));
}

void WorkStealingScheduler::submit(std::coroutine_handle<> handle) {
  size_t worker_idx = getNextWorkerIndex();
  submitTo(worker_idx, handle);
}

void WorkStealingScheduler::submitTo(size_t worker_index, std::function<void()> func) {
  if (worker_index >= num_workers_) {
    Logger::error("Invalid worker index: {} (max: {})", worker_index, num_workers_ - 1);
    return;
  }

  worker_queues_[worker_index]->push(WorkItem::makeFunction(std::move(func)));

  // Stream C1: Wake up workers in SLEEP phase immediately
  shutdown_cv_.notify_all();
}

void WorkStealingScheduler::submitTo(size_t worker_index, std::coroutine_handle<> handle) {
  if (worker_index >= num_workers_) {
    Logger::error("Invalid worker index: {} (max: {})", worker_index, num_workers_ - 1);
    return;
  }

  worker_queues_[worker_index]->push(WorkItem::makeCoroutine(handle));

  // Stream C1: Wake up workers in SLEEP phase immediately
  shutdown_cv_.notify_all();
}

void WorkStealingScheduler::shutdown() {
  if (!running_.load()) {
    return;  // Not running
  }

  Logger::info("WorkStealingScheduler shutting down...");

  // Set shutdown flag and notify all workers
  // FIX Issue #18: Wake up all sleeping workers immediately
  {
    std::lock_guard<std::mutex> lock(shutdown_mutex_);
    shutdown_requested_.store(true);
  }
  shutdown_cv_.notify_all();

  // Wait for all workers to finish
  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }

  workers_.clear();
  running_.store(false);

  Logger::info("WorkStealingScheduler shutdown complete");
}

bool WorkStealingScheduler::isRunning() const {
  return running_.load();
}

size_t WorkStealingScheduler::getNumWorkers() const {
  return num_workers_;
}

size_t WorkStealingScheduler::getApproximateWorkCount() const {
  size_t total = 0;
  for (const auto& queue : worker_queues_) {
    total += queue->size_approx();
  }
  return total;
}

std::optional<std::function<void()>> WorkStealingScheduler::tryStealWork() {
  // Try to steal from a random worker queue (for NUMA simulation)
  // Randomly select a victim worker to steal from
  // NOTE: Static initialization is thread-safe in C++11+
  static std::mt19937 rng{std::random_device{}()};
  std::uniform_int_distribution<size_t> dist(0, num_workers_ - 1);

  size_t victim_idx = dist(rng);
  auto work_item = worker_queues_[victim_idx]->steal();

  if (work_item.has_value()) {
    // Convert WorkItem to std::function<void()>
    if (work_item->type == WorkItem::Type::Function) {
      return work_item->func;
    } else if (work_item->type == WorkItem::Type::Coroutine) {
      // Wrap coroutine handle in a function
      // SAFETY: Validate handle before capture to prevent use-after-free
      auto handle = work_item->handle;
      if (!handle || handle.done()) {
        Logger::warn("Attempted to steal completed/invalid coroutine handle");
        return std::nullopt;
      }

      // WARNING: Returned function MUST be executed immediately before the
      // coroutine is destroyed. Do NOT store for later execution.
      return std::function<void()>([handle]() {
        if (handle && !handle.done()) {
          handle.resume();
        }
      });
    }
  }

  return std::nullopt;
}

size_t WorkStealingScheduler::getNextWorkerIndex() {
  // Round-robin: atomic increment and modulo
  size_t idx = next_worker_index_.fetch_add(1, std::memory_order_relaxed);
  return idx % num_workers_;
}

std::optional<WorkItem> WorkStealingScheduler::tryStealWithBackoff(size_t worker_index) {
  auto& own_queue = *worker_queues_[worker_index];
  size_t iterations = 0;

  // Phase 1: SPIN (0-100 iterations)
  // Tight loop for ultra-low latency (1-10 microseconds)
  while (iterations < SPIN_ITERATIONS) {
    // Try own queue first
    auto work = own_queue.pop();
    if (work.has_value()) {
      return work;
    }

    // Try stealing from other workers
    for (size_t i = 1; i < num_workers_; ++i) {
      size_t victim_idx = (worker_index + i) % num_workers_;
      work = worker_queues_[victim_idx]->steal();
      if (work.has_value()) {
        Logger::trace("Worker {} stole work from worker {} (SPIN phase)", worker_index, victim_idx);
        return work;
      }
    }

    ++iterations;

    // Early exit on shutdown
    if (shutdown_requested_.load()) {
      return std::nullopt;
    }
  }

  // Phase 2: YIELD (101-1000 iterations)
  // Yield CPU to other threads while staying responsive (10-100 microseconds)
  while (iterations < YIELD_ITERATIONS) {
    // Try own queue
    auto work = own_queue.pop();
    if (work.has_value()) {
      return work;
    }

    // Try stealing
    for (size_t i = 1; i < num_workers_; ++i) {
      size_t victim_idx = (worker_index + i) % num_workers_;
      work = worker_queues_[victim_idx]->steal();
      if (work.has_value()) {
        Logger::trace("Worker {} stole work from worker {} (YIELD phase)",
                      worker_index,
                      victim_idx);
        return work;
      }
    }

    // Yield CPU slice to other threads
    std::this_thread::yield();
    ++iterations;

    // Early exit on shutdown
    if (shutdown_requested_.load()) {
      return std::nullopt;
    }
  }

  // Phase 3: SLEEP (1001+ iterations)
  // Sleep with wake-up notification for minimal CPU usage
  while (true) {
    // Try own queue
    auto work = own_queue.pop();
    if (work.has_value()) {
      return work;
    }

    // Try stealing
    for (size_t i = 1; i < num_workers_; ++i) {
      size_t victim_idx = (worker_index + i) % num_workers_;
      work = worker_queues_[victim_idx]->steal();
      if (work.has_value()) {
        Logger::trace("Worker {} stole work from worker {} (SLEEP phase)",
                      worker_index,
                      victim_idx);
        return work;
      }
    }

    // Sleep with condition variable wake-up
    // This allows immediate wake-up when work is submitted
    std::unique_lock<std::mutex> lock(shutdown_mutex_);
    shutdown_cv_.wait_for(lock, SLEEP_DURATION, [this]() { return shutdown_requested_.load(); });

    if (shutdown_requested_.load()) {
      return std::nullopt;
    }
  }
}

void WorkStealingScheduler::workerLoop(size_t worker_index) {
  // Set thread-local logging context
  std::ostringstream oss;
  oss << "worker_" << worker_index;
  LogContext::set(oss.str(), static_cast<int>(worker_index), "scheduler");

  // Issue #19: Register this scheduler in thread-local storage
  // This allows Task<T>::await_suspend() to access the scheduler for async submission
  setCurrentScheduler(this);

  // Phase D: Set CPU affinity if enabled
  if (enable_cpu_affinity_) {
    setCPUAffinity(worker_index);
  }

  Logger::debug("Worker {} starting", worker_index);

  // Stream C1: Main work loop with 3-phase backoff
  while (!shutdown_requested_.load()) {
    // Try to get work with 3-phase backoff (SPIN → YIELD → SLEEP)
    auto work = tryStealWithBackoff(worker_index);

    if (work.has_value() && work->valid()) {
      // Execute the work item
      Logger::trace("Worker {} executing work", worker_index);
      work->execute();
      // Note: tryStealWithBackoff() resets internally when work is found
    }
    // No else needed - tryStealWithBackoff() handles backoff internally
  }

  // Drain remaining work from own queue before exiting
  Logger::debug("Worker {} draining queue before exit", worker_index);
  auto& own_queue = *worker_queues_[worker_index];
  while (auto work = own_queue.pop()) {
    if (work->valid()) {
      work->execute();
    }
  }

  Logger::debug("Worker {} exiting", worker_index);
  LogContext::clear();

  // Issue #19: Clear the thread-local scheduler when worker exits
  setCurrentScheduler(nullptr);
}

void WorkStealingScheduler::setCPUAffinity(size_t worker_index) {
#ifdef __linux__
  // Linux: Use pthread_setaffinity_np()
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);

  // Map worker to CPU core (wrap around if more workers than cores)
  size_t cpu_id = worker_index % std::thread::hardware_concurrency();
  CPU_SET(cpu_id, &cpuset);

  pthread_t thread = pthread_self();
  int result = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);

  if (result != 0) {
    Logger::warn("Worker {} failed to set CPU affinity to core {}: error {}",
                 worker_index,
                 cpu_id,
                 result);
  } else {
    Logger::debug("Worker {} pinned to CPU core {}", worker_index, cpu_id);
  }
#else
  // Other platforms: No-op (affinity not supported or not implemented)
  Logger::debug("Worker {}: CPU affinity not supported on this platform", worker_index);
  (void)worker_index;  // Suppress unused parameter warning
#endif
}

}  // namespace concurrency
}  // namespace keystone
