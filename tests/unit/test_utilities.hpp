/**
 * @file test_utilities.hpp
 * @brief Test utilities demonstrating Interface Segregation Principle (Issue
 * #46)
 *
 * This file provides helper functions that use specific MessageBus interfaces
 * instead of the monolithic MessageBus class, demonstrating best practices
 * for interface-segregated code.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "agents/agent_core.hpp"
#include "core/i_agent_registry.hpp"
#include "core/i_message_router.hpp"
#include "core/i_scheduler_integration.hpp"
#include "core/message_bus.hpp"

namespace keystone {
namespace test {

/**
 * @brief Register an agent using IAgentRegistry interface
 *
 * Demonstrates using only the registry interface when registration is needed.
 *
 * @param registry Registry interface (can be MessageBus or custom
 * implementation)
 * @param agent Agent to register
 */
template <typename AgentType>
inline void registerAgent(core::IAgentRegistry& registry,
                          std::shared_ptr<AgentType> agent) {
  registry.registerAgent(agent->getAgentId(), agent);
}

/**
 * @brief Setup agent's message router using IMessageRouter interface
 *
 * Demonstrates using only the router interface for agent setup.
 *
 * @param agent Agent to configure
 * @param router Router interface (can be MessageBus or custom implementation)
 */
inline void setupAgentRouter(agents::AgentCore& agent,
                             core::IMessageRouter* router) {
  agent.setMessageBus(router);
}

/**
 * @brief Setup scheduler using ISchedulerIntegration interface
 *
 * Demonstrates using only the scheduler interface for async configuration.
 *
 * @param integration Scheduler integration interface
 * @param scheduler Scheduler to set
 */
inline void setupScheduler(core::ISchedulerIntegration& integration,
                           concurrency::WorkStealingScheduler* scheduler) {
  integration.setScheduler(scheduler);
}

/**
 * @brief Batch register multiple agents
 *
 * @param registry Registry interface
 * @param agents Vector of agents to register
 */
template <typename AgentType>
inline void registerAgents(
    core::IAgentRegistry& registry,
    const std::vector<std::shared_ptr<AgentType>>& agents) {
  for (const auto& agent : agents) {
    registry.registerAgent(agent->getAgentId(), agent);
  }
}

/**
 * @brief Setup a complete test environment with registry and routing
 *
 * Convenience function for tests that need both registration and routing.
 * Still uses specific interfaces internally to demonstrate best practices.
 *
 * @param bus MessageBus instance (provides all interfaces)
 * @param agents Vector of agents to register and configure
 */
template <typename AgentType>
inline void setupTestEnvironment(
    core::MessageBus& bus,
    const std::vector<std::shared_ptr<AgentType>>& agents) {
  // Use IAgentRegistry interface for registration
  core::IAgentRegistry& registry = bus;
  registerAgents(registry, agents);

  // Use IMessageRouter interface for agent setup
  core::IMessageRouter* router = &bus;
  for (const auto& agent : agents) {
    setupAgentRouter(*agent, router);
  }
}

/**
 * @brief Verify all agents are registered
 *
 * @param registry Registry interface to query
 * @param agent_ids Expected agent IDs
 * @return true if all agents are registered
 */
inline bool verifyAllRegistered(const core::IAgentRegistry& registry,
                                const std::vector<std::string>& agent_ids) {
  for (const auto& id : agent_ids) {
    if (!registry.hasAgent(id)) {
      return false;
    }
  }
  return true;
}

}  // namespace test
}  // namespace keystone
