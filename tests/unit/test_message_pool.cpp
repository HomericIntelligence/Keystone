#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "core/message_pool.hpp"

using namespace keystone::core;

class MessagePoolTest : public ::testing::Test {
 protected:
  void SetUp() override {
    MessagePool::clear();
    MessagePool::resetStats();
  }
};

TEST_F(MessagePoolTest, AcquireFromEmptyPool) {
  // First acquire should be a pool miss (create new message)
  auto msg = MessagePool::acquire();

  EXPECT_TRUE(msg.msg_id.empty());
  EXPECT_TRUE(msg.sender_id.empty());
  EXPECT_TRUE(msg.receiver_id.empty());
  EXPECT_TRUE(msg.command.empty());
  EXPECT_FALSE(msg.payload.has_value());
  EXPECT_EQ(msg.priority, Priority::NORMAL);
  EXPECT_FALSE(msg.deadline.has_value());

  auto stats = MessagePool::getStats();
  EXPECT_EQ(stats.total_acquires, 1);
  EXPECT_EQ(stats.pool_hits, 0);
  EXPECT_EQ(stats.pool_misses, 1);
}

TEST_F(MessagePoolTest, ReleaseAndAcquire) {
  // Acquire a message
  auto msg1 = MessagePool::acquire();
  msg1.msg_id = "test-123";
  msg1.sender_id = "sender";
  msg1.receiver_id = "receiver";
  msg1.command = "test_command";
  msg1.priority = Priority::HIGH;

  // Release it back to the pool
  MessagePool::release(std::move(msg1));

  auto stats = MessagePool::getStats();
  EXPECT_EQ(stats.total_releases, 1);
  EXPECT_EQ(stats.current_size, 1);

  // Acquire again - should be a pool hit
  auto msg2 = MessagePool::acquire();

  // Should be reset to default state
  EXPECT_TRUE(msg2.msg_id.empty());
  EXPECT_TRUE(msg2.sender_id.empty());
  EXPECT_TRUE(msg2.receiver_id.empty());
  EXPECT_TRUE(msg2.command.empty());
  EXPECT_EQ(msg2.priority, Priority::NORMAL);
  EXPECT_FALSE(msg2.deadline.has_value());

  stats = MessagePool::getStats();
  EXPECT_EQ(stats.total_acquires, 2);
  EXPECT_EQ(stats.pool_hits, 1);
  EXPECT_EQ(stats.pool_misses, 1);
  EXPECT_EQ(stats.current_size, 0);
}

TEST_F(MessagePoolTest, MultipleReleaseAndAcquire) {
  // Acquire 10 messages and hold them
  std::vector<KeystoneMessage> messages;
  for (int32_t i = 0; i < 10; ++i) {
    auto msg = MessagePool::acquire();
    msg.msg_id = "msg-" + std::to_string(i);
    messages.push_back(std::move(msg));
  }

  // Now release all 10 to the pool
  for (auto& msg : messages) {
    MessagePool::release(std::move(msg));
  }

  EXPECT_EQ(MessagePool::getPoolSize(), 10);

  auto stats = MessagePool::getStats();
  EXPECT_EQ(stats.current_size, 10);
  EXPECT_EQ(stats.max_size_reached, 10);

  // Acquire 10 messages - all should be pool hits
  for (int32_t i = 0; i < 10; ++i) {
    auto msg = MessagePool::acquire();
    EXPECT_TRUE(msg.msg_id.empty());  // Should be reset
  }

  EXPECT_EQ(MessagePool::getPoolSize(), 0);

  stats = MessagePool::getStats();
  EXPECT_EQ(stats.pool_hits, 10);
}

TEST_F(MessagePoolTest, PoolSizeLimit) {
  // Acquire 1100 messages and hold them
  std::vector<KeystoneMessage> messages;
  for (int32_t i = 0; i < 1100; ++i) {
    messages.push_back(MessagePool::acquire());
  }

  // Try to release more than MAX_POOL_SIZE (1000) messages
  for (auto& msg : messages) {
    MessagePool::release(std::move(msg));
  }

  // Pool should be capped at MAX_POOL_SIZE
  size_t pool_size = MessagePool::getPoolSize();
  EXPECT_LE(pool_size, 1000);
  EXPECT_EQ(pool_size, 1000);  // Should be exactly at limit

  auto stats = MessagePool::getStats();
  EXPECT_EQ(stats.max_size_reached, 1000);
}

TEST_F(MessagePoolTest, StatisticsTracking) {
  MessagePool::resetStats();

  // Acquire 5 messages and hold them (all misses)
  std::vector<KeystoneMessage> messages;
  for (int32_t i = 0; i < 5; ++i) {
    messages.push_back(MessagePool::acquire());
  }

  auto stats = MessagePool::getStats();
  EXPECT_EQ(stats.total_acquires, 5);
  EXPECT_EQ(stats.pool_misses, 5);
  EXPECT_EQ(stats.pool_hits, 0);

  // Release 3 of them
  for (int32_t i = 0; i < 3; ++i) {
    MessagePool::release(std::move(messages[i]));
  }

  stats = MessagePool::getStats();
  EXPECT_EQ(stats.total_acquires, 5);
  EXPECT_EQ(stats.total_releases, 3);
  EXPECT_EQ(stats.pool_misses, 5);
  EXPECT_EQ(stats.current_size, 3);

  // Acquire 3 more (all hits from pool)
  for (int32_t i = 0; i < 3; ++i) {
    auto msg = MessagePool::acquire();
  }

  stats = MessagePool::getStats();
  EXPECT_EQ(stats.total_acquires, 8);
  EXPECT_EQ(stats.pool_hits, 3);
  EXPECT_EQ(stats.current_size, 0);
}

TEST_F(MessagePoolTest, ThreadLocalIsolation) {
  // Each thread should have its own pool
  std::atomic<int32_t> thread1_pool_size{0};
  std::atomic<int32_t> thread2_pool_size{0};

  auto worker1 = [&]() {
    MessagePool::clear();
    MessagePool::resetStats();

    // Acquire and hold 5 messages, then release
    std::vector<KeystoneMessage> messages;
    for (int32_t i = 0; i < 5; ++i) {
      messages.push_back(MessagePool::acquire());
    }
    for (auto& msg : messages) {
      MessagePool::release(std::move(msg));
    }

    thread1_pool_size = MessagePool::getPoolSize();
  };

  auto worker2 = [&]() {
    MessagePool::clear();
    MessagePool::resetStats();

    // Acquire and hold 10 messages, then release
    std::vector<KeystoneMessage> messages;
    for (int32_t i = 0; i < 10; ++i) {
      messages.push_back(MessagePool::acquire());
    }
    for (auto& msg : messages) {
      MessagePool::release(std::move(msg));
    }

    thread2_pool_size = MessagePool::getPoolSize();
  };

  std::thread t1(worker1);
  std::thread t2(worker2);

  t1.join();
  t2.join();

  // Each thread should have its own independent pool
  EXPECT_EQ(thread1_pool_size, 5);
  EXPECT_EQ(thread2_pool_size, 10);
}

TEST_F(MessagePoolTest, MessageResetOnRelease) {
  auto msg = MessagePool::acquire();

  // Populate with data
  msg.msg_id = "test-msg-123";
  msg.sender_id = "sender-agent";
  msg.receiver_id = "receiver-agent";
  msg.command = "test_command";
  msg.payload = "{\"key\": \"value\"}";
  msg.priority = Priority::HIGH;
  msg.deadline =
      std::chrono::system_clock::now() + std::chrono::milliseconds(100);

  // Release back to pool
  MessagePool::release(std::move(msg));

  // Acquire again
  auto msg2 = MessagePool::acquire();

  // All fields should be reset
  EXPECT_TRUE(msg2.msg_id.empty());
  EXPECT_TRUE(msg2.sender_id.empty());
  EXPECT_TRUE(msg2.receiver_id.empty());
  EXPECT_TRUE(msg2.command.empty());
  EXPECT_FALSE(msg2.payload.has_value());
  EXPECT_EQ(msg2.priority, Priority::NORMAL);
  EXPECT_FALSE(msg2.deadline.has_value());
}

TEST_F(MessagePoolTest, ClearPool) {
  // Acquire and hold 50 messages
  std::vector<KeystoneMessage> messages;
  for (int32_t i = 0; i < 50; ++i) {
    messages.push_back(MessagePool::acquire());
  }

  // Release all to pool
  for (auto& msg : messages) {
    MessagePool::release(std::move(msg));
  }

  EXPECT_EQ(MessagePool::getPoolSize(), 50);

  // Clear the pool
  MessagePool::clear();

  EXPECT_EQ(MessagePool::getPoolSize(), 0);

  // Stats should still be preserved
  auto stats = MessagePool::getStats();
  EXPECT_EQ(stats.total_acquires, 50);
  EXPECT_EQ(stats.total_releases, 50);
}

TEST_F(MessagePoolTest, ResetStats) {
  // Acquire and hold 20 messages
  std::vector<KeystoneMessage> messages;
  for (int32_t i = 0; i < 20; ++i) {
    messages.push_back(MessagePool::acquire());
  }

  // Release all to pool
  for (auto& msg : messages) {
    MessagePool::release(std::move(msg));
  }

  auto stats = MessagePool::getStats();
  EXPECT_GT(stats.total_acquires, 0);
  EXPECT_GT(stats.total_releases, 0);

  // Reset stats
  MessagePool::resetStats();

  stats = MessagePool::getStats();
  EXPECT_EQ(stats.total_acquires, 0);
  EXPECT_EQ(stats.total_releases, 0);
  EXPECT_EQ(stats.pool_hits, 0);
  EXPECT_EQ(stats.pool_misses, 0);
  EXPECT_EQ(stats.max_size_reached, 0);

  // Pool size should be unchanged
  EXPECT_EQ(MessagePool::getPoolSize(), 20);
}

TEST_F(MessagePoolTest, HighLoadScenario) {
  // Simulate high-load scenario with many acquires and releases
  const int32_t iterations = 10000;

  for (int32_t i = 0; i < iterations; ++i) {
    auto msg = MessagePool::acquire();
    msg.msg_id = "msg-" + std::to_string(i);
    msg.command = "high_load_test";

    // Randomly release 50% of messages
    if (i % 2 == 0) {
      MessagePool::release(std::move(msg));
    }
  }

  auto stats = MessagePool::getStats();
  EXPECT_EQ(stats.total_acquires, iterations);
  EXPECT_GT(stats.pool_hits, 0);  // Should have some hits

  // Pool should not exceed MAX_POOL_SIZE
  EXPECT_LE(stats.current_size, 1000);
  EXPECT_LE(stats.max_size_reached, 1000);
}

TEST_F(MessagePoolTest, MoveSemantics) {
  auto msg1 = MessagePool::acquire();
  msg1.msg_id = "original";
  msg1.sender_id = "sender";

  // Move construct
  auto msg2 = std::move(msg1);
  EXPECT_EQ(msg2.msg_id, "original");
  EXPECT_EQ(msg2.sender_id, "sender");

  // Release moved message
  MessagePool::release(std::move(msg2));

  auto stats = MessagePool::getStats();
  EXPECT_EQ(stats.total_releases, 1);
}
