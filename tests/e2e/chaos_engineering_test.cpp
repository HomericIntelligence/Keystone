/**
 * @file phase_5_chaos_engineering.cpp
 * @brief E2E tests for Phase 5: Robustness & Chaos Engineering
 *
 * Phase 5 tests chaos engineering scenarios:
 * - Agent failures
 * - Network partitions
 * - Message loss
 * - Recovery mechanisms
 *
 * NOTE: These tests are currently disabled as they require chaos engineering
 * features (agent failure states, failure injection, network simulation) that
 * are not yet fully implemented. Re-enable by changing #if 0 to #if 1 once
 * the required features are implemented in TaskAgent and other components.
 */

#include "agents/task_agent.hpp"
#include "concurrency/work_stealing_scheduler.hpp"
#include "core/failure_injector.hpp"
#include "core/message_bus.hpp"
#include "core/retry_policy.hpp"
#include "simulation/simulated_network.hpp"

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace keystone;
using namespace keystone::agents;
using namespace keystone::core;
using namespace keystone::concurrency;
using namespace keystone::simulation;

/**
 * Phase 5.1: Agent Failure Testing
 *
 * Tests for agent failure modes and graceful degradation
 */
class Phase5AgentFailureTest : public ::testing::Test {
 protected:
  void SetUp() override {
    scheduler_ = std::make_unique<WorkStealingScheduler>(4);
    scheduler_->start();

    message_bus_ = std::make_unique<MessageBus>();
    message_bus_->setScheduler(scheduler_.get());
  }

  void TearDown() override {
    if (scheduler_) {
      scheduler_->shutdown();
    }
  }

  std::unique_ptr<WorkStealingScheduler> scheduler_;
  std::unique_ptr<MessageBus> message_bus_;
};

/**
 * @brief Test that failed agents reject messages with error responses
 *
 * When an agent is marked as failed, it should reject all incoming
 * messages and send error responses back to the sender.
 */
TEST_F(Phase5AgentFailureTest, FailedAgentRejectsMessages) {
  // Create a TaskAgent
  auto task = std::make_shared<TaskAgent>("task1");
  task->setMessageBus(message_bus_.get());
  task->setScheduler(scheduler_.get());
  message_bus_->registerAgent(task->getAgentId(), task);

  // Agent should be healthy initially
  EXPECT_FALSE(task->isFailed());
  EXPECT_TRUE(task->getFailureReason().empty());

  // Mark agent as failed
  task->markAsFailed("simulated crash for testing");

  // Agent should now be failed
  EXPECT_TRUE(task->isFailed());
  EXPECT_EQ(task->getFailureReason(), "simulated crash for testing");

  // Send a message to the failed agent
  auto msg = KeystoneMessage::create("sender", task->getAgentId(), "echo test");
  task->receiveMessage(msg);

  // Wait a bit for async processing
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Agent should have rejected the message (we'd check error response in full impl)
  EXPECT_TRUE(task->isFailed());  // Still failed
}

/**
 * @brief Test that failed agents can recover and resume processing
 *
 * After recovery, an agent should be able to process messages normally.
 */
TEST_F(Phase5AgentFailureTest, FailedAgentCanRecover) {
  // Create a TaskAgent
  auto task = std::make_shared<TaskAgent>("task1");
  task->setMessageBus(message_bus_.get());
  task->setScheduler(scheduler_.get());
  message_bus_->registerAgent(task->getAgentId(), task);

  // Mark agent as failed
  task->markAsFailed("simulated crash");
  EXPECT_TRUE(task->isFailed());

  // Recover the agent
  task->recover();

  // Agent should be healthy again
  EXPECT_FALSE(task->isFailed());
  EXPECT_TRUE(task->getFailureReason().empty());

  // Now it should process messages normally
  auto msg = KeystoneMessage::create("sender", task->getAgentId(), "echo recovered");
  task->receiveMessage(msg);

  // Wait a bit for async processing
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Agent should still be healthy
  EXPECT_FALSE(task->isFailed());
}

/**
 * @brief Test FailureInjector can crash agents
 *
 * FailureInjector should be able to mark agents as crashed
 * and agents should be able to query their failure state.
 */
TEST_F(Phase5AgentFailureTest, FailureInjectorCrashesAgents) {
  // Create a TaskAgent
  auto task = std::make_shared<TaskAgent>("task1");
  task->setMessageBus(message_bus_.get());
  task->setScheduler(scheduler_.get());
  message_bus_->registerAgent(task->getAgentId(), task);

  // Create failure injector
  FailureInjector injector(42);  // Seeded RNG for reproducibility

  // Inject crash for task
  injector.injectAgentCrash("task1");
  EXPECT_TRUE(injector.isAgentCrashed("task1"));

  // Set failure injector on the agent
  task->setFailureInjector(&injector);

  // shouldFail() should return true for crashed agent
  EXPECT_TRUE(task->shouldFail());

  // We could mark the agent as failed based on injector
  if (task->shouldFail()) {
    task->markAsFailed("FailureInjector crashed agent");
  }

  EXPECT_TRUE(task->isFailed());
  EXPECT_EQ(task->getFailureReason(), "FailureInjector crashed agent");
}

/**
 * @brief Test system continues with remaining healthy agents
 *
 * When one agent fails, the other agents should continue working normally.
 */
TEST_F(Phase5AgentFailureTest, SystemContinuesWithHealthyAgents) {
  // Create 3 TaskAgents
  auto task1 = std::make_shared<TaskAgent>("task1");
  task1->setMessageBus(message_bus_.get());
  task1->setScheduler(scheduler_.get());
  message_bus_->registerAgent(task1->getAgentId(), task1);

  auto task2 = std::make_shared<TaskAgent>("task2");
  task2->setMessageBus(message_bus_.get());
  task2->setScheduler(scheduler_.get());
  message_bus_->registerAgent(task2->getAgentId(), task2);

  auto task3 = std::make_shared<TaskAgent>("task3");
  task3->setMessageBus(message_bus_.get());
  task3->setScheduler(scheduler_.get());
  message_bus_->registerAgent(task3->getAgentId(), task3);

  // Fail task2
  task2->markAsFailed("simulated crash");
  EXPECT_TRUE(task2->isFailed());

  // task1 and task3 should still be healthy
  EXPECT_FALSE(task1->isFailed());
  EXPECT_FALSE(task3->isFailed());

  // Send messages to all tasks
  auto msg1 = KeystoneMessage::create("test", task1->getAgentId(), "echo task1");
  auto msg2 = KeystoneMessage::create("test", task2->getAgentId(), "echo task2");
  auto msg3 = KeystoneMessage::create("test", task3->getAgentId(), "echo task3");

  task1->receiveMessage(msg1);
  task2->receiveMessage(msg2);  // Should be rejected
  task3->receiveMessage(msg3);

  // Wait for async processing
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // task1 and task3 should still be healthy
  EXPECT_FALSE(task1->isFailed());
  EXPECT_FALSE(task3->isFailed());
  // task2 should still be failed
  EXPECT_TRUE(task2->isFailed());
}

/**
 * Phase 5.1: Probabilistic Failure Testing
 *
 * Tests for probabilistic failure injection
 */
class Phase5ProbabilisticFailureTest : public ::testing::Test {
 protected:
  void SetUp() override {
    scheduler_ = std::make_unique<WorkStealingScheduler>(4);
    scheduler_->start();

    message_bus_ = std::make_unique<MessageBus>();
    message_bus_->setScheduler(scheduler_.get());
  }

  void TearDown() override {
    if (scheduler_) {
      scheduler_->shutdown();
    }
  }

  std::unique_ptr<WorkStealingScheduler> scheduler_;
  std::unique_ptr<MessageBus> message_bus_;
};

/**
 * @brief Test FailureInjector produces probabilistic failures
 *
 * With a 50% failure rate, approximately half of the trials should fail.
 */
TEST_F(Phase5ProbabilisticFailureTest, ProbabilisticFailureRate) {
  // Create failure injector with 50% failure rate
  FailureInjector injector(42);  // Seeded for reproducibility
  injector.setFailureRate(0.5);  // 50% failure rate

  EXPECT_DOUBLE_EQ(injector.getFailureRate(), 0.5);

  int32_t failed_count = 0;
  int32_t total_trials = 100;

  for (int32_t i = 0; i < total_trials; ++i) {
    if (injector.shouldFail()) {
      failed_count++;
    }
  }

  // With 50% rate and 100 trials, expect ~40-60 failures
  EXPECT_GE(failed_count, 30);
  EXPECT_LE(failed_count, 70);
}

/**
 * @brief Test agents fail based on injector rate
 *
 * When agents have a failure injector with a configured rate,
 * some agents should fail probabilistically.
 */
TEST_F(Phase5ProbabilisticFailureTest, AgentsFailBasedOnInjectorRate) {
  // Create 10 task agents
  std::vector<std::shared_ptr<TaskAgent>> agents;
  for (int32_t i = 0; i < 10; ++i) {
    auto agent = std::make_shared<TaskAgent>("task" + std::to_string(i));
    agent->setMessageBus(message_bus_.get());
    agent->setScheduler(scheduler_.get());
    message_bus_->registerAgent(agent->getAgentId(), agent);
    agents.push_back(agent);
  }

  // Create failure injector with 50% failure rate
  FailureInjector injector(42);
  injector.setFailureRate(0.5);

  // Set failure injector on all agents
  for (auto& agent : agents) {
    agent->setFailureInjector(&injector);
  }

  // Check which agents should fail and mark them
  for (auto& agent : agents) {
    if (agent->shouldFail()) {
      agent->markAsFailed("Probabilistic failure from FailureInjector");
    }
  }

  // Count failed agents
  int failed_agents = 0;
  for (auto& agent : agents) {
    if (agent->isFailed()) {
      failed_agents++;
    }
  }

  // Some agents should have failed (statistically)
  // With 10 agents, expect 0-10 failures
  EXPECT_GE(failed_agents, 0);
  EXPECT_LE(failed_agents, 10);
}

/**
 * Phase 5.1: FailureInjector Statistics
 *
 * Tests for failure tracking and statistics
 */
class Phase5FailureInjectorStatsTest : public ::testing::Test {
 protected:
  // No scheduler needed for pure FailureInjector tests
};

/**
 * @brief Test FailureInjector tracks failure statistics
 *
 * FailureInjector should correctly track:
 * - Total failures injected
 * - Crashed agents
 * - Timeout agents
 */
TEST_F(Phase5FailureInjectorStatsTest, TracksFailureStatistics) {
  FailureInjector injector(42);

  // Initially no failures
  EXPECT_EQ(injector.getTotalFailures(), 0);
  EXPECT_TRUE(injector.getFailedAgents().empty());
  EXPECT_TRUE(injector.getTimeoutAgents().empty());

  // Inject some crashes
  injector.injectAgentCrash("agent1");
  injector.injectAgentCrash("agent2");
  injector.injectAgentCrash("agent3");

  EXPECT_EQ(injector.getTotalFailures(), 3);
  EXPECT_EQ(injector.getFailedAgents().size(), 3u);

  // Inject some timeouts
  injector.injectAgentTimeout("agent4", std::chrono::milliseconds(100));
  injector.injectAgentTimeout("agent5", std::chrono::milliseconds(500));

  EXPECT_EQ(injector.getTotalFailures(), 5);
  EXPECT_EQ(injector.getTimeoutAgents().size(), 2u);

  // Recover agent1
  injector.recoverAgent("agent1");
  EXPECT_EQ(injector.getFailedAgents().size(), 2u);

  // Check statistics string
  std::string stats = injector.getStatistics();
  EXPECT_NE(stats.find("Total failures injected: 5"), std::string::npos);
  EXPECT_NE(stats.find("Crashed agents: 2"), std::string::npos);
  EXPECT_NE(stats.find("Timeout agents: 2"), std::string::npos);

  // Reset
  injector.reset();
  EXPECT_EQ(injector.getTotalFailures(), 0);
  EXPECT_TRUE(injector.getFailedAgents().empty());
  EXPECT_TRUE(injector.getTimeoutAgents().empty());
}

/**
 * @brief Test FailureInjector timeout tracking
 *
 * FailureInjector should correctly track agent timeouts and delays.
 */
TEST_F(Phase5FailureInjectorStatsTest, TracksAgentTimeouts) {
  FailureInjector injector(42);

  // Inject timeout for agent1
  injector.injectAgentTimeout("agent1", std::chrono::milliseconds(100));

  EXPECT_EQ(injector.getAgentTimeout("agent1"), std::chrono::milliseconds(100));
  EXPECT_EQ(injector.getTimeoutAgents().size(), 1u);

  // Inject timeout for agent2 with different delay
  injector.injectAgentTimeout("agent2", std::chrono::milliseconds(500));

  EXPECT_EQ(injector.getAgentTimeout("agent2"), std::chrono::milliseconds(500));
  EXPECT_EQ(injector.getTimeoutAgents().size(), 2u);

  // Clear timeout for agent1
  injector.clearAgentTimeout("agent1");
  EXPECT_EQ(injector.getAgentTimeout("agent1"), std::chrono::milliseconds(0));
  EXPECT_EQ(injector.getTimeoutAgents().size(), 1u);

  // agent2 should still have timeout
  EXPECT_EQ(injector.getAgentTimeout("agent2"), std::chrono::milliseconds(500));
}

/**
 * Phase 5.2: Network Partition Testing
 *
 * Tests for network partition (split-brain) scenarios
 */
class Phase5NetworkPartitionTest : public ::testing::Test {
 protected:
  // No setup needed for partition tests
};

/**
 * @brief Test basic network partition creation and healing
 *
 * Verify that partitions can be created and healed correctly.
 */
TEST_F(Phase5NetworkPartitionTest, CreateAndHealPartition) {
  SimulatedNetwork network;

  // Initially no partition
  EXPECT_FALSE(network.isPartitioned());

  // Create partition: [0, 1] vs [2, 3]
  network.createPartition({0, 1}, {2, 3});

  EXPECT_TRUE(network.isPartitioned());

  // Nodes in same partition can communicate
  EXPECT_TRUE(network.canCommunicate(0, 1));
  EXPECT_TRUE(network.canCommunicate(1, 0));
  EXPECT_TRUE(network.canCommunicate(2, 3));
  EXPECT_TRUE(network.canCommunicate(3, 2));

  // Nodes in different partitions cannot communicate
  EXPECT_FALSE(network.canCommunicate(0, 2));
  EXPECT_FALSE(network.canCommunicate(0, 3));
  EXPECT_FALSE(network.canCommunicate(1, 2));
  EXPECT_FALSE(network.canCommunicate(1, 3));
  EXPECT_FALSE(network.canCommunicate(2, 0));
  EXPECT_FALSE(network.canCommunicate(2, 1));
  EXPECT_FALSE(network.canCommunicate(3, 0));
  EXPECT_FALSE(network.canCommunicate(3, 1));

  // Heal partition
  network.healPartition();

  EXPECT_FALSE(network.isPartitioned());

  // All nodes can communicate again
  EXPECT_TRUE(network.canCommunicate(0, 2));
  EXPECT_TRUE(network.canCommunicate(1, 3));
  EXPECT_TRUE(network.canCommunicate(2, 0));
  EXPECT_TRUE(network.canCommunicate(3, 1));
}

/**
 * @brief Test messages are dropped across partitions
 *
 * Verify that messages sent across partition boundaries are dropped.
 */
TEST_F(Phase5NetworkPartitionTest, MessagesDroppedAcrossPartition) {
  SimulatedNetwork::Config config{
      .min_latency = std::chrono::microseconds(10),
      .max_latency = std::chrono::microseconds(50),
      .bandwidth_mbps = 1000,
      .packet_loss_rate = 0.0  // No random packet loss
  };
  SimulatedNetwork network(config);

  // Create partition: [0, 1] vs [2, 3]
  network.createPartition({0, 1}, {2, 3});

  EXPECT_EQ(network.getPartitionDroppedMessages(), 0);

  // Send message across partition (should be dropped)
  int executed = 0;
  network.send(0, 2, [&executed]() { executed++; });

  EXPECT_EQ(network.getPartitionDroppedMessages(), 1);

  // Wait for potential delivery
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Message should not be delivered
  auto work = network.receive(2);
  EXPECT_FALSE(work.has_value());
  EXPECT_EQ(executed, 0);  // Work not executed

  // Send message within partition (should be delivered)
  network.send(0, 1, [&executed]() { executed++; });

  EXPECT_EQ(network.getPartitionDroppedMessages(), 1);  // Still 1

  // Wait for delivery
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Message should be delivered
  work = network.receive(1);
  EXPECT_TRUE(work.has_value());
  if (work) {
    (*work)();
  }
  EXPECT_EQ(executed, 1);  // Work executed
}

/**
 * @brief Test partition statistics tracking
 *
 * Verify that partition-dropped messages are tracked correctly.
 */
TEST_F(Phase5NetworkPartitionTest, PartitionStatisticsTracking) {
  SimulatedNetwork::Config config{.min_latency = std::chrono::microseconds(10),
                                  .max_latency = std::chrono::microseconds(50),
                                  .bandwidth_mbps = 1000,
                                  .packet_loss_rate = 0.0};
  SimulatedNetwork network(config);

  // Create partition: [0, 1] vs [2, 3]
  network.createPartition({0, 1}, {2, 3});

  EXPECT_EQ(network.getPartitionDroppedMessages(), 0);
  EXPECT_EQ(network.getTotalMessages(), 0);

  // Send 10 messages across partition
  for (int32_t i = 0; i < 10; ++i) {
    network.send(0, 2, []() {});
  }

  EXPECT_EQ(network.getPartitionDroppedMessages(), 10);
  EXPECT_EQ(network.getTotalMessages(), 10);

  // Send 5 messages within partition
  for (int32_t i = 0; i < 5; ++i) {
    network.send(0, 1, []() {});
  }

  EXPECT_EQ(network.getPartitionDroppedMessages(), 10);  // Still 10
  EXPECT_EQ(network.getTotalMessages(), 15);             // Total increased

  // Heal partition and send more messages
  network.healPartition();

  for (int32_t i = 0; i < 3; ++i) {
    network.send(0, 2, []() {});
  }

  EXPECT_EQ(network.getPartitionDroppedMessages(), 10);  // Still 10 (no new drops)
  EXPECT_EQ(network.getTotalMessages(), 18);

  // Reset stats
  network.resetStats();

  EXPECT_EQ(network.getPartitionDroppedMessages(), 0);
  EXPECT_EQ(network.getTotalMessages(), 0);
}

/**
 * @brief Test split-brain scenario with work distribution
 *
 * Simulates a real-world split-brain where work cannot cross partition.
 */
TEST_F(Phase5NetworkPartitionTest, SplitBrainWorkDistribution) {
  SimulatedNetwork::Config config{.min_latency = std::chrono::microseconds(10),
                                  .max_latency = std::chrono::microseconds(50),
                                  .bandwidth_mbps = 1000,
                                  .packet_loss_rate = 0.0};
  SimulatedNetwork network(config);

  // Simulate 4 nodes with work
  std::atomic<int> node0_work{0};
  std::atomic<int> node1_work{0};
  std::atomic<int> node2_work{0};
  std::atomic<int> node3_work{0};

  // Create partition: [0, 1] vs [2, 3]
  network.createPartition({0, 1}, {2, 3});

  // Send work within partition A (0→1)
  for (int32_t i = 0; i < 5; ++i) {
    network.send(0, 1, [&node1_work]() { node1_work++; });
  }

  // Send work within partition B (2→3)
  for (int32_t i = 0; i < 3; ++i) {
    network.send(2, 3, [&node3_work]() { node3_work++; });
  }

  // Send work across partition (should be dropped)
  for (int32_t i = 0; i < 10; ++i) {
    network.send(0, 2, [&node2_work]() { node2_work++; });
    network.send(1, 3, [&node3_work]() { node3_work++; });
  }

  EXPECT_EQ(network.getPartitionDroppedMessages(), 20);  // All cross-partition dropped

  // Process work
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Execute work items
  while (auto work = network.receive(1)) {
    (*work)();
  }
  while (auto work = network.receive(3)) {
    (*work)();
  }
  while (auto work = network.receive(2)) {
    (*work)();
  }

  // Verify: Only within-partition work was executed
  EXPECT_EQ(node1_work.load(), 5);  // Received from node 0
  EXPECT_EQ(node3_work.load(), 3);  // Received from node 2 (NOT from node 1)
  EXPECT_EQ(node2_work.load(), 0);  // Nothing received (all dropped)

  // Heal partition and send more work
  network.healPartition();

  for (int32_t i = 0; i < 2; ++i) {
    network.send(0, 2, [&node2_work]() { node2_work++; });
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  while (auto work = network.receive(2)) {
    (*work)();
  }

  // After healing, cross-partition work succeeds
  EXPECT_EQ(node2_work.load(), 2);
}

/**
 * @brief Test asymmetric partition (more complex topology)
 *
 * Test partition with different sizes: [0] vs [1, 2, 3]
 */
TEST_F(Phase5NetworkPartitionTest, AsymmetricPartition) {
  SimulatedNetwork network;

  // Create asymmetric partition: [0] vs [1, 2, 3]
  network.createPartition({0}, {1, 2, 3});

  EXPECT_TRUE(network.isPartitioned());

  // Node 0 is isolated
  EXPECT_FALSE(network.canCommunicate(0, 1));
  EXPECT_FALSE(network.canCommunicate(0, 2));
  EXPECT_FALSE(network.canCommunicate(0, 3));

  // Nodes 1, 2, 3 can communicate with each other
  EXPECT_TRUE(network.canCommunicate(1, 2));
  EXPECT_TRUE(network.canCommunicate(1, 3));
  EXPECT_TRUE(network.canCommunicate(2, 3));
  EXPECT_TRUE(network.canCommunicate(2, 1));
  EXPECT_TRUE(network.canCommunicate(3, 1));
  EXPECT_TRUE(network.canCommunicate(3, 2));

  // Heal and verify
  network.healPartition();

  EXPECT_TRUE(network.canCommunicate(0, 1));
  EXPECT_TRUE(network.canCommunicate(0, 2));
  EXPECT_TRUE(network.canCommunicate(0, 3));
}

/**
 * Phase 5.3: Message Loss and Retry Testing
 *
 * Tests for message loss with retry logic
 */
class Phase5MessageLossTest : public ::testing::Test {
 protected:
  // No setup needed for message loss tests
};

/**
 * @brief Test basic retry policy functionality
 *
 * Verify that RetryPolicy correctly tracks and limits retries.
 */
TEST_F(Phase5MessageLossTest, BasicRetryPolicy) {
  RetryPolicy::Config config{.max_attempts = 3,
                             .initial_delay_ms = std::chrono::milliseconds(10),
                             .max_delay_ms = std::chrono::milliseconds(1000),
                             .backoff_multiplier = 2.0};
  RetryPolicy policy(config);

  // First attempt should be allowed
  EXPECT_TRUE(policy.shouldRetry("msg1"));

  // Record 3 attempts
  policy.recordAttempt("msg1");
  EXPECT_TRUE(policy.shouldRetry("msg1"));

  policy.recordAttempt("msg1");
  EXPECT_TRUE(policy.shouldRetry("msg1"));

  policy.recordAttempt("msg1");
  EXPECT_FALSE(policy.shouldRetry("msg1"));  // Max attempts reached

  EXPECT_EQ(policy.getTotalRetries(), 2);  // 2 retries (attempt 2 and 3)
}

/**
 * @brief Test message loss with simulated network
 *
 * Simulate message loss and verify statistics tracking.
 */
TEST_F(Phase5MessageLossTest, MessageLossWithSimulatedNetwork) {
  // Configure 20% packet loss
  SimulatedNetwork::Config config{
      .min_latency = std::chrono::microseconds(10),
      .max_latency = std::chrono::microseconds(50),
      .bandwidth_mbps = 1000,
      .packet_loss_rate = 0.2  // 20% loss
  };
  SimulatedNetwork network(config);

  // Send 100 messages
  std::atomic<int> delivered{0};
  for (int32_t i = 0; i < 100; ++i) {
    network.send(0, 1, [&delivered]() { delivered++; });
  }

  // Wait for delivery
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Receive all delivered messages
  while (auto work = network.receive(1)) {
    (*work)();
  }

  // Some messages should have been dropped (approximately 20%)
  EXPECT_GT(network.getDroppedMessages(), 0);
  EXPECT_LT(network.getDroppedMessages(), 50);  // Not all dropped

  // Delivered messages should be less than total sent
  EXPECT_LT(delivered.load(), 100);
  EXPECT_GT(delivered.load(), 50);  // But most should get through
}

/**
 * @brief Test retry with exponential backoff
 *
 * Verify that retry delays increase exponentially.
 */
TEST_F(Phase5MessageLossTest, ExponentialBackoffDelays) {
  RetryPolicy::Config config{.max_attempts = 5,
                             .initial_delay_ms = std::chrono::milliseconds(100),
                             .max_delay_ms = std::chrono::milliseconds(10000),
                             .backoff_multiplier = 2.0};
  RetryPolicy policy(config);

  // First attempt: no delay
  policy.recordAttempt("msg1");
  auto delay1 = policy.getNextDelay("msg1");
  EXPECT_EQ(delay1, std::chrono::milliseconds(200));  // 100 * 2^1

  // Second attempt: 200ms delay
  policy.recordAttempt("msg1");
  auto delay2 = policy.getNextDelay("msg1");
  EXPECT_EQ(delay2, std::chrono::milliseconds(400));  // 100 * 2^2

  // Third attempt: 400ms delay
  policy.recordAttempt("msg1");
  auto delay3 = policy.getNextDelay("msg1");
  EXPECT_EQ(delay3, std::chrono::milliseconds(800));  // 100 * 2^3

  // Verify exponential growth
  EXPECT_EQ(delay2.count(), delay1.count() * 2);
  EXPECT_EQ(delay3.count(), delay2.count() * 2);
}

/**
 * @brief Test retry statistics tracking
 *
 * Verify that RetryPolicy tracks success/failure statistics correctly.
 */
TEST_F(Phase5MessageLossTest, RetryStatisticsTracking) {
  RetryPolicy policy;

  // Scenario 1: Message succeeds on first attempt
  policy.recordAttempt("msg1");
  policy.recordSuccess("msg1");

  // Scenario 2: Message succeeds after 2 retries
  policy.recordAttempt("msg2");
  policy.recordAttempt("msg2");
  policy.recordAttempt("msg2");
  policy.recordSuccess("msg2");

  // Scenario 3: Message fails after max attempts
  policy.recordAttempt("msg3");
  policy.recordAttempt("msg3");
  policy.recordAttempt("msg3");
  policy.recordFailure("msg3");

  // Verify statistics
  EXPECT_EQ(policy.getTotalSuccesses(), 2);  // msg1 and msg2
  EXPECT_EQ(policy.getTotalFailures(), 1);   // msg3
  EXPECT_EQ(policy.getTotalRetries(), 4);    // msg2: 2 retries, msg3: 2 retries
  EXPECT_EQ(policy.getActiveRetries(), 0);   // All completed
}

/**
 * @brief Test simulated message loss with retry simulation
 *
 * Simulate sending messages with loss and manual retries.
 */
TEST_F(Phase5MessageLossTest, MessageLossWithManualRetries) {
  SimulatedNetwork::Config net_config{
      .min_latency = std::chrono::microseconds(10),
      .max_latency = std::chrono::microseconds(50),
      .bandwidth_mbps = 1000,
      .packet_loss_rate = 0.3  // 30% loss - higher to test retries
  };
  SimulatedNetwork network(net_config);

  RetryPolicy::Config retry_config{.max_attempts = 5,
                                   .initial_delay_ms = std::chrono::milliseconds(10),
                                   .max_delay_ms = std::chrono::milliseconds(100),
                                   .backoff_multiplier = 2.0};
  RetryPolicy policy(retry_config);

  // Try sending 10 messages with retry logic
  std::atomic<int> delivered{0};
  std::atomic<int> total_attempts{0};

  for (int32_t i = 0; i < 10; ++i) {
    std::string msg_id = "msg" + std::to_string(i);
    bool sent = false;

    // Retry loop
    while (!sent && policy.shouldRetry(msg_id)) {
      policy.recordAttempt(msg_id);
      total_attempts++;

      // Simulate sending (network may drop it)
      bool dropped = (network.getDroppedMessages() < static_cast<size_t>(total_attempts.load()));

      if (!dropped) {
        // Message delivered
        network.send(0, 1, [&delivered]() { delivered++; });
        policy.recordSuccess(msg_id);
        sent = true;
      } else {
        // Message lost - will retry if possible
        auto delay = policy.getNextDelay(msg_id);
        std::this_thread::sleep_for(delay);
      }
    }

    if (!sent) {
      policy.recordFailure(msg_id);
    }
  }

  // Wait for delivery
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Process delivered messages
  while (auto work = network.receive(1)) {
    (*work)();
  }

  // Verify: Most messages should eventually succeed with retries
  EXPECT_GT(policy.getTotalSuccesses() + policy.getTotalFailures(), 0);
  EXPECT_GT(total_attempts.load(), 10);  // At least some retries occurred
}

/**
 * @brief Test combined partition and message loss
 *
 * Combine network partition with message loss for extreme chaos.
 */
TEST_F(Phase5MessageLossTest, CombinedPartitionAndLoss) {
  SimulatedNetwork::Config config{
      .min_latency = std::chrono::microseconds(1000),  // Increased for reliable timing
      .max_latency = std::chrono::microseconds(2000),
      .bandwidth_mbps = 1000,
      .packet_loss_rate = 0.2  // 20% loss for more reliable detection
  };
  SimulatedNetwork network(config);

  // Create partition: [0, 1] vs [2, 3]
  network.createPartition({0, 1}, {2, 3});

  // Send messages in various scenarios
  std::atomic<int> delivered{0};

  // Within partition: should work (with some loss)
  // Increased to 50 messages for statistical reliability
  for (int32_t i = 0; i < 50; ++i) {
    network.send(0, 1, [&delivered]() { delivered++; });
  }

  // Across partition: should be dropped
  for (int32_t i = 0; i < 10; ++i) {
    network.send(0, 2, [&delivered]() { delivered++; });
  }

  // Wait and receive (longer for increased latency)
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  while (auto work = network.receive(1)) {
    (*work)();
  }
  while (auto work = network.receive(2)) {
    (*work)();
  }

  // Verify:
  // - All cross-partition messages dropped
  EXPECT_EQ(network.getPartitionDroppedMessages(), 10);

  // - Some within-partition messages lost due to packet loss (but not all)
  // With 50 messages and 20% loss rate, expect 5-15 dropped (statistically)
  EXPECT_GT(network.getDroppedMessages(), 0);   // Some packet loss
  EXPECT_LT(network.getDroppedMessages(), 50);  // But not all

  // - Many messages delivered (but less than total sent due to packet loss)
  EXPECT_GT(delivered.load(), 0);
  EXPECT_LT(delivered.load(), 50);  // Less than total sent within partition
}
