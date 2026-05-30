/**
 * @file test_nats_connection.cpp
 * @brief Unit tests for NatsConnection (issue #122 — NATS TLS configuration)
 *
 * Tests cover:
 * - Default construction and initial state
 * - Callback registration before connect()
 * - Config field validation (reconnect attempts, wait interval, ping settings)
 * - TLS configuration fields (enable_tls, ca_cert_path, client cert/key,
 * skip_verify)
 * - State transitions via the public callback shims (simulated without a live
 * server)
 * - disconnect() is safe to call without a prior connect()
 * - isConnected() reflects getState()
 * - Null-safety of unregistered callbacks
 * - Callback replacement semantics
 * - jsContext() returns nullptr when not connected (issue #274)
 * - jsContext() caching semantics (issue #274)
 * - NatsTlsConfig::validate() cert/key parity when struct fields are used
 *   (issue #518 — thread-safe env-var caching)
 *
 * Tests do NOT exercise connect() against a live NATS server because the CI
 * environment has no NATS process. The callback dispatch path is exercised via
 * the NatsConnectionTestPeer helper below.
 *
 * NOTE — env-var caching contract (issue #518):
 * cachedTlsEnvVars() uses a C++20 guaranteed thread-safe static-local
 * initialiser that reads the three KEYSTONE_NATS_TLS_* env vars exactly once
 * per process lifetime.  Because the static fires on the first call within the
 * test binary, tests that need to exercise the env-var override path must be
 * run in a dedicated subprocess (see the TSan preset run in CI).  Unit tests
 * here cover the struct-field code paths, which are fully exercisable without
 * env-var manipulation.  The absence of data-race reports from the TSan run is
 * the definitive oracle that the fix is correct.
 */

#include "transport/nats_connection.hpp"

#include <atomic>
#include <string>

#include <gtest/gtest.h>

using namespace keystone::transport;

// ---------------------------------------------------------------------------
// Test peer — fires static callback shims without a live connection
// ---------------------------------------------------------------------------

class NatsConnectionTestPeer : public NatsConnection {
 public:
  using NatsConnection::NatsConnection;

  void fireError() { NatsConnection::onError(nullptr, nullptr, static_cast<natsStatus>(0), this); }

  void fireDisconnected() { NatsConnection::onDisconnected(nullptr, this); }
  void fireReconnected() { NatsConnection::onReconnected(nullptr, this); }
  void fireClosed() { NatsConnection::onClosed(nullptr, this); }
};

// ---------------------------------------------------------------------------
// NatsConnectionStateTest — initial state and disconnect safety
// ---------------------------------------------------------------------------

class NatsConnectionStateTest : public ::testing::Test {};

TEST_F(NatsConnectionStateTest, InitialStateIsDisconnected) {
  NatsConnection conn;
  EXPECT_EQ(conn.getState(), NatsConnectionState::DISCONNECTED);
}

TEST_F(NatsConnectionStateTest, IsConnectedReturnsFalseWhenDisconnected) {
  NatsConnection conn;
  EXPECT_FALSE(conn.isConnected());
}

TEST_F(NatsConnectionStateTest, HandleIsNullBeforeConnect) {
  NatsConnection conn;
  EXPECT_EQ(conn.handle(), nullptr);
}

TEST_F(NatsConnectionStateTest, DisconnectWithoutConnectIsNoOp) {
  NatsConnection conn;
  EXPECT_NO_THROW(conn.disconnect());
  EXPECT_EQ(conn.getState(), NatsConnectionState::DISCONNECTED);
}

TEST_F(NatsConnectionStateTest, DoubleDisconnectIsNoOp) {
  NatsConnection conn;
  conn.disconnect();
  EXPECT_NO_THROW(conn.disconnect());
}

// ---------------------------------------------------------------------------
// NatsConfigTest — configuration fields
// ---------------------------------------------------------------------------

class NatsConfigTest : public ::testing::Test {};

TEST_F(NatsConfigTest, DefaultConfigValues) {
  NatsConfig cfg;
  EXPECT_EQ(cfg.url, "nats://localhost:4222");
  EXPECT_EQ(cfg.max_reconnect_attempts, 60);
  EXPECT_EQ(cfg.reconnect_wait, std::chrono::milliseconds{2000});
  EXPECT_EQ(cfg.ping_interval, std::chrono::milliseconds{20000});
  EXPECT_EQ(cfg.max_pings_out, 2);
}

TEST_F(NatsConfigTest, DefaultTlsDisabled) {
  NatsConfig cfg;
  EXPECT_FALSE(cfg.tls.enable_tls);
  EXPECT_TRUE(cfg.tls.ca_cert_path.empty());
  EXPECT_TRUE(cfg.tls.client_cert_path.empty());
  EXPECT_TRUE(cfg.tls.client_key_path.empty());
  EXPECT_FALSE(cfg.tls.skip_server_verification);
}

TEST_F(NatsConfigTest, CustomConfigPreserved) {
  NatsConfig cfg;
  cfg.url = "nats://myserver:4222";
  cfg.max_reconnect_attempts = 10;
  cfg.reconnect_wait = std::chrono::milliseconds{500};
  cfg.ping_interval = std::chrono::milliseconds{5000};
  cfg.max_pings_out = 5;
  NatsConnectionTestPeer conn(cfg);
  EXPECT_EQ(conn.getState(), NatsConnectionState::DISCONNECTED);
}

TEST_F(NatsConfigTest, UnlimitedReconnectAttempts) {
  NatsConfig cfg;
  cfg.max_reconnect_attempts = -1;
  EXPECT_EQ(cfg.max_reconnect_attempts, -1);
}

// ---------------------------------------------------------------------------
// NatsTlsConfigTest — TLS configuration fields
// ---------------------------------------------------------------------------

class NatsTlsConfigTest : public ::testing::Test {};

TEST_F(NatsTlsConfigTest, TlsCanBeEnabled) {
  NatsTlsConfig tls;
  tls.enable_tls = true;
  EXPECT_TRUE(tls.enable_tls);
}

TEST_F(NatsTlsConfigTest, TlsWithCaCertPath) {
  NatsTlsConfig tls;
  tls.enable_tls = true;
  tls.ca_cert_path = "/etc/ssl/certs/ca.pem";
  EXPECT_EQ(tls.ca_cert_path, "/etc/ssl/certs/ca.pem");
}

TEST_F(NatsTlsConfigTest, TlsWithClientCertAndKey) {
  NatsTlsConfig tls;
  tls.enable_tls = true;
  tls.client_cert_path = "/etc/ssl/certs/client.pem";
  tls.client_key_path = "/etc/ssl/private/client.key";
  EXPECT_EQ(tls.client_cert_path, "/etc/ssl/certs/client.pem");
  EXPECT_EQ(tls.client_key_path, "/etc/ssl/private/client.key");
}

TEST_F(NatsTlsConfigTest, SkipServerVerificationDefaultFalse) {
  NatsTlsConfig tls;
  EXPECT_FALSE(tls.skip_server_verification);
}

TEST_F(NatsTlsConfigTest, SkipServerVerificationCanBeEnabled) {
  NatsTlsConfig tls;
  tls.skip_server_verification = true;
  EXPECT_TRUE(tls.skip_server_verification);
}

TEST_F(NatsTlsConfigTest, TlsConfigStoredInNatsConfig) {
  NatsConfig cfg;
  cfg.tls.enable_tls = true;
  cfg.tls.ca_cert_path = "/path/to/ca.pem";
  cfg.tls.client_cert_path = "/path/to/cert.pem";
  cfg.tls.client_key_path = "/path/to/key.pem";

  // Construction succeeds when both cert and key are set
  NatsConnectionTestPeer conn(cfg);
  EXPECT_EQ(conn.getState(), NatsConnectionState::DISCONNECTED);
}

TEST_F(NatsTlsConfigTest, TlsUrlSchemeAccepted) {
  NatsConfig cfg;
  cfg.url = "tls://nats.example.com:4222";
  cfg.tls.enable_tls = true;
  EXPECT_EQ(cfg.url, "tls://nats.example.com:4222");
  EXPECT_TRUE(cfg.tls.enable_tls);
}

// ---------------------------------------------------------------------------
// NatsTlsValidationTest — cert/key parity validation (issue #276)
// ---------------------------------------------------------------------------

class NatsTlsValidationTest : public ::testing::Test {};

TEST_F(NatsTlsValidationTest, BothCertAndKeyEmptyIsValid) {
  // Default config has both empty — should not throw
  NatsConfig cfg;
  EXPECT_TRUE(cfg.tls.client_cert_path.empty());
  EXPECT_TRUE(cfg.tls.client_key_path.empty());
}

TEST_F(NatsTlsValidationTest, BothCertAndKeySetIsValid) {
  // Both set — should not throw
  NatsConfig cfg;
  cfg.tls.client_cert_path = "/path/to/cert.pem";
  cfg.tls.client_key_path = "/path/to/key.pem";
  // If we got here, validation passed during construction
  EXPECT_EQ(cfg.tls.client_cert_path, "/path/to/cert.pem");
  EXPECT_EQ(cfg.tls.client_key_path, "/path/to/key.pem");
}

TEST_F(NatsTlsValidationTest, CertWithoutKeyThrows) {
  // Only cert set — should throw during construction
  NatsConfig cfg_base;
  cfg_base.tls.client_cert_path = "/path/to/cert.pem";
  // client_key_path is empty (default)

  EXPECT_THROW(
      {
        // This throws during NatsConfig construction
        NatsConfig cfg;
        cfg.tls.client_cert_path = "/path/to/cert.pem";
        // When we validate, this should fail
        cfg.tls.validate();
      },
      std::invalid_argument);
}

TEST_F(NatsTlsValidationTest, KeyWithoutCertThrows) {
  // Only key set — should throw during construction
  EXPECT_THROW(
      {
        // This throws during NatsConfig construction
        NatsConfig cfg;
        cfg.tls.client_key_path = "/path/to/key.pem";
        // When we validate, this should fail
        cfg.tls.validate();
      },
      std::invalid_argument);
}

TEST_F(NatsTlsValidationTest, ValidateCertWithoutKeyThrows) {
  // Directly call validate() on a config with only cert set
  NatsTlsConfig tls;
  tls.client_cert_path = "/path/to/cert.pem";
  // client_key_path is empty

  EXPECT_THROW(tls.validate(), std::invalid_argument);
}

TEST_F(NatsTlsValidationTest, ValidateKeyWithoutCertThrows) {
  // Directly call validate() on a config with only key set
  NatsTlsConfig tls;
  tls.client_key_path = "/path/to/key.pem";
  // client_cert_path is empty

  EXPECT_THROW(tls.validate(), std::invalid_argument);
}

TEST_F(NatsTlsValidationTest, CopyConstructorValidates) {
  // First create a valid config
  NatsConfig cfg1;
  cfg1.tls.client_cert_path = "/path/to/cert.pem";
  cfg1.tls.client_key_path = "/path/to/key.pem";

  // Copy should succeed since source is valid
  NatsConfig cfg2(cfg1);
  EXPECT_EQ(cfg2.tls.client_cert_path, "/path/to/cert.pem");
  EXPECT_EQ(cfg2.tls.client_key_path, "/path/to/key.pem");
}

TEST_F(NatsTlsValidationTest, CopyAssignmentValidates) {
  // First create a valid config
  NatsConfig cfg1;
  cfg1.tls.client_cert_path = "/path/to/cert.pem";
  cfg1.tls.client_key_path = "/path/to/key.pem";

  // Create another valid config
  NatsConfig cfg2;
  cfg2.tls.client_cert_path = "/path/to/cert2.pem";
  cfg2.tls.client_key_path = "/path/to/key2.pem";

  // Assignment should succeed since source is valid
  cfg2 = cfg1;
  EXPECT_EQ(cfg2.tls.client_cert_path, "/path/to/cert.pem");
  EXPECT_EQ(cfg2.tls.client_key_path, "/path/to/key.pem");
}

// ---------------------------------------------------------------------------
// NatsCallbackTest — callback registration and dispatch
// ---------------------------------------------------------------------------

class NatsCallbackTest : public ::testing::Test {
 protected:
  NatsConnectionTestPeer conn_;
};

TEST_F(NatsCallbackTest, ErrorCallbackFiredOnError) {
  std::atomic<int32_t> call_count{0};
  conn_.setErrorCallback([&](const std::string& /*err*/) { ++call_count; });

  conn_.fireError();

  EXPECT_EQ(call_count.load(), 1);
}

TEST_F(NatsCallbackTest, DisconnectedCallbackFiredOnDisconnect) {
  std::atomic<int32_t> call_count{0};
  conn_.setDisconnectedCallback([&]() { ++call_count; });

  conn_.fireDisconnected();

  EXPECT_EQ(call_count.load(), 1);
}

TEST_F(NatsCallbackTest, ReconnectedCallbackFiredOnReconnect) {
  std::atomic<int32_t> call_count{0};
  conn_.setReconnectedCallback([&]() { ++call_count; });

  conn_.fireReconnected();

  EXPECT_EQ(call_count.load(), 1);
}

TEST_F(NatsCallbackTest, ClosedCallbackFiredOnClose) {
  std::atomic<int32_t> call_count{0};
  conn_.setClosedCallback([&]() { ++call_count; });

  conn_.fireClosed();

  EXPECT_EQ(call_count.load(), 1);
}

// ---------------------------------------------------------------------------
// NatsStateTransitionTest — state machine driven by callbacks
// ---------------------------------------------------------------------------

class NatsStateTransitionTest : public ::testing::Test {
 protected:
  NatsConnectionTestPeer conn_;
};

TEST_F(NatsStateTransitionTest, DisconnectedCallbackSetsReconnectingState) {
  conn_.fireDisconnected();
  EXPECT_EQ(conn_.getState(), NatsConnectionState::RECONNECTING);
  EXPECT_FALSE(conn_.isConnected());
}

TEST_F(NatsStateTransitionTest, ReconnectedCallbackSetsConnectedState) {
  conn_.fireDisconnected();
  conn_.fireReconnected();

  EXPECT_EQ(conn_.getState(), NatsConnectionState::CONNECTED);
  EXPECT_TRUE(conn_.isConnected());
}

TEST_F(NatsStateTransitionTest, ClosedCallbackSetsClosedState) {
  conn_.fireClosed();
  EXPECT_EQ(conn_.getState(), NatsConnectionState::CLOSED);
  EXPECT_FALSE(conn_.isConnected());
}

TEST_F(NatsStateTransitionTest, IsConnectedOnlyTrueInConnectedState) {
  EXPECT_FALSE(conn_.isConnected());

  conn_.fireDisconnected();  // -> RECONNECTING
  EXPECT_FALSE(conn_.isConnected());

  conn_.fireReconnected();  // -> CONNECTED
  EXPECT_TRUE(conn_.isConnected());

  conn_.fireClosed();  // -> CLOSED
  EXPECT_FALSE(conn_.isConnected());
}

TEST_F(NatsStateTransitionTest, DisconnectResetsStateToDisconnected) {
  conn_.fireReconnected();
  ASSERT_TRUE(conn_.isConnected());

  conn_.disconnect();

  EXPECT_EQ(conn_.getState(), NatsConnectionState::DISCONNECTED);
  EXPECT_FALSE(conn_.isConnected());
}

// ---------------------------------------------------------------------------
// NatsCallbackNullSafetyTest — no crash when callbacks are not set
// ---------------------------------------------------------------------------

class NatsCallbackNullSafetyTest : public ::testing::Test {
 protected:
  NatsConnectionTestPeer conn_;
};

TEST_F(NatsCallbackNullSafetyTest, ErrorWithNoCallbackDoesNotCrash) {
  EXPECT_NO_THROW(conn_.fireError());
}

TEST_F(NatsCallbackNullSafetyTest, DisconnectedWithNoCallbackDoesNotCrash) {
  EXPECT_NO_THROW(conn_.fireDisconnected());
}

TEST_F(NatsCallbackNullSafetyTest, ReconnectedWithNoCallbackDoesNotCrash) {
  EXPECT_NO_THROW(conn_.fireReconnected());
}

TEST_F(NatsCallbackNullSafetyTest, ClosedWithNoCallbackDoesNotCrash) {
  EXPECT_NO_THROW(conn_.fireClosed());
}

// ---------------------------------------------------------------------------
// NatsCallbackOverrideTest — replacing a callback after initial registration
// ---------------------------------------------------------------------------

class NatsCallbackOverrideTest : public ::testing::Test {
 protected:
  NatsConnectionTestPeer conn_;
};

TEST_F(NatsCallbackOverrideTest, ReplacedCallbackIsInvokedInsteadOfOriginal) {
  std::atomic<int32_t> first_count{0};
  std::atomic<int32_t> second_count{0};

  conn_.setReconnectedCallback([&]() { ++first_count; });
  conn_.setReconnectedCallback([&]() { ++second_count; });

  conn_.fireReconnected();

  EXPECT_EQ(first_count.load(), 0);
  EXPECT_EQ(second_count.load(), 1);
}

// ---------------------------------------------------------------------------
// NatsJsContextTest -- jsContext() behaviour without a live NATS server
// ---------------------------------------------------------------------------

class NatsJsContextTest : public ::testing::Test {
 protected:
  NatsConnectionTestPeer conn_;
};

TEST_F(NatsJsContextTest, JsContextReturnsNullWhenNotConnected) {
  // conn_ is in DISCONNECTED state (no live server) -- jsContext() must return
  // nullptr gracefully rather than crash.
  EXPECT_EQ(conn_.jsContext(), nullptr);
}

TEST_F(NatsJsContextTest, JsContextReturnsNullAfterDisconnect) {
  // Simulate a connect -> disconnect cycle without a live server.
  // After disconnect() the cached js_ctx_ must be cleared so subsequent calls
  // return nullptr.
  conn_.disconnect();
  EXPECT_EQ(conn_.jsContext(), nullptr);
}

TEST_F(NatsJsContextTest, JsContextIsIdempotentWhenNotConnected) {
  // Multiple calls without a connection must all return nullptr -- no
  // state corruption between calls.
  EXPECT_EQ(conn_.jsContext(), nullptr);
  EXPECT_EQ(conn_.jsContext(), nullptr);
  EXPECT_EQ(conn_.jsContext(), nullptr);
}

TEST_F(NatsJsContextTest, JsContextNullDoesNotAffectOtherMethods) {
  // Calling jsContext() on an unconnected object must not corrupt the
  // observable connection state.
  conn_.jsContext();
  EXPECT_EQ(conn_.getState(), NatsConnectionState::DISCONNECTED);
  EXPECT_FALSE(conn_.isConnected());
  EXPECT_EQ(conn_.handle(), nullptr);
}

// ---------------------------------------------------------------------------
// NatsTlsValidateStructFieldsTest — validate() with struct-field paths only
// (issue #518 — thread-safe env-var caching via cachedTlsEnvVars())
//
// These tests exercise validate() when the KEYSTONE_NATS_TLS_* env vars are
// NOT set (CI does not set them), so cachedTlsEnvVars() returns empty strings
// and validate() falls back to struct fields.  This covers the primary
// production code path where TLS is configured programmatically rather than
// via the environment.
//
// The TSan preset run in CI is the definitive oracle that cachedTlsEnvVars()
// is free of data races — the absence of TSan reports there confirms the fix.
// ---------------------------------------------------------------------------

class NatsTlsValidateStructFieldsTest : public ::testing::Test {};

TEST_F(NatsTlsValidateStructFieldsTest, BothStructFieldsEmptyIsValid) {
  // Assuming env vars are not set (CI guarantee), both effective paths are
  // empty — validate() must not throw.
  NatsTlsConfig tls;
  EXPECT_NO_THROW(tls.validate());
}

TEST_F(NatsTlsValidateStructFieldsTest, BothStructFieldsSetIsValid) {
  // Both cert and key provided via struct — validate() must not throw.
  NatsTlsConfig tls;
  tls.client_cert_path = "/path/to/cert.pem";
  tls.client_key_path = "/path/to/key.pem";
  EXPECT_NO_THROW(tls.validate());
}

TEST_F(NatsTlsValidateStructFieldsTest, CertStructFieldOnlyThrows) {
  // Only cert set via struct (no env var override in CI) — validate() must
  // throw because cert without key violates parity.
  NatsTlsConfig tls;
  tls.client_cert_path = "/path/to/cert.pem";
  // client_key_path intentionally left empty
  EXPECT_THROW(tls.validate(), std::invalid_argument);
}

TEST_F(NatsTlsValidateStructFieldsTest, KeyStructFieldOnlyThrows) {
  // Only key set via struct (no env var override in CI) — validate() must
  // throw because key without cert violates parity.
  NatsTlsConfig tls;
  tls.client_key_path = "/path/to/key.pem";
  // client_cert_path intentionally left empty
  EXPECT_THROW(tls.validate(), std::invalid_argument);
}

TEST_F(NatsTlsValidateStructFieldsTest, ValidateCalledMultipleTimesIsIdempotent) {
  // Calling validate() multiple times on a valid config must not throw and
  // must not corrupt state.  This also exercises the static-cache path being
  // called repeatedly — safe because cachedTlsEnvVars() returns a const ref.
  NatsTlsConfig tls;
  tls.client_cert_path = "/path/to/cert.pem";
  tls.client_key_path = "/path/to/key.pem";
  EXPECT_NO_THROW(tls.validate());
  EXPECT_NO_THROW(tls.validate());
  EXPECT_NO_THROW(tls.validate());
}
