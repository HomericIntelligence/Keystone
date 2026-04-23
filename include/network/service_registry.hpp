#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "service_registry.grpc.pb.h"
#include "service_registry.pb.h"

namespace keystone {
namespace network {

/// Agent registration information
struct AgentRegistrationInfo {
  std::string agent_id;
  std::string agent_type;
  int32_t level;
  std::string ip_port;
  std::vector<std::string> capabilities;
  std::chrono::system_clock::time_point last_heartbeat;
  float cpu_usage_percent{0.0f};
  float memory_usage_mb{0.0f};
  int32_t active_tasks{0};
};

/// Service Registry - manages agent registration and discovery
/// Thread-safe registry for distributed agent coordination
class ServiceRegistry final {
 public:
  /// Constructor
  /// @param heartbeat_timeout_ms Timeout for heartbeat (default: 3000ms)
  explicit ServiceRegistry(int32_t heartbeat_timeout_ms = 3000);

  /// Destructor
  ~ServiceRegistry();

  // Prevent copying
  ServiceRegistry(const ServiceRegistry&) = delete;
  ServiceRegistry& operator=(const ServiceRegistry&) = delete;

  /// Register an agent with the registry
  /// @param agent_id Unique agent identifier
  /// @param agent_type Agent type (e.g., "ChiefArchitectAgent")
  /// @param level Hierarchy level (0-3)
  /// @param ip_port IP address and port (e.g., "192.168.1.100:50051")
  /// @param capabilities List of agent capabilities
  /// @return true if registered successfully, false if agent_id already exists
  bool registerAgent(const std::string& agent_id,
                     const std::string& agent_type,
                     int32_t level,
                     const std::string& ip_port,
                     const std::vector<std::string>& capabilities);

  /// Update heartbeat for an agent
  /// @param agent_id Agent identifier
  /// @param cpu_usage_percent Optional CPU usage
  /// @param memory_usage_mb Optional memory usage
  /// @param active_tasks Optional active task count
  /// @return true if agent exists, false otherwise
  bool updateHeartbeat(const std::string& agent_id,
                       float cpu_usage_percent = 0.0f,
                       float memory_usage_mb = 0.0f,
                       int32_t active_tasks = 0);

  /// Unregister an agent
  /// @param agent_id Agent identifier
  /// @return true if unregistered, false if not found
  bool unregisterAgent(const std::string& agent_id);

  /// Get agent information
  /// @param agent_id Agent identifier
  /// @return Agent info if found, nullopt otherwise
  std::optional<AgentRegistrationInfo> getAgent(const std::string& agent_id) const;

  /// Query agents by criteria
  /// @param agent_type Filter by agent type (empty = all)
  /// @param level Filter by level (-1 = all)
  /// @param required_capabilities Must have ALL these capabilities
  /// @param max_results Maximum number of results (0 = unlimited)
  /// @param only_alive Only return agents with recent heartbeat
  /// @return List of matching agents
  std::vector<AgentRegistrationInfo> queryAgents(
      const std::string& agent_type = "",
      int32_t level = -1,
      const std::vector<std::string>& required_capabilities = {},
      int32_t max_results = 0,
      bool only_alive = true) const;

  /// List all registered agents
  /// @param only_alive Only return agents with recent heartbeat
  /// @return List of all agents
  std::vector<AgentRegistrationInfo> listAllAgents(bool only_alive = true) const;

  /// Check if an agent is alive (recent heartbeat)
  /// @param agent_id Agent identifier
  /// @return true if agent has sent heartbeat within timeout
  bool isAgentAlive(const std::string& agent_id) const;

  /// Clean up dead agents (those with expired heartbeats)
  /// @return Number of agents removed
  int32_t cleanupDeadAgents();

  /// Get heartbeat timeout
  std::chrono::milliseconds getHeartbeatTimeout() const { return heartbeat_timeout_; }

  /// Set heartbeat timeout
  void setHeartbeatTimeout(std::chrono::milliseconds timeout) { heartbeat_timeout_ = timeout; }

  /// Get number of registered agents
  size_t getAgentCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return agents_.size();
  }

  /// Get number of alive agents
  size_t getAliveAgentCount() const;

  /// Convert to protobuf AgentInfo
  static hmas::AgentInfo toProtoAgentInfo(const AgentRegistrationInfo& info);

  /// Convert from protobuf AgentRegistration
  static AgentRegistrationInfo fromProtoRegistration(const hmas::AgentRegistration& reg);

 private:
  /// Check if agent has required capabilities
  bool hasRequiredCapabilities(const AgentRegistrationInfo& agent,
                               const std::vector<std::string>& required_capabilities) const;

  /// Registry map: agent_id -> registration info
  std::unordered_map<std::string, AgentRegistrationInfo> agents_;

  /// Mutex for thread-safe access
  mutable std::mutex mutex_;

  /// Heartbeat timeout duration
  std::chrono::milliseconds heartbeat_timeout_;
};

/// gRPC Service implementation for ServiceRegistry
class ServiceRegistryServiceImpl final : public hmas::ServiceRegistry::Service {
 public:
  explicit ServiceRegistryServiceImpl(std::shared_ptr<ServiceRegistry> registry);

  grpc::Status RegisterAgent(grpc::ServerContext* context,
                             const hmas::AgentRegistration* request,
                             hmas::RegistrationResponse* response) override;

  grpc::Status Heartbeat(grpc::ServerContext* context,
                         const hmas::HeartbeatRequest* request,
                         hmas::HeartbeatResponse* response) override;

  grpc::Status UnregisterAgent(grpc::ServerContext* context,
                               const hmas::UnregisterRequest* request,
                               hmas::UnregisterResponse* response) override;

  grpc::Status QueryAgents(grpc::ServerContext* context,
                           const hmas::AgentQuery* request,
                           hmas::AgentList* response) override;

  grpc::Status GetAgent(grpc::ServerContext* context,
                        const hmas::GetAgentRequest* request,
                        hmas::AgentInfo* response) override;

  grpc::Status ListAllAgents(grpc::ServerContext* context,
                             const hmas::ListAllAgentsRequest* request,
                             hmas::AgentList* response) override;

 private:
  std::shared_ptr<ServiceRegistry> registry_;
};

}  // namespace network
}  // namespace keystone
