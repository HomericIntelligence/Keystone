/**
 * @file test_message_bus.cpp
 * @brief Unit tests for MessageBus
 *
 * FIX ISP (Issue #46): MessageBus implements three interfaces:
 * - IAgentRegistry: Registration/discovery (lines 21-119)
 * - IMessageRouter: Message routing (lines 74-99)
 * - ISchedulerIntegration: Scheduler setup (tested elsewhere)
 *
 * These tests validate the complete MessageBus implementation,
 * which provides all three interfaces in a single class.
 */

#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "agents/agent_core.hpp"
#include "core/message_bus.hpp"

using namespace keystone::core;
using namespace keystone::agents;

// Minimal concrete AgentCore subclass for testing transport behavior.
// The hierarchy agents (TaskAgent, ChiefArchitectAgent, etc.) were extracted
// to ProjectAgamemnon per ADR-015; tests use this stub instead.
class StubAgent : public AgentCore {
 public:
  explicit StubAgent(const std::string& id) : AgentCore(id) {}
};

/**
 * @brief Test: Register a single agent (IAgentRegistry interface)
 */
TEST(MessageBus, RegisterAgent) {
  MessageBus bus;
  auto agent = std::make_shared<StubAgent>("test_1");

  EXPECT_NO_THROW(bus.registerAgent(agent->getAgentId(), agent));
  EXPECT_TRUE(bus.hasAgent("test_1"));
}

/**
 * @brief Test: Registering duplicate agent throws (IAgentRegistry interface)
 */
TEST(MessageBus, RegisterDuplicateAgentThrows) {
  MessageBus bus;
  auto agent1 = std::make_shared<StubAgent>("test_1");
  auto agent2 = std::make_shared<StubAgent>("test_1");  // Same ID

  bus.registerAgent(agent1->getAgentId(), agent1);
  EXPECT_THROW(bus.registerAgent(agent2->getAgentId(), agent2),
               std::runtime_error);
}

/**
 * @brief Test: Register null agent throws
 */
TEST(MessageBus, RegisterNullAgentThrows) {
  MessageBus bus;
  EXPECT_THROW(bus.registerAgent("null_agent", nullptr), std::invalid_argument);
}

/**
 * @brief Test: Unregister an agent
 */
TEST(MessageBus, UnregisterAgent) {
  MessageBus bus;
  auto agent = std::make_shared<StubAgent>("test_1");

  bus.registerAgent(agent->getAgentId(), agent);
  EXPECT_TRUE(bus.hasAgent("test_1"));

  bus.unregisterAgent("test_1");
  EXPECT_FALSE(bus.hasAgent("test_1"));
}

/**
 * @brief Test: Unregister non-existent agent (should not throw)
 */
TEST(MessageBus, UnregisterNonExistentAgent) {
  MessageBus bus;
  EXPECT_NO_THROW(bus.unregisterAgent("nonexistent"));
}

/**
 * @brief Test: Route message to registered agent (IMessageRouter interface)
 */
TEST(MessageBus, RouteMessageToRegisteredAgent) {
  MessageBus bus;
  auto agent = std::make_shared<StubAgent>("test_1");
  agent->setMessageBus(&bus);

  bus.registerAgent(agent->getAgentId(), agent);

  auto msg = KeystoneMessage::create("sender", "test_1", "echo hello");
  EXPECT_TRUE(bus.routeMessage(msg));

  // Agent should have received the message
  auto received = agent->getMessage();
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->sender_id, "sender");
  EXPECT_EQ(received->receiver_id, "test_1");
}

/**
 * @brief Test: Route message to unregistered agent fails
 */
TEST(MessageBus, RouteMessageToUnregisteredAgentFails) {
  MessageBus bus;

  auto msg = KeystoneMessage::create("sender", "nonexistent", "echo hello");
  EXPECT_FALSE(bus.routeMessage(msg));
}

/**
 * @brief Test: List all registered agents
 */
TEST(MessageBus, ListAgents) {
  MessageBus bus;
  auto agent1 = std::make_shared<StubAgent>("agent_1");
  auto agent2 = std::make_shared<StubAgent>("agent_2");
  auto agent3 = std::make_shared<StubAgent>("agent_3");

  bus.registerAgent(agent1->getAgentId(), agent1);
  bus.registerAgent(agent2->getAgentId(), agent2);
  bus.registerAgent(agent3->getAgentId(), agent3);

  auto agents = bus.listAgents();
  EXPECT_EQ(agents.size(), 3u);
  EXPECT_TRUE(std::find(agents.begin(), agents.end(), "agent_1") !=
              agents.end());
  EXPECT_TRUE(std::find(agents.begin(), agents.end(), "agent_2") !=
              agents.end());
  EXPECT_TRUE(std::find(agents.begin(), agents.end(), "agent_3") !=
              agents.end());
}

/**
 * @brief Test: Thread safety - concurrent registration
 */
TEST(MessageBus, ThreadSafetyConcurrentRegistration) {
  MessageBus bus;
  std::vector<std::shared_ptr<StubAgent>> agents;

  // Create agents
  for (int32_t i = 0; i < 10; ++i) {
    agents.push_back(std::make_shared<StubAgent>("agent_" + std::to_string(i)));
  }

  // Concurrent registration
  std::vector<std::thread> threads;
  for (int32_t i = 0; i < 10; ++i) {
    threads.emplace_back([&bus, &agents, i]() {
      bus.registerAgent(agents[i]->getAgentId(), agents[i]);
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // All agents should be registered
  EXPECT_EQ(bus.listAgents().size(), 10u);
  for (int32_t i = 0; i < 10; ++i) {
    EXPECT_TRUE(bus.hasAgent("agent_" + std::to_string(i)));
  }
}

/**
 * @brief Test: Thread safety - concurrent message routing
 */
TEST(MessageBus, ThreadSafetyConcurrentRouting) {
  MessageBus bus;
  std::vector<std::shared_ptr<StubAgent>> agents;

  // Create and register 10 agents
  for (int32_t i = 0; i < 10; ++i) {
    auto agent = std::make_shared<StubAgent>("agent_" + std::to_string(i));
    agent->setMessageBus(&bus);
    bus.registerAgent(agent->getAgentId(), agent);
    agents.push_back(agent);
  }

  // Concurrent message routing
  std::vector<std::thread> threads;
  for (int32_t i = 0; i < 10; ++i) {
    threads.emplace_back([&bus, i]() {
      for (int32_t j = 0; j < 100; ++j) {
        auto msg = KeystoneMessage::create(
            "sender", "agent_" + std::to_string(i), "test");
        bus.routeMessage(msg);
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // All agents should have received 100 messages
  for (const auto& agent : agents) {
    int32_t count = 0;
    while (agent->getMessage().has_value()) {
      ++count;
    }
    EXPECT_EQ(count, 100);
  }
}

/**
 * @brief Test: Bidirectional message flow
 */
TEST(MessageBus, BidirectionalMessageFlow) {
  MessageBus bus;
  auto chief = std::make_shared<StubAgent>("chief");
  auto task = std::make_shared<StubAgent>("task");

  // Register and configure
  bus.registerAgent(chief->getAgentId(), chief);
  bus.registerAgent(task->getAgentId(), task);
  chief->setMessageBus(&bus);
  task->setMessageBus(&bus);

  // Chief sends to Task
  auto msg1 = KeystoneMessage::create("chief", "task", "echo test");
  EXPECT_TRUE(bus.routeMessage(msg1));

  // Task receives
  auto received1 = task->getMessage();
  ASSERT_TRUE(received1.has_value());
  EXPECT_EQ(received1->sender_id, "chief");

  // Task sends back to Chief
  auto msg2 = KeystoneMessage::create("task", "chief", "response", "result");
  EXPECT_TRUE(bus.routeMessage(msg2));

  // Chief receives
  auto received2 = chief->getMessage();
  ASSERT_TRUE(received2.has_value());
  EXPECT_EQ(received2->sender_id, "task");
  EXPECT_EQ(received2->payload.value_or(""), "result");
}
