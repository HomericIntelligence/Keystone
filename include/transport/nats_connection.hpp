/**
 * @file nats_connection.hpp
 * @brief NATS connection wrapper with TLS, reconnection callbacks, and error
 * handling
 *
 * Wraps nats.c to provide:
 * - Optional TLS with CA certificate, client certificate, and hostname
 * verification
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
 * @brief RAII owner for a nats.c message.
 *
 * Wraps a raw natsMsg* with a custom deleter so that ownership is explicit and
 * compiler-enforced.  The deleter is natsMsg_Destroy, which nats.c requires the
 * caller to invoke after processing a message.
 *
 * A null NatsMsgPtr (constructed with nullptr) is valid and represents the
 * "no message" (timeout) case returned by NatsConnection::fetch().
 *
 * Example:
 * @code
 * NatsMsgPtr msg = conn.fetch("hi.tasks.>", "my-consumer", 5000);
 * if (msg) {
 *   natsMsg_Ack(msg.get(), nullptr);
 *   // msg is destroyed automatically when it goes out of scope
 * }
 * @endcode
 */
using NatsMsgPtr = std::unique_ptr<natsMsg, decltype(&natsMsg_Destroy)>;

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
 * @brief TLS configuration for NatsConnection
 *
 * When enable_tls is true, the connection uses TLS. The ca_cert_path,
 * client_cert_path, and client_key_path fields are optional and override
 * the corresponding KEYSTONE_NATS_TLS_CA_PATH, KEYSTONE_NATS_TLS_CERT_PATH,
 * and KEYSTONE_NATS_TLS_KEY_PATH environment variables respectively.
 *
 * skip_server_verification disables hostname and certificate chain validation
 * and must only be used in controlled test environments.
 *
 * The validate() method ensures cert_path and key_path are either both set or
 * both empty; a misconfiguration is detected at construction time rather than
 * at connection time.
 */
struct NatsTlsConfig {
  bool enable_tls{false};

  /// Path to PEM CA certificate file. Empty = use system CAs.
  /// Overridden by env var KEYSTONE_NATS_TLS_CA_PATH if that is set.
  std::string ca_cert_path;

  /// Path to PEM client certificate file (mutual TLS). Empty = no client cert.
  /// Overridden by env var KEYSTONE_NATS_TLS_CERT_PATH if that is set.
  std::string client_cert_path;

  /// Path to PEM client private key file (mutual TLS). Empty = no client key.
  /// Overridden by env var KEYSTONE_NATS_TLS_KEY_PATH if that is set.
  std::string client_key_path;

  /// Disable server certificate/hostname verification. TEST USE ONLY.
  bool skip_server_verification{false};

  /**
   * @brief Validate that client_cert_path and client_key_path are either both
   * set or both empty
   *
   * @throws std::invalid_argument if exactly one of the two paths is set
   */
  void validate() const;
};

/**
 * @brief Configuration for NatsConnection
 *
 * TLS validation occurs in the constructor to catch misconfiguration
 * (cert without key, or vice versa) at construction time rather than at
 * connection time (issue #276).
 */
struct NatsConfig {
  std::string url{"nats://localhost:4222"};

  /// Maximum reconnect attempts before giving up (-1 = unlimited)
  int max_reconnect_attempts{60};

  /// Wait between reconnect attempts
  std::chrono::milliseconds reconnect_wait{std::chrono::milliseconds{2000}};

  /// Ping interval for keep-alive detection
  std::chrono::milliseconds ping_interval{std::chrono::milliseconds{20000}};

  /// How many pings may go unacknowledged before the connection is declared
  /// dead
  int max_pings_out{2};

  /// TLS configuration (disabled by default)
  NatsTlsConfig tls;

  /**
   * @brief Construct NatsConfig with default values.
   *
   * TLS validation is deliberately deferred until connect() (issue #522).
   * Construction must not perform I/O — including std::getenv() inside
   * NatsTlsConfig::validate() — so that callers may construct configs
   * cheaply, e.g. in unit tests or in code paths where TLS is disabled.
   */
  NatsConfig() = default;

  // Defaulted copy/move/assignment — no validation at construction time.
  NatsConfig(const NatsConfig&) = default;
  NatsConfig& operator=(const NatsConfig&) = default;

  /**
   * @brief Move constructor that validates TLS config
   */
  NatsConfig(NatsConfig&&) noexcept = default;

  /**
   * @brief Move assignment that validates TLS config
   */
  NatsConfig& operator=(NatsConfig&&) noexcept = default;
};

/**
 * @brief Callback types for connection lifecycle events
 */
using ErrorCallback = std::function<void(const std::string& error_text)>;
using DisconnectedCallback = std::function<void()>;
using ReconnectedCallback = std::function<void()>;
using ClosedCallback = std::function<void()>;

/**
 * @brief NATS connection wrapper with TLS, reconnection, and error-handling
 * support
 *
 * Owns a natsConnection* and configures it with optional TLS and the four
 * lifecycle callbacks required for production resilience. The connection state
 * is tracked atomically so health-check threads can observe it without taking
 * a lock.
 *
 * TLS behaviour:
 * - When NatsTlsConfig::enable_tls is false, the connection is plaintext.
 * - When true, natsOptions_SetSecure is called. If ca_cert_path is provided
 *   (or KEYSTONE_NATS_TLS_CA_PATH is set), that CA is loaded; otherwise the
 *   nats.c default (system CAs) is used. Client certificates are loaded when
 *   both client_cert_path and client_key_path are non-empty.
 *
 * Thread-safety:
 * - connect() / disconnect() must be called from a single owner thread.
 * - All callbacks are invoked on nats.c internal threads; they must not
 *   block or call back into NatsConnection.
 * - getState() / isConnected() are lock-free and safe from any thread.
 *
 * Example (TLS with CA cert from environment):
 * @code
 * NatsConfig cfg;
 * cfg.url = "tls://nats.example.com:4222";
 * cfg.tls.enable_tls = true;  // reads KEYSTONE_NATS_TLS_CA_PATH from env
 * NatsConnection conn(cfg);
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

  /**
   * @brief Acquire (and cache) a JetStream context for this connection
   *
   * On first call after a successful connect(), acquires a jsCtx* via
   * natsConnection_JetStream() and caches it. Subsequent calls return
   * the cached pointer without re-acquiring.
   *
   * @return Non-null jsCtx* on success, nullptr if not connected or if
   *         natsConnection_JetStream() fails.
   *
   * The returned pointer is owned by this NatsConnection and is destroyed
   * automatically in disconnect() / destructor. Callers must NOT call
   * jsCtx_Destroy() on it.
   *
   * Thread-safety: this method is NOT thread-safe. Call it from the same
   * owner thread that calls connect() / disconnect().
   */
  jsCtx* jsContext() noexcept;

  // =========================================================================
  // Pull-based fetch for durable consumers (rate-limiting pattern per
  // CLAUDE.md)
  // =========================================================================

  /**
   * @brief Fetch a single message from a durable consumer (pull pattern).
   *
   * Implements the rate-limiting pull pattern described in CLAUDE.md:
   * - Myrmidon pulls exactly one message when ready for more work
   * - Provides backpressure: slow consumer simply stops fetching
   * - Timeout allows periodic wakeup for graceful shutdown checks
   *
   * Ownership is transferred to the caller via NatsMsgPtr.  The caller does
   * NOT need to call natsMsg_Destroy() — the unique_ptr destructor handles
   * cleanup automatically.  A null NatsMsgPtr (i.e. !msg) indicates that the
   * fetch timed out and no message is available; this is normal, not an error.
   *
   * @param subject       Subject pattern to subscribe to (e.g., "hi.tasks.>")
   * @param consumer_name Durable consumer name (e.g., "my-myrmidon")
   * @param timeout_ms    Fetch timeout in milliseconds (default 30000)
   * @return              NatsMsgPtr owning the fetched message, or a null
   *                      NatsMsgPtr on timeout.  Ownership is transferred via
   *                      NatsMsgPtr; the caller must NOT call natsMsg_Destroy().
   *
   * @throws std::system_error if a network error occurs (transient)
   * @throws std::domain_error if consumer or stream not found (configuration)
   * @throws std::runtime_error if authentication or authorization failure
   *
   * Thread-safety: NOT thread-safe. Call from single consumer thread.
   *
   * Exception contract (per ADR-014):
   * - std::domain_error: Configuration errors (stream/consumer not found)
   * - std::system_error: Transient errors (network, timeout)
   * - std::runtime_error: Permanent errors (auth, permission denied)
   */
  NatsMsgPtr fetch(std::string_view subject,
                   std::string_view consumer_name,
                   int64_t timeout_ms = 30000);

  // =========================================================================
  // Exception mapping utility (exposed for testing, not part of public API)
  // =========================================================================

  /**
   * @brief Map nats.c error code to standard C++ exception.
   *
   * Follows ADR-014 exception contract:
   * - NATS_NOT_FOUND, stream/consumer errors → std::domain_error
   * - Network, timeout errors → std::system_error
   * - Auth, authorization errors → std::runtime_error
   *
   * @param status NATS error code from nats.c
   * @param context Description of operation (e.g., "subscribe")
   * @throws One of: std::domain_error, std::system_error, std::runtime_error
   */
  static void throwForNatsStatus(natsStatus status, const std::string& context);

 protected:
  // nats.c static callback shims — nats.c passes a void* user data pointer
  // which we cast back to NatsConnection*. Protected to allow test subclasses
  // to invoke them directly without a live nats.c connection.
  static void onError(natsConnection* nc,
                      natsSubscription* sub,
                      natsStatus err,
                      void* closure) noexcept;
  static void onDisconnected(natsConnection* nc, void* closure) noexcept;
  static void onReconnected(natsConnection* nc, void* closure) noexcept;
  static void onClosed(natsConnection* nc, void* closure) noexcept;

 private:
  /**
   * @brief Apply TLS options to natsOptions
   * @return true on success, false on any nats.c error
   */
  bool applyTlsOptions(natsOptions* opts) const;

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

  // Cached JetStream context — acquired lazily by jsContext(), destroyed by
  // disconnect() / destructor
  jsCtx* js_ctx_{nullptr};

  // Observable state (atomic for lock-free health checks)
  std::atomic<NatsConnectionState> state_{NatsConnectionState::DISCONNECTED};
};

}  // namespace transport
}  // namespace keystone
