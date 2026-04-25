# NATS TLS Configuration

## Overview

NATS connections in ProjectKeystone support optional TLS encryption. While the
primary security layer is **Tailscale WireGuard mesh** (`tail8906b5.ts.net`),
per-connection TLS provides additional encryption and certificate verification
when needed.

## Configuration

TLS is configured via `NatsTlsConfig` struct in `NatsConfig`:

```cpp
struct NatsTlsConfig {
  bool enable_tls{false};                    // Enable TLS (default: disabled)
  std::string ca_cert_path;                  // PEM CA cert path (optional)
  std::string client_cert_path;              // PEM client cert path (mTLS)
  std::string client_key_path;               // PEM client key path (mTLS)
  bool skip_server_verification{false};      // Disable verification (TEST ONLY)
};
```

## Configuration Methods

### 1. Direct Config Struct

```cpp
NatsConfig cfg;
cfg.url = "tls://nats.example.com:4222";
cfg.tls.enable_tls = true;
cfg.tls.ca_cert_path = "/path/to/ca.pem";
cfg.tls.client_cert_path = "/path/to/client.pem";
cfg.tls.client_key_path = "/path/to/client-key.pem";

NatsConnection conn(cfg);
conn.connect();
```

### 2. Environment Variables

If TLS paths are not provided in the config struct, these environment variables
are checked:

- `KEYSTONE_NATS_TLS_CA_PATH` — Path to CA certificate
- `KEYSTONE_NATS_TLS_CERT_PATH` — Path to client certificate (mTLS)
- `KEYSTONE_NATS_TLS_KEY_PATH` — Path to client private key (mTLS)

Environment variables are overridden by explicit struct values.

## TLS Behavior

- **No CA cert provided**: System default CA certificates are used
- **Mutual TLS (mTLS)**: If both `client_cert_path` and `client_key_path` are
  set, the client presents a certificate for authentication
- **Test mode**: `skip_server_verification=true` disables hostname and
  certificate chain validation; use only in controlled test environments

## Security Architecture

- **Primary layer**: Tailscale WireGuard mesh (encrypted IP tunnel)
- **Secondary layer**: Per-connection TLS (optional, for defense in depth)

Most deployments rely on Tailscale alone. TLS is recommended for external NATS
clusters or high-sensitivity environments.
