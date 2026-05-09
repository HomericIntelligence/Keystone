/**
 * @file hmas_load_test.cpp
 * @brief Load testing harness for HMAS production readiness validation
 *
 * Phase 6.7 M1 (P1): Determine resource requirements and system capacity
 *
 * Test Scenarios:
 * 1. Sustained Load - Find maximum sustainable throughput
 * 2. Burst Load - Validate handling of traffic spikes
 * 3. Large Hierarchy - Test scalability with maximum agent count
 * 4. Priority Fairness - Verify QoS under load
 * 5. Chaos + Load - Validate resilience under failures
 *
 * Usage:
 *   ./hmas_load_test --scenario=sustained_load --duration=600
 *   ./hmas_load_test --scenario=burst --duration=300 --message-rate=500
 *   ./hmas_load_test --all-scenarios
 */

#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <thread>
#include <vector>

#include "agents/chief_architect_agent.hpp"
#include "agents/component_lead_agent.hpp"
#include "agents/module_lead_agent.hpp"
#include "agents/task_agent.hpp"
#include "core/message_bus.hpp"
#include "core/metrics.hpp"

using namespace keystone;
using namespace std::chrono_literals;

/**
 * @brief Load test configuration
 */
struct LoadTestConfig {
  std::string scenario;
  int32_t duration_seconds{600};
  int32_t message_rate{100};  // messages per second
  int32_t num_component_leads{2};
  int32_t num_module_leads{4};
  int32_t num_task_agents{16};
  double high_priority_pct{0.2};
  double normal_priority_pct{0.7};
  double low_priority_pct{0.1};
  std::string output_file;
  bool verbose{false};
};

/**
 * @brief Message generator - produces messages at specified rate
 */
class MessageGenerator {
 public:
  explicit MessageGenerator(const LoadTestConfig& config)
      : config_(config), running_(false), gen_(rd_()) {
    priority_dist_ = std::uniform_real_distribution<>(0.0, 1.0);
  }

  void start() {
    running_ = true;
    generator_thread_ = std::thread([this]() { this->generateLoop(); });
  }

  void stop() {
    running_ = false;
    if (generator_thread_.joinable()) {
      generator_thread_.join();
    }
  }

  uint64_t getTotalGenerated() const { return total_generated_.load(); }

  std::queue<core::KeystoneMessage>& getMessageQueue() {
    return message_queue_;
  }
  std::mutex& getQueueMutex() { return queue_mutex_; }

 private:
  void generateLoop() {
    using clock = std::chrono::steady_clock;
    using duration = std::chrono::microseconds;

    const auto interval = duration(1000000 / config_.message_rate);
    auto next_send = clock::now();

    while (running_) {
      // Wait until next send time
      std::this_thread::sleep_until(next_send);
      next_send += interval;

      // Generate message with priority distribution
      auto msg = createMessage();

      // Add to queue
      {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        message_queue_.push(msg);
      }

      total_generated_++;
    }
  }

  core::KeystoneMessage createMessage() {
    auto msg = core::KeystoneMessage::create(
        "load_generator", "chief_architect",
        "echo test_" + std::to_string(total_generated_.load()));

    // Assign priority based on distribution
    double rand_val = priority_dist_(gen_);
    if (rand_val < config_.high_priority_pct) {
      msg.priority = core::Priority::HIGH;
    } else if (rand_val <
               config_.high_priority_pct + config_.normal_priority_pct) {
      msg.priority = core::Priority::NORMAL;
    } else {
      msg.priority = core::Priority::LOW;
    }

    return msg;
  }

  const LoadTestConfig& config_;
  std::atomic<bool> running_;
  std::thread generator_thread_;
  std::atomic<uint64_t> total_generated_{0};

  std::queue<core::KeystoneMessage> message_queue_;
  std::mutex queue_mutex_;

  std::random_device rd_;
  std::mt19937 gen_;
  std::uniform_real_distribution<> priority_dist_;
};

/**
 * @brief Metrics collector - samples metrics during load test
 */
class MetricsCollector {
 public:
  explicit MetricsCollector(int32_t sample_interval_ms = 1000)
      : sample_interval_(sample_interval_ms), running_(false) {}

  void start() {
    running_ = true;
    collector_thread_ = std::thread([this]() { this->collectLoop(); });
  }

  void stop() {
    running_ = false;
    if (collector_thread_.joinable()) {
      collector_thread_.join();
    }
  }

  struct MetricsSample {
    std::chrono::steady_clock::time_point timestamp;
    uint64_t messages_sent;
    uint64_t messages_processed;
    double messages_per_second;
    double avg_latency_us;
    size_t max_queue_depth;
    double worker_utilization;
    uint64_t deadline_misses;
    uint64_t high_count;
    uint64_t normal_count;
    uint64_t low_count;
  };

  const std::vector<MetricsSample>& getSamples() const { return samples_; }

 private:
  void collectLoop() {
    using clock = std::chrono::steady_clock;

    while (running_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(sample_interval_));

      auto& metrics = core::Metrics::getInstance();

      MetricsSample sample;
      sample.timestamp = clock::now();
      sample.messages_sent = metrics.getTotalMessagesSent();
      sample.messages_processed = metrics.getTotalMessagesProcessed();
      sample.messages_per_second = metrics.getMessagesPerSecond();

      auto latency = metrics.getAverageLatencyUs();
      sample.avg_latency_us = latency.value_or(0.0);

      sample.max_queue_depth = metrics.getMaxQueueDepth();
      sample.worker_utilization = metrics.getWorkerUtilization();
      sample.deadline_misses = metrics.getTotalDeadlineMisses();

      auto prio_stats = metrics.getPriorityStats();
      sample.high_count = prio_stats.high_count;
      sample.normal_count = prio_stats.normal_count;
      sample.low_count = prio_stats.low_count;

      samples_.push_back(sample);
    }
  }

  int32_t sample_interval_;
  std::atomic<bool> running_;
  std::thread collector_thread_;
  std::vector<MetricsSample> samples_;
};

/**
 * @brief Load test harness - manages agent lifecycle and orchestration
 */
class LoadTestHarness {
 public:
  explicit LoadTestHarness(const LoadTestConfig& config) : config_(config) {
    message_bus_ = std::make_unique<core::MessageBus>();
  }

  bool setup() {
    std::cout << "Setting up " << getTotalAgents() << " agents...\n";

    // Create Chief Architect (L0)
    chief_ = std::make_shared<agents::ChiefArchitectAgent>("chief_architect");
    chief_->setMessageBus(message_bus_.get());
    message_bus_->registerAgent(chief_->getAgentId(), chief_);

    // Create Component Leads (L1)
    for (int32_t i = 0; i < config_.num_component_leads; ++i) {
      auto comp = std::make_shared<agents::ComponentLeadAgent>(
          "component_lead_" + std::to_string(i));
      comp->setMessageBus(message_bus_.get());
      message_bus_->registerAgent(comp->getAgentId(), comp);
      component_leads_.push_back(comp);
    }

    // Create Module Leads (L2)
    for (int32_t i = 0; i < config_.num_module_leads; ++i) {
      auto mod = std::make_shared<agents::ModuleLeadAgent>("module_lead_" +
                                                           std::to_string(i));
      mod->setMessageBus(message_bus_.get());
      message_bus_->registerAgent(mod->getAgentId(), mod);
      module_leads_.push_back(mod);

      // Distribute module leads to component leads
      int32_t comp_idx = i % config_.num_component_leads;
      std::vector<std::string> module_ids;
      for (const auto& m : module_leads_) {
        module_ids.push_back(m->getAgentId());
      }
      component_leads_[comp_idx]->setAvailableModuleLeads(module_ids);
    }

    // Create Task Agents (L3)
    for (int32_t i = 0; i < config_.num_task_agents; ++i) {
      auto task = std::make_shared<agents::TaskAgent>("task_agent_" +
                                                      std::to_string(i));
      task->setMessageBus(message_bus_.get());
      message_bus_->registerAgent(task->getAgentId(), task);
      task_agents_.push_back(task);

      // Distribute task agents to module leads
      int32_t mod_idx = i % config_.num_module_leads;
      std::vector<std::string> task_ids;
      for (const auto& t : task_agents_) {
        task_ids.push_back(t->getAgentId());
      }
      module_leads_[mod_idx]->setAvailableTaskAgents(task_ids);
    }

    std::cout << "✓ Agents created successfully\n";
    std::cout << "  L0 (Chief): 1\n";
    std::cout << "  L1 (ComponentLeads): " << config_.num_component_leads
              << "\n";
    std::cout << "  L2 (ModuleLeads): " << config_.num_module_leads << "\n";
    std::cout << "  L3 (TaskAgents): " << config_.num_task_agents << "\n";

    return true;
  }

  void runTest() {
    std::cout << "\n=== Starting Load Test: " << config_.scenario << " ===\n";
    std::cout << "Duration: " << config_.duration_seconds << "s\n";
    std::cout << "Message rate: " << config_.message_rate << " msg/s\n\n";

    // Reset metrics
    core::Metrics::getInstance().reset();

    // Start metrics collector
    MetricsCollector collector;
    collector.start();

    // Start message generator
    MessageGenerator generator(config_);
    generator.start();

    // Run for specified duration
    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::seconds(config_.duration_seconds);

    int32_t last_progress = 0;
    while (std::chrono::steady_clock::now() < end_time) {
      // Send messages from generator queue to chief
      processGeneratorQueue(generator);

      // Show progress
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::steady_clock::now() - start_time)
                         .count();
      int32_t progress = (elapsed * 100) / config_.duration_seconds;
      if (progress > last_progress && progress % 10 == 0) {
        std::cout << "Progress: " << progress << "% (" << elapsed << "s)\n";
        last_progress = progress;
      }

      std::this_thread::sleep_for(100ms);
    }

    // Stop generator and collector
    generator.stop();
    collector.stop();

    std::cout << "\n✓ Load test completed\n";

    // Analyze and report results
    analyzeResults(collector);
  }

  void teardown() {
    std::cout << "\nTearing down agents...\n";

    // Unregister all agents
    for (const auto& agent : task_agents_) {
      message_bus_->unregisterAgent(agent->getAgentId());
    }
    for (const auto& agent : module_leads_) {
      message_bus_->unregisterAgent(agent->getAgentId());
    }
    for (const auto& agent : component_leads_) {
      message_bus_->unregisterAgent(agent->getAgentId());
    }
    message_bus_->unregisterAgent(chief_->getAgentId());

    std::cout << "✓ Teardown complete\n";
  }

 private:
  int32_t getTotalAgents() const {
    return 1 + config_.num_component_leads + config_.num_module_leads +
           config_.num_task_agents;
  }

  void processGeneratorQueue(MessageGenerator& generator) {
    std::lock_guard<std::mutex> lock(generator.getQueueMutex());
    auto& queue = generator.getMessageQueue();

    while (!queue.empty()) {
      auto msg = queue.front();
      queue.pop();

      // Send via chief architect
      chief_->sendMessage(msg);
    }
  }

  void analyzeResults(const MetricsCollector& collector) {
    const auto& samples = collector.getSamples();
    if (samples.empty()) {
      std::cerr << "No metrics samples collected!\n";
      return;
    }

    std::cout << "\n=== Load Test Results ===\n\n";

    // Calculate statistics
    auto& metrics = core::Metrics::getInstance();

    std::cout << "Total Messages:\n";
    std::cout << "  Sent: " << metrics.getTotalMessagesSent() << "\n";
    std::cout << "  Processed: " << metrics.getTotalMessagesProcessed() << "\n";

    auto prio_stats = metrics.getPriorityStats();
    std::cout << "\nPriority Distribution:\n";
    std::cout << "  HIGH: " << prio_stats.high_count << " ("
              << (prio_stats.high_count * 100.0 /
                  metrics.getTotalMessagesSent())
              << "%)\n";
    std::cout << "  NORMAL: " << prio_stats.normal_count << " ("
              << (prio_stats.normal_count * 100.0 /
                  metrics.getTotalMessagesSent())
              << "%)\n";
    std::cout << "  LOW: " << prio_stats.low_count << " ("
              << (prio_stats.low_count * 100.0 / metrics.getTotalMessagesSent())
              << "%)\n";

    std::cout << "\nThroughput:\n";
    std::cout << "  Average: " << metrics.getMessagesPerSecond() << " msg/s\n";

    auto latency = metrics.getAverageLatencyUs();
    std::cout << "\nLatency:\n";
    std::cout << "  Average: " << (latency.value_or(0.0) / 1000.0) << " ms\n";

    std::cout << "\nQueue Depth:\n";
    std::cout << "  Maximum: " << metrics.getMaxQueueDepth() << " messages\n";

    std::cout << "\nDeadlines:\n";
    std::cout << "  Misses: " << metrics.getTotalDeadlineMisses() << "\n";

    // Generate report
    std::cout << "\n" << metrics.generateReport() << "\n";

    // Save to file if specified
    if (!config_.output_file.empty()) {
      saveResultsToFile(samples);
    }
  }

  void saveResultsToFile(
      const std::vector<MetricsCollector::MetricsSample>& samples) {
    std::ofstream out(config_.output_file);
    if (!out.is_open()) {
      std::cerr << "Failed to open output file: " << config_.output_file
                << "\n";
      return;
    }

    out << "{\n";
    out << "  \"scenario\": \"" << config_.scenario << "\",\n";
    out << "  \"duration_seconds\": " << config_.duration_seconds << ",\n";
    out << "  \"message_rate\": " << config_.message_rate << ",\n";
    out << "  \"num_agents\": " << getTotalAgents() << ",\n";
    out << "  \"samples\": [\n";

    for (size_t i = 0; i < samples.size(); ++i) {
      const auto& s = samples[i];
      out << "    {\n";
      out << "      \"messages_sent\": " << s.messages_sent << ",\n";
      out << "      \"messages_processed\": " << s.messages_processed << ",\n";
      out << "      \"messages_per_second\": " << s.messages_per_second
          << ",\n";
      out << "      \"avg_latency_us\": " << s.avg_latency_us << ",\n";
      out << "      \"max_queue_depth\": " << s.max_queue_depth << ",\n";
      out << "      \"worker_utilization\": " << s.worker_utilization << ",\n";
      out << "      \"deadline_misses\": " << s.deadline_misses << "\n";
      out << "    }" << (i < samples.size() - 1 ? "," : "") << "\n";
    }

    out << "  ]\n";
    out << "}\n";
    out.close();

    std::cout << "✓ Results saved to " << config_.output_file << "\n";
  }

  const LoadTestConfig& config_;
  std::unique_ptr<core::MessageBus> message_bus_;

  std::shared_ptr<agents::ChiefArchitectAgent> chief_;
  std::vector<std::shared_ptr<agents::ComponentLeadAgent>> component_leads_;
  std::vector<std::shared_ptr<agents::ModuleLeadAgent>> module_leads_;
  std::vector<std::shared_ptr<agents::TaskAgent>> task_agents_;
};

/**
 * @brief Parse command line arguments
 */
LoadTestConfig parseArgs(int argc, char** argv) {
  LoadTestConfig config;

  for (int32_t i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg.find("--scenario=") == 0) {
      config.scenario = arg.substr(11);
    } else if (arg.find("--duration=") == 0) {
      config.duration_seconds = std::stoi(arg.substr(11));
    } else if (arg.find("--message-rate=") == 0) {
      config.message_rate = std::stoi(arg.substr(15));
    } else if (arg.find("--output=") == 0) {
      config.output_file = arg.substr(9);
    } else if (arg == "--verbose" || arg == "-v") {
      config.verbose = true;
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: hmas_load_test [OPTIONS]\n\n";
      std::cout << "Options:\n";
      std::cout << "  --scenario=NAME      Load test scenario to run\n";
      std::cout << "  --duration=SECONDS   Test duration (default: 600)\n";
      std::cout
          << "  --message-rate=N     Messages per second (default: 100)\n";
      std::cout << "  --output=FILE        Save results to JSON file\n";
      std::cout << "  --verbose, -v        Verbose output\n";
      std::cout << "  --help, -h           Show this help\n\n";
      std::cout << "Scenarios:\n";
      std::cout
          << "  sustained_load       Find maximum sustainable throughput\n";
      std::cout << "  burst                Validate traffic spike handling\n";
      std::cout << "  large_hierarchy      Test scalability\n";
      std::cout << "  priority_fairness    Verify QoS\n";
      std::cout << "  chaos                Resilience under failures\n";
      std::exit(0);
    }
  }

  // Set defaults based on scenario
  if (config.scenario.empty()) {
    config.scenario = "sustained_load";
  }

  if (config.scenario == "burst") {
    config.duration_seconds = 300;
    config.message_rate = 500;
  } else if (config.scenario == "large_hierarchy") {
    config.num_component_leads = 4;
    config.num_module_leads = 12;
    config.num_task_agents = 48;
  } else if (config.scenario == "priority_fairness") {
    config.duration_seconds = 180;
    config.message_rate = 300;
    config.high_priority_pct = 0.5;
    config.normal_priority_pct = 0.4;
    config.low_priority_pct = 0.1;
  }

  // Auto-generate output filename if not specified
  if (config.output_file.empty()) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "results/" << config.scenario << "_"
       << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S") << ".json";
    config.output_file = ss.str();
  }

  return config;
}

int main(int argc, char** argv) {
  std::cout << "=== ProjectKeystone HMAS Load Test ===\n\n";

  auto config = parseArgs(argc, argv);

  LoadTestHarness harness(config);

  if (!harness.setup()) {
    std::cerr << "Failed to setup load test harness\n";
    return 1;
  }

  harness.runTest();
  harness.teardown();

  std::cout << "\n✓ Load test completed successfully\n";
  return 0;
}
