#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "core/agent_id_interning.hpp"

using namespace keystone::core;

// Test 1: InternNewString - First call creates ID 0
TEST(AgentIdInterningTest, InternNewString) {
  AgentIdInterning interning;

  uint32_t id = interning.intern("agent_1");
  EXPECT_EQ(id, 0);  // First ID should be 0
  EXPECT_EQ(interning.size(), 1u);
}

// Test 2: InternExistingString - Second call returns same ID
TEST(AgentIdInterningTest, InternExistingString) {
  AgentIdInterning interning;

  uint32_t id1 = interning.intern("agent_1");
  uint32_t id2 = interning.intern("agent_1");

  EXPECT_EQ(id1, id2);              // Should return same ID
  EXPECT_EQ(interning.size(), 1u);  // Only one unique string
}

// Test 3: InternMultipleStrings - IDs increment sequentially
TEST(AgentIdInterningTest, InternMultipleStrings) {
  AgentIdInterning interning;

  uint32_t id0 = interning.intern("agent_0");
  uint32_t id1 = interning.intern("agent_1");
  uint32_t id2 = interning.intern("agent_2");

  EXPECT_EQ(id0, 0);
  EXPECT_EQ(id1, 1);
  EXPECT_EQ(id2, 2);
  EXPECT_EQ(interning.size(), 3u);
}

// Test 4: TryGetIdExisting - Lookup existing string
TEST(AgentIdInterningTest, TryGetIdExisting) {
  AgentIdInterning interning;

  uint32_t id = interning.intern("agent_1");
  auto result = interning.tryGetId("agent_1");

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, id);
}

// Test 5: TryGetIdNonexistent - Returns nullopt for unknown string
TEST(AgentIdInterningTest, TryGetIdNonexistent) {
  AgentIdInterning interning;

  auto result = interning.tryGetId("nonexistent");
  EXPECT_FALSE(result.has_value());
}

// Test 6: TryGetStringExisting - Reverse lookup
TEST(AgentIdInterningTest, TryGetStringExisting) {
  AgentIdInterning interning;

  uint32_t id = interning.intern("agent_1");
  auto result = interning.tryGetString(id);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, "agent_1");
}

// Test 7: TryGetStringNonexistent - Returns nullopt for unknown ID
TEST(AgentIdInterningTest, TryGetStringNonexistent) {
  AgentIdInterning interning;

  auto result = interning.tryGetString(9999);
  EXPECT_FALSE(result.has_value());
}

// Test 8: Clear - Reset all mappings
TEST(AgentIdInterningTest, Clear) {
  AgentIdInterning interning;

  interning.intern("agent_1");
  interning.intern("agent_2");
  EXPECT_EQ(interning.size(), 2u);

  interning.clear();
  EXPECT_EQ(interning.size(), 0u);

  // After clear, next intern should start at ID 0 again
  uint32_t id = interning.intern("agent_3");
  EXPECT_EQ(id, 0);
}

// Test 9: ThreadSafety - Concurrent intern/lookup from 10 threads
TEST(AgentIdInterningTest, ThreadSafety) {
  AgentIdInterning interning;
  constexpr int32_t num_threads = 10;
  constexpr int32_t iterations_per_thread = 100;
  std::atomic<int32_t> successes{0};

  std::vector<std::thread> threads;
  threads.reserve(num_threads);

  // Launch threads that intern and lookup concurrently
  for (int32_t t = 0; t < num_threads; ++t) {
    threads.emplace_back([&interning, &successes, t]() {
      for (int32_t i = 0; i < iterations_per_thread; ++i) {
        std::string agent_id =
            "agent_" + std::to_string(t * iterations_per_thread + i);

        // Intern the ID
        uint32_t int_id = interning.intern(agent_id);

        // Verify reverse lookup
        auto str_result = interning.tryGetString(int_id);
        if (str_result && *str_result == agent_id) {
          successes++;
        }

        // Verify forward lookup
        auto id_result = interning.tryGetId(agent_id);
        if (id_result && *id_result == int_id) {
          // Success (expected)
        }
      }
    });
  }

  // Wait for all threads
  for (auto& thread : threads) {
    thread.join();
  }

  // All lookups should have succeeded
  EXPECT_EQ(successes, num_threads * iterations_per_thread);

  // Total unique IDs should match
  EXPECT_EQ(interning.size(), num_threads * iterations_per_thread);
}

// Test 10: LargeScaleInterning - Intern 10,000 agent IDs
TEST(AgentIdInterningTest, LargeScaleInterning) {
  AgentIdInterning interning;
  constexpr int32_t num_agents = 10000;

  // Intern 10,000 agents
  for (int32_t i = 0; i < num_agents; ++i) {
    std::string agent_id = "agent_" + std::to_string(i);
    uint32_t int_id = interning.intern(agent_id);
    EXPECT_EQ(int_id, static_cast<uint32_t>(i));
  }

  EXPECT_EQ(interning.size(), num_agents);

  // Verify all lookups still work
  for (int32_t i = 0; i < num_agents; i += 100) {  // Sample every 100th agent
    std::string agent_id = "agent_" + std::to_string(i);
    auto result = interning.tryGetId(agent_id);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, static_cast<uint32_t>(i));
  }
}

// Test 11: BidirectionalConsistency - Forward and reverse lookups are
// consistent
TEST(AgentIdInterningTest, BidirectionalConsistency) {
  AgentIdInterning interning;

  // Intern multiple agents
  std::vector<std::string> agent_ids = {"chief", "component_lead_1",
                                        "module_lead_1", "task_1", "task_2"};

  for (const auto& agent_id : agent_ids) {
    interning.intern(agent_id);
  }

  // Verify bidirectional consistency
  for (const auto& agent_id : agent_ids) {
    auto int_id = interning.tryGetId(agent_id);
    ASSERT_TRUE(int_id.has_value());

    auto str_id = interning.tryGetString(*int_id);
    ASSERT_TRUE(str_id.has_value());
    EXPECT_EQ(*str_id, agent_id);
  }
}

// Test 12: EmptyString - Can intern empty string
TEST(AgentIdInterningTest, EmptyString) {
  AgentIdInterning interning;

  uint32_t id = interning.intern("");
  EXPECT_EQ(id, 0);

  auto result = interning.tryGetId("");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, id);

  auto str_result = interning.tryGetString(id);
  ASSERT_TRUE(str_result.has_value());
  EXPECT_EQ(*str_result, "");
}

// Test 13: ConcurrentReads - Multiple readers can access simultaneously
TEST(AgentIdInterningTest, ConcurrentReads) {
  AgentIdInterning interning;

  // Pre-populate with some agents
  for (int32_t i = 0; i < 100; ++i) {
    interning.intern("agent_" + std::to_string(i));
  }

  constexpr int32_t num_readers = 20;
  constexpr int32_t reads_per_thread = 1000;
  std::atomic<int32_t> successes{0};

  std::vector<std::thread> readers;
  readers.reserve(num_readers);

  // Launch reader threads
  for (int32_t t = 0; t < num_readers; ++t) {
    readers.emplace_back([&interning, &successes]() {
      for (int32_t i = 0; i < reads_per_thread; ++i) {
        std::string agent_id = "agent_" + std::to_string(i % 100);
        auto result = interning.tryGetId(agent_id);
        if (result.has_value()) {
          successes++;
        }
      }
    });
  }

  // Wait for all readers
  for (auto& reader : readers) {
    reader.join();
  }

  // All reads should have succeeded
  EXPECT_EQ(successes, num_readers * reads_per_thread);
}
