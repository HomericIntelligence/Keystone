#include <gtest/gtest.h>

#include <chrono>

#include "core/failure_injector.hpp"

using namespace keystone::core;
using namespace std::chrono_literals;

/**
 * Phase 5 Unit Tests: FailureInjector
 *
 * Tests the chaos engineering tool for injecting failures.
 */
class FailureInjectorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Use fixed seed for reproducibility
    injector = std::make_unique<FailureInjector>(12345);
  }

  std::unique_ptr<FailureInjector> injector;
};

// ============================================================================
// Agent Crash Tests
// ============================================================================

TEST_F(FailureInjectorTest, InjectAgentCrash) {
  std::string agent_id = "test_agent";

  // Initially not crashed
  EXPECT_FALSE(injector->isAgentCrashed(agent_id));

  // Inject crash
  injector->injectAgentCrash(agent_id);

  // Now crashed
  EXPECT_TRUE(injector->isAgentCrashed(agent_id));

  // Appears in failed agents list
  auto failed_agents = injector->getFailedAgents();
  EXPECT_EQ(failed_agents.size(), 1u);
  EXPECT_EQ(failed_agents[0], agent_id);

  // Total failures increased
  EXPECT_EQ(injector->getTotalFailures(), 1);
}

TEST_F(FailureInjectorTest, RecoverAgent) {
  std::string agent_id = "test_agent";

  // Inject crash
  injector->injectAgentCrash(agent_id);
  EXPECT_TRUE(injector->isAgentCrashed(agent_id));

  // Recover
  injector->recoverAgent(agent_id);
  EXPECT_FALSE(injector->isAgentCrashed(agent_id));

  // No longer in failed agents list
  auto failed_agents = injector->getFailedAgents();
  EXPECT_EQ(failed_agents.size(), 0u);
}

TEST_F(FailureInjectorTest, MultipleAgentCrashes) {
  injector->injectAgentCrash("agent1");
  injector->injectAgentCrash("agent2");
  injector->injectAgentCrash("agent3");

  EXPECT_TRUE(injector->isAgentCrashed("agent1"));
  EXPECT_TRUE(injector->isAgentCrashed("agent2"));
  EXPECT_TRUE(injector->isAgentCrashed("agent3"));
  EXPECT_FALSE(injector->isAgentCrashed("agent4"));

  auto failed_agents = injector->getFailedAgents();
  EXPECT_EQ(failed_agents.size(), 3u);

  EXPECT_EQ(injector->getTotalFailures(), 3);
}

// ============================================================================
// Agent Timeout Tests
// ============================================================================

TEST_F(FailureInjectorTest, InjectAgentTimeout) {
  std::string agent_id = "test_agent";
  auto delay = 100ms;

  // Initially no timeout
  EXPECT_EQ(injector->getAgentTimeout(agent_id), 0ms);

  // Inject timeout
  injector->injectAgentTimeout(agent_id, delay);

  // Timeout set
  EXPECT_EQ(injector->getAgentTimeout(agent_id), delay);

  // Appears in timeout agents list
  auto timeout_agents = injector->getTimeoutAgents();
  EXPECT_EQ(timeout_agents.size(), 1u);
  EXPECT_EQ(timeout_agents[0], agent_id);

  // Total failures increased
  EXPECT_EQ(injector->getTotalFailures(), 1);
}

TEST_F(FailureInjectorTest, ClearAgentTimeout) {
  std::string agent_id = "test_agent";

  injector->injectAgentTimeout(agent_id, 100ms);
  EXPECT_EQ(injector->getAgentTimeout(agent_id), 100ms);

  injector->clearAgentTimeout(agent_id);
  EXPECT_EQ(injector->getAgentTimeout(agent_id), 0ms);

  auto timeout_agents = injector->getTimeoutAgents();
  EXPECT_EQ(timeout_agents.size(), 0u);
}

TEST_F(FailureInjectorTest, MultipleAgentTimeouts) {
  injector->injectAgentTimeout("agent1", 100ms);
  injector->injectAgentTimeout("agent2", 200ms);
  injector->injectAgentTimeout("agent3", 300ms);

  EXPECT_EQ(injector->getAgentTimeout("agent1"), 100ms);
  EXPECT_EQ(injector->getAgentTimeout("agent2"), 200ms);
  EXPECT_EQ(injector->getAgentTimeout("agent3"), 300ms);
  EXPECT_EQ(injector->getAgentTimeout("agent4"), 0ms);

  auto timeout_agents = injector->getTimeoutAgents();
  EXPECT_EQ(timeout_agents.size(), 3u);
}

// ============================================================================
// Random Failure Tests
// ============================================================================

TEST_F(FailureInjectorTest, SetFailureRate) {
  // Initially 0% failure rate
  EXPECT_EQ(injector->getFailureRate(), 0.0);

  // Set 50% failure rate
  injector->setFailureRate(0.5);
  EXPECT_EQ(injector->getFailureRate(), 0.5);

  // Clamp to [0.0, 1.0]
  injector->setFailureRate(1.5);
  EXPECT_EQ(injector->getFailureRate(), 1.0);

  injector->setFailureRate(-0.5);
  EXPECT_EQ(injector->getFailureRate(), 0.0);
}

TEST_F(FailureInjectorTest, ShouldFailZeroRate) {
  injector->setFailureRate(0.0);

  // With 0% rate, should never fail
  for (int32_t i = 0; i < 100; ++i) {
    EXPECT_FALSE(injector->shouldFail());
  }
}

TEST_F(FailureInjectorTest, ShouldFailOneRate) {
  injector->setFailureRate(1.0);

  // With 100% rate, should always fail
  for (int32_t i = 0; i < 100; ++i) {
    EXPECT_TRUE(injector->shouldFail());
  }
}

TEST_F(FailureInjectorTest, ShouldFailProbabilistic) {
  injector->setFailureRate(0.5);

  // With 50% rate, approximately half should fail
  int32_t fail_count = 0;
  const int32_t trials = 1000;

  for (int32_t i = 0; i < trials; ++i) {
    if (injector->shouldFail()) {
      fail_count++;
    }
  }

  // Allow ±10% variance (450-550 failures out of 1000)
  EXPECT_GT(fail_count, 400);
  EXPECT_LT(fail_count, 600);
}

TEST_F(FailureInjectorTest, ShouldAgentFailCrashed) {
  std::string agent_id = "test_agent";

  // Crash the agent
  injector->injectAgentCrash(agent_id);

  // Should always fail (regardless of random rate)
  injector->setFailureRate(0.0);
  EXPECT_TRUE(injector->shouldAgentFail(agent_id));
}

TEST_F(FailureInjectorTest, ShouldAgentFailRandom) {
  std::string agent_id = "test_agent";

  // Agent not crashed, but 100% random failure rate
  injector->setFailureRate(1.0);
  EXPECT_TRUE(injector->shouldAgentFail(agent_id));

  // 0% random failure rate
  injector->setFailureRate(0.0);
  EXPECT_FALSE(injector->shouldAgentFail(agent_id));
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST_F(FailureInjectorTest, GetTotalFailures) {
  EXPECT_EQ(injector->getTotalFailures(), 0);

  injector->injectAgentCrash("agent1");
  EXPECT_EQ(injector->getTotalFailures(), 1);

  injector->injectAgentTimeout("agent2", 100ms);
  EXPECT_EQ(injector->getTotalFailures(), 2);

  injector->injectAgentCrash("agent3");
  EXPECT_EQ(injector->getTotalFailures(), 3);
}

TEST_F(FailureInjectorTest, Reset) {
  injector->injectAgentCrash("agent1");
  injector->injectAgentTimeout("agent2", 100ms);
  injector->setFailureRate(0.5);

  EXPECT_EQ(injector->getTotalFailures(), 2);
  EXPECT_EQ(injector->getFailedAgents().size(), 1u);
  EXPECT_EQ(injector->getTimeoutAgents().size(), 1u);

  // Reset clears all failures and timeouts
  injector->reset();

  EXPECT_EQ(injector->getTotalFailures(), 0);
  EXPECT_EQ(injector->getFailedAgents().size(), 0u);
  EXPECT_EQ(injector->getTimeoutAgents().size(), 0u);

  // Failure rate preserved
  EXPECT_EQ(injector->getFailureRate(), 0.5);
}

TEST_F(FailureInjectorTest, GetStatistics) {
  injector->injectAgentCrash("crashed_agent");
  injector->injectAgentTimeout("slow_agent", 250ms);
  injector->setFailureRate(0.1);

  std::string stats = injector->getStatistics();

  // Should contain key information
  EXPECT_NE(stats.find("Total failures injected: 2"), std::string::npos);
  EXPECT_NE(stats.find("Crashed agents: 1"), std::string::npos);
  EXPECT_NE(stats.find("crashed_agent"), std::string::npos);
  EXPECT_NE(stats.find("Timeout agents: 1"), std::string::npos);
  EXPECT_NE(stats.find("slow_agent"), std::string::npos);
  EXPECT_NE(stats.find("250ms delay"), std::string::npos);
  EXPECT_NE(stats.find("Random failure rate: 10%"), std::string::npos);
}

// ============================================================================
// Thread Safety Tests (Basic)
// ============================================================================

TEST_F(FailureInjectorTest, ConcurrentCrashInjection) {
  const int32_t THREADS = 4;
  const int32_t CRASHES_PER_THREAD = 10;

  std::vector<std::thread> threads;
  for (int32_t t = 0; t < THREADS; ++t) {
    threads.emplace_back([&, t]() {
      for (int32_t i = 0; i < CRASHES_PER_THREAD; ++i) {
        std::string agent_id =
            "agent_" + std::to_string(t) + "_" + std::to_string(i);
        injector->injectAgentCrash(agent_id);
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // All crashes should be recorded
  EXPECT_EQ(injector->getTotalFailures(), THREADS * CRASHES_PER_THREAD);
  EXPECT_EQ(injector->getFailedAgents().size(), THREADS * CRASHES_PER_THREAD);
}

TEST_F(FailureInjectorTest, ConcurrentRandomFailures) {
  injector->setFailureRate(0.5);

  const int32_t THREADS = 4;
  const int32_t CHECKS_PER_THREAD = 100;
  std::atomic<int32_t> total_failures{0};

  std::vector<std::thread> threads;
  for (int32_t t = 0; t < THREADS; ++t) {
    threads.emplace_back([&]() {
      for (int32_t i = 0; i < CHECKS_PER_THREAD; ++i) {
        if (injector->shouldFail()) {
          total_failures++;
        }
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // Approximately half should fail (allow wide variance due to threading)
  int32_t total_checks = THREADS * CHECKS_PER_THREAD;
  EXPECT_GT(total_failures.load(), total_checks * 0.3);
  EXPECT_LT(total_failures.load(), total_checks * 0.7);
}
