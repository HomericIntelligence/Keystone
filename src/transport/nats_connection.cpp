/**
 * @file nats_connection.cpp
 * @brief NATS connection wrapper implementation with TLS support
 */

#include "transport/nats_connection.hpp"

#include <cstdlib>
#include <string>

#include <nats.h>
#include <spdlog/spdlog.h>

namespace keystone {
namespace transport {

namespace {

std::string getEnvVar(const char* name) {
  const char* value = std::getenv(name);  // NOLINT(concurrency-mt-unsafe)
  return value != nullptr ? std::string(value) : std::string{};
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

NatsConnection::NatsConnection(NatsConfig config) : config_(std::move(config)) {}

NatsConnection::~NatsConnection() {
  disconnect();
}

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

  // CA certificate: env var takes precedence over config field
  std::string ca_path = getEnvVar("KEYSTONE_NATS_TLS_CA_PATH");
  if (ca_path.empty()) {
    ca_path = tls.ca_cert_path;
  }
  if (!ca_path.empty()) {
    if (natsOptions_LoadCATrustedCertificates(opts, ca_path.c_str()) != NATS_OK) {
      spdlog::error("NatsConnection: failed to load CA certificate from {}", ca_path);
      return false;
    }
  }

  // Client certificate (mutual TLS): env vars take precedence over config
  // fields
  std::string cert_path = getEnvVar("KEYSTONE_NATS_TLS_CERT_PATH");
  std::string key_path = getEnvVar("KEYSTONE_NATS_TLS_KEY_PATH");
  if (cert_path.empty()) {
    cert_path = tls.client_cert_path;
  }
  if (key_path.empty()) {
    key_path = tls.client_key_path;
  }

  if (!cert_path.empty() && !key_path.empty()) {
    if (natsOptions_LoadCertificatesChain(opts, cert_path.c_str(), key_path.c_str()) != NATS_OK) {
      spdlog::error("NatsConnection: failed to load client certificate from {} / {}",
                    cert_path,
                    key_path);
      return false;
    }
  } else if (!cert_path.empty() || !key_path.empty()) {
    // Exactly one of cert/key was provided — this is always a misconfiguration
    spdlog::error(
        "NatsConnection: TLS client certificate requires both cert and key "
        "paths; "
        "cert_path='{}' key_path='{}'",
        cert_path,
        key_path);
    return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
// Connection lifecycle
// ---------------------------------------------------------------------------

bool NatsConnection::connect() {
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
  if (natsOptions_SetMaxReconnect(opts, config_.max_reconnect_attempts) != NATS_OK) {
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
  if (natsOptions_SetErrorHandler(opts, NatsConnection::onError, this) != NATS_OK) {
    return false;
  }
  if (natsOptions_SetDisconnectedCB(opts, NatsConnection::onDisconnected, this) != NATS_OK) {
    return false;
  }
  if (natsOptions_SetReconnectedCB(opts, NatsConnection::onReconnected, this) != NATS_OK) {
    return false;
  }
  if (natsOptions_SetClosedCB(opts, NatsConnection::onClosed, this) != NATS_OK) {
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
    js_Destroy(js_ctx_);
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
  const natsStatus status = js_Context(&js_ctx_, conn_, nullptr, nullptr);
  if (status != NATS_OK) {
    spdlog::error("NatsConnection::jsContext: js_Context failed: {}", natsStatus_GetText(status));
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

natsConnection* NatsConnection::handle() const noexcept {
  return conn_;
}

// ---------------------------------------------------------------------------
// Static callback shims
// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void NatsConnection::onError(natsConnection* /*nc*/,
                             natsSubscription* /*sub*/,
                             natsStatus err,
                             void* closure) noexcept {
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

void NatsConnection::onDisconnected(natsConnection* /*nc*/, void* closure) noexcept {
  auto* self = static_cast<NatsConnection*>(closure);
  self->state_.store(NatsConnectionState::RECONNECTING, std::memory_order_release);
  DisconnectedCallback cb;
  {
    std::lock_guard<std::mutex> lock(self->callbacks_mutex_);
    cb = self->disconnected_cb_;
  }
  if (cb) {
    cb();
  }
}

void NatsConnection::onReconnected(natsConnection* /*nc*/, void* closure) noexcept {
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

}  // namespace transport
}  // namespace keystone
