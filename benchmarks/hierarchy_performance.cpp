/**
 * @file hierarchy_performance.cpp
 * @brief Performance benchmarks for HMAS hierarchy
 *
 * Phase 3.1: Re-enabled with unified async API
 *
 * Measures:
 * - Message throughput across hierarchy levels
 * - Latency of task execution through agent layers
 * - Scalability with varying task counts
 * - Work-stealing scheduler efficiency
 */

#include "agents/chief_architect_agent.hpp"
#include "agents/component_lead_agent.hpp"
#include "agents/module_lead_agent.hpp"
#include "agents/task_agent.hpp"
#include "concurrency/work_stealing_scheduler.hpp"
#include "core/message_bus.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <benchmark/benchmark.h>

using namespace keystone;
using namespace keystone::agents;
using namespace keystone::core;
using namespace keystone::concurrency;
using namespace std::chrono_literals;

/**
 * Performance Benchmarks: HMAS Agent Hierarchy
 *
 * These benchmarks measure:
 * - Message routing latency through hierarchy
 * - Task execution throughput with varying worker counts
 * - Scalability with changing agent counts
 * - Work-stealing scheduler efficiency
 */

// Benchmark: Message routing throughput (simple case)
static void BM_MessageRouting_Throughput(benchmark::State& state) {
  MessageBus bus;

  auto agent = std::make_shared<TaskAgent>("task");
  bus.registerAgent(agent->getAgentId(), agent);
  agent->setMessageBus(&bus);

  auto msg = KeystoneMessage::create("sender", "task", "test");

  for (auto _ : state) {
    bus.routeMessage(msg);
  }

  state.SetItemsProcessed(state.iterations());
  state.counters["messages/sec"] = benchmark::Counter(state.iterations(),
                                                      benchmark::Counter::kIsRate);
}
BENCHMARK(BM_MessageRouting_Throughput);

// Benchmark: 4-Layer Hierarchy Message Flow
// Chief -> Component -> Module -> Task (with 6 total tasks)
static void BM_4LayerHierarchy_MessageFlow(benchmark::State& state) {
  MessageBus bus;

  // Create hierarchy: Chief -> Component -> 2 Modules -> 6 Tasks
  auto chief = std::make_shared<ChiefArchitectAgent>("chief");
  auto comp_lead = std::make_shared<ComponentLeadAgent>("comp_lead");
  auto module1 = std::make_shared<ModuleLeadAgent>("module1");
  auto module2 = std::make_shared<ModuleLeadAgent>("module2");

  bus.registerAgent(chief->getAgentId(), chief);
  bus.registerAgent(comp_lead->getAgentId(), comp_lead);
  bus.registerAgent(module1->getAgentId(), module1);
  bus.registerAgent(module2->getAgentId(), module2);

  chief->setMessageBus(&bus);
  comp_lead->setMessageBus(&bus);
  module1->setMessageBus(&bus);
  module2->setMessageBus(&bus);

  std::vector<std::shared_ptr<TaskAgent>> tasks;
  for (int32_t i = 1; i <= 6; ++i) {
    auto task = std::make_shared<TaskAgent>("task" + std::to_string(i));
    bus.registerAgent(task->getAgentId(), task);
    task->setMessageBus(&bus);
    tasks.push_back(task);
  }

  // Benchmark: Route a message through the hierarchy
  for (auto _ : state) {
    auto msg = KeystoneMessage::create("chief",
                                       "comp_lead",
                                       "Component: ProcessData(100) and Analyze(200)");
    bus.routeMessage(msg);
  }

  state.SetItemsProcessed(state.iterations());
  state.counters["hierarchy_depth"] = 4;
  state.counters["total_agents"] = 9;
}
BENCHMARK(BM_4LayerHierarchy_MessageFlow);

// Benchmark: Task Agent Registry Lookup Scalability
static void BM_TaskAgent_RegistryLookup(benchmark::State& state) {
  int32_t num_agents = static_cast<int32_t>(state.range(0));

  MessageBus bus;
  std::vector<std::shared_ptr<TaskAgent>> agents;

  // Register N task agents
  for (int32_t i = 0; i < num_agents; ++i) {
    auto agent = std::make_shared<TaskAgent>("task" + std::to_string(i));
    bus.registerAgent(agent->getAgentId(), agent);
    agents.push_back(agent);
  }

  // Benchmark lookup operations
  int32_t lookup_idx = 0;
  for (auto _ : state) {
    std::string agent_id = "task" + std::to_string(lookup_idx % num_agents);
    benchmark::DoNotOptimize(bus.hasAgent(agent_id));
    lookup_idx++;
  }

  state.SetItemsProcessed(state.iterations());
  state.counters["agents"] = num_agents;
}
BENCHMARK(BM_TaskAgent_RegistryLookup)->Range(10, 1000);

// Benchmark: Scheduler Submission Rate (Work-Stealing)
static void BM_Scheduler_SubmissionRate(benchmark::State& state) {
  const int32_t num_workers = static_cast<int32_t>(state.range(0));

  WorkStealingScheduler scheduler(num_workers);
  scheduler.start();

  // Warmup: get workers ready
  for (int32_t i = 0; i < num_workers; ++i) {
    scheduler.submit([]() {
      volatile int32_t x = 42;
      (void)x;
    });
  }
  std::this_thread::sleep_for(10ms);

  // Benchmark submission rate
  for (auto _ : state) {
    scheduler.submit([]() {
      volatile int32_t sum = 0;
      for (int32_t j = 0; j < 10; ++j) {
        sum += j;
      }
    });
  }

  // Wait for all work to complete
  std::this_thread::sleep_for(100ms);

  scheduler.shutdown();
  state.SetItemsProcessed(state.iterations());
  state.counters["workers"] = num_workers;
  state.counters["tasks/sec"] = benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_Scheduler_SubmissionRate)->Arg(1)->Arg(2)->Arg(4)->Arg(8);

// Benchmark: Agent Message Processing Throughput
static void BM_Agent_MessageProcessing(benchmark::State& state) {
  MessageBus bus;
  auto agent = std::make_shared<TaskAgent>("processor");
  bus.registerAgent(agent->getAgentId(), agent);
  agent->setMessageBus(&bus);

  // Create messages to process
  for (auto _ : state) {
    auto msg = KeystoneMessage::create("sender", "processor", "echo 42");
    bus.routeMessage(msg);
  }

  state.SetItemsProcessed(state.iterations());
  state.counters["messages/sec"] = benchmark::Counter(state.iterations(),
                                                      benchmark::Counter::kIsRate);
}
BENCHMARK(BM_Agent_MessageProcessing);

// Benchmark: Component Leadership with Multiple Modules
static void BM_ComponentLead_MultiModule(benchmark::State& state) {
  const int32_t num_modules = static_cast<int32_t>(state.range(0));

  MessageBus bus;
  auto comp_lead = std::make_shared<ComponentLeadAgent>("comp_lead");
  bus.registerAgent(comp_lead->getAgentId(), comp_lead);
  comp_lead->setMessageBus(&bus);

  std::vector<std::shared_ptr<ModuleLeadAgent>> modules;
  for (int32_t i = 0; i < num_modules; ++i) {
    auto module = std::make_shared<ModuleLeadAgent>("module" + std::to_string(i));
    bus.registerAgent(module->getAgentId(), module);
    module->setMessageBus(&bus);
    modules.push_back(module);
  }

  // Benchmark: Route messages through component to modules
  for (auto _ : state) {
    auto msg = KeystoneMessage::create("chief",
                                       "comp_lead",
                                       "Component: Process(100) Analyze(200) Report(300)");
    bus.routeMessage(msg);
  }

  state.SetItemsProcessed(state.iterations());
  state.counters["modules"] = num_modules;
}
BENCHMARK(BM_ComponentLead_MultiModule)->Range(2, 16);

// Benchmark: Module Leadership with Multiple Tasks
static void BM_ModuleLead_MultiTask(benchmark::State& state) {
  const int32_t num_tasks = static_cast<int32_t>(state.range(0));

  MessageBus bus;
  auto module = std::make_shared<ModuleLeadAgent>("module");
  bus.registerAgent(module->getAgentId(), module);
  module->setMessageBus(&bus);

  std::vector<std::shared_ptr<TaskAgent>> tasks;
  for (int32_t i = 0; i < num_tasks; ++i) {
    auto task = std::make_shared<TaskAgent>("task" + std::to_string(i));
    bus.registerAgent(task->getAgentId(), task);
    task->setMessageBus(&bus);
    tasks.push_back(task);
  }

  // Benchmark: Route messages through module to tasks
  for (auto _ : state) {
    auto msg = KeystoneMessage::create("comp_lead",
                                       "module",
                                       "Module: Task1(10) Task2(20) Task3(30)");
    bus.routeMessage(msg);
  }

  state.SetItemsProcessed(state.iterations());
  state.counters["tasks"] = num_tasks;
}
BENCHMARK(BM_ModuleLead_MultiTask)->Range(3, 32);

BENCHMARK_MAIN();
