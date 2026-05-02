#pragma once

#include <gmock/gmock.h>

#include "core/i_agent_registry.hpp"
#include "core/i_message_router.hpp"
#include "core/i_scheduler_integration.hpp"

namespace keystone::test {

/**
 * @brief Mock for IAgentRegistry interface
 *
 * Enables testing agent registration/discovery in isolation.
 */
class MockAgentRegistry : public core::IAgentRegistry {
 public:
  MOCK_METHOD(void, registerAgent,
              (const std::string& id, std::shared_ptr<agents::AgentCore> agent),
              (override));
  MOCK_METHOD(void, unregisterAgent, (const std::string& id), (override));
  MOCK_METHOD(bool, hasAgent, (const std::string& id), (const, override));
  MOCK_METHOD(std::vector<std::string>, listAgents, (), (const, override));
};

/**
 * @brief Mock for IMessageRouter interface
 *
 * Enables testing message routing behavior in isolation.
 */
class MockMessageRouter : public core::IMessageRouter {
 public:
  MOCK_METHOD(bool, routeMessage, (const core::KeystoneMessage& msg),
              (override));
};

/**
 * @brief Mock for ISchedulerIntegration interface
 *
 * Enables testing scheduler integration in isolation.
 */
class MockSchedulerIntegration : public core::ISchedulerIntegration {
 public:
  MOCK_METHOD(void, setScheduler,
              (concurrency::WorkStealingScheduler * scheduler), (override));
  MOCK_METHOD(concurrency::WorkStealingScheduler*, getScheduler, (),
              (const, override));
};

/**
 * @brief Combined mock for complete MessageBus (legacy tests)
 *
 * Provides all three interfaces in a single mock for tests
 * that need full MessageBus functionality.
 */
class MockMessageBus : public MockAgentRegistry,
                       public MockMessageRouter,
                       public MockSchedulerIntegration {
 public:
  // Inherits all mock methods from interfaces
};

}  // namespace keystone::test
