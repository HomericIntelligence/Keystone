#include "network/grpc_server.hpp"

#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server_builder.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace keystone::network {

namespace {

/// Read file contents into a string
/// @param file_path Path to the file
/// @return File contents as string
/// @throws std::runtime_error if file cannot be read
std::string readFileContents(const std::string& file_path) {
  std::ifstream file(file_path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open file: " + file_path);
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();

  if (file.fail() && !file.eof()) {
    throw std::runtime_error("Failed to read file: " + file_path);
  }

  return buffer.str();
}

/// Get environment variable value
/// @param name Environment variable name
/// @return Value if set, empty string otherwise
std::string getEnvVar(const char* name) {
  const char* value = std::getenv(name);
  return value ? std::string(value) : std::string();
}

}  // anonymous namespace

GrpcServer::GrpcServer(const GrpcServerConfig& config) : config_(config) {}

GrpcServer::~GrpcServer() {
  if (server_) {
    stop();
  }
}

void GrpcServer::registerService(grpc::Service* service) {
  if (started_) {
    throw std::runtime_error(
        "Cannot register service after server has started");
  }
  services_.push_back(service);
}

bool GrpcServer::start() {
  if (started_) {
    return false;
  }

  grpc::ServerBuilder builder;

  // Set server address
  builder.AddListeningPort(config_.server_address, buildCredentials());

  // Register all services
  for (auto* service : services_) {
    builder.RegisterService(service);
  }

  // Set max message size
  builder.SetMaxReceiveMessageSize(config_.max_message_size);
  builder.SetMaxSendMessageSize(config_.max_message_size);

  // Build and start server
  server_ = builder.BuildAndStart();

  if (server_) {
    started_ = true;
    return true;
  }

  return false;
}

void GrpcServer::stop(int grace_period_ms) {
  if (server_) {
    auto deadline = std::chrono::system_clock::now() +
                    std::chrono::milliseconds(grace_period_ms);
    server_->Shutdown(deadline);
    server_.reset();
    started_ = false;
  }
}

void GrpcServer::wait() {
  if (server_) {
    server_->Wait();
  }
}

std::shared_ptr<grpc::ServerCredentials> GrpcServer::buildCredentials() {
  if (config_.enable_tls) {
    // Get certificate paths from environment variables
    std::string cert_path = getEnvVar("KEYSTONE_TLS_CERT_PATH");
    std::string key_path = getEnvVar("KEYSTONE_TLS_KEY_PATH");
    std::string ca_path = getEnvVar("KEYSTONE_TLS_CA_PATH");

    // If paths not in environment, fall back to config
    if (cert_path.empty()) {
      cert_path = config_.tls_cert_path;
    }
    if (key_path.empty()) {
      key_path = config_.tls_key_path;
    }

    // Validate that required paths are set
    if (cert_path.empty() || key_path.empty()) {
      throw std::runtime_error(
          "TLS enabled but certificate or key path not provided. "
          "Set KEYSTONE_TLS_CERT_PATH and KEYSTONE_TLS_KEY_PATH environment "
          "variables.");
    }

    try {
      // Read certificate and key files
      std::string cert_contents = readFileContents(cert_path);
      std::string key_contents = readFileContents(key_path);

      // Setup SSL credentials
      grpc::SslServerCredentialsOptions ssl_opts;
      grpc::SslServerCredentialsOptions::PemKeyCertPair key_cert_pair;
      key_cert_pair.private_key = key_contents;
      key_cert_pair.cert_chain = cert_contents;
      ssl_opts.pem_key_cert_pairs.push_back(key_cert_pair);

      // If CA cert provided, enable client certificate verification
      if (!ca_path.empty()) {
        std::string ca_contents = readFileContents(ca_path);
        ssl_opts.pem_root_certs = ca_contents;
        ssl_opts.client_certificate_request =
            GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;
      }

      return grpc::SslServerCredentials(ssl_opts);
    } catch (const std::exception& e) {
      throw std::runtime_error(std::string("Failed to load TLS credentials: ") +
                               e.what());
    }
  } else {
    return grpc::InsecureServerCredentials();
  }
}

}  // namespace keystone::network
