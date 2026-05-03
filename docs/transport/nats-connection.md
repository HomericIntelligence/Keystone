# NatsConnection - NATS Client Wrapper

## Overview

`NatsConnection` is a C++20 wrapper around `nats.c` v3.12.0 that manages NATS server connections with built-in support
for TLS, reconnection policies, and lifecycle callbacks.

## Construction

```cpp
NatsConfig cfg;
cfg.url = "nats://localhost:4222";
cfg.max_reconnect_attempts = 60;
cfg.reconnect_wait = std::chrono::milliseconds{2000};
cfg.tls.enable_tls = true;

NatsConnection conn(cfg);
```

## TLS Configuration

Enable TLS with optional custom CA and mutual certificates:

```cpp
NatsConfig cfg;
cfg.url = "tls://nats.example.com:4222";
cfg.tls.enable_tls = true;
cfg.tls.ca_cert_path = "/path/to/ca.pem";
cfg.tls.client_cert_path = "/path/to/client.pem";
cfg.tls.client_key_path = "/path/to/key.pem";
cfg.tls.skip_server_verification = false;  // Never in production

NatsConnection conn(cfg);
```

Environment variables override config paths:

- `KEYSTONE_NATS_TLS_CA_PATH`
- `KEYSTONE_NATS_TLS_CERT_PATH`
- `KEYSTONE_NATS_TLS_KEY_PATH`

## Lifecycle Callbacks

Register callbacks **before** calling `connect()`. Callbacks fire on internal nats.c threads:

```cpp
conn.setErrorCallback([](const std::string& error) {
  spdlog::warn("NATS error: {}", error);
});

conn.setDisconnectedCallback([]() {
  spdlog::info("NATS disconnected");
});

conn.setReconnectedCallback([]() {
  spdlog::info("NATS reconnected");
});

conn.setClosedCallback([]() {
  spdlog::info("NATS connection closed");
});

conn.connect();
```

**Critical**: Callbacks must not block or re-enter `NatsConnection` methods.

## State Observation

Query connection state from any thread (lock-free):

```cpp
if (conn.isConnected()) {
  natsConnection* nc = conn.handle();
  // Use nc for publish/subscribe
}

NatsConnectionState state = conn.getState();
// DISCONNECTED, CONNECTED, RECONNECTING, or CLOSED
```

## Connection Lifecycle

```cpp
if (conn.connect()) {
  // Connected successfully
  natsConnection* nc = conn.handle();
  // Publish/subscribe using nc
  conn.disconnect();  // Safe to call at any time
}
```

Destructor automatically calls `disconnect()` — safe to let the object go out of scope.

## Properties

| Property | Default | Notes |
|----------|---------|-------|
| `url` | `nats://localhost:4222` | NATS server URL |
| `max_reconnect_attempts` | 60 | -1 = unlimited |
| `reconnect_wait` | 2000ms | Wait between reconnect attempts |
| `ping_interval` | 20000ms | Keep-alive ping interval |
| `max_pings_out` | 2 | Unacknowledged pings before dead |
