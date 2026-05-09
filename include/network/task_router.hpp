#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "network/service_registry.hpp"
#include "network/yaml_parser.hpp"

namespace keystone {
namespace network {

/// Load balancing strategy for agent selection
enum class LoadBalancingStrategy {
  ROUND_ROBIN,   // Cycle through available agents
  LEAST_LOADED,  // Select agent with fewest active tasks
  RANDOM         // Random selection
};

/// Task routing result
struct TaskRoutingResult {
  bool success{false};
  std::string target_agent_id;
  std::string target_ip_port;
  std::string error_message;
};

/// Task Router - routes tasks to appropriate agents based on YAML specs
class TaskRouter {
 public:
  /// Constructor
  /// @param registry Service registry for agent discovery
  /// @param strategy Load balancing strategy (default: LEAST_LOADED)
  explicit TaskRouter(
      std::shared_ptr<ServiceRegistry> registry,
      LoadBalancingStrategy strategy = LoadBalancingStrategy::LEAST_LOADED);

  /// Route task to appropriate agent
  /// @param spec Task specification with routing info
  /// @return Routing result with target agent info
  TaskRoutingResult routeTask(const HierarchicalTaskSpec& spec);

  /// Route task by parameters (alternative interface)
  /// @param target_level Target hierarchy level (0-3)
  /// @param target_agent_type Target agent type
  /// @param required_capabilities Required agent capabilities
  /// @param preferred_agent_id Preferred agent ID (optional)
  /// @return Routing result with target agent info
  TaskRoutingResult routeTask(
      int target_level, const std::string& target_agent_type,
      const std::vector<std::string>& required_capabilities = {},
      const std::string& preferred_agent_id = "");

  /// Select best agent for task based on criteria
  /// @param target_level Target hierarchy level
  /// @param agent_type Required agent type
  /// @param required_capabilities Required capabilities
  /// @return Selected agent info, or nullopt if none available
  std::optional<AgentRegistrationInfo> selectAgent(
      int target_level, const std::string& agent_type,
      const std::vector<std::string>& required_capabilities = {});

  /// Get load balancing strategy
  LoadBalancingStrategy getStrategy() const { return strategy_; }

  /// Set load balancing strategy
  void setStrategy(LoadBalancingStrategy strategy) { strategy_ = strategy; }

  /// Reset round-robin counter
  void resetRoundRobin() { round_robin_index_ = 0; }

 private:
  /// Select agent using round-robin strategy
  std::optional<AgentRegistrationInfo> selectRoundRobin(
      const std::vector<AgentRegistrationInfo>& candidates);

  /// Select agent with least load
  std::optional<AgentRegistrationInfo> selectLeastLoaded(
      const std::vector<AgentRegistrationInfo>& candidates);

  /// Select random agent
  std::optional<AgentRegistrationInfo> selectRandom(
      const std::vector<AgentRegistrationInfo>& candidates);

  std::shared_ptr<ServiceRegistry> registry_;
  LoadBalancingStrategy strategy_;
  size_t round_robin_index_{0};
};

}  // namespace network
}  // namespace keystone
