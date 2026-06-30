/**
 * @file test_message_sink.cpp
 * @brief Decoupling test: MessageBus routes to a NON-agent IMessageSink.
 *
 * Part A of the "Keystone pure transport" effort: the transport core
 * (MessageBus / IAgentRegistry) no longer depends on agents::AgentCore. It only
 * depends on core::IMessageSink (a single receiveMessage method).
 *
 * This test deliberately uses NO agent types. It defines a minimal StubSink
 * that implements core::IMessageSink, registers it on a MessageBus, routes a
 * message, and asserts delivery. This proves transport works with an arbitrary
 * non-agent sink and exercises the decoupled path end-to-end.
 */

#include "core/message.hpp"
#include "core/message_bus.hpp"
#include "core/message_sink.hpp"

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using namespace keystone::core;

namespace {

/**
 * @brief Minimal non-agent message sink for transport decoupling tests.
 *
 * Implements only the IMessageSink contract. It is intentionally NOT an agent:
 * no inbox, no lifecycle, no scheduler, no children. If this compiles and
 * routes, the transport core is fully decoupled from the agent layer.
 */
struct StubSink : public IMessageSink {
  std::vector<KeystoneMessage> got;
  void receiveMessage(const KeystoneMessage& msg) override { got.push_back(msg); }
};

}  // namespace

/**
 * @brief Route a message to a registered non-agent IMessageSink (sync path).
 */
TEST(MessageSink, RoutesToNonAgentSink) {
  MessageBus bus;
  auto sink = std::make_shared<StubSink>();

  // Register the bare sink via the IAgentRegistry interface (no agent type).
  EXPECT_NO_THROW(bus.registerAgent("stub_sink", sink));
  EXPECT_TRUE(bus.hasAgent("stub_sink"));

  auto msg = KeystoneMessage::create("sender", "stub_sink", "ping");
  EXPECT_TRUE(bus.routeMessage(msg));

  ASSERT_EQ(sink->got.size(), 1u);
  EXPECT_EQ(sink->got[0].sender_id, "sender");
  EXPECT_EQ(sink->got[0].receiver_id, "stub_sink");
}

/**
 * @brief Routing to an unregistered receiver returns false (no sink involved).
 */
TEST(MessageSink, RouteToUnregisteredReturnsFalse) {
  MessageBus bus;
  auto msg = KeystoneMessage::create("sender", "absent", "ping");
  EXPECT_FALSE(bus.routeMessage(msg));
}

/**
 * @brief Unregistering a non-agent sink stops delivery.
 */
TEST(MessageSink, UnregisterStopsDelivery) {
  MessageBus bus;
  auto sink = std::make_shared<StubSink>();

  bus.registerAgent("stub_sink", sink);
  bus.unregisterAgent("stub_sink");
  EXPECT_FALSE(bus.hasAgent("stub_sink"));

  auto msg = KeystoneMessage::create("sender", "stub_sink", "ping");
  EXPECT_FALSE(bus.routeMessage(msg));
  EXPECT_TRUE(sink->got.empty());
}
