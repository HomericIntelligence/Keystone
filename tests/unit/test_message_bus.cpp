/**
 * @file test_message_bus.cpp
 * @brief Unit tests for MessageBus registration, routing, NATS forwarding,
 *        and scheduler integration.
 *
 * MessageBus is a pure in-memory transport hub — these tests use only a
 * minimal non-agent IMessageSink stub and (optionally) a real
 * WorkStealingScheduler. No live NATS broker is required: the NATS-forwarding
 * path is exercised via an injected publisher callback.
 */

#include "concurrency/work_stealing_scheduler.hpp"
#include "core/config.hpp"
#include "core/message.hpp"
#include "core/message_bus.hpp"
#include "core/message_sink.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

using namespace keystone::core;
using namespace std::chrono_literals;

namespace {

/// Minimal non-agent sink that records and counts received messages.
struct CountingSink : public IMessageSink {
  std::atomic<int> count{0};
  std::mutex mu;
  std::condition_variable cv;

  void receiveMessage(const KeystoneMessage& /*msg*/) override {
    {
      std::lock_guard<std::mutex> lock(mu);
      count.fetch_add(1, std::memory_order_relaxed);
    }
    cv.notify_all();
  }

  void waitForCount(int expected) {
    std::unique_lock<std::mutex> lock(mu);
    cv.wait_for(lock, 2s, [&] { return count.load(std::memory_order_relaxed) >= expected; });
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// Registration error paths
// ---------------------------------------------------------------------------

TEST(MessageBusRegistration, RegisterNullAgentThrows) {
  MessageBus bus;
  EXPECT_THROW(bus.registerAgent("a1", nullptr), std::invalid_argument);
}

TEST(MessageBusRegistration, DuplicateRegistrationThrows) {
  MessageBus bus;
  auto sink = std::make_shared<CountingSink>();
  EXPECT_NO_THROW(bus.registerAgent("a1", sink));
  EXPECT_THROW(bus.registerAgent("a1", sink), std::runtime_error);
}

TEST(MessageBusRegistration, InvalidAgentIdTokenThrows) {
  MessageBus bus;
  auto sink = std::make_shared<CountingSink>();
  // Path-traversal / injection characters are rejected (Issue #113).
  EXPECT_ANY_THROW(bus.registerAgent("../evil", sink));
}

// ---------------------------------------------------------------------------
// Unregister / listing / discovery
// ---------------------------------------------------------------------------

TEST(MessageBusRegistration, UnregisterUnknownAgentIsNoOp) {
  MessageBus bus;
  // Never registered → tryGetId returns nullopt → early return, no throw.
  EXPECT_NO_THROW(bus.unregisterAgent("never-seen"));
  EXPECT_FALSE(bus.hasAgent("never-seen"));
}

TEST(MessageBusRegistration, HasAgentFalseForUnknown) {
  MessageBus bus;
  EXPECT_FALSE(bus.hasAgent("nope"));
}

TEST(MessageBusRegistration, ListAgentsReturnsRegisteredIds) {
  MessageBus bus;
  bus.registerAgent("a1", std::make_shared<CountingSink>());
  bus.registerAgent("a2", std::make_shared<CountingSink>());
  bus.registerAgent("a3", std::make_shared<CountingSink>());

  auto ids = bus.listAgents();
  EXPECT_EQ(ids.size(), 3u);
  EXPECT_NE(std::find(ids.begin(), ids.end(), "a1"), ids.end());
  EXPECT_NE(std::find(ids.begin(), ids.end(), "a2"), ids.end());
  EXPECT_NE(std::find(ids.begin(), ids.end(), "a3"), ids.end());
}

TEST(MessageBusRegistration, ListAgentsEmptyInitially) {
  MessageBus bus;
  EXPECT_TRUE(bus.listAgents().empty());
}

// ---------------------------------------------------------------------------
// Scheduler integration
// ---------------------------------------------------------------------------

TEST(MessageBusScheduler, SchedulerDefaultsToNull) {
  MessageBus bus;
  EXPECT_EQ(bus.getScheduler(), nullptr);
}

TEST(MessageBusScheduler, SetAndGetScheduler) {
  MessageBus bus;
  keystone::concurrency::WorkStealingScheduler sched(2);
  bus.setScheduler(&sched);
  EXPECT_EQ(bus.getScheduler(), &sched);

  bus.setScheduler(nullptr);
  EXPECT_EQ(bus.getScheduler(), nullptr);
}

TEST(MessageBusScheduler, AsyncRoutingSubmitsToScheduler) {
  MessageBus bus;
  keystone::concurrency::WorkStealingScheduler sched(2);
  sched.start();
  bus.setScheduler(&sched);

  auto sink = std::make_shared<CountingSink>();
  bus.registerAgent("worker", sink);

  auto msg = KeystoneMessage::create("client", "worker", "task");
  EXPECT_TRUE(bus.routeMessage(msg));

  sink->waitForCount(1);
  EXPECT_EQ(sink->count.load(), 1);
}

// ---------------------------------------------------------------------------
// NATS forwarding (off-host path) — no broker, injected publisher callback
// ---------------------------------------------------------------------------

TEST(MessageBusNats, PublisherDefaultsToNull) {
  MessageBus bus;
  EXPECT_FALSE(static_cast<bool>(bus.getNatsPublisher()));
}

TEST(MessageBusNats, SetAndGetPublisher) {
  MessageBus bus;
  bus.setNatsPublisher([](std::string_view, std::span<const std::byte>) {});
  EXPECT_TRUE(static_cast<bool>(bus.getNatsPublisher()));
}

TEST(MessageBusNats, UnregisteredReceiverForwardsToPublisher) {
  MessageBus bus;

  std::string captured_subject;
  size_t captured_len = 0;
  bus.setNatsPublisher([&](std::string_view subject, std::span<const std::byte> payload) {
    captured_subject = std::string(subject);
    captured_len = payload.size();
  });

  // Receiver was never interned → first forwarding branch.
  auto msg = KeystoneMessage::create("client", "remote-agent", "task");
  EXPECT_FALSE(bus.routeMessage(msg));

  EXPECT_EQ(captured_subject, "hi.agents.remote-agent");
  EXPECT_GT(captured_len, 0u);
}

TEST(MessageBusNats, InternedButUnregisteredReceiverForwards) {
  MessageBus bus;

  int publish_calls = 0;
  bus.setNatsPublisher([&](std::string_view, std::span<const std::byte>) { ++publish_calls; });

  auto sink = std::make_shared<CountingSink>();
  // Register then unregister so the id stays interned but has no live sink,
  // exercising the second forwarding branch (interned, agents_.find fails).
  bus.registerAgent("transient", sink);
  bus.unregisterAgent("transient");

  auto msg = KeystoneMessage::create("client", "transient", "task");
  EXPECT_FALSE(bus.routeMessage(msg));
  EXPECT_EQ(publish_calls, 1);
}

TEST(MessageBusNats, UnregisteredReceiverWithoutPublisherReturnsFalse) {
  MessageBus bus;
  // No publisher set → pub is null → no forwarding, just returns false.
  auto msg = KeystoneMessage::create("client", "remote-agent", "task");
  EXPECT_FALSE(bus.routeMessage(msg));
}

// ---------------------------------------------------------------------------
// Maximum agent limit (DoS guard, FIX P2-10)
// ---------------------------------------------------------------------------

TEST(MessageBusRegistration, EnforcesMaxAgentLimit) {
  MessageBus bus;
  auto sink = std::make_shared<CountingSink>();

  // Fill to the limit; all shall succeed.
  for (size_t i = 0; i < Config::MAX_AGENTS; ++i) {
    bus.registerAgent("agent-" + std::to_string(i), sink);
  }

  // One more must be rejected.
  EXPECT_THROW(bus.registerAgent("one-too-many", sink), std::runtime_error);
}
