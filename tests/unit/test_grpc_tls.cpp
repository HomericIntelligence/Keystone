/**
 * @file test_grpc_tls.cpp
 * @brief Unit tests for gRPC TLS configuration
 *
 * Tests TLS certificate loading and credential creation for both
 * gRPC server and clients. Validates environment variable configuration
 * and error handling.
 *
 * Phase 1.1: TLS support for gRPC (Issue #52)
 */

#ifdef ENABLE_GRPC

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "network/grpc_client.hpp"
#include "network/grpc_server.hpp"

using namespace keystone::network;
namespace fs = std::filesystem;

class GrpcTlsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create temporary directory for test certificates
    test_cert_dir_ = fs::temp_directory_path() / "keystone_tls_test";
    fs::create_directories(test_cert_dir_);

    // Create dummy certificate files (not real certs, just for file I/O
    // testing)
    cert_file_ = test_cert_dir_ / "server.crt";
    key_file_ = test_cert_dir_ / "server.key";
    ca_file_ = test_cert_dir_ / "ca.crt";

    std::ofstream(cert_file_)
        << "-----BEGIN CERTIFICATE-----\nDUMMY_CERT\n-----END "
           "CERTIFICATE-----\n";
    std::ofstream(key_file_)
        << "-----BEGIN PRIVATE KEY-----\nDUMMY_KEY\n-----END PRIVATE "
           "KEY-----\n";
    std::ofstream(ca_file_)
        << "-----BEGIN CERTIFICATE-----\nDUMMY_CA\n-----END CERTIFICATE-----\n";

    // Save original environment variables
    saved_cert_path_ = getenv("KEYSTONE_TLS_CERT_PATH");
    saved_key_path_ = getenv("KEYSTONE_TLS_KEY_PATH");
    saved_ca_path_ = getenv("KEYSTONE_TLS_CA_PATH");
  }

  void TearDown() override {
    // Restore original environment variables
    restoreEnvVar("KEYSTONE_TLS_CERT_PATH", saved_cert_path_);
    restoreEnvVar("KEYSTONE_TLS_KEY_PATH", saved_key_path_);
    restoreEnvVar("KEYSTONE_TLS_CA_PATH", saved_ca_path_);

    // Clean up temporary directory
    if (fs::exists(test_cert_dir_)) {
      fs::remove_all(test_cert_dir_);
    }
  }

  void setEnvVar(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
  }

  void unsetEnvVar(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
  }

  void restoreEnvVar(const char* name, const char* value) {
    if (value) {
      setEnvVar(name, value);
    } else {
      unsetEnvVar(name);
    }
  }

  fs::path test_cert_dir_;
  fs::path cert_file_;
  fs::path key_file_;
  fs::path ca_file_;

  const char* saved_cert_path_ = nullptr;
  const char* saved_key_path_ = nullptr;
  const char* saved_ca_path_ = nullptr;
};

/**
 * @brief Test: Server with TLS disabled uses insecure credentials
 */
TEST_F(GrpcTlsTest, ServerInsecureByDefault) {
  GrpcServerConfig config;
  config.server_address = "localhost:0";
  config.enable_tls = false;

  // Should not throw even without certificates
  EXPECT_NO_THROW({
    GrpcServer server(config);
    server.start();
    server.stop();
  });
}

/**
 * @brief Test: Server with TLS enabled but missing cert paths throws
 */
TEST_F(GrpcTlsTest, ServerTlsMissingCertsThrows) {
  unsetEnvVar("KEYSTONE_TLS_CERT_PATH");
  unsetEnvVar("KEYSTONE_TLS_KEY_PATH");

  GrpcServerConfig config;
  config.server_address = "localhost:0";
  config.enable_tls = true;

  GrpcServer server(config);
  // Should throw when trying to start without certificate paths
  EXPECT_THROW(server.start(), std::runtime_error);
}

/**
 * @brief Test: Server with TLS enabled via environment variables
 */
TEST_F(GrpcTlsTest, ServerTlsWithEnvVars) {
  setEnvVar("KEYSTONE_TLS_CERT_PATH", cert_file_.string());
  setEnvVar("KEYSTONE_TLS_KEY_PATH", key_file_.string());

  GrpcServerConfig config;
  config.server_address = "localhost:0";
  config.enable_tls = true;

  // Note: This will fail to start because dummy certs are not valid,
  // but we're testing that the file loading logic works
  GrpcServer server(config);
  // The start() will fail due to invalid certs, but shouldn't throw
  // from file reading - gRPC will handle invalid cert format
  try {
    server.start();
  } catch (const std::exception& e) {
    // Expected: gRPC may reject invalid certificates
    // We just want to ensure file reading worked
    std::string error_msg = e.what();
    EXPECT_TRUE(error_msg.find("Failed to open file") == std::string::npos);
  }
}

/**
 * @brief Test: Server with TLS enabled via config (fallback from env vars)
 */
TEST_F(GrpcTlsTest, ServerTlsWithConfigPaths) {
  unsetEnvVar("KEYSTONE_TLS_CERT_PATH");
  unsetEnvVar("KEYSTONE_TLS_KEY_PATH");

  GrpcServerConfig config;
  config.server_address = "localhost:0";
  config.enable_tls = true;
  config.tls_cert_path = cert_file_.string();
  config.tls_key_path = key_file_.string();

  // Note: This will fail to start because dummy certs are not valid
  GrpcServer server(config);
  try {
    server.start();
  } catch (const std::exception& e) {
    std::string error_msg = e.what();
    EXPECT_TRUE(error_msg.find("Failed to open file") == std::string::npos);
  }
}

/**
 * @brief Test: Server with non-existent certificate file throws
 */
TEST_F(GrpcTlsTest, ServerTlsInvalidCertPathThrows) {
  setEnvVar("KEYSTONE_TLS_CERT_PATH", "/nonexistent/cert.pem");
  setEnvVar("KEYSTONE_TLS_KEY_PATH", key_file_.string());

  GrpcServerConfig config;
  config.server_address = "localhost:0";
  config.enable_tls = true;

  GrpcServer server(config);
  EXPECT_THROW(server.start(), std::runtime_error);
}

/**
 * @brief Test: Client with TLS disabled uses insecure credentials
 */
TEST_F(GrpcTlsTest, ClientInsecureByDefault) {
  GrpcClientConfig config;
  config.server_address = "localhost:50051";
  config.enable_tls = false;

  // Should not throw even without CA certificate
  EXPECT_NO_THROW({
    HMASCoordinatorClient client(config);
    EXPECT_TRUE(client.isConnected());
  });
}

/**
 * @brief Test: Client with TLS enabled but missing CA path throws
 */
TEST_F(GrpcTlsTest, ClientTlsMissingCaThrows) {
  unsetEnvVar("KEYSTONE_TLS_CA_PATH");

  GrpcClientConfig config;
  config.server_address = "localhost:50051";
  config.enable_tls = true;

  // Should throw during construction when trying to create credentials
  EXPECT_THROW(HMASCoordinatorClient client(config), std::runtime_error);
}

/**
 * @brief Test: Client with TLS enabled via environment variable
 */
TEST_F(GrpcTlsTest, ClientTlsWithEnvVar) {
  setEnvVar("KEYSTONE_TLS_CA_PATH", ca_file_.string());

  GrpcClientConfig config;
  config.server_address = "localhost:50051";
  config.enable_tls = true;

  // Should successfully create client (connection will fail but that's ok)
  EXPECT_NO_THROW({
    HMASCoordinatorClient client(config);
    EXPECT_TRUE(client.isConnected());
  });
}

/**
 * @brief Test: Client with TLS enabled via config (fallback from env var)
 */
TEST_F(GrpcTlsTest, ClientTlsWithConfigPath) {
  unsetEnvVar("KEYSTONE_TLS_CA_PATH");

  GrpcClientConfig config;
  config.server_address = "localhost:50051";
  config.enable_tls = true;
  config.tls_ca_path = ca_file_.string();

  // Should successfully create client
  EXPECT_NO_THROW({
    HMASCoordinatorClient client(config);
    EXPECT_TRUE(client.isConnected());
  });
}

/**
 * @brief Test: Client with non-existent CA file throws
 */
TEST_F(GrpcTlsTest, ClientTlsInvalidCaPathThrows) {
  setEnvVar("KEYSTONE_TLS_CA_PATH", "/nonexistent/ca.pem");

  GrpcClientConfig config;
  config.server_address = "localhost:50051";
  config.enable_tls = true;

  EXPECT_THROW(HMASCoordinatorClient client(config), std::runtime_error);
}

/**
 * @brief Test: ServiceRegistryClient with TLS disabled
 */
TEST_F(GrpcTlsTest, ServiceRegistryClientInsecure) {
  GrpcClientConfig config;
  config.server_address = "localhost:50051";
  config.enable_tls = false;

  EXPECT_NO_THROW({
    ServiceRegistryClient client(config);
    EXPECT_TRUE(client.isConnected());
  });
}

/**
 * @brief Test: ServiceRegistryClient with TLS enabled
 */
TEST_F(GrpcTlsTest, ServiceRegistryClientTlsWithEnvVar) {
  setEnvVar("KEYSTONE_TLS_CA_PATH", ca_file_.string());

  GrpcClientConfig config;
  config.server_address = "localhost:50051";
  config.enable_tls = true;

  EXPECT_NO_THROW({
    ServiceRegistryClient client(config);
    EXPECT_TRUE(client.isConnected());
  });
}

/**
 * @brief Test: Environment variables take precedence over config
 */
TEST_F(GrpcTlsTest, EnvVarTakesPrecedenceOverConfig) {
  // Set environment variable to valid path
  setEnvVar("KEYSTONE_TLS_CA_PATH", ca_file_.string());

  GrpcClientConfig config;
  config.server_address = "localhost:50051";
  config.enable_tls = true;
  config.tls_ca_path = "/nonexistent/ca.pem";  // Invalid config path

  // Should succeed because env var takes precedence
  EXPECT_NO_THROW({
    HMASCoordinatorClient client(config);
    EXPECT_TRUE(client.isConnected());
  });
}

#endif  // ENABLE_GRPC
