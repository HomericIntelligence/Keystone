/**
 * @file string_allocation_profiling.cpp
 * @brief P2-06: Profile and optimize string allocations in hot paths
 *
 * Phase 1: Measure (Required First)
 * - Benchmark message creation rate
 * - Profile allocation hotspots
 * - Measure message throughput
 * - Determine if optimization is worth effort
 *
 * This benchmark isolates string allocation overhead in KeystoneMessage creation.
 */

#include "core/message.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <benchmark/benchmark.h>

using namespace keystone::core;

/**
 * Baseline: Message creation with string allocations
 * Allocates 4+ strings: msg_id (UUID), sender_id, receiver_id, command
 */
static void BM_MessageCreation_Baseline(benchmark::State& state) {
  for (auto _ : state) {
    auto msg = KeystoneMessage::create("sender-agent-001", "receiver-agent-002", "EXECUTE");
    benchmark::DoNotOptimize(msg);
  }
  state.SetItemsProcessed(state.iterations());
  state.counters["msgs/sec"] = benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_MessageCreation_Baseline);

/**
 * Measure string allocation overhead by varying sender/receiver ID lengths
 */
static void BM_MessageCreation_VariableIDLength(benchmark::State& state) {
  int32_t id_length = static_cast<int32_t>(state.range(0));
  std::string sender(id_length, 'a');
  std::string receiver(id_length, 'b');

  for (auto _ : state) {
    auto msg = KeystoneMessage::create(sender, receiver, "EXECUTE");
    benchmark::DoNotOptimize(msg);
  }
  state.SetItemsProcessed(state.iterations());
  state.counters["id_length"] = id_length;
  state.counters["msgs/sec"] = benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_MessageCreation_VariableIDLength)
    ->Arg(8)     // Short IDs
    ->Arg(32)    // Medium IDs (typical UUID length)
    ->Arg(64)    // Long IDs
    ->Arg(128);  // Very long IDs

/**
 * Measure allocation overhead with payload strings
 */
static void BM_MessageCreation_WithPayload(benchmark::State& state) {
  int32_t payload_size = static_cast<int32_t>(state.range(0));
  std::string payload_data(payload_size, 'x');

  for (auto _ : state) {
    auto msg = KeystoneMessage::create("sender", "receiver", "EXECUTE", payload_data);
    benchmark::DoNotOptimize(msg);
  }
  state.SetItemsProcessed(state.iterations());
  state.counters["payload_bytes"] = payload_size;
  state.counters["msgs/sec"] = benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_MessageCreation_WithPayload)
    ->Arg(0)      // No payload
    ->Arg(64)     // Small payload
    ->Arg(256)    // Medium payload
    ->Arg(1024)   // Large payload
    ->Arg(4096);  // Very large payload

/**
 * High-frequency message creation (simulates 10K msgs/sec load)
 * This measures allocation rate under realistic load
 */
static void BM_HighFrequency_MessageCreation(benchmark::State& state) {
  const int32_t burst_size = static_cast<int32_t>(state.range(0));

  for (auto _ : state) {
    std::vector<KeystoneMessage> messages;
    messages.reserve(static_cast<size_t>(burst_size));

    for (int32_t i = 0; i < burst_size; ++i) {
      messages.push_back(KeystoneMessage::create("sender-agent", "receiver-agent", "EXECUTE"));
    }

    benchmark::DoNotOptimize(messages);
    // All destroyed here - measures allocation + deallocation
  }

  state.SetItemsProcessed(state.iterations() * burst_size);
  state.counters["burst_size"] = burst_size;
  state.counters["msgs/sec"] = benchmark::Counter(state.iterations() * burst_size,
                                                  benchmark::Counter::kIsRate);
}
BENCHMARK(BM_HighFrequency_MessageCreation)
    ->Arg(100)     // 100 msgs/burst
    ->Arg(1000)    // 1K msgs/burst
    ->Arg(10000);  // 10K msgs/burst

/**
 * Measure string copy overhead when passing messages
 */
static void BM_MessageCopy_Overhead(benchmark::State& state) {
  auto original = KeystoneMessage::create("sender", "receiver", "EXECUTE");
  original.payload = "test payload data";

  for (auto _ : state) {
    // Copy constructor - copies all strings
    KeystoneMessage copy = original;
    benchmark::DoNotOptimize(copy);
  }

  state.SetItemsProcessed(state.iterations());
  state.counters["copies/sec"] = benchmark::Counter(state.iterations(),
                                                    benchmark::Counter::kIsRate);
}
BENCHMARK(BM_MessageCopy_Overhead);

/**
 * Measure string move overhead (should be cheaper than copy)
 */
static void BM_MessageMove_Overhead(benchmark::State& state) {
  for (auto _ : state) {
    auto msg = KeystoneMessage::create("sender", "receiver", "EXECUTE");
    msg.payload = "test payload data";

    // Move to new message (should not allocate)
    KeystoneMessage moved = std::move(msg);
    benchmark::DoNotOptimize(moved);
  }

  state.SetItemsProcessed(state.iterations());
  state.counters["moves/sec"] = benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_MessageMove_Overhead);

/**
 * Measure allocation overhead of string interning simulation
 * (Potential optimization from issue #22)
 */
static void BM_StringInterning_Simulation(benchmark::State& state) {
  // Simulate string pool (unordered_set keeps single copy)
  static std::unordered_set<std::string> pool;
  pool.insert("sender-agent");
  pool.insert("receiver-agent");
  pool.insert("EXECUTE");

  for (auto _ : state) {
    // Find interned strings (no allocation if exists)
    auto sender_it = pool.find("sender-agent");
    auto receiver_it = pool.find("receiver-agent");
    auto command_it = pool.find("EXECUTE");

    // Reference existing strings (no copy)
    std::string sender = *sender_it;
    std::string receiver = *receiver_it;
    std::string command = *command_it;

    // Store pointers in variables to avoid deprecated const-ref DoNotOptimize overload
    auto sender_ptr = sender.data();
    auto receiver_ptr = receiver.data();
    auto command_ptr = command.data();
    benchmark::DoNotOptimize(sender_ptr);
    benchmark::DoNotOptimize(receiver_ptr);
    benchmark::DoNotOptimize(command_ptr);
  }

  state.SetItemsProcessed(state.iterations());
  state.counters["lookups/sec"] = benchmark::Counter(state.iterations(),
                                                     benchmark::Counter::kIsRate);
}
BENCHMARK(BM_StringInterning_Simulation);

/**
 * Measure overhead of integer agent IDs (potential optimization)
 */
static void BM_IntegerIDs_Simulation(benchmark::State& state) {
  using AgentId = uint64_t;
  AgentId sender = 1001;
  AgentId receiver = 2002;

  for (auto _ : state) {
    // Integer operations - no allocation
    AgentId s = sender;
    AgentId r = receiver;
    benchmark::DoNotOptimize(s);
    benchmark::DoNotOptimize(r);
  }

  state.SetItemsProcessed(state.iterations());
  state.counters["ops/sec"] = benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_IntegerIDs_Simulation);

/**
 * Concurrent message creation (multi-threaded allocation pressure)
 */
static void BM_Concurrent_MessageCreation(benchmark::State& state) {
  for (auto _ : state) {
    auto msg = KeystoneMessage::create("sender-" + std::to_string(state.thread_index()),
                                       "receiver-" + std::to_string(state.thread_index()),
                                       "EXECUTE");
    benchmark::DoNotOptimize(msg);
  }

  state.SetItemsProcessed(state.iterations());
  state.counters["msgs/sec"] = benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_Concurrent_MessageCreation)->Threads(1)->Threads(2)->Threads(4)->Threads(8);

/**
 * Memory pressure test: Create and hold many messages
 */
static void BM_Memory_Pressure(benchmark::State& state) {
  const int32_t message_count = static_cast<int32_t>(state.range(0));

  for (auto _ : state) {
    std::vector<KeystoneMessage> messages;
    messages.reserve(static_cast<size_t>(message_count));

    // Allocate many messages
    for (int32_t i = 0; i < message_count; ++i) {
      messages.push_back(KeystoneMessage::create("sender-" + std::to_string(i),
                                                 "receiver-" + std::to_string(i),
                                                 "EXECUTE"));
    }

    benchmark::DoNotOptimize(messages);
    // All destroyed here - measures deallocation performance too
  }

  state.SetItemsProcessed(state.iterations() * message_count);
  state.counters["total_messages"] = message_count;
}
BENCHMARK(BM_Memory_Pressure)->Arg(100)->Arg(1000)->Arg(10000);

BENCHMARK_MAIN();
