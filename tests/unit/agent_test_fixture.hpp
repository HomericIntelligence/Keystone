#pragma once

#include <gtest/gtest.h>

#include <memory>

#include "core/message_bus.hpp"
#include "mocks/mock_agents.hpp"
#include "mocks/mock_message_bus.hpp"

namespace keystone::test {

/**
 * @brief Base fixture for agent tests with real MessageBus
 *
 * Provides a real MessageBus instance for integration-style unit tests
 * that verify agent behavior with actual message routing.
 */
class AgentTestFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create real MessageBus for integration-style tests
    bus_ = std::make_unique<core::MessageBus>();
  }

  void TearDown() override { bus_.reset(); }

  std::unique_ptr<core::MessageBus> bus_;
};

/**
 * @brief Fixture with mocked MessageBus components
 *
 * Provides mock interfaces for testing agent behavior in complete isolation.
 * Use this when you need fine-grained control over MessageBus behavior.
 */
class MockedAgentTestFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    mock_router_ = std::make_shared<MockMessageRouter>();
    mock_registry_ = std::make_shared<MockAgentRegistry>();
    mock_scheduler_integration_ = std::make_shared<MockSchedulerIntegration>();
  }

  void TearDown() override {
    mock_router_.reset();
    mock_registry_.reset();
    mock_scheduler_integration_.reset();
  }

  std::shared_ptr<MockMessageRouter> mock_router_;
  std::shared_ptr<MockAgentRegistry> mock_registry_;
  std::shared_ptr<MockSchedulerIntegration> mock_scheduler_integration_;
};

}  // namespace keystone::test
