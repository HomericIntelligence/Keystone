/**
 * @file nats_connection.hpp
 * @brief NATS connection wrapper with reconnection callbacks and error handling
 *
 * Wraps nats.c to provide:
 * - Configurable reconnection (max attempts, wait interval)
 * - Error, disconnected, reconnected, and closed callbacks
 * - Observable connection state for health checks
 * - Graceful shutdown without crashing the daemon
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include <nats.h>

namespace keystone {
namespace transport {

/**
 * @brief NATS connection state observable by health checks
 */
enum class NatsConnectionState {
  DISCONNECTED,  ///< Not connected (initial / after close)
  CONNECTED,     ///< Active connection established
  RECONNECTING,  ///< Temporarily disconnected, attempting reconnect
  CLOSED,        ///< Connection permanently closed
};

/**
 * @brief Configuration for NatsConnection
 */
struct NatsConfig {
  std::string url{"nats://localhost:4222"};

  /// Maximum reconnect attempts before giving up (-1 = unlimited)
  int max_reconnect_attempts{60};

  /// Wait between reconnect attempts
  std::chrono::milliseconds reconnect_wait{std::chrono::milliseconds{2000}};

  /// Ping interval for keep-alive detection
  std::chrono::milliseconds ping_interval{std::chrono::milliseconds{20000}};

  /// How many pings may go unacknowledged before the connection is declared dead
  int max_pings_out{2};
};

/**
 * @brief Callback types for connection lifecycle events
 */
using ErrorCallback = std::function<void(const std::string& error_text)>;
using DisconnectedCallback = std::function<void()>;
using ReconnectedCallback = std::function<void()>;
using ClosedCallback = std::function<void()>;

/**
 * @brief NATS connection wrapper with reconnection and error-handling support
 *
 * Owns a natsConnection* and configures it with the four lifecycle callbacks
 * required for production resilience. The connection state is tracked
 * atomically so health-check threads can observe it without taking a lock.
 *
 * Thread-safety:
 * - connect() / disconnect() must be called from a single owner thread.
 * - All callbacks are invoked on nats.c internal threads; they must not
 *   block or call back into NatsConnection.
 * - getState() / isConnected() are lock-free and safe from any thread.
 *
 * Example:
 * @code
 * NatsConfig cfg{.url = "nats://localhost:4222", .max_reconnect_attempts = 60};
 * NatsConnection conn(cfg);
 * conn.setErrorCallback([](const std::string& e){ log(e); });
 * conn.setDisconnectedCallback([]{ mark_degraded(); });
 * conn.setReconnectedCallback([]{ mark_healthy(); });
 * conn.setClosedCallback([]{ mark_degraded(); });
 * conn.connect();
 * @endcode
 */
class NatsConnection {
 public:
  explicit NatsConnection(NatsConfig config = {});
  ~NatsConnection();

  // Non-copyable, non-movable (owns raw nats.c handle)
  NatsConnection(const NatsConnection&) = delete;
  NatsConnection& operator=(const NatsConnection&) = delete;
  NatsConnection(NatsConnection&&) = delete;
  NatsConnection& operator=(NatsConnection&&) = delete;

  // =========================================================================
  // Callback registration — must be called before connect()
  // =========================================================================

  void setErrorCallback(ErrorCallback cb);
  void setDisconnectedCallback(DisconnectedCallback cb);
  void setReconnectedCallback(ReconnectedCallback cb);
  void setClosedCallback(ClosedCallback cb);

  // =========================================================================
  // Connection lifecycle
  // =========================================================================

  /**
   * @brief Establish connection to NATS server
   * @return true on success, false if nats.c reports an error
   */
  bool connect();

  /**
   * @brief Close connection and release nats.c resources
   *
   * Safe to call even if connect() was never called or already disconnected.
   */
  void disconnect();

  // =========================================================================
  // State inspection (lock-free, any thread)
  // =========================================================================

  NatsConnectionState getState() const noexcept;
  bool isConnected() const noexcept;

  /**
   * @brief Return the raw nats.c connection handle
   *
   * The caller must NOT close or destroy the returned pointer. Only use it
   * to publish messages or subscribe. The handle is valid until disconnect()
   * is called.
   */
  natsConnection* handle() const noexcept;

 protected:
  // nats.c static callback shims — nats.c passes a void* user data pointer
  // which we cast back to NatsConnection*.  Protected to allow test subclasses
  // to invoke them directly without a live nats.c connection.
  static void onError(natsConnection* nc,
                      natsSubscription* sub,
                      natsStatus err,
                      void* closure) noexcept;
  static void onDisconnected(natsConnection* nc, void* closure) noexcept;
  static void onReconnected(natsConnection* nc, void* closure) noexcept;
  static void onClosed(natsConnection* nc, void* closure) noexcept;

 private:
  NatsConfig config_;

  // Callbacks (protected by callbacks_mutex_ during registration only;
  // nats.c callbacks fire after connect() so no concurrent write is possible)
  mutable std::mutex callbacks_mutex_;
  ErrorCallback error_cb_;
  DisconnectedCallback disconnected_cb_;
  ReconnectedCallback reconnected_cb_;
  ClosedCallback closed_cb_;

  // Raw handle — owned by this object
  natsConnection* conn_{nullptr};

  // Observable state (atomic for lock-free health checks)
  std::atomic<NatsConnectionState> state_{NatsConnectionState::DISCONNECTED};
};

}  // namespace transport
}  // namespace keystone
