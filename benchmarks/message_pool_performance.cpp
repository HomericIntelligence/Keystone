/**
 * @file message_pool_performance.cpp
 * @brief Benchmarks for MessagePool allocation reduction
 *
 * Phase D.2: Verify message pooling reduces allocation overhead
 */

// KeystoneMessage::command is [[deprecated]]; benchmarks intentionally access
// it to measure legacy-field allocation behaviour.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include "core/message.hpp"
#include "core/message_pool.hpp"

#include <cstdint>

#include <benchmark/benchmark.h>

using namespace keystone::core;

/**
 * Baseline: Create and destroy messages without pooling
 */
static void BM_MessageCreation_NoPooing(benchmark::State& state) {
  for (auto _ : state) {
    auto msg = KeystoneMessage::create("sender", "receiver", "test_command");
    msg.payload = "test payload data";
    msg.priority = Priority::HIGH;
    benchmark::DoNotOptimize(msg);
    // Message destroyed here
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MessageCreation_NoPooing);

/**
 * With pooling: Acquire and release messages
 */
static void BM_MessageCreation_WithPooling(benchmark::State& state) {
  // Warmup pool
  for (int32_t i = 0; i < 100; ++i) {
    auto msg = MessagePool::acquire();
    MessagePool::release(std::move(msg));
  }

  for (auto _ : state) {
    auto msg = MessagePool::acquire();
    msg.msg_id = "test-msg";
    msg.sender_id = "sender";
    msg.receiver_id = "receiver";
    msg.command = "test_command";
    msg.payload = "test payload data";
    msg.priority = Priority::HIGH;
    benchmark::DoNotOptimize(msg);
    MessagePool::release(std::move(msg));
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MessageCreation_WithPooling);

/**
 * Burst pattern: Create many messages then destroy them (no pooling)
 */
static void BM_MessageBurst_NoPooling(benchmark::State& state) {
  const int32_t burst_size = static_cast<int32_t>(state.range(0));

  for (auto _ : state) {
    std::vector<KeystoneMessage> messages;
    messages.reserve(static_cast<size_t>(burst_size));

    // Create burst
    for (int32_t i = 0; i < burst_size; ++i) {
      messages.push_back(KeystoneMessage::create("sender", "receiver", "cmd"));
    }

    benchmark::DoNotOptimize(messages);
    // All destroyed here
  }
  state.SetItemsProcessed(state.iterations() * burst_size);
}
BENCHMARK(BM_MessageBurst_NoPooling)->Arg(10)->Arg(100)->Arg(1000);

/**
 * Burst pattern: Acquire many messages then release them (with pooling)
 */
static void BM_MessageBurst_WithPooling(benchmark::State& state) {
  const int32_t burst_size = static_cast<int32_t>(state.range(0));

  // Warmup pool
  MessagePool::clear();
  for (int32_t i = 0; i < burst_size; ++i) {
    auto msg = MessagePool::acquire();
    MessagePool::release(std::move(msg));
  }

  for (auto _ : state) {
    std::vector<KeystoneMessage> messages;
    messages.reserve(static_cast<size_t>(burst_size));

    // Acquire burst
    for (int32_t i = 0; i < burst_size; ++i) {
      messages.push_back(MessagePool::acquire());
    }

    // Release burst
    for (auto& msg : messages) {
      MessagePool::release(std::move(msg));
    }
  }
  state.SetItemsProcessed(state.iterations() * burst_size);
}
BENCHMARK(BM_MessageBurst_WithPooling)->Arg(10)->Arg(100)->Arg(1000);

/**
 * Steady-state: Continuous acquire/release (simulates message passing)
 */
static void BM_SteadyState_NoPooling(benchmark::State& state) {
  for (auto _ : state) {
    auto msg = KeystoneMessage::create("sender", "receiver", "cmd");
    msg.payload = "payload";
    benchmark::DoNotOptimize(msg);
    // Destroyed
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SteadyState_NoPooling);

/**
 * Steady-state: Continuous acquire/release with pool (optimal case)
 */
static void BM_SteadyState_WithPooling(benchmark::State& state) {
  // Warmup
  for (int32_t i = 0; i < 10; ++i) {
    auto msg = MessagePool::acquire();
    MessagePool::release(std::move(msg));
  }

  for (auto _ : state) {
    auto msg = MessagePool::acquire();
    msg.sender_id = "sender";
    msg.receiver_id = "receiver";
    msg.command = "cmd";
    msg.payload = "payload";
    benchmark::DoNotOptimize(msg);
    MessagePool::release(std::move(msg));
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SteadyState_WithPooling);

/**
 * Pool statistics overhead
 */
static void BM_PoolStatistics(benchmark::State& state) {
  // Warmup
  for (int32_t i = 0; i < 100; ++i) {
    auto msg = MessagePool::acquire();
    MessagePool::release(std::move(msg));
  }

  for (auto _ : state) {
    auto stats = MessagePool::getStats();
    benchmark::DoNotOptimize(stats);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PoolStatistics);

/**
 * Pool hit rate under load
 */
static void BM_PoolHitRate(benchmark::State& state) {
  MessagePool::clear();
  MessagePool::resetStats();

  // Build up pool
  std::vector<KeystoneMessage> warmup;
  for (int32_t i = 0; i < 100; ++i) {
    warmup.push_back(MessagePool::acquire());
  }
  for (auto& msg : warmup) {
    MessagePool::release(std::move(msg));
  }

  for (auto _ : state) {
    auto msg = MessagePool::acquire();
    MessagePool::release(std::move(msg));
  }

  auto stats = MessagePool::getStats();
  double hit_rate = static_cast<double>(stats.pool_hits) / stats.total_acquires * 100.0;
  state.counters["HitRate%"] = hit_rate;
  state.counters["PoolHits"] = stats.pool_hits;
  state.counters["PoolMisses"] = stats.pool_misses;
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PoolHitRate);

/**
 * Multi-threaded pooling (thread-local isolation)
 */
static void BM_ThreadLocalPooling(benchmark::State& state) {
  // Each thread gets its own pool
  if (state.thread_index() == 0) {
    MessagePool::clear();
    MessagePool::resetStats();
  }

  // Warmup per-thread pool
  for (int32_t i = 0; i < 10; ++i) {
    auto msg = MessagePool::acquire();
    MessagePool::release(std::move(msg));
  }

  for (auto _ : state) {
    auto msg = MessagePool::acquire();
    msg.command = "test";
    benchmark::DoNotOptimize(msg);
    MessagePool::release(std::move(msg));
  }

  if (state.thread_index() == 0) {
    auto stats = MessagePool::getStats();
    double hit_rate = static_cast<double>(stats.pool_hits) / stats.total_acquires * 100.0;
    state.counters["HitRate%"] = hit_rate;
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ThreadLocalPooling)->Threads(1)->Threads(2)->Threads(4);

/**
 * Message field reset overhead
 */
static void BM_MessageReset(benchmark::State& state) {
  for (auto _ : state) {
    KeystoneMessage msg;
    msg.msg_id = "test-id-123";
    msg.sender_id = "sender-agent";
    msg.receiver_id = "receiver-agent";
    msg.command = "test_command";
    msg.payload = "payload data";
    msg.priority = Priority::HIGH;

    // Simulate reset (what release() does)
    msg.msg_id.clear();
    msg.sender_id.clear();
    msg.receiver_id.clear();
    msg.command.clear();
    msg.payload.reset();
    msg.priority = Priority::NORMAL;
    msg.deadline.reset();

    benchmark::DoNotOptimize(msg);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MessageReset);

BENCHMARK_MAIN();

#pragma GCC diagnostic pop
