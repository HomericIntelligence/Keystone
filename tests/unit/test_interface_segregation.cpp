/**
 * @file test_interface_segregation.cpp
 * @brief Tests demonstrating Interface Segregation Principle (Issue #46)
 *
 * These tests show how to use specific MessageBus interfaces instead of
 * the monolithic MessageBus class, following the Interface Segregation
 * Principle.
 */

// KeystoneMessage::command is [[deprecated]]; test files intentionally access
// it to verify backward-compat behaviour.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <gtest/gtest.h>

#include "agents/task_agent.hpp"
#include "core/i_agent_registry.hpp"
#include "core/i_message_router.hpp"
#include "core/i_scheduler_integration.hpp"
#include "core/message_bus.hpp"
#include "test_utilities.hpp"

using namespace keystone;
using namespace keystone::core;
using namespace keystone::agents;
using namespace keystone::test;

/**
 * @brief Test: Register agents using IAgentRegistry interface
 *
 * Demonstrates that code needing only registration should use IAgentRegistry,
 * not the full MessageBus.
 */
TEST(InterfaceSegregation, RegistryInterfaceOnly) {
  MessageBus bus;
  IAgentRegistry& registry = bus;  // Use specific interface

  auto agent1 = std::make_shared<TaskAgent>("agent_1");
  auto agent2 = std::make_shared<TaskAgent>("agent_2");

  // Use IAgentRegistry methods only
  registerAgent(registry, agent1);
  registerAgent(registry, agent2);

  EXPECT_TRUE(registry.hasAgent("agent_1"));
  EXPECT_TRUE(registry.hasAgent("agent_2"));

  auto agents = registry.listAgents();
  EXPECT_EQ(agents.size(), 2u);

  registry.unregisterAgent("agent_1");
  EXPECT_FALSE(registry.hasAgent("agent_1"));
  EXPECT_TRUE(registry.hasAgent("agent_2"));
}

/**
 * @brief Test: Route messages using IMessageRouter interface
 *
 * Demonstrates that code needing only routing should use IMessageRouter,
 * not the full MessageBus.
 */
TEST(InterfaceSegregation, RouterInterfaceOnly) {
  MessageBus bus;
  IMessageRouter* router = &bus;   // Use specific interface
  IAgentRegistry& registry = bus;  // Separate interface for registration

  auto agent = std::make_shared<TaskAgent>("test_agent");

  // Use IAgentRegistry for registration
  registerAgent(registry, agent);

  // Use IMessageRouter for agent setup
  setupAgentRouter(*agent, router);

  // Use IMessageRouter for message routing
  auto msg = KeystoneMessage::create("sender", "test_agent", "echo hello");
  EXPECT_TRUE(router->routeMessage(msg));

  auto received = agent->getMessage();
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->command, "echo hello");
}

/**
 * @brief Test: Batch registration using utilities
 *
 * Demonstrates using utility functions for common patterns.
 */
TEST(InterfaceSegregation, BatchRegistrationUtility) {
  MessageBus bus;

  std::vector<std::shared_ptr<TaskAgent>> agents;
  for (int32_t i = 0; i < 5; ++i) {
    agents.push_back(std::make_shared<TaskAgent>("agent_" + std::to_string(i)));
  }

  // Use utility function that operates on IAgentRegistry
  setupTestEnvironment(bus, agents);

  // Verify all registered
  std::vector<std::string> agent_ids;
  for (const auto& agent : agents) {
    agent_ids.push_back(agent->getAgentId());
  }

  EXPECT_TRUE(verifyAllRegistered(bus, agent_ids));
}

/**
 * @brief Test: Agent only has access to routing
 *
 * Demonstrates that agents (via AgentCore) only have access to IMessageRouter,
 * not the full MessageBus capabilities.
 */
TEST(InterfaceSegregation, AgentRestrictedToRouting) {
  MessageBus bus;
  auto agent = std::make_shared<TaskAgent>("test_agent");

  // Register using IAgentRegistry
  IAgentRegistry& registry = bus;
  registerAgent(registry, agent);

  // Agent gets IMessageRouter* (not MessageBus*)
  IMessageRouter* router = &bus;
  setupAgentRouter(*agent, router);

  // Agent can send messages (routing)
  auto msg = KeystoneMessage::create("test_agent", "other_agent", "test");
  agent->sendMessage(msg);  // Uses IMessageRouter internally

  // But agent CANNOT call:
  // - bus.registerAgent() - requires IAgentRegistry
  // - bus.setScheduler() - requires ISchedulerIntegration
  // - bus.hasAgent() - requires IAgentRegistry
  // This enforces Interface Segregation at compile time
}

/**
 * @brief Test: Different code paths use different interfaces
 *
 * Demonstrates a realistic scenario where different parts of code
 * need different MessageBus capabilities.
 */
TEST(InterfaceSegregation, SeparateInterfaceUsage) {
  MessageBus bus;

  // Setup code uses IAgentRegistry
  auto setupAgentsForTest = [](IAgentRegistry& registry) {
    auto agent1 = std::make_shared<TaskAgent>("agent_1");
    auto agent2 = std::make_shared<TaskAgent>("agent_2");
    registerAgent(registry, agent1);
    registerAgent(registry, agent2);
  };

  // Routing code uses IMessageRouter
  auto routeTestMessage = [](IMessageRouter& router) {
    auto msg = KeystoneMessage::create("sender", "agent_1", "test");
    return router.routeMessage(msg);
  };

  // Execute
  IAgentRegistry& registry = bus;
  setupAgentsForTest(registry);

  IMessageRouter& router = bus;
  EXPECT_TRUE(routeTestMessage(router));
}

/**
 * @brief Test: Compile-time interface enforcement
 *
 * This test demonstrates that the interface segregation is enforced
 * at compile time, not runtime.
 */
TEST(InterfaceSegregation, CompileTimeEnforcement) {
  MessageBus bus;
  IMessageRouter* router = &bus;

  // This compiles - routing is part of IMessageRouter
  auto msg = KeystoneMessage::create("sender", "receiver", "test");
  router->routeMessage(msg);

  // These would NOT compile (uncomment to verify):
  // router->registerAgent("id", agent);  // ERROR: no such method
  // router->setScheduler(&scheduler);    // ERROR: no such method
  // router->hasAgent("id");              // ERROR: no such method

  // To access those methods, must use appropriate interface:
  IAgentRegistry* registry = &bus;
  // Now registry->registerAgent() works
  (void)registry;  // Suppress unused warning
}

#pragma GCC diagnostic pop
