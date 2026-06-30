// resilience_performance.cpp
// Benchmarks for resilience components: retry policy, circuit breaker,
// heartbeat
//
// Measures:
// - Retry policy overhead
// - Circuit breaker state transition latency
// - Heartbeat monitoring performance
// - Failure detection speed

#include "core/circuit_breaker.hpp"
#include "core/heartbeat_monitor.hpp"
#include "core/retry_policy.hpp"

#include <chrono>
#include <cstdint>
#include <thread>

#include <benchmark/benchmark.h>

using namespace keystone;
using namespace keystone::core;

// ==========================
// Retry Policy Benchmarks
// ==========================

// Benchmark: Retry policy creation
static void BM_RetryPolicy_Creation(benchmark::State& state) {
  for (auto _ : state) {
    auto policy = RetryPolicy(5, std::chrono::milliseconds(100), 2.0, std::chrono::seconds(30));
    benchmark::DoNotOptimize(policy);
  }
}
BENCHMARK(BM_RetryPolicy_Creation);

// Benchmark: shouldRetry check
static void BM_RetryPolicy_ShouldRetry(benchmark::State& state) {
  auto policy = RetryPolicy(100, std::chrono::milliseconds(100), 2.0, std::chrono::seconds(30));

  uint32_t attempt = 0;
  for (auto _ : state) {
    bool should_retry = policy.shouldRetry(attempt % 150);
    benchmark::DoNotOptimize(should_retry);
    attempt++;
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RetryPolicy_ShouldRetry);

// Benchmark: Backoff delay calculation
static void BM_RetryPolicy_BackoffCalculation(benchmark::State& state) {
  auto policy = RetryPolicy(100, std::chrono::milliseconds(100), 2.0, std::chrono::seconds(30));

  uint32_t attempt = 0;
  for (auto _ : state) {
    auto delay = policy.getBackoffDelay(attempt % 20);
    benchmark::DoNotOptimize(delay);
    attempt++;
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RetryPolicy_BackoffCalculation);

// Benchmark: Exponential backoff sequence
static void BM_RetryPolicy_FullSequence(benchmark::State& state) {
  int32_t max_retries = static_cast<int32_t>(state.range(0));

  for (auto _ : state) {
    auto policy =
        RetryPolicy(max_retries, std::chrono::milliseconds(10), 2.0, std::chrono::seconds(10));

    for (int32_t attempt = 0; attempt < max_retries; ++attempt) {
      if (!policy.shouldRetry(attempt))
        break;
      auto delay = policy.getBackoffDelay(attempt);
      benchmark::DoNotOptimize(delay);
    }
  }

  state.counters["max_retries"] = max_retries;
}
BENCHMARK(BM_RetryPolicy_FullSequence)->Range(1, 64);

// Benchmark: Retry policy with varying multipliers
static void BM_RetryPolicy_VaryingMultiplier(benchmark::State& state) {
  double multiplier = state.range(0) / 10.0;  // 1.0 to 5.0

  auto policy =
      RetryPolicy(20, std::chrono::milliseconds(100), multiplier, std::chrono::seconds(30));

  for (auto _ : state) {
    for (int32_t attempt = 0; attempt < 10; ++attempt) {
      auto delay = policy.getBackoffDelay(attempt);
      benchmark::DoNotOptimize(delay);
    }
  }

  state.counters["multiplier"] = multiplier;
}
BENCHMARK(BM_RetryPolicy_VaryingMultiplier)->DenseRange(10, 50, 10);

// ==========================
// Circuit Breaker Benchmarks
// ==========================

// Benchmark: Circuit breaker creation
static void BM_CircuitBreaker_Creation(benchmark::State& state) {
  for (auto _ : state) {
    auto cb = CircuitBreaker("test", 5, std::chrono::seconds(10), std::chrono::seconds(5));
    benchmark::DoNotOptimize(cb);
  }
}
BENCHMARK(BM_CircuitBreaker_Creation);

// Benchmark: allowRequest check (closed state)
static void BM_CircuitBreaker_AllowRequest_Closed(benchmark::State& state) {
  auto cb = CircuitBreaker("test", 5, std::chrono::seconds(10), std::chrono::seconds(5));

  for (auto _ : state) {
    bool allowed = cb.allowRequest();
    benchmark::DoNotOptimize(allowed);
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CircuitBreaker_AllowRequest_Closed);

// Benchmark: recordSuccess
static void BM_CircuitBreaker_RecordSuccess(benchmark::State& state) {
  auto cb = CircuitBreaker("test", 5, std::chrono::seconds(10), std::chrono::seconds(5));

  for (auto _ : state) {
    cb.recordSuccess();
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CircuitBreaker_RecordSuccess);

// Benchmark: recordFailure
static void BM_CircuitBreaker_RecordFailure(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    auto cb = CircuitBreaker("test", 100, std::chrono::seconds(10), std::chrono::seconds(5));
    state.ResumeTiming();

    cb.recordFailure();
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CircuitBreaker_RecordFailure);

// Benchmark: State transition (closed -> open)
static void BM_CircuitBreaker_StateTransition(benchmark::State& state) {
  int32_t failure_threshold = static_cast<int32_t>(state.range(0));

  for (auto _ : state) {
    auto cb = CircuitBreaker("test",
                             failure_threshold,
                             std::chrono::seconds(10),
                             std::chrono::seconds(5));

    // Trigger failures to open circuit
    for (int32_t i = 0; i < failure_threshold; ++i) {
      cb.recordFailure();
    }

    // Verify it's open
    bool allowed = cb.allowRequest();
    benchmark::DoNotOptimize(allowed);
  }

  state.counters["threshold"] = failure_threshold;
}
BENCHMARK(BM_CircuitBreaker_StateTransition)->Range(1, 128);

// Benchmark: getState
static void BM_CircuitBreaker_GetState(benchmark::State& state) {
  auto cb = CircuitBreaker("test", 5, std::chrono::seconds(10), std::chrono::seconds(5));

  for (auto _ : state) {
    auto state_val = cb.getState();
    benchmark::DoNotOptimize(state_val);
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CircuitBreaker_GetState);

// Benchmark: Concurrent circuit breaker access
static void BM_CircuitBreaker_Concurrent(benchmark::State& state) {
  static CircuitBreaker cb("test", 100, std::chrono::seconds(10), std::chrono::seconds(5));

  for (auto _ : state) {
    if (cb.allowRequest()) {
      // Simulate success
      cb.recordSuccess();
    }
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CircuitBreaker_Concurrent)->ThreadRange(1, 8);

// ==========================
// Heartbeat Monitor Benchmarks
// ==========================

// Benchmark: Heartbeat monitor creation
static void BM_HeartbeatMonitor_Creation(benchmark::State& state) {
  for (auto _ : state) {
    auto monitor = HeartbeatMonitor(std::chrono::milliseconds(1000));
    benchmark::DoNotOptimize(monitor);
  }
}
BENCHMARK(BM_HeartbeatMonitor_Creation);

// Benchmark: registerAgent
static void BM_HeartbeatMonitor_RegisterAgent(benchmark::State& state) {
  HeartbeatMonitor monitor(std::chrono::milliseconds(1000));

  size_t count = 0;
  for (auto _ : state) {
    monitor.registerAgent("agent-" + std::to_string(count++));
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HeartbeatMonitor_RegisterAgent);

// Benchmark: recordHeartbeat
static void BM_HeartbeatMonitor_RecordHeartbeat(benchmark::State& state) {
  HeartbeatMonitor monitor(std::chrono::milliseconds(1000));
  monitor.registerAgent("test-agent");

  for (auto _ : state) {
    monitor.recordHeartbeat("test-agent");
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HeartbeatMonitor_RecordHeartbeat);

// Benchmark: isAgentAlive check
static void BM_HeartbeatMonitor_IsAgentAlive(benchmark::State& state) {
  HeartbeatMonitor monitor(std::chrono::seconds(10));
  monitor.registerAgent("test-agent");
  monitor.recordHeartbeat("test-agent");

  for (auto _ : state) {
    bool alive = monitor.isAgentAlive("test-agent");
    benchmark::DoNotOptimize(alive);
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HeartbeatMonitor_IsAgentAlive);

// Benchmark: getDeadAgents
static void BM_HeartbeatMonitor_GetDeadAgents(benchmark::State& state) {
  int32_t num_agents = static_cast<int32_t>(state.range(0));

  HeartbeatMonitor monitor(std::chrono::milliseconds(100));

  for (int32_t i = 0; i < num_agents; ++i) {
    monitor.registerAgent("agent-" + std::to_string(i));
  }

  // Let some agents go stale
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  for (auto _ : state) {
    auto dead = monitor.getDeadAgents();
    benchmark::DoNotOptimize(dead);
  }

  state.counters["agents"] = num_agents;
}
BENCHMARK(BM_HeartbeatMonitor_GetDeadAgents)->Range(8, 512);

// Benchmark: Concurrent heartbeat recording
static void BM_HeartbeatMonitor_ConcurrentHeartbeat(benchmark::State& state) {
  static HeartbeatMonitor monitor(std::chrono::seconds(10));
  static std::atomic<bool> initialized{false};

  if (!initialized.exchange(true)) {
    for (int32_t i = 0; i < 100; ++i) {
      monitor.registerAgent("agent-" + std::to_string(i));
    }
  }

  int32_t agent_idx = static_cast<int32_t>(state.thread_index()) % 100;
  std::string agent_id = "agent-" + std::to_string(agent_idx);

  for (auto _ : state) {
    monitor.recordHeartbeat(agent_id);
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HeartbeatMonitor_ConcurrentHeartbeat)->ThreadRange(1, 8);

BENCHMARK_MAIN();
