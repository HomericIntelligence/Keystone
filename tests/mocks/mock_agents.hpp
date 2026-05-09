#pragma once

#include <gmock/gmock.h>

#include "agents/agent_core.hpp"
#include "agents/async_agent.hpp"
#include "agents/task_agent.hpp"
#include "concurrency/task.hpp"

namespace keystone::test {

/**
 * @brief Mock AgentCore for testing base functionality
 *
 * Provides controllable behavior for inbox/outbox operations.
 */
class MockAgentCore : public agents::AgentCore {
 public:
  explicit MockAgentCore(const std::string& id) : AgentCore(id) {}

  // Expose protected methods for testing
  using AgentCore::getMessage;
  using AgentCore::receiveMessage;
  using AgentCore::sendMessage;
  using AgentCore::setMessageBus;
};

/**
 * @brief Mock AsyncAgent for testing async behavior
 *
 * Allows mocking processMessage() for async agents.
 */
class MockAsyncAgent : public agents::AsyncAgent {
 public:
  explicit MockAsyncAgent(const std::string& id) : AsyncAgent(id) {}

  MOCK_METHOD(concurrency::Task<core::Response>, processMessage,
              (const core::KeystoneMessage& msg), (override));

  // Expose protected methods for testing
  using AsyncAgent::getMessage;
  using AsyncAgent::receiveMessage;
  using AsyncAgent::sendMessage;
  using AsyncAgent::setMessageBus;
};

/**
 * @brief Mock TaskAgent for testing task execution
 *
 * Allows mocking processMessage() for TaskAgent-specific behavior.
 */
class MockTaskAgent : public agents::TaskAgent {
 public:
  explicit MockTaskAgent(const std::string& id) : TaskAgent(id) {}

  MOCK_METHOD(concurrency::Task<core::Response>, processMessage,
              (const core::KeystoneMessage& msg), (override));
};

}  // namespace keystone::test
