#pragma once

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>

#include "hmas_coordinator.grpc.pb.h"
#include "service_registry.grpc.pb.h"

namespace keystone {
namespace network {

/// gRPC Client configuration
struct GrpcClientConfig {
  std::string server_address;
  int timeout_ms{30000};                    // Default 30s timeout
  int max_message_size{100 * 1024 * 1024};  // 100MB
  bool enable_tls{false};
  std::string tls_ca_path;
  int max_retries{3};
  int initial_backoff_ms{1000};  // 1s initial backoff
  int max_backoff_ms{30000};     // 30s max backoff
};

/// gRPC Client wrapper for HMASCoordinator service
class HMASCoordinatorClient {
 public:
  /// Constructor
  /// @param config Client configuration
  explicit HMASCoordinatorClient(const GrpcClientConfig& config);

  /// Submit a task for execution
  /// @param yaml_spec YAML task specification
  /// @param session_id Session identifier
  /// @param deadline_unix_ms Deadline timestamp
  /// @param priority Task priority
  /// @param parent_task_id Parent task ID (optional)
  /// @return Task response with task_id and assigned node
  hmas::TaskResponse submitTask(
      const std::string& yaml_spec, const std::string& session_id,
      int64_t deadline_unix_ms,
      hmas::TaskPriority priority = hmas::TASK_PRIORITY_NORMAL,
      const std::string& parent_task_id = "");

  /// Submit task result back to parent
  /// @param result Task result to submit
  /// @return Result acknowledgement
  hmas::ResultAcknowledgement submitResult(const hmas::TaskResult& result);

  /// Get task result (blocking with optional timeout)
  /// @param task_id Task identifier
  /// @param timeout_ms Timeout in milliseconds (0 = return immediately)
  /// @return Task result
  hmas::TaskResult getTaskResult(const std::string& task_id,
                                 int64_t timeout_ms = 0);

  /// Get task progress
  /// @param task_id Task identifier
  /// @param include_subtasks Include subtask status
  /// @return Task progress
  hmas::TaskProgress getTaskProgress(const std::string& task_id,
                                     bool include_subtasks = false);

  /// Cancel a running task
  /// @param task_id Task identifier
  /// @param reason Cancellation reason
  /// @return Cancel response
  hmas::CancelResponse cancelTask(const std::string& task_id,
                                  const std::string& reason = "");

  /// Check if connected
  bool isConnected() const { return stub_ != nullptr; }

  /// Get server address
  std::string getServerAddress() const { return config_.server_address; }

 private:
  /// Create channel credentials
  std::shared_ptr<grpc::ChannelCredentials> createCredentials();

  /// Create client context with timeout
  std::unique_ptr<grpc::ClientContext> createContext(int timeout_ms = 0);

  GrpcClientConfig config_;
  std::shared_ptr<grpc::Channel> channel_;
  std::unique_ptr<hmas::HMASCoordinator::Stub> stub_;
};

/// gRPC Client wrapper for ServiceRegistry service
class ServiceRegistryClient {
 public:
  /// Constructor
  /// @param config Client configuration
  explicit ServiceRegistryClient(const GrpcClientConfig& config);

  /// Register agent with the registry
  /// @param registration Agent registration info
  /// @return Registration response
  hmas::RegistrationResponse registerAgent(
      const hmas::AgentRegistration& registration);

  /// Send heartbeat to registry
  /// @param agent_id Agent identifier
  /// @param cpu_usage_percent CPU usage percentage
  /// @param memory_usage_mb Memory usage in MB
  /// @param active_tasks Number of active tasks
  /// @return Heartbeat response
  hmas::HeartbeatResponse heartbeat(const std::string& agent_id,
                                    float cpu_usage_percent = 0.0f,
                                    float memory_usage_mb = 0.0f,
                                    int active_tasks = 0);

  /// Unregister agent from registry
  /// @param agent_id Agent identifier
  /// @param reason Unregistration reason
  /// @return Unregister response
  hmas::UnregisterResponse unregisterAgent(const std::string& agent_id,
                                           const std::string& reason = "");

  /// Query agents by criteria
  /// @param query Agent query criteria
  /// @return List of matching agents
  hmas::AgentList queryAgents(const hmas::AgentQuery& query);

  /// Get specific agent by ID
  /// @param agent_id Agent identifier
  /// @return Agent info
  hmas::AgentInfo getAgent(const std::string& agent_id);

  /// List all agents
  /// @param only_alive Only return alive agents
  /// @return List of all agents
  hmas::AgentList listAllAgents(bool only_alive = true);

  /// Check if connected
  bool isConnected() const { return stub_ != nullptr; }

  /// Get server address
  std::string getServerAddress() const { return config_.server_address; }

 private:
  /// Create channel credentials
  std::shared_ptr<grpc::ChannelCredentials> createCredentials();

  /// Create client context with timeout
  std::unique_ptr<grpc::ClientContext> createContext(int timeout_ms = 0);

  GrpcClientConfig config_;
  std::shared_ptr<grpc::Channel> channel_;
  std::unique_ptr<hmas::ServiceRegistry::Stub> stub_;
};

}  // namespace network
}  // namespace keystone
