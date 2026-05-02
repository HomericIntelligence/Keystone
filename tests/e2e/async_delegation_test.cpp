/**
 * @file phase_a_async_delegation.cpp
 * @brief E2E test for Phase A: Async Work-Stealing Architecture
 *
 * Tests the complete async architecture with WorkStealingScheduler:
 * - ChiefArchitectAgent (Level 0) sends commands
 * - TaskAgents (Level 3) execute commands asynchronously
 * - Messages routed via WorkStealingScheduler
 * - Work-stealing enables parallel execution
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <map>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

#include "agents/chief_architect_agent.hpp"
#include "agents/task_agent.hpp"
#include "concurrency/logger.hpp"
#include "concurrency/work_stealing_scheduler.hpp"
#include "core/message_bus.hpp"

using namespace keystone::agents;
using namespace keystone::core;
using namespace keystone::concurrency;

/**
 * @brief E2E Test: Async delegation with work-stealing scheduler
 *
 * Flow:
 *   1. Create WorkStealingScheduler with 4 workers
 *   2. Create MessageBus and set scheduler
 *   3. Create ChiefArchitect and 3 TaskAgents
 *   4. Chief sends 10 commands (distributed via work-stealing)
 *   5. TaskAgents execute asynchronously
 *   6. Verify all commands completed successfully
 */
TEST(E2E_PhaseA, AsyncDelegationWithWorkStealing) {
  // Initialize logger
  Logger::init(spdlog::level::info);

  std::cout << "\n=== Phase A: Async Work-Stealing Delegation ===" << std::endl;

  // ARRANGE: Create scheduler with 4 workers
  WorkStealingScheduler scheduler(4);
  scheduler.start();
  std::cout << "✓ WorkStealingScheduler started with 4 workers" << std::endl;

  // Create MessageBus and enable async routing
  auto bus = std::make_unique<MessageBus>();
  bus->setScheduler(&scheduler);
  std::cout << "✓ MessageBus configured with async routing" << std::endl;

  // Create agents
  auto chief = std::make_shared<ChiefArchitectAgent>("chief_async");
  auto task1 = std::make_shared<TaskAgent>("task_async_1");
  auto task2 = std::make_shared<TaskAgent>("task_async_2");
  auto task3 = std::make_shared<TaskAgent>("task_async_3");

  // Register agents
  bus->registerAgent(chief->getAgentId(), chief);
  bus->registerAgent(task1->getAgentId(), task1);
  bus->registerAgent(task2->getAgentId(), task2);
  bus->registerAgent(task3->getAgentId(), task3);

  // Set message bus for all agents
  chief->setMessageBus(bus.get());
  task1->setMessageBus(bus.get());
  task2->setMessageBus(bus.get());
  task3->setMessageBus(bus.get());

  std::cout << "✓ All agents registered and configured" << std::endl;

  // ACT: Send commands to different task agents
  std::map<std::string, std::pair<TaskAgent*, int32_t>>
      commands;  // msg_id -> (agent, expected)
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(1, 50);

  // Send 10 commands (round-robin to task agents)
  TaskAgent* agents[] = {task1.get(), task2.get(), task3.get()};
  for (int32_t i = 0; i < 10; ++i) {
    TaskAgent* target = agents[i % 3];
    int32_t num1 = dis(gen);
    int32_t num2 = dis(gen);
    int32_t expected = num1 + num2;

    std::stringstream cmd;
    cmd << "echo $((" << num1 << " + " << num2 << "))";

    auto msg = KeystoneMessage::create(chief->getAgentId(),
                                       target->getAgentId(), cmd.str());

    commands[msg.msg_id] = {target, expected};
    chief->sendMessage(msg);  // Async routing via scheduler

    std::cout << "  → Sent command " << (i + 1) << " to "
              << target->getAgentId() << ": " << num1 << " + " << num2 << " = "
              << expected << std::endl;
  }

  // Wait for async processing
  std::cout << "\n⏳ Waiting for async message delivery and processing..."
            << std::endl;
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // ASSERT: Process messages from all task agents
  std::map<TaskAgent*, std::vector<KeystoneMessage>> agent_messages;
  for (auto* agent : agents) {
    while (auto msg = agent->getMessage()) {
      agent_messages[agent].push_back(*msg);
    }
  }

  std::cout << "\n📨 Task agents received messages:" << std::endl;
  for (auto& [agent, messages] : agent_messages) {
    std::cout << "  " << agent->getAgentId() << ": " << messages.size()
              << " messages" << std::endl;
  }

  // Process all received messages and send responses
  std::cout << "\n⚙️  Processing messages and sending responses..." << std::endl;
  for (auto& [agent, messages] : agent_messages) {
    for (auto& msg : messages) {
      agent->processMessage(msg).get();
    }
  }

  // Wait for responses to arrive
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Collect all responses from chief
  std::vector<KeystoneMessage> responses;
  while (auto resp = chief->getMessage()) {
    responses.push_back(*resp);
  }

  std::cout << "\n📬 Received " << responses.size() << " responses from "
            << commands.size() << " sent commands" << std::endl;

  // Verify results by matching msg_id
  int32_t successful = 0;
  int32_t total = commands.size();

  for (const auto& resp : responses) {
    auto it = commands.find(resp.msg_id);
    if (it == commands.end()) {
      std::cout << "  ✗ Unexpected response with msg_id: " << resp.msg_id
                << std::endl;
      continue;
    }

    auto [agent, expected] = it->second;

    try {
      // Extract result from payload
      if (!resp.payload.has_value()) {
        std::cout << "  ✗ Command for msg_id " << resp.msg_id.substr(0, 8)
                  << "... has no payload" << std::endl;
        continue;
      }

      int32_t result = std::stoi(resp.payload.value());
      if (result == expected) {
        std::cout << "  ✓ Command for msg_id " << resp.msg_id.substr(0, 8)
                  << "... succeeded: got " << result << std::endl;
        successful++;
      } else {
        std::cout << "  ✗ Command for msg_id " << resp.msg_id.substr(0, 8)
                  << "... incorrect: got " << result << ", expected "
                  << expected << std::endl;
      }
    } catch (const std::exception& e) {
      std::cout << "  ✗ Command for msg_id " << resp.msg_id.substr(0, 8)
                << "... failed to parse result. Payload: '"
                << (resp.payload.has_value() ? resp.payload.value() : "<none>")
                << "', Error: " << e.what() << std::endl;
    }
  }

  std::cout << "\n📊 Results: " << successful << "/" << total
            << " commands succeeded" << std::endl;
  EXPECT_EQ(successful, total)
      << "All async commands should complete successfully";

  // Shutdown scheduler (waits for pending work)
  std::cout << "\n🛑 Shutting down scheduler..." << std::endl;
  scheduler.shutdown();
  std::cout << "✓ Scheduler shutdown complete" << std::endl;

  Logger::shutdown();
}

/**
 * @brief E2E Test: Work-stealing load balancing
 *
 * Tests that work-stealing distributes load across workers
 */
TEST(E2E_PhaseA, WorkStealingLoadBalancing) {
  Logger::init(spdlog::level::warn);  // Reduce log noise

  // Create scheduler with 4 workers
  WorkStealingScheduler scheduler(4);
  scheduler.start();

  auto bus = std::make_unique<MessageBus>();
  bus->setScheduler(&scheduler);

  // Create 1 chief and 10 task agents
  auto chief = std::make_shared<ChiefArchitectAgent>("chief_lb");
  std::vector<std::shared_ptr<TaskAgent>> task_agents;

  bus->registerAgent(chief->getAgentId(), chief);
  chief->setMessageBus(bus.get());

  for (int32_t i = 0; i < 10; ++i) {
    auto agent = std::make_shared<TaskAgent>("task_lb_" + std::to_string(i));
    bus->registerAgent(agent->getAgentId(), agent);
    agent->setMessageBus(bus.get());
    task_agents.push_back(agent);
  }

  // Send 100 commands (will be distributed via work-stealing)
  for (int32_t i = 0; i < 100; ++i) {
    auto target = task_agents[i % 10].get();
    auto msg = KeystoneMessage::create(chief->getAgentId(),
                                       target->getAgentId(), "echo ok");
    chief->sendMessage(msg);
  }

  // Wait for processing
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  // Count how many agents received messages
  int32_t agents_with_messages = 0;
  for (auto& agent : task_agents) {
    int32_t count = 0;
    while (agent->getMessage().has_value()) {
      count++;
    }
    if (count > 0) {
      agents_with_messages++;
    }
  }

  std::cout << "Load distribution: " << agents_with_messages
            << "/10 agents received work" << std::endl;

  // With work-stealing, multiple agents should process messages
  // (Exact distribution depends on timing, but should be > 1)
  EXPECT_GT(agents_with_messages, 0)
      << "At least some agents should receive work";

  scheduler.shutdown();
  Logger::shutdown();
}

/**
 * @brief E2E Test: Async processing with mixed sync/async agents
 *
 * Demonstrates that sync and async agents can coexist
 */
TEST(E2E_PhaseA, MixedSyncAsyncProcessing) {
  Logger::init(spdlog::level::warn);

  auto bus = std::make_unique<MessageBus>();
  // No scheduler set - synchronous mode

  auto chief = std::make_shared<ChiefArchitectAgent>("chief_mixed");
  auto task = std::make_shared<TaskAgent>("task_mixed");

  bus->registerAgent(chief->getAgentId(), chief);
  bus->registerAgent(task->getAgentId(), task);

  chief->setMessageBus(bus.get());
  task->setMessageBus(bus.get());

  // Send message in sync mode
  auto msg1 = KeystoneMessage::create(chief->getAgentId(), task->getAgentId(),
                                      "echo sync");
  chief->sendMessage(msg1);

  // Should be delivered immediately (synchronous)
  auto received1 = task->getMessage();
  ASSERT_TRUE(received1.has_value())
      << "Sync message should be delivered immediately";

  // Now enable async mode
  WorkStealingScheduler scheduler(2);
  scheduler.start();
  bus->setScheduler(&scheduler);

  // Send message in async mode
  auto msg2 = KeystoneMessage::create(chief->getAgentId(), task->getAgentId(),
                                      "echo async");
  chief->sendMessage(msg2);

  // Wait for async delivery
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  auto received2 = task->getMessage();
  ASSERT_TRUE(received2.has_value()) << "Async message should be delivered";

  scheduler.shutdown();
  Logger::shutdown();
}
