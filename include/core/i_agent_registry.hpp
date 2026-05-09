#pragma once

#include <memory>
#include <string>
#include <vector>

// Forward declarations
namespace keystone {
namespace agents {
class AgentCore;
}
}  // namespace keystone

#include "agents/concepts.hpp"

namespace keystone {
namespace core {

/**
 * @brief Interface for agent registry management
 *
 * Separates agent lifecycle operations (registration, unregistration,
 * discovery) from message routing and scheduler integration, following the
 * Interface Segregation Principle.
 *
 * Responsibilities:
 * - Register agents with the system
 * - Unregister agents from the system
 * - Query agent existence
 * - List all registered agents
 *
 * This interface enables:
 * - Independent testing of registration logic
 * - Different registry implementations (local, distributed, cached)
 * - Clearer separation of concerns in agent management code
 */
class IAgentRegistry {
 public:
  virtual ~IAgentRegistry() = default;

  /**
   * @brief Register an agent with the registry
   *
   * @param agent_id Unique identifier for the agent
   * @param agent Shared pointer to the agent (lifetime managed by shared_ptr)
   * @throws std::runtime_error if agent_id already registered
   */
  virtual void registerAgent(const std::string& agent_id,
                             std::shared_ptr<agents::AgentCore> agent) = 0;

  /**
   * @brief Register an agent with compile-time interface verification
   *
   * Template method using C++20 concepts to verify at compile time
   * that the agent implements the required Agent interface.
   *
   * @tparam A Agent type satisfying the Agent concept
   * @param agent Shared pointer to the agent
   * @throws std::runtime_error if agent_id already registered
   */
  template <agents::Agent A>
  void registerAgent(std::shared_ptr<A> agent) {
    if (!agent) {
      throw std::runtime_error(
          "IAgentRegistry::registerAgent: null agent pointer");
    }

    std::string agent_id = agent->getAgentId();
    std::shared_ptr<agents::AgentCore> base_agent = agent;
    registerAgent(agent_id, base_agent);
  }

  /**
   * @brief Unregister an agent from the registry
   *
   * @param agent_id Agent identifier to unregister
   */
  virtual void unregisterAgent(const std::string& agent_id) = 0;

  /**
   * @brief Check if an agent is registered
   *
   * @param agent_id Agent identifier to check
   * @return true if agent is registered
   */
  virtual bool hasAgent(const std::string& agent_id) const = 0;

  /**
   * @brief Get list of all registered agent IDs
   *
   * @return std::vector<std::string> List of agent IDs
   */
  virtual std::vector<std::string> listAgents() const = 0;
};

}  // namespace core
}  // namespace keystone
