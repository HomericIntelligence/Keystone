/**
 * @file test_message_bus_async.cpp
 * @brief Tests for MessageBus with WorkStealingScheduler integration
 */

// KeystoneMessage::command is [[deprecated]]; test files intentionally access
// it to verify backward-compat behaviour.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "agents/async_agent.hpp"
#include "concurrency/work_stealing_scheduler.hpp"
#include "core/message_bus.hpp"

using namespace keystone::core;
using namespace keystone::agents;
using namespace keystone::concurrency;

// Simple test agent that counts received messages
class CountingAgent : public AsyncAgent {
 public:
  explicit CountingAgent(const std::string& agent_id)
      : AsyncAgent(agent_id),
        count_(std::make_shared<std::atomic<int32_t>>(0)) {}

  Task<Response> processMessage(const KeystoneMessage& msg) override {
    count_->fetch_add(1);
    Response resp;
    resp.msg_id = msg.msg_id;
    resp.sender_id = agent_id_;
    resp.receiver_id = msg.sender_id;
    resp.status = Response::Status::Success;
    resp.result = "OK";
    resp.timestamp = std::chrono::system_clock::now();
    co_return resp;
  }

  int32_t getCount() const { return count_->load(); }

 private:
  std::shared_ptr<std::atomic<int32_t>> count_;
};

// Test: MessageBus without scheduler (synchronous routing)
TEST(MessageBusAsyncTest, SynchronousRoutingWithoutScheduler) {
  MessageBus bus;
  auto agent1 = std::make_shared<CountingAgent>("agent1");
  auto agent2 = std::make_shared<CountingAgent>("agent2");

  bus.registerAgent("agent1", agent1);
  bus.registerAgent("agent2", agent2);

  agent1->setMessageBus(&bus);
  agent2->setMessageBus(&bus);

  // No scheduler set - should use synchronous routing
  EXPECT_EQ(bus.getScheduler(), nullptr);

  // Send messages
  auto msg1 = KeystoneMessage::create("agent1", "agent2", "test1");
  auto msg2 = KeystoneMessage::create("agent1", "agent2", "test2");

  bus.routeMessage(msg1);
  bus.routeMessage(msg2);

  // Messages delivered synchronously - inbox should have them immediately
  auto received1 = agent2->getMessage();
  auto received2 = agent2->getMessage();

  EXPECT_TRUE(received1.has_value());
  EXPECT_TRUE(received2.has_value());
  EXPECT_EQ(received1->command, "test1");
  EXPECT_EQ(received2->command, "test2");
}

// Test: MessageBus with scheduler (async routing)
TEST(MessageBusAsyncTest, AsyncRoutingWithScheduler) {
  WorkStealingScheduler scheduler(2);
  scheduler.start();

  MessageBus bus;
  bus.setScheduler(&scheduler);

  auto agent1 = std::make_shared<CountingAgent>("agent1");
  auto agent2 = std::make_shared<CountingAgent>("agent2");

  bus.registerAgent("agent1", agent1);
  bus.registerAgent("agent2", agent2);

  agent1->setMessageBus(&bus);
  agent2->setMessageBus(&bus);

  // Scheduler should be set
  EXPECT_EQ(bus.getScheduler(), &scheduler);

  // Send messages - they will be delivered asynchronously
  for (int32_t i = 0; i < 10; ++i) {
    auto msg = KeystoneMessage::create("agent1", "agent2",
                                       "test_" + std::to_string(i));
    bus.routeMessage(msg);
  }

  // Wait for async delivery
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Check that messages were received
  int32_t received_count = 0;
  while (auto msg = agent2->getMessage()) {
    received_count++;
  }

  EXPECT_EQ(received_count, 10);

  scheduler.shutdown();
}

// Test: Multiple agents with scheduler
TEST(MessageBusAsyncTest, MultipleAgentsAsyncRouting) {
  WorkStealingScheduler scheduler(4);
  scheduler.start();

  MessageBus bus;
  bus.setScheduler(&scheduler);

  auto agent1 = std::make_shared<CountingAgent>("agent1");
  auto agent2 = std::make_shared<CountingAgent>("agent2");
  auto agent3 = std::make_shared<CountingAgent>("agent3");

  bus.registerAgent("agent1", agent1);
  bus.registerAgent("agent2", agent2);
  bus.registerAgent("agent3", agent3);

  agent1->setMessageBus(&bus);
  agent2->setMessageBus(&bus);
  agent3->setMessageBus(&bus);

  // Send messages to multiple agents
  for (int32_t i = 0; i < 20; ++i) {
    auto msg2 =
        KeystoneMessage::create("agent1", "agent2", "msg" + std::to_string(i));
    auto msg3 =
        KeystoneMessage::create("agent1", "agent3", "msg" + std::to_string(i));
    bus.routeMessage(msg2);
    bus.routeMessage(msg3);
  }

  // Wait for async delivery
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Check both agents received messages
  int32_t count2 = 0, count3 = 0;
  while (agent2->getMessage()) count2++;
  while (agent3->getMessage()) count3++;

  EXPECT_EQ(count2, 20);
  EXPECT_EQ(count3, 20);

  scheduler.shutdown();
}

// Test: Switching from sync to async
TEST(MessageBusAsyncTest, SwitchFromSyncToAsync) {
  MessageBus bus;
  auto agent1 = std::make_shared<CountingAgent>("agent1");
  auto agent2 = std::make_shared<CountingAgent>("agent2");

  bus.registerAgent("agent1", agent1);
  bus.registerAgent("agent2", agent2);

  agent1->setMessageBus(&bus);
  agent2->setMessageBus(&bus);

  // Start with synchronous routing
  EXPECT_EQ(bus.getScheduler(), nullptr);

  auto msg1 = KeystoneMessage::create("agent1", "agent2", "sync_msg");
  bus.routeMessage(msg1);

  // Should be delivered immediately
  EXPECT_TRUE(agent2->getMessage().has_value());

  // Now enable async routing
  WorkStealingScheduler scheduler(2);
  scheduler.start();
  bus.setScheduler(&scheduler);

  auto msg2 = KeystoneMessage::create("agent1", "agent2", "async_msg");
  bus.routeMessage(msg2);

  // Wait for async delivery
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  EXPECT_TRUE(agent2->getMessage().has_value());

  scheduler.shutdown();
}

// Test: High-load async routing
TEST(MessageBusAsyncTest, HighLoadAsyncRouting) {
  WorkStealingScheduler scheduler(4);
  scheduler.start();

  MessageBus bus;
  bus.setScheduler(&scheduler);

  auto agent1 = std::make_shared<CountingAgent>("agent1");
  auto agent2 = std::make_shared<CountingAgent>("agent2");

  bus.registerAgent("agent1", agent1);
  bus.registerAgent("agent2", agent2);

  agent1->setMessageBus(&bus);
  agent2->setMessageBus(&bus);

  // Send many messages
  const int32_t num_messages = 1000;
  for (int32_t i = 0; i < num_messages; ++i) {
    auto msg =
        KeystoneMessage::create("agent1", "agent2", "msg" + std::to_string(i));
    bus.routeMessage(msg);
  }

  // Wait for async delivery
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Count received messages
  int32_t received = 0;
  while (agent2->getMessage()) {
    received++;
  }

  EXPECT_EQ(received, num_messages);

  scheduler.shutdown();
}

// Test: Scheduler shutdown doesn't affect MessageBus
TEST(MessageBusAsyncTest, SchedulerShutdownGraceful) {
  WorkStealingScheduler scheduler(2);
  scheduler.start();

  MessageBus bus;
  bus.setScheduler(&scheduler);

  auto agent1 = std::make_shared<CountingAgent>("agent1");
  auto agent2 = std::make_shared<CountingAgent>("agent2");

  bus.registerAgent("agent1", agent1);
  bus.registerAgent("agent2", agent2);

  agent1->setMessageBus(&bus);
  agent2->setMessageBus(&bus);

  // Send messages
  for (int32_t i = 0; i < 5; ++i) {
    auto msg =
        KeystoneMessage::create("agent1", "agent2", "msg" + std::to_string(i));
    bus.routeMessage(msg);
  }

  // Shutdown scheduler (waits for pending work)
  scheduler.shutdown();

  // All messages should have been delivered before shutdown completed
  int32_t received = 0;
  while (agent2->getMessage()) {
    received++;
  }

  EXPECT_EQ(received, 5);
}

// =============================================================================
// FIX P2-09: Concurrent lifecycle stress test
// =============================================================================

// Test: Concurrent agent registration, unregistration, and message routing
TEST(MessageBusAsyncTest, ConcurrentLifecycleStressTest) {
  WorkStealingScheduler scheduler(4);
  scheduler.start();

  MessageBus bus;
  bus.setScheduler(&scheduler);

  std::atomic<bool> stop{false};
  std::atomic<int32_t> agents_created{0};
  std::atomic<int32_t> agents_destroyed{0};
  std::atomic<int32_t> messages_sent{0};
  std::atomic<int32_t> registration_errors{0};

  // Helper to generate unique agent IDs
  auto get_agent_id = [](int32_t counter) {
    return "stress_agent_" + std::to_string(counter);
  };

  // Thread 1: Register agents
  std::thread register_thread([&]() {
    int32_t counter = 0;
    while (!stop.load(std::memory_order_relaxed)) {
      try {
        auto agent_id = get_agent_id(counter++);
        auto agent = std::make_shared<CountingAgent>(agent_id);
        agent->setMessageBus(&bus);
        bus.registerAgent(agent_id, agent);
        agents_created.fetch_add(1, std::memory_order_relaxed);

        // Small delay to allow interleaving
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      } catch (const std::exception& e) {
        registration_errors.fetch_add(1, std::memory_order_relaxed);
      }
    }
  });

  // Thread 2: Unregister agents
  std::thread unregister_thread([&]() {
    int32_t counter = 0;
    while (!stop.load(std::memory_order_relaxed)) {
      auto agent_id = get_agent_id(counter++);

      // Only unregister if agent exists
      if (bus.hasAgent(agent_id)) {
        bus.unregisterAgent(agent_id);
        agents_destroyed.fetch_add(1, std::memory_order_relaxed);
      }

      std::this_thread::sleep_for(std::chrono::microseconds(150));
    }
  });

  // Thread 3: Route messages to existing agents
  std::thread route_thread([&]() {
    int32_t counter = 0;
    while (!stop.load(std::memory_order_relaxed)) {
      // Get list of registered agents
      auto agents = bus.listAgents();

      if (!agents.empty()) {
        // Pick random agent from list
        size_t idx = counter % agents.size();
        auto target = agents[idx];

        // Send message
        auto msg = KeystoneMessage::create(
            "test_sender", target, "stress_test_" + std::to_string(counter));
        bool routed = bus.routeMessage(msg);
        if (routed) {
          messages_sent.fetch_add(1, std::memory_order_relaxed);
        }
      }

      counter++;
      std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
  });

  // Thread 4: Additional concurrent registration (stress test)
  std::thread register_thread2([&]() {
    int32_t counter = 10000;  // Different range to avoid ID conflicts
    while (!stop.load(std::memory_order_relaxed)) {
      try {
        auto agent_id = get_agent_id(counter++);
        auto agent = std::make_shared<CountingAgent>(agent_id);
        agent->setMessageBus(&bus);
        bus.registerAgent(agent_id, agent);
        agents_created.fetch_add(1, std::memory_order_relaxed);

        std::this_thread::sleep_for(std::chrono::microseconds(120));
      } catch (const std::exception& e) {
        registration_errors.fetch_add(1, std::memory_order_relaxed);
      }
    }
  });

  // Run stress test for 2 seconds
  std::this_thread::sleep_for(std::chrono::seconds(2));
  stop.store(true, std::memory_order_relaxed);

  // Wait for all threads to finish
  register_thread.join();
  unregister_thread.join();
  route_thread.join();
  register_thread2.join();

  // Shutdown scheduler
  scheduler.shutdown();

  // Verify no crashes occurred and operations completed
  EXPECT_GT(agents_created.load(), 0) << "Should have created some agents";
  EXPECT_GT(messages_sent.load(), 0) << "Should have sent some messages";

  // Some registration errors are expected due to duplicate IDs
  // but the system should handle them gracefully
  std::cout << "Stress test results:\n"
            << "  Agents created: " << agents_created.load() << "\n"
            << "  Agents destroyed: " << agents_destroyed.load() << "\n"
            << "  Messages sent: " << messages_sent.load() << "\n"
            << "  Registration errors (expected): "
            << registration_errors.load() << "\n";

  // The fact that we got here without crashing means the test passed
  SUCCEED();
}

// Test: Concurrent unregistration doesn't cause use-after-free
TEST(MessageBusAsyncTest, ConcurrentUnregistrationSafety) {
  WorkStealingScheduler scheduler(2);
  scheduler.start();

  MessageBus bus;
  bus.setScheduler(&scheduler);

  // Create agents
  auto agent1 = std::make_shared<CountingAgent>("agent1");
  auto agent2 = std::make_shared<CountingAgent>("agent2");

  agent1->setMessageBus(&bus);
  agent2->setMessageBus(&bus);

  bus.registerAgent("agent1", agent1);
  bus.registerAgent("agent2", agent2);

  std::atomic<bool> stop{false};

  // Thread 1: Continuously send messages to agent2
  std::thread sender([&]() {
    int32_t counter = 0;
    while (!stop.load()) {
      auto msg = KeystoneMessage::create("agent1", "agent2",
                                         "msg_" + std::to_string(counter++));
      bus.routeMessage(msg);
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  });

  // Thread 2: Unregister and re-register agent2
  std::thread lifecycle([&]() {
    for (int32_t i = 0; i < 10; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));

      // Unregister
      bus.unregisterAgent("agent2");

      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      // Re-register (need to create new agent with same ID)
      auto new_agent2 = std::make_shared<CountingAgent>("agent2");
      new_agent2->setMessageBus(&bus);
      bus.registerAgent("agent2", new_agent2);
      agent2 = new_agent2;  // Update reference
    }
  });

  // Run for 1 second
  std::this_thread::sleep_for(std::chrono::seconds(1));
  stop.store(true);

  sender.join();
  lifecycle.join();

  scheduler.shutdown();

  // If we got here without crashes/use-after-free, test passes
  SUCCEED();
}

// Test: Message routing to non-existent agent (concurrent deletion)
TEST(MessageBusAsyncTest, MessageToDeletedAgentGraceful) {
  WorkStealingScheduler scheduler(2);
  scheduler.start();

  MessageBus bus;
  bus.setScheduler(&scheduler);

  auto agent1 = std::make_shared<CountingAgent>("agent1");
  auto agent2 = std::make_shared<CountingAgent>("agent2");

  agent1->setMessageBus(&bus);
  agent2->setMessageBus(&bus);

  bus.registerAgent("agent1", agent1);
  bus.registerAgent("agent2", agent2);

  // Send messages to agent2
  for (int32_t i = 0; i < 10; ++i) {
    auto msg =
        KeystoneMessage::create("agent1", "agent2", "msg_" + std::to_string(i));
    bus.routeMessage(msg);
  }

  // Immediately unregister agent2 (messages may still be in-flight)
  bus.unregisterAgent("agent2");

  // Try to send more messages (should fail gracefully)
  for (int32_t i = 10; i < 20; ++i) {
    auto msg =
        KeystoneMessage::create("agent1", "agent2", "msg_" + std::to_string(i));
    bool routed = bus.routeMessage(msg);
    // May or may not route depending on timing
    (void)routed;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  scheduler.shutdown();

  // No crash = success
  SUCCEED();
}

#pragma GCC diagnostic pop
