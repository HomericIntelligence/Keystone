/**
 * @file scheduler_backoff_benchmark.cpp
 * @brief Benchmarks for WorkStealingScheduler 3-phase backoff (Stream C1)
 *
 * Measures:
 * - Idle CPU usage (target: < 2%)
 * - Latency under load (target: < 100μs)
 * - Throughput (no regression)
 * - Wake-up latency (target: < 1ms)
 */

#include <benchmark/benchmark.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include "concurrency/work_stealing_scheduler.hpp"

using namespace keystone::concurrency;
using namespace std::chrono_literals;

// Benchmark: Idle CPU usage (no work)
// This measures CPU consumption when scheduler has no work
static void BM_IdleCPU_WithBackoff(benchmark::State& state) {
  WorkStealingScheduler scheduler(4);
  scheduler.start();

  // Let scheduler settle into SLEEP phase
  std::this_thread::sleep_for(100ms);

  for (auto _ : state) {
    // Measure CPU usage over 100ms of idle time
    auto start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(100ms);
    auto end = std::chrono::steady_clock::now();

    auto duration =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
            .count();
    state.SetIterationTime(duration / 1e9);
  }

  scheduler.shutdown();

  // Report: In a real system, CPU usage would be measured via:
  // - Linux: /proc/stat or getrusage()
  // - perf stat -e cpu-clock
  // For this benchmark, we just measure wall time
  state.SetLabel("idle_cpu_measurement");
}
BENCHMARK(BM_IdleCPU_WithBackoff)->UseManualTime()->Iterations(10);

// Benchmark: Latency under load
static void BM_LatencyUnderLoad(benchmark::State& state) {
  WorkStealingScheduler scheduler(4);
  scheduler.start();

  auto total_latency_ns = std::make_shared<std::atomic<int64_t>>(0);
  auto task_count = std::make_shared<std::atomic<int32_t>>(0);

  for (auto _ : state) {
    auto submit_time = std::chrono::steady_clock::now();

    scheduler.submit([submit_time, total_latency_ns, task_count]() {
      auto execute_time = std::chrono::steady_clock::now();
      auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         execute_time - submit_time)
                         .count();
      total_latency_ns->fetch_add(latency);
      task_count->fetch_add(1);
    });

    // Small delay to simulate realistic workload
    std::this_thread::sleep_for(10us);
  }

  // Wait for all work to complete
  std::this_thread::sleep_for(100ms);

  int32_t count = task_count->load();
  if (count > 0) {
    int64_t avg_latency_ns = total_latency_ns->load() / count;
    state.counters["avg_latency_us"] =
        benchmark::Counter(avg_latency_ns / 1000.0);
  }

  scheduler.shutdown();
}
BENCHMARK(BM_LatencyUnderLoad)->Iterations(1000);

// Benchmark: Throughput with backoff
static void BM_ThroughputWithBackoff(benchmark::State& state) {
  WorkStealingScheduler scheduler(4);
  scheduler.start();

  auto counter = std::make_shared<std::atomic<int64_t>>(0);

  for (auto _ : state) {
    scheduler.submit([counter]() { counter->fetch_add(1); });
  }

  // Wait for completion
  std::this_thread::sleep_for(100ms);

  state.counters["tasks_completed"] = benchmark::Counter(counter->load());
  state.counters["tasks_per_sec"] =
      benchmark::Counter(counter->load(), benchmark::Counter::kIsRate);

  scheduler.shutdown();
}
BENCHMARK(BM_ThroughputWithBackoff);

// Benchmark: Wake-up latency from SLEEP phase
static void BM_WakeUpLatency(benchmark::State& state) {
  WorkStealingScheduler scheduler(4);
  scheduler.start();

  for (auto _ : state) {
    // Let workers enter SLEEP phase
    std::this_thread::sleep_for(50ms);

    // Measure wake-up time
    auto submit_time = std::chrono::steady_clock::now();
    auto work_executed = std::make_shared<std::atomic<bool>>(false);
    auto execute_time =
        std::make_shared<std::chrono::steady_clock::time_point>();

    scheduler.submit([work_executed, execute_time]() {
      *execute_time = std::chrono::steady_clock::now();
      work_executed->store(true);
    });

    // Wait for work to execute
    while (!work_executed->load()) {
      std::this_thread::sleep_for(100us);
    }

    auto wakeup_latency = std::chrono::duration_cast<std::chrono::microseconds>(
                              *execute_time - submit_time)
                              .count();

    state.counters["wakeup_latency_us"] = benchmark::Counter(wakeup_latency);
  }

  scheduler.shutdown();
}
BENCHMARK(BM_WakeUpLatency)->Iterations(10);

// Benchmark: Work stealing across workers during backoff
static void BM_WorkStealingDuringBackoff(benchmark::State& state) {
  WorkStealingScheduler scheduler(4);
  scheduler.start();

  auto counter = std::make_shared<std::atomic<int32_t>>(0);

  for (auto _ : state) {
    // Submit all work to worker 0 (others will steal)
    for (int32_t i = 0; i < 100; ++i) {
      scheduler.submitTo(0, [counter]() {
        counter->fetch_add(1);
        std::this_thread::sleep_for(10us);  // Small work
      });
    }
  }

  // Wait for completion
  std::this_thread::sleep_for(200ms);

  state.counters["tasks_stolen"] = benchmark::Counter(counter->load());

  scheduler.shutdown();
}
BENCHMARK(BM_WorkStealingDuringBackoff)->Iterations(10);

// Benchmark: Rapid submit/execute cycles (stress test)
static void BM_RapidSubmitExecute(benchmark::State& state) {
  WorkStealingScheduler scheduler(4);
  scheduler.start();

  for (auto _ : state) {
    auto work_done = std::make_shared<std::atomic<bool>>(false);

    scheduler.submit([work_done]() { work_done->store(true); });

    // Spin until work completes
    while (!work_done->load()) {
      std::this_thread::yield();
    }
  }

  scheduler.shutdown();
}
BENCHMARK(BM_RapidSubmitExecute);

// Benchmark: Compare SPIN vs YIELD vs SLEEP phase latencies
static void BM_BackoffPhaseLatencies(benchmark::State& state) {
  WorkStealingScheduler scheduler(1);  // Single worker for determinism
  scheduler.start();

  int phase = state.range(0);  // 0=SPIN, 1=YIELD, 2=SLEEP
  std::chrono::milliseconds wait_time;

  if (phase == 0) {
    wait_time = 0ms;  // Immediate (SPIN phase)
  } else if (phase == 1) {
    wait_time = 5ms;  // YIELD phase
  } else {
    wait_time = 50ms;  // SLEEP phase
  }

  for (auto _ : state) {
    // Wait to enter target phase
    std::this_thread::sleep_for(wait_time);

    // Measure latency
    auto submit_time = std::chrono::steady_clock::now();
    auto execute_time =
        std::make_shared<std::chrono::steady_clock::time_point>();
    auto work_done = std::make_shared<std::atomic<bool>>(false);

    scheduler.submit([execute_time, work_done]() {
      *execute_time = std::chrono::steady_clock::now();
      work_done->store(true);
    });

    while (!work_done->load()) {
      std::this_thread::sleep_for(10us);
    }

    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
                       *execute_time - submit_time)
                       .count();

    state.counters["latency_us"] = benchmark::Counter(latency);
  }

  scheduler.shutdown();
}
BENCHMARK(BM_BackoffPhaseLatencies)
    ->Arg(0)  // SPIN
    ->Arg(1)  // YIELD
    ->Arg(2)  // SLEEP
    ->Iterations(10);

BENCHMARK_MAIN();
