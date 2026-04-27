/**
 * @file test_tls_integration.cpp
 * @brief Opt-in TLS handshake integration test for NatsConnection
 *
 * Exercises the TLS handshake through NatsConnection with a self-signed
 * certificate. The test spins up a nats-server with TLS, verifies that
 * NatsConnection connects successfully with ca_cert_path set, then
 * disconnects.
 *
 * Prerequisites (build-time opt-in):
 *   cmake -DENABLE_TLS_INTEGRATION_TESTS=ON ...
 *
 * Prerequisites (runtime):
 *   - nats-server binary on PATH
 *   - openssl binary on PATH
 *
 * When either binary is missing or cert generation fails the test is
 * SKIPPED (not failed), so it is safe to run in any CI environment that
 * may lack nats-server.
 */

#include "transport/nats_connection.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include <gtest/gtest.h>

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Run a shell command and return its exit code.
int runCommand(const std::string& cmd) {
  // NOLINTNEXTLINE(cert-env33-c)
  return std::system(cmd.c_str());
}

/// Return true if a binary exists somewhere on PATH.
bool binaryOnPath(const std::string& name) {
  // `which` returns 0 when found, non-zero otherwise.
  std::string check = "which " + name + " > /dev/null 2>&1";
  return runCommand(check) == 0;
}

/// Return the path to the binary on PATH, or empty string if not found.
std::string findBinary(const std::string& name) {
  if (!binaryOnPath(name)) {
    return {};
  }
  // Use `which` to capture the path.
  std::string cmd = "which " + name + " 2>/dev/null";
  // NOLINTNEXTLINE(cert-env33-c)
  FILE* pipe = popen(cmd.c_str(), "r");
  if (pipe == nullptr) {
    return {};
  }
  std::array<char, 512> buf{};
  std::string result;
  while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
    result += buf.data();
  }
  pclose(pipe);
  // Strip trailing newline
  while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
    result.pop_back();
  }
  return result;
}

/// Return a free-ish TCP port in the ephemeral range for the TLS nats-server.
/// We use a fixed high port unlikely to collide with the default NATS port.
constexpr uint16_t kTlsTestPort = 14443;

// ---------------------------------------------------------------------------
// TlsIntegrationTest fixture
// ---------------------------------------------------------------------------

/**
 * @brief Test fixture that sets up a self-signed CA + server cert and starts
 * a nats-server with TLS.
 *
 * SetUpTestSuite:
 *   1. Locate nats-server and openssl on PATH; SKIP if either is absent.
 *   2. Create a temporary directory for certs.
 *   3. Generate self-signed CA + server cert via openssl.
 *   4. Write a nats-server TLS config file.
 *   5. Start nats-server in the background.
 *   6. Wait up to 3 s for the server to accept connections.
 *
 * TearDownTestSuite:
 *   Kill the nats-server process and remove the temporary directory.
 */
class TlsIntegrationTest : public ::testing::Test {
 public:
  // -----------------------------------------------------------------------
  // Suite-level setup — shared across all tests in this fixture
  // -----------------------------------------------------------------------

  static void SetUpTestSuite() {
    // Locate required binaries.
    nats_server_path_ = findBinary("nats-server");
    if (nats_server_path_.empty()) {
      skip_reason_ = "nats-server binary not found on PATH";
      return;
    }

    openssl_path_ = findBinary("openssl");
    if (openssl_path_.empty()) {
      skip_reason_ = "openssl binary not found on PATH";
      return;
    }

    // Create a temporary directory under /tmp.
    tmp_dir_ = "/tmp/keystone_tls_test_" + std::to_string(getpid());
    std::filesystem::create_directories(tmp_dir_);

    // Generate certs; if anything fails, skip.
    if (!generateCerts()) {
      skip_reason_ = "Failed to generate self-signed certificate";
      cleanupTmpDir();
      return;
    }

    // Write nats-server TLS config.
    if (!writeNatsConfig()) {
      skip_reason_ = "Failed to write nats-server TLS configuration file";
      cleanupTmpDir();
      return;
    }

    // Start nats-server.
    if (!startNatsServer()) {
      skip_reason_ = "Failed to start nats-server with TLS";
      cleanupTmpDir();
      return;
    }
  }

  static void TearDownTestSuite() {
    stopNatsServer();
    cleanupTmpDir();
  }

 protected:
  // -----------------------------------------------------------------------
  // Per-test setup — skip if suite setup failed
  // -----------------------------------------------------------------------

  void SetUp() override {
    if (!skip_reason_.empty()) {
      GTEST_SKIP() << skip_reason_;
    }
  }

  // -----------------------------------------------------------------------
  // Accessors for test code
  // -----------------------------------------------------------------------

  static std::string caPath() { return tmp_dir_ + "/ca.pem"; }
  static std::string serverUrl() { return "tls://127.0.0.1:" + std::to_string(kTlsTestPort); }

 private:
  // -----------------------------------------------------------------------
  // Cert generation
  // -----------------------------------------------------------------------

  /**
   * @brief Generate a self-signed CA and a server certificate signed by it.
   *
   * Steps:
   *   1. Generate CA private key + self-signed CA cert.
   *   2. Generate server private key + CSR.
   *   3. Sign the server CSR with the CA.
   *
   * All files are written to tmp_dir_.
   * Returns true on success, false if any openssl invocation fails.
   */
  static bool generateCerts() {
    const std::string ca_key = tmp_dir_ + "/ca.key";
    const std::string ca_cert = tmp_dir_ + "/ca.pem";
    const std::string server_key = tmp_dir_ + "/server.key";
    const std::string server_csr = tmp_dir_ + "/server.csr";
    const std::string server_cert = tmp_dir_ + "/server.pem";
    const std::string extfile = tmp_dir_ + "/server.ext";

    // Write a minimal SAN extension file so openssl does not warn about missing
    // Subject Alternative Names (required by modern TLS stacks).
    {
      std::ofstream ofs(extfile);
      if (!ofs) {
        return false;
      }
      ofs << "subjectAltName=IP:127.0.0.1\n";
    }

    // 1. CA key + self-signed cert (2048-bit RSA, valid 10 years)
    std::string cmd = openssl_path_ +
                      " req -newkey rsa:2048 -nodes"
                      " -keyout " +
                      ca_key +
                      " -x509 -days 3650"
                      " -subj '/CN=KeystoneTestCA'"
                      " -out " +
                      ca_cert + " > /dev/null 2>&1";
    if (runCommand(cmd) != 0) {
      return false;
    }

    // 2. Server key + CSR
    cmd = openssl_path_ +
          " req -newkey rsa:2048 -nodes"
          " -keyout " +
          server_key +
          " -subj '/CN=127.0.0.1'"
          " -out " +
          server_csr + " > /dev/null 2>&1";
    if (runCommand(cmd) != 0) {
      return false;
    }

    // 3. Sign server cert with CA
    cmd = openssl_path_ +
          " x509 -req -days 3650"
          " -in " +
          server_csr + " -CA " + ca_cert + " -CAkey " + ca_key +
          " -CAcreateserial"
          " -extfile " +
          extfile + " -out " + server_cert + " > /dev/null 2>&1";
    if (runCommand(cmd) != 0) {
      return false;
    }

    server_cert_path_ = server_cert;
    server_key_path_ = server_key;
    return true;
  }

  // -----------------------------------------------------------------------
  // nats-server config
  // -----------------------------------------------------------------------

  /**
   * @brief Write a minimal nats-server configuration file that enables TLS.
   */
  static bool writeNatsConfig() {
    const std::string cfg_path = tmp_dir_ + "/nats-tls.conf";
    std::ofstream ofs(cfg_path);
    if (!ofs) {
      return false;
    }

    ofs << "port: " << kTlsTestPort << "\n"
        << "tls {\n"
        << "  cert_file: \"" << server_cert_path_ << "\"\n"
        << "  key_file:  \"" << server_key_path_ << "\"\n"
        << "  ca_file:   \"" << (tmp_dir_ + "/ca.pem") << "\"\n"
        << "  timeout:   5\n"
        << "}\n";

    nats_config_path_ = cfg_path;
    return true;
  }

  // -----------------------------------------------------------------------
  // Process management
  // -----------------------------------------------------------------------

  /**
   * @brief Start nats-server in the background and wait until it is
   * accepting connections (up to 3 s).
   *
   * Returns true when the server is ready, false on timeout.
   */
  static bool startNatsServer() {
    const std::string log_path = tmp_dir_ + "/nats-server.log";
    std::string cmd = nats_server_path_ + " -c " + nats_config_path_ + " > " + log_path +
                      " 2>&1 &"
                      " echo $!";
    // NOLINTNEXTLINE(cert-env33-c)
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
      return false;
    }
    std::array<char, 64> buf{};
    std::string pid_str;
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
      pid_str += buf.data();
    }
    pclose(pipe);

    // Strip whitespace
    while (!pid_str.empty() &&
           (pid_str.back() == '\n' || pid_str.back() == '\r' || pid_str.back() == ' ')) {
      pid_str.pop_back();
    }
    if (pid_str.empty()) {
      return false;
    }
    try {
      nats_server_pid_ = std::stoi(pid_str);
    } catch (...) {
      return false;
    }

    // Poll until nats-server accepts TCP connections on kTlsTestPort.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (std::chrono::steady_clock::now() < deadline) {
      // Use bash /dev/tcp to test TCP reachability.
      std::string probe = "bash -c 'echo > /dev/tcp/127.0.0.1/" + std::to_string(kTlsTestPort) +
                          "' > /dev/null 2>&1";
      if (runCommand(probe) == 0) {
        // Server is accepting connections.
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    // Server did not come up in time.
    stopNatsServer();
    return false;
  }

  static void stopNatsServer() {
    if (nats_server_pid_ > 0) {
      // Send SIGTERM, then SIGKILL after a short wait.
      std::string cmd = "kill " + std::to_string(nats_server_pid_) + " > /dev/null 2>&1";
      runCommand(cmd);
      std::this_thread::sleep_for(std::chrono::milliseconds{200});
      cmd = "kill -9 " + std::to_string(nats_server_pid_) + " > /dev/null 2>&1";
      runCommand(cmd);
      nats_server_pid_ = -1;
    }
  }

  static void cleanupTmpDir() {
    if (!tmp_dir_.empty()) {
      std::filesystem::remove_all(tmp_dir_);
      tmp_dir_.clear();
    }
  }

  // -----------------------------------------------------------------------
  // Suite-level state (shared across all tests)
  // -----------------------------------------------------------------------

  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static std::string skip_reason_;
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static std::string tmp_dir_;
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static std::string nats_server_path_;
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static std::string openssl_path_;
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static std::string nats_config_path_;
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static std::string server_cert_path_;
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static std::string server_key_path_;
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static int nats_server_pid_;
};

// Static member definitions
std::string TlsIntegrationTest::skip_reason_;
std::string TlsIntegrationTest::tmp_dir_;
std::string TlsIntegrationTest::nats_server_path_;
std::string TlsIntegrationTest::openssl_path_;
std::string TlsIntegrationTest::nats_config_path_;
std::string TlsIntegrationTest::server_cert_path_;
std::string TlsIntegrationTest::server_key_path_;
int TlsIntegrationTest::nats_server_pid_ = -1;

}  // namespace

// ===========================================================================
// Tests
// ===========================================================================

/**
 * @brief NatsConnection completes a TLS handshake with a self-signed CA cert.
 *
 * Issue #275: Validates that:
 *   1. NatsTlsConfig::enable_tls = true enables TLS negotiation.
 *   2. NatsTlsConfig::ca_cert_path loaded via openssl self-signed CA is
 *      accepted by nats.c without hostname verification errors.
 *   3. connect() returns true and getState() transitions to CONNECTED.
 *   4. disconnect() returns the connection to DISCONNECTED cleanly.
 */
TEST_F(TlsIntegrationTest, ConnectWithSelfSignedCert) {
  using namespace keystone::transport;

  NatsConfig cfg;
  cfg.url = serverUrl();
  cfg.tls.enable_tls = true;
  cfg.tls.ca_cert_path = caPath();

  // Reduce reconnection noise: one attempt only (test-time behaviour).
  cfg.max_reconnect_attempts = 0;

  NatsConnection conn(cfg);

  EXPECT_EQ(conn.getState(), NatsConnectionState::DISCONNECTED)
      << "Initial state must be DISCONNECTED";

  const bool connected = conn.connect();
  ASSERT_TRUE(connected)
      << "NatsConnection::connect() failed for TLS server at " << serverUrl() << " with CA cert "
      << caPath() << ". Check that nats-server started correctly and the cert was generated.";

  EXPECT_EQ(conn.getState(), NatsConnectionState::CONNECTED)
      << "State must be CONNECTED after successful connect()";
  EXPECT_TRUE(conn.isConnected()) << "isConnected() must return true";
  EXPECT_NE(conn.handle(), nullptr) << "Raw handle must be non-null after connect()";

  conn.disconnect();

  EXPECT_EQ(conn.getState(), NatsConnectionState::DISCONNECTED)
      << "State must be DISCONNECTED after disconnect()";
  EXPECT_FALSE(conn.isConnected()) << "isConnected() must return false after disconnect()";
}

/**
 * @brief NatsConnection correctly fails TLS handshake without a CA cert.
 *
 * When no CA cert is provided and the server uses a self-signed cert that is
 * not in the system trust store, the TLS handshake must fail and connect()
 * must return false. This guards against accidentally skipping certificate
 * verification.
 */
TEST_F(TlsIntegrationTest, ConnectWithoutCaCertFails) {
  using namespace keystone::transport;

  NatsConfig cfg;
  cfg.url = serverUrl();
  cfg.tls.enable_tls = true;
  // Intentionally do NOT set ca_cert_path — the self-signed CA is not in
  // the system trust store, so the handshake must fail.
  cfg.max_reconnect_attempts = 0;

  NatsConnection conn(cfg);

  const bool connected = conn.connect();
  // The connection must fail because the server cert is not trusted.
  EXPECT_FALSE(connected) << "connect() should fail when CA cert is absent and the server uses a "
                             "self-signed certificate not in the system trust store";

  EXPECT_FALSE(conn.isConnected());
}
