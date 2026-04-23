/**
 * @file nats_listener.hpp
 * @brief NATS JetStream listener with startup error handling and retry logic
 *
 * Issue #139: Handle nats stream-not-found during startup with exponential
 * backoff retry and optional auto-create-stream support.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace keystone {
namespace network {

/**
 * @brief Error categories for NATS startup failures.
 *
 * Determines whether a failure is retriable (transient) or permanent.
 */
enum class NatsStartupError {
  StreamNotFound,    ///< JetStream stream does not match any configured stream
  ConnectionFailed,  ///< Transient connectivity failure
  Permanent,         ///< Auth failure or misconfiguration — do not retry
};

/**
 * @brief Callback invoked when a NATS message arrives.
 *
 * @param subject  The NATS subject the message was published to
 * @param payload  Raw message payload bytes
 */
using NatsMessageHandler =
    std::function<void(const std::string& subject, const std::string& payload)>;

/**
 * @brief Abstraction over a NATS JetStream connection.
 *
 * Isolates nats.c library calls so unit tests can inject a mock without
 * requiring a real NATS server.
 */
class INatsConnection {
 public:
  virtual ~INatsConnection() = default;

  /**
   * @brief Connect to the NATS server at the given URL.
   * @throws std::runtime_error on permanent failure
   * @throws std::system_error  on transient failure
   */
  virtual void connect(const std::string& url) = 0;

  /**
   * @brief Subscribe to a JetStream durable consumer.
   *
   * @param subject      Subject filter (e.g. "hi.tasks.>")
   * @param stream_name  JetStream stream name
   * @param durable_name Durable consumer name
   * @param handler      Message callback
   * @throws std::domain_error if the stream does not exist
   * @throws std::runtime_error on other permanent failures
   * @throws std::system_error  on transient failures
   */
  virtual void subscribe(const std::string& subject,
                         const std::string& stream_name,
                         const std::string& durable_name,
                         NatsMessageHandler handler) = 0;

  /**
   * @brief Create a JetStream stream with the given name and subject filter.
   *
   * Only called when auto_create_stream is enabled.
   *
   * @throws std::runtime_error on failure
   */
  virtual void createStream(const std::string& stream_name, const std::string& subject_filter) = 0;

  /**
   * @brief Unsubscribe and close the connection.
   */
  virtual void disconnect() = 0;
};

/**
 * @brief NATS JetStream listener with startup retry and error recovery.
 *
 * Wraps a NATS JetStream subscription with:
 * - Exponential backoff retry on stream-not-found and transient errors
 * - Optional auto-create-stream on first-run scenarios
 * - Shutdown-aware sleep so stop() is responsive even during long backoffs
 * - Permanent error detection (auth failures) — raised immediately, no retry
 *
 * Example:
 * @code
 * NatsListener::Config config{
 *     .server_url       = "nats://localhost:4222",
 *     .subject          = "hi.tasks.>",
 *     .stream_name      = "homeric-tasks",
 *     .durable_name     = "keystone-worker",
 *     .auto_create_stream = false,
 * };
 * NatsListener listener(config, std::move(connection));
 * listener.setMessageHandler([](auto& subj, auto& payload) { ... });
 * listener.start();   // blocks until stop() is called or permanent error
 * @endcode
 */
class NatsListener {
 public:
  /**
   * @brief Listener configuration.
   */
  struct Config {
    std::string server_url{"nats://localhost:4222"};  ///< NATS server URL
    std::string subject{"hi.tasks.>"};                ///< JetStream subject filter
    std::string stream_name{"homeric-tasks"};         ///< JetStream stream name
    std::string durable_name{"keystone-worker"};      ///< Durable consumer name

    /// When true, Keystone creates the stream if it does not exist.
    /// When false, logs instructions for manual creation and retries.
    bool auto_create_stream{false};

    std::chrono::milliseconds initial_backoff_ms{1000};  ///< First retry delay
    std::chrono::milliseconds max_backoff_ms{60000};     ///< Backoff cap
    double backoff_multiplier{2.0};                      ///< Exponential factor

    /// Poll interval for shutdown checks during backoff sleep.
    std::chrono::milliseconds shutdown_poll_ms{200};
  };

  /**
   * @brief Construct with configuration and NATS connection implementation.
   *
   * @param config      Listener configuration
   * @param connection  NATS connection abstraction (injected for testability)
   */
  NatsListener(Config config, std::unique_ptr<INatsConnection> connection);

  ~NatsListener();

  // Non-copyable, non-movable (owns thread state)
  NatsListener(const NatsListener&) = delete;
  NatsListener& operator=(const NatsListener&) = delete;

  /**
   * @brief Register a message handler invoked for every received message.
   *
   * Must be called before start().
   */
  void setMessageHandler(NatsMessageHandler handler);

  /**
   * @brief Connect and subscribe, retrying on recoverable errors.
   *
   * Blocks until:
   * - The subscription is established successfully, or
   * - stop() is called (returns normally), or
   * - A permanent error occurs (throws std::runtime_error).
   *
   * Retry behaviour:
   * - StreamNotFound + auto_create_stream=true  → create stream, retry
   * - StreamNotFound + auto_create_stream=false → log instructions, backoff, retry
   * - ConnectionFailed                          → log warning, backoff, retry
   * - Permanent (auth, bad config)              → throw immediately
   *
   * @throws std::runtime_error on permanent NATS errors
   */
  void start();

  /**
   * @brief Signal the retry loop to exit and disconnect.
   *
   * Safe to call from any thread. Returns immediately; the loop exits within
   * one shutdown_poll_ms interval.
   */
  void stop();

  /**
   * @brief Return whether the listener is currently subscribed.
   */
  bool isConnected() const { return connected_.load(std::memory_order_acquire); }

  /**
   * @brief Return how many startup attempts have been made.
   */
  uint32_t getAttemptCount() const { return attempt_count_.load(std::memory_order_acquire); }

 private:
  Config config_;
  std::unique_ptr<INatsConnection> connection_;
  NatsMessageHandler handler_;

  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> connected_{false};
  std::atomic<uint32_t> attempt_count_{0};

  /**
   * @brief Classify an exception thrown by INatsConnection into an error category.
   */
  static NatsStartupError classifyException(const std::exception& ex);

  /**
   * @brief Compute exponential backoff delay for a given attempt number.
   */
  std::chrono::milliseconds computeBackoff(uint32_t attempt) const;

  /**
   * @brief Sleep for `duration`, polling stop_requested_ every shutdown_poll_ms.
   *
   * @return true if stop was requested during the sleep
   */
  bool sleepInterruptible(std::chrono::milliseconds duration) const;

  /**
   * @brief Log a clear human-readable message explaining stream-not-found
   *        and how to create the stream manually.
   */
  void logStreamNotFoundInstructions() const;
};

}  // namespace network
}  // namespace keystone
