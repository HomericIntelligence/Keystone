#pragma once

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>
#include <vector>

namespace keystone {
namespace network {

/// gRPC Server configuration
struct GrpcServerConfig {
  std::string server_address{"0.0.0.0:50051"};
  int max_message_size{100 * 1024 * 1024};  // 100MB
  bool enable_tls{false};
  std::string tls_cert_path;
  std::string tls_key_path;
  int num_threads{4};  // Number of async server threads
};

/// gRPC Server wrapper - manages server lifecycle and multiple services
class GrpcServer {
 public:
  /// Constructor
  /// @param config Server configuration
  explicit GrpcServer(const GrpcServerConfig& config = GrpcServerConfig());

  /// Destructor - automatically stops server
  ~GrpcServer();

  // Prevent copying
  GrpcServer(const GrpcServer&) = delete;
  GrpcServer& operator=(const GrpcServer&) = delete;

  /// Register a gRPC service
  /// @param service Service implementation to register
  /// @note Must be called before Start()
  void registerService(grpc::Service* service);

  /// Start the gRPC server
  /// @return true if started successfully, false if already running
  bool start();

  /// Stop the gRPC server
  /// @param grace_period_ms Graceful shutdown timeout in milliseconds
  void stop(int grace_period_ms = 5000);

  /// Wait for server to terminate
  void wait();

  /// Check if server is running
  bool isRunning() const { return server_ != nullptr; }

  /// Get server address
  std::string getAddress() const { return config_.server_address; }

 private:
  /// Build server credentials (TLS or insecure)
  std::shared_ptr<grpc::ServerCredentials> buildCredentials();

  GrpcServerConfig config_;
  std::unique_ptr<grpc::Server> server_;
  std::vector<grpc::Service*> services_;
  bool started_{false};
};

}  // namespace network
}  // namespace keystone
