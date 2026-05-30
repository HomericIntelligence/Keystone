#pragma once

// agent_awaitable.hpp — Async inbox polling helpers for AgentCore-based agents.
//
// Provides:
//   YieldAwaitable  — a trivial awaitable that suspends once and schedules the
//                     coroutine back onto the WorkStealingScheduler (or yields
//                     the OS thread when no scheduler is present).
//   awaitMessage()  — a free-function coroutine that polls
//   AgentCore::getMessage()
//                     with bounded retry, properly suspending between polls
//                     instead of busy-spinning.
//
// Design note (Issue #509): getMessage() is a non-blocking lock-free poll.
// Calling it directly inside a coroutine immediately after sendMessage() is a
// timing bug: the response may not have arrived yet, so the coroutine silently
// returns nullopt and reports an error.  awaitMessage() bridges this by polling
// with genuine coroutine suspension between attempts, capped by a deadline so
// callers have a bounded worst-case latency.
//
// Default deadline: 500 ms (sufficient for Phase-1 test scenarios; callers that
// need a different bound pass an explicit deadline).
//
// Thread-safety: each poll calls AgentCore::getMessage() which uses a lock-free
// ConcurrentQueue — no additional locking is introduced.

#include <chrono>
#include <coroutine>
#include <optional>
#include <thread>

#include "agents/agent_core.hpp"
#include "concurrency/scheduler_accessor.hpp"
#include "concurrency/task.hpp"
#include "core/message.hpp"

namespace keystone {
namespace agents {

/**
 * @brief Trivial awaitable that yields the coroutine to the scheduler once.
 *
 * When a WorkStealingScheduler is present on the current thread (set by the
 * scheduler worker loop via setCurrentScheduler), the coroutine handle is
 * submitted back to the scheduler and the current thread is released.
 *
 * When no scheduler is present (e.g., single-threaded unit tests), the OS
 * thread is yielded via std::this_thread::yield() to avoid busy-spinning, and
 * the coroutine is immediately resumed on the same thread.
 */
struct YieldAwaitable {
  // Never immediately ready — we always want at least one yield point.
  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> handle) const noexcept {
    auto* sched = concurrency::getCurrentScheduler();
    if (sched != nullptr) {
      // Hand the coroutine back to the work-stealing scheduler.
      // The current worker thread is free to pick up other work.
      sched->submit(handle);
    } else {
      // No scheduler — yield the OS thread to reduce CPU burn, then resume
      // inline.  In unit tests this collapses the yield to a single
      // thread::yield() call rather than a true suspension, which is acceptable
      // because tests drive the coroutine synchronously via Task::get().
      std::this_thread::yield();
      handle.resume();
    }
  }

  void await_resume() const noexcept {}
};

/**
 * @brief Poll agent inbox until a message arrives or the deadline passes.
 *
 * Replaces a direct call to AgentCore::getMessage() inside a coroutine.
 * Between each failed poll the coroutine suspends via YieldAwaitable, allowing
 * the scheduler to run other work while the response is in-flight.
 *
 * @param agent  The AgentCore whose inbox to poll.
 * @param deadline  Upper bound on how long to wait.  Defaults to 500 ms from
 *                  the time of call.  Callers may pass a shorter or longer
 *                  deadline; the function does NOT silently extend it.
 * @return Task<std::optional<core::KeystoneMessage>>
 *         The next inbox message, or std::nullopt if the deadline expired
 *         before any message arrived.
 *
 * Latency contract: callers of sendCommand() should be aware that this
 * function may take up to `deadline - now` milliseconds to return nullopt when
 * the peer is unresponsive.  The default 500 ms bound is intentional and
 * documented here so callers can override it.
 */
inline concurrency::Task<std::optional<core::KeystoneMessage>> awaitMessage(
    AgentCore& agent,
    std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds{500}) {
  while (true) {
    auto msg = agent.getMessage();
    if (msg.has_value()) {
      co_return msg;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      co_return std::nullopt;
    }
    // Suspend once, then re-poll.
    co_await YieldAwaitable{};
  }
}

}  // namespace agents
}  // namespace keystone
