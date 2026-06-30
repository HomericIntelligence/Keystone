/**
 * @file nats_connection.cpp
 * @brief NATS connection wrapper implementation with TLS support
 */

#include "transport/nats_connection.hpp"

#include <nats.h>
#include <spdlog/spdlog.h>

#include <cerrno>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>

namespace keystone {
namespace transport {

namespace {

/**
 * @brief TLS environment variable cache.
 *
 * The three KEYSTONE_NATS_TLS_* env vars are read exactly once at first use
 * via a C++20 guaranteed thread-safe static local initialiser (ISO C++11 §6.7
 * / C++20 §9.7).  The immediately-invoked lambda ensures the three
 * std::getenv() calls happen atomically before any concurrent first-caller
 * races on the static.  No std::once_flag or std::mutex is required — the
 * language guarantees are sufficient.
 *
 * Behavioural note: because the values are cached at first access, changes to
 * these env vars after the first call to cachedTlsEnvVars() (or any function
 * that delegates to it) will NOT be reflected.  This is intentional: env vars
 * should be set before process start and must not change while the process is
 * running.
 */
struct TlsEnvVars {
  std::string ca_path;
  std::string cert_path;
  std::string key_path;
};

const TlsEnvVars& cachedTlsEnvVars() {
  // Thread-safe by C++11/20 guaranteed static-local initialisation.
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static const TlsEnvVars vars = []() {
    TlsEnvVars v;
    const char* ca = std::getenv("KEYSTONE_NATS_TLS_CA_PATH");
    const char* cert = std::getenv("KEYSTONE_NATS_TLS_CERT_PATH");
    const char* key = std::getenv("KEYSTONE_NATS_TLS_KEY_PATH");
    v.ca_path = ca != nullptr ? ca : "";
    v.cert_path = cert != nullptr ? cert : "";
    v.key_path = key != nullptr ? key : "";
    return v;
  }();
  return vars;
}

// NATS error code categorization per ADR-014
enum class NatsErrorCategory {
  kConfigError,  // Configuration error — requires manual intervention
  kTransient,    // Transient error — retry with backoff
  kPermanent,    // Permanent error — fail fast
};

/**
 * @brief Classify a NATS error code by semantic category (per ADR-014).
 *
 * @param status NATS error code from nats.c
 * @return Error category (config, transient, or permanent)
 */
NatsErrorCategory categorizeNatsError(natsStatus status) {
  switch (status) {
    // Configuration errors
    case NATS_ERR:
    case NATS_INVALID_ARG:
      return NatsErrorCategory::kConfigError;

    // Transient errors
    case NATS_NO_SERVER:
    case NATS_NOT_YET_CONNECTED:
    case NATS_TIMEOUT:
    case NATS_CONNECTION_CLOSED:
    case NATS_NO_MEMORY:
    case NATS_STALE_CONNECTION:
    case NATS_PROTOCOL_ERROR:
    case NATS_IO_ERROR:
    case NATS_ILLEGAL_STATE:
      return NatsErrorCategory::kTransient;

    // Permanent errors
    case NATS_CONNECTION_AUTH_FAILED:
    case NATS_INSUFFICIENT_BUFFER:
    case NATS_NOT_PERMITTED:
    case NATS_DRAINING:
    case NATS_FAILED_TO_INITIALIZE:
    case NATS_SECURE_CONNECTION_WANTED:
    case NATS_INVALID_SUBJECT:
    case NATS_MAX_PAYLOAD:
      return NatsErrorCategory::kPermanent;

    default:
      return NatsErrorCategory::kTransient;
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// NatsTlsConfig validation
// ---------------------------------------------------------------------------

void NatsTlsConfig::validate() const {
  // Get effective cert and key paths (env vars take precedence).
  // cachedTlsEnvVars() reads the environment exactly once (thread-safe static
  // initialisation); see the implementation note in the anonymous namespace.
  const TlsEnvVars& env = cachedTlsEnvVars();
  std::string cert_path =
      env.cert_path.empty() ? client_cert_path : env.cert_path;
  std::string key_path = env.key_path.empty() ? client_key_path : env.key_path;

  // Both must be set or both must be empty
  if ((!cert_path.empty() && key_path.empty()) ||
      (cert_path.empty() && !key_path.empty())) {
    throw std::invalid_argument(
        "NatsTlsConfig: client certificate and key must both be set or both "
        "be empty; cert_path='" +
        cert_path + "' key_path='" + key_path + "'");
  }
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

NatsConnection::NatsConnection(NatsConfig config)
    : config_(std::move(config)) {}

NatsConnection::~NatsConnection() { disconnect(); }

// ---------------------------------------------------------------------------
// Callback registration
// ---------------------------------------------------------------------------

void NatsConnection::setErrorCallback(ErrorCallback cb) {
  std::lock_guard<std::mutex> lock(callbacks_mutex_);
  error_cb_ = std::move(cb);
}

void NatsConnection::setDisconnectedCallback(DisconnectedCallback cb) {
  std::lock_guard<std::mutex> lock(callbacks_mutex_);
  disconnected_cb_ = std::move(cb);
}

void NatsConnection::setReconnectedCallback(ReconnectedCallback cb) {
  std::lock_guard<std::mutex> lock(callbacks_mutex_);
  reconnected_cb_ = std::move(cb);
}

void NatsConnection::setClosedCallback(ClosedCallback cb) {
  std::lock_guard<std::mutex> lock(callbacks_mutex_);
  closed_cb_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// TLS configuration
// ---------------------------------------------------------------------------

bool NatsConnection::applyTlsOptions(natsOptions* opts) const {
  const NatsTlsConfig& tls = config_.tls;

  if (!tls.enable_tls) {
    return true;
  }

  if (natsOptions_SetSecure(opts, true) != NATS_OK) {
    spdlog::error("NatsConnection: failed to enable TLS on natsOptions");
    return false;
  }

  if (tls.skip_server_verification) {
    if (natsOptions_SkipServerVerification(opts, true) != NATS_OK) {
      spdlog::error("NatsConnection: failed to set skip_server_verification");
      return false;
    }
  }

  // CA certificate: env var takes precedence over config field.
  // cachedTlsEnvVars() reads the environment exactly once (thread-safe static
  // initialisation); see the implementation note in the anonymous namespace.
  const TlsEnvVars& env = cachedTlsEnvVars();
  std::string ca_path = env.ca_path.empty() ? tls.ca_cert_path : env.ca_path;
  if (!ca_path.empty()) {
    if (natsOptions_LoadCATrustedCertificates(opts, ca_path.c_str()) !=
        NATS_OK) {
      spdlog::error("NatsConnection: failed to load CA certificate from {}",
                    ca_path);
      return false;
    }
  }

  // Client certificate (mutual TLS): env vars take precedence over config
  // fields
  std::string cert_path =
      env.cert_path.empty() ? tls.client_cert_path : env.cert_path;
  std::string key_path =
      env.key_path.empty() ? tls.client_key_path : env.key_path;

  if (!cert_path.empty() && !key_path.empty()) {
    if (natsOptions_LoadCertificatesChain(opts, cert_path.c_str(),
                                          key_path.c_str()) != NATS_OK) {
      spdlog::error(
          "NatsConnection: failed to load client certificate from {} / {}",
          cert_path, key_path);
      return false;
    }
  }
  // Note: cert/key parity is validated in NatsConfig constructor (issue #276),
  // so we cannot reach the case where exactly one is set.

  return true;
}

// ---------------------------------------------------------------------------
// Connection lifecycle
// ---------------------------------------------------------------------------

bool NatsConnection::connect() {
  // Validate TLS configuration lazily at connect time (issue #522). Pushing
  // this out of the NatsConfig constructor lets callers construct configs
  // without reading the environment until a connection is actually
  // requested.
  try {
    config_.tls.validate();
  } catch (const std::invalid_argument& e) {
    spdlog::error("NatsConnection::connect: invalid TLS config: {}", e.what());
    return false;
  }

  natsOptions* opts = nullptr;

  if (natsOptions_Create(&opts) != NATS_OK) {
    return false;
  }

  // RAII guard: always destroy opts on exit from this function, whether we
  // succeed or fail. natsConnection_Connect() internally reference-counts
  // the options, so destroying them here is safe.
  struct OptsGuard {
    natsOptions* ptr;
    ~OptsGuard() { natsOptions_Destroy(ptr); }
  } opts_guard{opts};

  // Server URL
  if (natsOptions_SetURL(opts, config_.url.c_str()) != NATS_OK) {
    return false;
  }

  // Reconnection policy
  if (natsOptions_SetMaxReconnect(opts, config_.max_reconnect_attempts) !=
      NATS_OK) {
    return false;
  }

  int64_t wait_ms = static_cast<int64_t>(config_.reconnect_wait.count());
  if (natsOptions_SetReconnectWait(opts, wait_ms) != NATS_OK) {
    return false;
  }

  // Keep-alive
  int64_t ping_ms = static_cast<int64_t>(config_.ping_interval.count());
  if (natsOptions_SetPingInterval(opts, ping_ms) != NATS_OK) {
    return false;
  }
  if (natsOptions_SetMaxPingsOut(opts, config_.max_pings_out) != NATS_OK) {
    return false;
  }

  // TLS
  if (!applyTlsOptions(opts)) {
    return false;
  }

  // Lifecycle callbacks — pass `this` as closure so static shims can dispatch
  if (natsOptions_SetErrorHandler(opts, NatsConnection::onError, this) !=
      NATS_OK) {
    return false;
  }
  if (natsOptions_SetDisconnectedCB(opts, NatsConnection::onDisconnected,
                                    this) != NATS_OK) {
    return false;
  }
  if (natsOptions_SetReconnectedCB(opts, NatsConnection::onReconnected, this) !=
      NATS_OK) {
    return false;
  }
  if (natsOptions_SetClosedCB(opts, NatsConnection::onClosed, this) !=
      NATS_OK) {
    return false;
  }

  natsConnection* conn = nullptr;
  if (natsConnection_Connect(&conn, opts) != NATS_OK) {
    return false;
  }

  conn_ = conn;
  state_.store(NatsConnectionState::CONNECTED, std::memory_order_release);
  return true;
}

void NatsConnection::disconnect() {
  if (js_ctx_ != nullptr) {
    jsCtx_Destroy(js_ctx_);
    js_ctx_ = nullptr;
  }
  if (conn_ != nullptr) {
    natsConnection_Close(conn_);
    natsConnection_Destroy(conn_);
    conn_ = nullptr;
  }
  state_.store(NatsConnectionState::DISCONNECTED, std::memory_order_release);
}

jsCtx* NatsConnection::jsContext() noexcept {
  if (js_ctx_ != nullptr) {
    return js_ctx_;
  }
  if (conn_ == nullptr) {
    spdlog::error("NatsConnection::jsContext: called before connect()");
    return nullptr;
  }
  const natsStatus status = natsConnection_JetStream(&js_ctx_, conn_, nullptr);
  if (status != NATS_OK) {
    spdlog::error(
        "NatsConnection::jsContext: natsConnection_JetStream failed: {}",
        natsStatus_GetText(status));
    js_ctx_ = nullptr;
    return nullptr;
  }
  return js_ctx_;
}

// ---------------------------------------------------------------------------
// State inspection
// ---------------------------------------------------------------------------

NatsConnectionState NatsConnection::getState() const noexcept {
  return state_.load(std::memory_order_acquire);
}

bool NatsConnection::isConnected() const noexcept {
  return getState() == NatsConnectionState::CONNECTED;
}

natsConnection* NatsConnection::handle() const noexcept { return conn_; }

// ---------------------------------------------------------------------------
// Static callback shims
// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void NatsConnection::onError(natsConnection* /*nc*/, natsSubscription* /*sub*/,
                             natsStatus err, void* closure) noexcept {
  auto* self = static_cast<NatsConnection*>(closure);
  ErrorCallback cb;
  {
    std::lock_guard<std::mutex> lock(self->callbacks_mutex_);
    cb = self->error_cb_;
  }
  if (cb) {
    const char* text = natsStatus_GetText(err);
    cb(text != nullptr ? text : "unknown nats error");
  }
}

void NatsConnection::onDisconnected(natsConnection* /*nc*/,
                                    void* closure) noexcept {
  auto* self = static_cast<NatsConnection*>(closure);
  self->state_.store(NatsConnectionState::RECONNECTING,
                     std::memory_order_release);
  DisconnectedCallback cb;
  {
    std::lock_guard<std::mutex> lock(self->callbacks_mutex_);
    cb = self->disconnected_cb_;
  }
  if (cb) {
    cb();
  }
}

void NatsConnection::onReconnected(natsConnection* /*nc*/,
                                   void* closure) noexcept {
  auto* self = static_cast<NatsConnection*>(closure);
  self->state_.store(NatsConnectionState::CONNECTED, std::memory_order_release);
  ReconnectedCallback cb;
  {
    std::lock_guard<std::mutex> lock(self->callbacks_mutex_);
    cb = self->reconnected_cb_;
  }
  if (cb) {
    cb();
  }
}

void NatsConnection::onClosed(natsConnection* /*nc*/, void* closure) noexcept {
  auto* self = static_cast<NatsConnection*>(closure);
  self->state_.store(NatsConnectionState::CLOSED, std::memory_order_release);
  ClosedCallback cb;
  {
    std::lock_guard<std::mutex> lock(self->callbacks_mutex_);
    cb = self->closed_cb_;
  }
  if (cb) {
    cb();
  }
}

// ---------------------------------------------------------------------------
// Exception mapping (ADR-014: exception contract)
// ---------------------------------------------------------------------------

void NatsConnection::throwForNatsStatus(natsStatus status,
                                        const std::string& context) {
  if (status == NATS_OK) {
    return;  // No error
  }

  const char* nats_text = natsStatus_GetText(status);
  std::string error_msg =
      context + ": " + (nats_text != nullptr ? nats_text : "unknown error") +
      " (nats_status=" + std::to_string(static_cast<int>(status)) + ")";

  NatsErrorCategory category = categorizeNatsError(status);

  switch (category) {
    case NatsErrorCategory::kConfigError:
      throw std::domain_error(error_msg);

    case NatsErrorCategory::kTransient:
      throw std::system_error(std::error_code(EAGAIN, std::generic_category()),
                              error_msg);

    case NatsErrorCategory::kPermanent:
      throw std::runtime_error(error_msg);
  }
}

// ---------------------------------------------------------------------------
// Pull-based fetch for durable consumers (rate-limiting pattern)
// ---------------------------------------------------------------------------

NatsMsgPtr NatsConnection::fetch(std::string_view subject,
                                 std::string_view consumer_name,
                                 int64_t timeout_ms) {
  jsCtx* js = jsContext();
  if (js == nullptr) {
    throw std::runtime_error(
        "NatsConnection::fetch: not connected to NATS (jsContext is null)");
  }

  if (subject.empty() || consumer_name.empty()) {
    throw std::domain_error(
        "NatsConnection::fetch: subject and consumer_name must not be empty");
  }

  // Subscribe to the subject with durable consumer semantics
  jsSubOptions sub_opts;
  jsSubOptions_Init(&sub_opts);
  sub_opts.Config.Durable = const_cast<char*>(consumer_name.data());
  sub_opts.Config.MaxAckPending = 1;  // Rate-limiting per CLAUDE.md

  natsSubscription* sub = nullptr;
  natsStatus s = js_Subscribe(&sub, js, std::string(subject).c_str(), nullptr,
                              nullptr, nullptr, &sub_opts, nullptr);

  if (s != NATS_OK) {
    throwForNatsStatus(s, "NatsConnection::fetch subscribe");
  }

  if (sub == nullptr) {
    throw std::runtime_error(
        "NatsConnection::fetch: subscription returned null");
  }

  // Fetch a single message with timeout using natsMsgList
  natsMsgList list{};
  jsErrCode js_err = static_cast<jsErrCode>(0);
  s = natsSubscription_Fetch(&list, sub, 1, timeout_ms, &js_err);

  // Clean up subscription (durable consumer state persists in NATS)
  natsSubscription_Unsubscribe(sub);
  natsSubscription_Destroy(sub);

  if (s != NATS_OK) {
    // NATS_TIMEOUT is not an error in fetch semantics — it's normal when
    // no message is available. Return a null NatsMsgPtr instead of throwing.
    if (s == NATS_TIMEOUT) {
      return NatsMsgPtr{nullptr, &natsMsg_Destroy};
    }
    throwForNatsStatus(s, "NatsConnection::fetch");
  }

  if (list.Count == 0 || list.Msgs == nullptr) {
    return NatsMsgPtr{nullptr, &natsMsg_Destroy};
  }

  // Transfer ownership to the caller via NatsMsgPtr.  The unique_ptr destructor
  // will call natsMsg_Destroy() automatically; the caller must NOT call it.
  // Remaining entries in list (none, since batch=1) would be destroyed here.
  NatsMsgPtr msg{list.Msgs[0], &natsMsg_Destroy};
  list.Msgs[0] = nullptr;
  list.Count = 0;
  natsMsgList_Destroy(&list);
  return msg;
}

}  // namespace transport
}  // namespace keystone
