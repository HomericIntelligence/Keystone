#include "network/task_router.hpp"

#include <algorithm>
#include <random>

namespace keystone::network {

TaskRouter::TaskRouter(std::shared_ptr<ServiceRegistry> registry,
                       LoadBalancingStrategy strategy)
    : registry_(std::move(registry)), strategy_(strategy) {}

TaskRoutingResult TaskRouter::routeTask(const HierarchicalTaskSpec& spec) {
  return routeTask(spec.routing.target_level, spec.routing.target_agent_type,
                   spec.payload.required_capabilities,
                   spec.routing.target_agent_id.value_or(""));
}

TaskRoutingResult TaskRouter::routeTask(
    int target_level, const std::string& target_agent_type,
    const std::vector<std::string>& required_capabilities,
    const std::string& preferred_agent_id) {
  TaskRoutingResult result;

  // If specific agent ID is provided, try to use it
  if (!preferred_agent_id.empty()) {
    auto agent_opt = registry_->getAgent(preferred_agent_id);
    if (agent_opt.has_value()) {
      const auto& agent = agent_opt.value();

      // Verify agent is alive and matches criteria
      if (registry_->isAgentAlive(preferred_agent_id) &&
          agent.level == target_level &&
          agent.agent_type == target_agent_type) {
        result.success = true;
        result.target_agent_id = agent.agent_id;
        result.target_ip_port = agent.ip_port;
        return result;
      }
    }

    // Preferred agent not available, fall through to general selection
  }

  // Select agent based on criteria
  auto selected_agent =
      selectAgent(target_level, target_agent_type, required_capabilities);

  if (!selected_agent.has_value()) {
    result.success = false;
    result.error_message =
        "No available agent found for level=" + std::to_string(target_level) +
        ", type=" + target_agent_type;
    return result;
  }

  result.success = true;
  result.target_agent_id = selected_agent->agent_id;
  result.target_ip_port = selected_agent->ip_port;
  return result;
}

std::optional<AgentRegistrationInfo> TaskRouter::selectAgent(
    int target_level, const std::string& agent_type,
    const std::vector<std::string>& required_capabilities) {
  // Query registry for matching agents
  auto candidates =
      registry_->queryAgents(agent_type, target_level, required_capabilities,
                             0,    // max_results = 0 (unlimited)
                             true  // only_alive = true
      );

  if (candidates.empty()) {
    return std::nullopt;
  }

  // Apply load balancing strategy
  switch (strategy_) {
    case LoadBalancingStrategy::ROUND_ROBIN:
      return selectRoundRobin(candidates);
    case LoadBalancingStrategy::LEAST_LOADED:
      return selectLeastLoaded(candidates);
    case LoadBalancingStrategy::RANDOM:
      return selectRandom(candidates);
    default:
      return selectLeastLoaded(candidates);
  }
}

std::optional<AgentRegistrationInfo> TaskRouter::selectRoundRobin(
    const std::vector<AgentRegistrationInfo>& candidates) {
  if (candidates.empty()) {
    return std::nullopt;
  }

  size_t index = round_robin_index_ % candidates.size();
  round_robin_index_ = (round_robin_index_ + 1) % candidates.size();

  return candidates[index];
}

std::optional<AgentRegistrationInfo> TaskRouter::selectLeastLoaded(
    const std::vector<AgentRegistrationInfo>& candidates) {
  if (candidates.empty()) {
    return std::nullopt;
  }

  // Find agent with minimum active tasks
  auto min_it = std::min_element(
      candidates.begin(), candidates.end(),
      [](const AgentRegistrationInfo& a, const AgentRegistrationInfo& b) {
        return a.active_tasks < b.active_tasks;
      });

  return *min_it;
}

std::optional<AgentRegistrationInfo> TaskRouter::selectRandom(
    const std::vector<AgentRegistrationInfo>& candidates) {
  if (candidates.empty()) {
    return std::nullopt;
  }

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);

  return candidates[dist(gen)];
}

}  // namespace keystone::network
