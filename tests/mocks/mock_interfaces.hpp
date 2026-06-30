#pragma once

#include "core/i_agent_registry.hpp"
#include "core/i_message_router.hpp"
#include "core/i_scheduler_integration.hpp"
#include "core/message.hpp"

#include <memory>
#include <string>
#include <vector>

#include <gmock/gmock.h>

// Forward declarations
namespace keystone {
namespace concurrency {
class WorkStealingScheduler;
}
}  // namespace keystone

namespace keystone::test {

/**
 * @brief Mock implementation of IAgentRegistry for testing
 *
 * Enables unit tests to verify agent registration logic
 * independently of the real MessageBus.
 */
class MockAgentRegistry : public core::IAgentRegistry {
 public:
  MockAgentRegistry() = default;
  ~MockAgentRegistry() override = default;

  MOCK_METHOD(void,
              registerAgent,
              (const std::string& agent_id, std::shared_ptr<core::IMessageSink> agent),
              (override));

  MOCK_METHOD(void, unregisterAgent, (const std::string& agent_id), (override));

  MOCK_METHOD(bool, hasAgent, (const std::string& agent_id), (const, override));

  MOCK_METHOD(std::vector<std::string>, listAgents, (), (const, override));
};

/**
 * @brief Mock implementation of IMessageRouter for testing
 *
 * Enables unit tests to verify message routing logic
 * independently of the real MessageBus.
 */
class MockMessageRouter : public core::IMessageRouter {
 public:
  MockMessageRouter() = default;
  ~MockMessageRouter() override = default;

  MOCK_METHOD(bool, routeMessage, (const core::KeystoneMessage& msg), (override));
};

/**
 * @brief Mock implementation of ISchedulerIntegration for testing
 *
 * Enables unit tests to verify scheduler integration
 * independently of the real MessageBus.
 */
class MockSchedulerIntegration : public core::ISchedulerIntegration {
 public:
  MockSchedulerIntegration() = default;
  ~MockSchedulerIntegration() override = default;

  MOCK_METHOD(void, setScheduler, (concurrency::WorkStealingScheduler * scheduler), (override));

  MOCK_METHOD(concurrency::WorkStealingScheduler*, getScheduler, (), (const, override));
};

/**
 * @brief Combined mock for testing all three interfaces together
 *
 * Implements all three interfaces in a single class for comprehensive
 * integration testing.
 */
class MockMessageBus : public core::IAgentRegistry,
                       public core::IMessageRouter,
                       public core::ISchedulerIntegration {
 public:
  MockMessageBus() = default;
  ~MockMessageBus() override = default;

  // IAgentRegistry interface
  MOCK_METHOD(void,
              registerAgent,
              (const std::string& agent_id, std::shared_ptr<core::IMessageSink> agent),
              (override));

  MOCK_METHOD(void, unregisterAgent, (const std::string& agent_id), (override));

  MOCK_METHOD(bool, hasAgent, (const std::string& agent_id), (const, override));

  MOCK_METHOD(std::vector<std::string>, listAgents, (), (const, override));

  // IMessageRouter interface
  MOCK_METHOD(bool, routeMessage, (const core::KeystoneMessage& msg), (override));

  // ISchedulerIntegration interface
  MOCK_METHOD(void, setScheduler, (concurrency::WorkStealingScheduler * scheduler), (override));

  MOCK_METHOD(concurrency::WorkStealingScheduler*, getScheduler, (), (const, override));
};

}  // namespace keystone::test
