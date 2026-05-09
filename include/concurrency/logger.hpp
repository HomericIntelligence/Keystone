#pragma once

#include <spdlog/fmt/fmt.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <string>
#include <thread>

namespace keystone {
namespace concurrency {

/**
 * @brief Generate a UUID4-format correlation ID
 *
 * Uses thread-local random state for efficiency. Not cryptographically secure
 * but suitable for log correlation across a single NATS event lifecycle.
 *
 * @return std::string UUID4 string (e.g.
 * "550e8400-e29b-41d4-a716-446655440000")
 */
std::string generateCorrelationId();

/**
 * @brief LogContext - Thread-local context for distributed logging
 *
 * Provides thread-local context information (agent_id, worker_id, session_id,
 * correlation_id) that is automatically included in all log messages from that
 * thread. correlation_id ties all log lines within a single logical operation
 * (e.g. one NATS message receipt → DAG walk → claim) together.
 *
 * Usage:
 *   LogContext::set("agent_123", 5, "session_abc");
 *   LogContext::setCorrelationId(generateCorrelationId());
 *   Logger::info("Processing message");  // Automatically includes context
 */
class LogContext {
 public:
  /**
   * @brief Set the thread-local logging context
   *
   * @param agent_id ID of the agent in this thread
   * @param worker_id Worker thread index
   * @param session_id Session identifier
   */
  static void set(const std::string& agent_id, int32_t worker_id,
                  const std::string& session_id);

  /**
   * @brief Clear the thread-local logging context (including correlation ID)
   */
  static void clear();

  /**
   * @brief Get current agent ID
   */
  static std::string getAgentId();

  /**
   * @brief Get current worker ID
   */
  static int32_t getWorkerId();

  /**
   * @brief Get current session ID
   */
  static std::string getSessionId();

  /**
   * @brief Set a correlation ID on the current thread's context
   *
   * The correlation ID is propagated into every log line emitted on this
   * thread until cleared or overwritten. Call this at the entry point of a
   * logical operation (e.g. on NATS message receipt) and clear it on exit.
   *
   * @param correlation_id UUID4 string produced by generateCorrelationId()
   */
  static void setCorrelationId(const std::string& correlation_id);

  /**
   * @brief Clear only the correlation ID, leaving agent/worker/session intact
   */
  static void clearCorrelationId();

  /**
   * @brief Get current correlation ID (empty string if not set)
   */
  static std::string getCorrelationId();

  /**
   * @brief Get formatted context string for logging
   *
   * Returns format: "[agent_id:worker_id:session_id:corr=<id>]" when a
   * correlation ID is set, or "[agent_id:worker_id:session_id]" otherwise.
   */
  static std::string getContextString();

 private:
  struct Context {
    std::string agent_id;
    int32_t worker_id = -1;
    std::string session_id;
    std::string correlation_id;
  };

  static thread_local Context context_;
};

/**
 * @brief RAII guard that sets a correlation ID for the duration of a scope
 *
 * Generates a fresh UUID4 on construction (or accepts a provided one) and
 * restores the previous correlation ID on destruction. This makes it safe to
 * nest scopes without losing the outer operation's ID.
 *
 * **Non-moveable by design**: CorrelationScope deliberately deletes move
 * constructor and move assignment to prevent use in contexts where the
 * object's lifetime may be unclear (e.g., inside std::async lambdas,
 * coroutines, or other deferred execution contexts). Moving would break RAII
 * semantics: the correlation ID would be restored at an unpredictable time.
 *
 * If you need to pass a correlation ID across async boundaries, capture a
 * std::string copy of current() via LogContext::getCorrelationId() and set it
 * explicitly on the remote thread with LogContext::setCorrelationId(id).
 *
 * Usage (correct):
 *   void onNatsMessage(const NatsMsg& msg) {
 *     CorrelationScope scope;  // new UUID, restored on exit
 *     Logger::info("Received NATS event");
 *     advanceDag(msg);         // all logs share scope.id()
 *   }
 *
 * Usage (async - capture ID string instead):
 *   void asyncHandler() {
 *     CorrelationScope scope;
 *     std::string id = scope.id();  // capture as string
 *     std::async([id]() {
 *       LogContext::setCorrelationId(id);  // set on remote thread
 *       // work here
 *     });
 *   }
 */
class CorrelationScope {
 public:
  /** Construct with a freshly generated correlation ID */
  explicit CorrelationScope();

  /** Construct with a caller-supplied correlation ID */
  explicit CorrelationScope(std::string correlation_id);

  CorrelationScope(const CorrelationScope&) = delete;
  CorrelationScope& operator=(const CorrelationScope&) = delete;
  CorrelationScope(CorrelationScope&&) = delete;
  CorrelationScope& operator=(CorrelationScope&&) = delete;

  ~CorrelationScope();

  /** Return the correlation ID active for this scope */
  const std::string& id() const noexcept { return current_id_; }

 private:
  std::string previous_id_;
  std::string current_id_;
};

/**
 * @brief Logger - Wrapper around spdlog with automatic context injection
 *
 * Provides structured logging with automatic thread-local context.
 * All log messages include [agent_id:worker_id:session_id] prefix.
 *
 * Features:
 * - Multiple log levels (trace, debug, info, warn, error, critical)
 * - Automatic context from LogContext
 * - Thread-safe
 * - Colored console output
 *
 * Usage:
 *   Logger::init();  // One-time initialization
 *   LogContext::set("chief", 0, "session_1");
 *   Logger::info("Delegating to {}", "task_agent");
 *   // Output: [2025-11-18 12:34:56.789] [info] [chief:0:session_1] Delegating
 * to task_agent
 */
class Logger {
 public:
  /**
   * @brief Initialize the logger (call once at startup)
   *
   * @param level Log level (default: info)
   */
  static void init(spdlog::level::level_enum level = spdlog::level::info);

  /**
   * @brief Shutdown the logger (call at exit)
   */
  static void shutdown();

  /**
   * @brief Set the global log level
   */
  static void setLevel(spdlog::level::level_enum level);

  // Logging methods with automatic context injection
  template <typename... Args>
  static void trace(const std::string& fmt, Args&&... args) {
    log(spdlog::level::trace, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void debug(const std::string& fmt, Args&&... args) {
    log(spdlog::level::debug, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void info(const std::string& fmt, Args&&... args) {
    log(spdlog::level::info, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void warn(const std::string& fmt, Args&&... args) {
    log(spdlog::level::warn, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void error(const std::string& fmt, Args&&... args) {
    log(spdlog::level::err, fmt, std::forward<Args>(args)...);
  }

  template <typename... Args>
  static void critical(const std::string& fmt, Args&&... args) {
    log(spdlog::level::critical, fmt, std::forward<Args>(args)...);
  }

 private:
  static std::shared_ptr<spdlog::logger> logger_;

  template <typename... Args>
  static void log(spdlog::level::level_enum level, const std::string& fmt,
                  Args&&... args) {
    if (!logger_) {
      init();
    }

    // Inject context into message
    std::string context = LogContext::getContextString();
    std::string full_fmt = context + " " + fmt;

    // Use runtime format to avoid compile-time format string requirement
    logger_->log(spdlog::source_loc{}, level, fmt::runtime(full_fmt),
                 std::forward<Args>(args)...);
  }
};

}  // namespace concurrency
}  // namespace keystone
