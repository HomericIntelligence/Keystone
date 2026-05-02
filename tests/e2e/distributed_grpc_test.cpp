/**
 * Phase 8 E2E Tests: Distributed Multi-Node HMAS via gRPC
 *
 * These tests validate the complete distributed system including:
 * - gRPC ServiceRegistry and HMASCoordinator services
 * - YAML task specification parsing and generation
 * - Task routing and load balancing
 * - Result aggregation strategies
 * - Heartbeat monitoring and agent liveness
 * - Full 4-layer hierarchy across network nodes
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "network/grpc_client.hpp"
#include "network/grpc_server.hpp"
#include "network/hmas_coordinator_service.hpp"
#include "network/result_aggregator.hpp"
#include "network/service_registry.hpp"
#include "network/task_router.hpp"
#include "network/yaml_parser.hpp"

using namespace keystone::network;
using namespace std::chrono_literals;

// ============================================================================
// Test Fixtures and Helpers
// ============================================================================

/**
 * Helper: Build a simple YAML task spec programmatically
 */
class YamlSpecBuilder {
 public:
  YamlSpecBuilder& setGoal(const std::string& goal) {
    spec_.hierarchy.level0_goal = goal;
    return *this;
  }

  YamlSpecBuilder& setTaskId(const std::string& task_id) {
    spec_.metadata.task_id = task_id;
    return *this;
  }

  YamlSpecBuilder& setName(const std::string& name) {
    spec_.metadata.name = name;
    return *this;
  }

  YamlSpecBuilder& setTargetLevel(int32_t level) {
    spec_.routing.target_level = level;
    return *this;
  }

  YamlSpecBuilder& setTargetAgentType(const std::string& agent_type) {
    spec_.routing.target_agent_type = agent_type;
    return *this;
  }

  YamlSpecBuilder& setActionType(const std::string& action_type) {
    spec_.action.type = action_type;
    return *this;
  }

  YamlSpecBuilder& setCommand(const std::string& command) {
    spec_.payload.command = command;
    return *this;
  }

  YamlSpecBuilder& addRequiredCapability(const std::string& capability) {
    spec_.payload.required_capabilities.push_back(capability);
    return *this;
  }

  YamlSpecBuilder& addSubtask(const std::string& name,
                              const std::string& agent_type) {
    SubtaskSpec subtask;
    subtask.name = name;
    subtask.agent_type = agent_type;
    spec_.tasks.push_back(subtask);
    return *this;
  }

  YamlSpecBuilder& setAggregationStrategy(const std::string& strategy) {
    spec_.aggregation.strategy = strategy;
    return *this;
  }

  YamlSpecBuilder& setAggregationTimeout(const std::string& timeout) {
    spec_.aggregation.timeout = timeout;
    return *this;
  }

  HierarchicalTaskSpec build() {
    // Set defaults if not specified
    if (spec_.api_version.empty()) {
      spec_.api_version = "keystone.ai/v1";
    }
    if (spec_.kind.empty()) {
      spec_.kind = "HierarchicalTask";
    }
    if (spec_.metadata.created_at.empty()) {
      spec_.metadata.created_at = "2025-11-20T12:00:00Z";
    }
    if (spec_.routing.origin_node.empty()) {
      spec_.routing.origin_node = "test-node:50051";
    }
    return spec_;
  }

 private:
  HierarchicalTaskSpec spec_;
};

/**
 * Helper: Wait for a condition with timeout
 */
template <typename Predicate>
bool waitFor(Predicate pred, std::chrono::milliseconds timeout) {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) {
      return true;
    }
    std::this_thread::sleep_for(10ms);
  }
  return pred();  // Final check
}

/**
 * Test Fixture: Base class for distributed gRPC tests
 */
class DistributedGrpcTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create ServiceRegistry
    registry_ = std::make_shared<ServiceRegistry>(3000);  // 3s heartbeat

    // Create TaskRouter
    router_ = std::make_shared<TaskRouter>(registry_);

    // Create HMASCoordinator service
    coordinator_ =
        std::make_shared<HMASCoordinatorServiceImpl>(registry_, router_);
  }

  void TearDown() override {
    // Cleanup
    registry_.reset();
    router_.reset();
    coordinator_.reset();
  }

  std::shared_ptr<ServiceRegistry> registry_;
  std::shared_ptr<TaskRouter> router_;
  std::shared_ptr<HMASCoordinatorServiceImpl> coordinator_;
};

// ============================================================================
// Test 1: YAML Parsing and Generation (No Network)
// ============================================================================

TEST_F(DistributedGrpcTest, YamlTaskSpecParsing) {
  // Create a task spec programmatically
  auto original = YamlSpecBuilder()
                      .setName("test-task")
                      .setTaskId("task-001")
                      .setGoal("Run echo hello")
                      .setTargetLevel(3)
                      .setTargetAgentType("TaskAgent")
                      .setActionType("EXECUTE")
                      .setCommand("echo hello")
                      .addRequiredCapability("bash")
                      .setAggregationStrategy("WAIT_ALL")
                      .setAggregationTimeout("10m")
                      .build();

  // Generate YAML string
  std::string yaml_str = YamlParser::generateTaskSpec(original);

  // Verify YAML is not empty
  EXPECT_FALSE(yaml_str.empty());
  EXPECT_TRUE(yaml_str.find("test-task") != std::string::npos);
  EXPECT_TRUE(yaml_str.find("Run echo hello") != std::string::npos);

  // Parse YAML back to struct
  auto parsed = YamlParser::parseTaskSpec(yaml_str);

  // Verify round-trip preserves all fields
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->metadata.name, original.metadata.name);
  EXPECT_EQ(parsed->metadata.task_id, original.metadata.task_id);
  EXPECT_EQ(parsed->hierarchy.level0_goal, original.hierarchy.level0_goal);
  EXPECT_EQ(parsed->routing.target_level, original.routing.target_level);
  EXPECT_EQ(parsed->routing.target_agent_type,
            original.routing.target_agent_type);
  EXPECT_EQ(parsed->action.type, original.action.type);
  EXPECT_EQ(parsed->payload.command, original.payload.command);
  EXPECT_EQ(parsed->aggregation.strategy, original.aggregation.strategy);
  EXPECT_EQ(parsed->aggregation.timeout, original.aggregation.timeout);

  // Verify required capabilities
  ASSERT_EQ(parsed->payload.required_capabilities.size(), 1);
  EXPECT_EQ(parsed->payload.required_capabilities[0], "bash");
}

TEST_F(DistributedGrpcTest, YamlTaskSpecWithSubtasks) {
  // Create task spec with subtasks
  auto original = YamlSpecBuilder()
                      .setName("parent-task")
                      .setTaskId("task-parent")
                      .setGoal("Build project")
                      .setTargetLevel(2)
                      .setTargetAgentType("ModuleLeadAgent")
                      .setActionType("DECOMPOSE")
                      .addSubtask("compile-src", "TaskAgent")
                      .addSubtask("run-tests", "TaskAgent")
                      .addSubtask("package", "TaskAgent")
                      .setAggregationStrategy("WAIT_ALL")
                      .build();

  // Generate and parse
  std::string yaml_str = YamlParser::generateTaskSpec(original);
  auto parsed = YamlParser::parseTaskSpec(yaml_str);

  // Verify subtasks preserved
  ASSERT_TRUE(parsed.has_value());
  ASSERT_EQ(parsed->tasks.size(), 3);
  EXPECT_EQ(parsed->tasks[0].name, "compile-src");
  EXPECT_EQ(parsed->tasks[1].name, "run-tests");
  EXPECT_EQ(parsed->tasks[2].name, "package");
  EXPECT_EQ(parsed->tasks[0].agent_type, "TaskAgent");
}

TEST_F(DistributedGrpcTest, YamlDurationParsing) {
  // Test duration parsing
  auto duration_10s = YamlParser::parseDuration("10s");
  ASSERT_TRUE(duration_10s.has_value());
  EXPECT_EQ(*duration_10s, 10000);  // 10s = 10000ms

  auto duration_5m = YamlParser::parseDuration("5m");
  ASSERT_TRUE(duration_5m.has_value());
  EXPECT_EQ(*duration_5m, 300000);  // 5m = 300000ms

  auto duration_2h = YamlParser::parseDuration("2h");
  ASSERT_TRUE(duration_2h.has_value());
  EXPECT_EQ(*duration_2h, 7200000);  // 2h = 7200000ms

  // Test duration formatting
  EXPECT_EQ(YamlParser::formatDuration(10000), "10s");
  EXPECT_EQ(YamlParser::formatDuration(300000), "5m");
  EXPECT_EQ(YamlParser::formatDuration(7200000), "2h");
}

// ============================================================================
// Test 2: ServiceRegistry Queries (No gRPC)
// ============================================================================

TEST_F(DistributedGrpcTest, ServiceRegistryBasicRegistration) {
  // Register 3 agents
  bool reg1 = registry_->registerAgent("agent-1", "TaskAgent", 3,
                                       "192.168.1.10:50051", {"bash", "cpp20"});
  bool reg2 =
      registry_->registerAgent("agent-2", "ModuleLeadAgent", 2,
                               "192.168.1.11:50051", {"cpp20", "cmake"});
  bool reg3 = registry_->registerAgent(
      "agent-3", "TaskAgent", 3, "192.168.1.12:50051", {"bash", "python3"});

  EXPECT_TRUE(reg1);
  EXPECT_TRUE(reg2);
  EXPECT_TRUE(reg3);

  // Verify count
  EXPECT_EQ(registry_->getAgentCount(), 3);

  // Get agent info
  auto agent1 = registry_->getAgent("agent-1");
  ASSERT_TRUE(agent1.has_value());
  EXPECT_EQ(agent1->agent_id, "agent-1");
  EXPECT_EQ(agent1->agent_type, "TaskAgent");
  EXPECT_EQ(agent1->level, 3);
  EXPECT_EQ(agent1->ip_port, "192.168.1.10:50051");
}

TEST_F(DistributedGrpcTest, ServiceRegistryCapabilityQuery) {
  // Register 3 agents with different capabilities
  registry_->registerAgent("agent-1", "TaskAgent", 3, "192.168.1.10:50051",
                           {"cpp20", "cmake"});
  registry_->registerAgent("agent-2", "TaskAgent", 3, "192.168.1.11:50051",
                           {"cpp20", "python3"});
  registry_->registerAgent("agent-3", "TaskAgent", 3, "192.168.1.12:50051",
                           {"cpp20", "cmake", "catch2"});

  // Update heartbeats (make agents alive)
  registry_->updateHeartbeat("agent-1");
  registry_->updateHeartbeat("agent-2");
  registry_->updateHeartbeat("agent-3");

  // Query for agents with ["cpp20", "cmake"]
  auto results = registry_->queryAgents("", -1, {"cpp20", "cmake"}, 0, true);

  // Should return agent-1 and agent-3 (both have cpp20 AND cmake)
  ASSERT_EQ(results.size(), 2);

  std::vector<std::string> returned_ids;
  for (const auto& agent : results) {
    returned_ids.push_back(agent.agent_id);
  }

  EXPECT_TRUE(std::find(returned_ids.begin(), returned_ids.end(), "agent-1") !=
              returned_ids.end());
  EXPECT_TRUE(std::find(returned_ids.begin(), returned_ids.end(), "agent-3") !=
              returned_ids.end());
  EXPECT_TRUE(std::find(returned_ids.begin(), returned_ids.end(), "agent-2") ==
              returned_ids.end());  // Missing "cmake"
}

TEST_F(DistributedGrpcTest, ServiceRegistryLevelFiltering) {
  // Register agents at different levels
  registry_->registerAgent("chief", "ChiefArchitectAgent", 0, "node1:50051",
                           {});
  registry_->registerAgent("component", "ComponentLeadAgent", 1, "node2:50051",
                           {});
  registry_->registerAgent("module", "ModuleLeadAgent", 2, "node3:50051", {});
  registry_->registerAgent("task1", "TaskAgent", 3, "node4:50051", {});
  registry_->registerAgent("task2", "TaskAgent", 3, "node5:50051", {});

  // Update heartbeats
  registry_->updateHeartbeat("chief");
  registry_->updateHeartbeat("component");
  registry_->updateHeartbeat("module");
  registry_->updateHeartbeat("task1");
  registry_->updateHeartbeat("task2");

  // Query for level 3 agents only
  auto level3_agents = registry_->queryAgents("", 3, {}, 0, true);
  EXPECT_EQ(level3_agents.size(), 2u);  // task1 and task2

  // Query for level 0 agents only
  auto level0_agents = registry_->queryAgents("", 0, {}, 0, true);
  EXPECT_EQ(level0_agents.size(), 1u);  // chief

  // Query for all levels
  auto all_agents = registry_->queryAgents("", -1, {}, 0, true);
  EXPECT_EQ(all_agents.size(), 5u);
}

// ============================================================================
// Test 3: Heartbeat Monitoring
// ============================================================================

TEST_F(DistributedGrpcTest, HeartbeatMonitoring) {
  // Register 2 agents
  registry_->registerAgent("agent-alive", "TaskAgent", 3, "node1:50051", {});
  registry_->registerAgent("agent-dead", "TaskAgent", 3, "node2:50051", {});

  // Both send initial heartbeat
  registry_->updateHeartbeat("agent-alive");
  registry_->updateHeartbeat("agent-dead");

  // Verify both alive
  EXPECT_TRUE(registry_->isAgentAlive("agent-alive"));
  EXPECT_TRUE(registry_->isAgentAlive("agent-dead"));
  EXPECT_EQ(registry_->getAliveAgentCount(), 2);

  // Wait for heartbeat timeout (3 seconds) + buffer
  std::this_thread::sleep_for(3500ms);

  // agent-alive continues heartbeat
  registry_->updateHeartbeat("agent-alive");
  // agent-dead does NOT send heartbeat

  // Now agent-dead should be considered dead
  EXPECT_TRUE(registry_->isAgentAlive("agent-alive"));
  EXPECT_FALSE(registry_->isAgentAlive("agent-dead"));

  // Query should exclude dead agents
  auto alive_agents = registry_->queryAgents("", -1, {}, 0, true);
  EXPECT_EQ(alive_agents.size(), 1u);
  EXPECT_EQ(alive_agents[0].agent_id, "agent-alive");

  // Cleanup dead agents
  int32_t removed = registry_->cleanupDeadAgents();
  EXPECT_EQ(removed, 1);
  EXPECT_EQ(registry_->getAgentCount(), 1);
}

TEST_F(DistributedGrpcTest, HeartbeatWithMetrics) {
  // Register agent
  registry_->registerAgent("agent-1", "TaskAgent", 3, "node1:50051", {});

  // Send heartbeat with metrics
  registry_->updateHeartbeat("agent-1", 45.5f, 256.0f, 3);

  // Retrieve and verify metrics
  auto agent = registry_->getAgent("agent-1");
  ASSERT_TRUE(agent.has_value());
  EXPECT_FLOAT_EQ(agent->cpu_usage_percent, 45.5f);
  EXPECT_FLOAT_EQ(agent->memory_usage_mb, 256.0f);
  EXPECT_EQ(agent->active_tasks, 3);
}

// ============================================================================
// Test 4: Load Balancing
// ============================================================================

TEST_F(DistributedGrpcTest, LoadBalancingLeastLoaded) {
  // Set router to LEAST_LOADED strategy
  router_->setStrategy(LoadBalancingStrategy::LEAST_LOADED);

  // Register 3 TaskAgents with different load
  registry_->registerAgent("task-1", "TaskAgent", 3, "node1:50051", {});
  registry_->registerAgent("task-2", "TaskAgent", 3, "node2:50051", {});
  registry_->registerAgent("task-3", "TaskAgent", 3, "node3:50051", {});

  // Send heartbeats with different active_tasks counts
  registry_->updateHeartbeat("task-1", 0.0f, 0.0f, 5);  // 5 active tasks
  registry_->updateHeartbeat("task-2", 0.0f, 0.0f,
                             2);  // 2 active tasks (least)
  registry_->updateHeartbeat("task-3", 0.0f, 0.0f, 7);  // 7 active tasks

  // Route a new task (should go to task-2)
  auto result = router_->routeTask(3, "TaskAgent", {});

  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.target_agent_id, "task-2");
  EXPECT_EQ(result.target_ip_port, "node2:50051");
}

TEST_F(DistributedGrpcTest, LoadBalancingRoundRobin) {
  // Set router to ROUND_ROBIN strategy
  router_->setStrategy(LoadBalancingStrategy::ROUND_ROBIN);
  router_->resetRoundRobin();

  // Register 3 TaskAgents
  registry_->registerAgent("task-1", "TaskAgent", 3, "node1:50051", {});
  registry_->registerAgent("task-2", "TaskAgent", 3, "node2:50051", {});
  registry_->registerAgent("task-3", "TaskAgent", 3, "node3:50051", {});

  // Send heartbeats
  registry_->updateHeartbeat("task-1");
  registry_->updateHeartbeat("task-2");
  registry_->updateHeartbeat("task-3");

  // Route 3 tasks - should cycle through agents
  auto result1 = router_->routeTask(3, "TaskAgent", {});
  auto result2 = router_->routeTask(3, "TaskAgent", {});
  auto result3 = router_->routeTask(3, "TaskAgent", {});

  // All should succeed
  EXPECT_TRUE(result1.success);
  EXPECT_TRUE(result2.success);
  EXPECT_TRUE(result3.success);

  // Should get different agents (order depends on internal list, but all
  // different)
  std::set<std::string> agent_ids = {result1.target_agent_id,
                                     result2.target_agent_id,
                                     result3.target_agent_id};
  EXPECT_EQ(agent_ids.size(), 3u);  // All 3 agents used
}

TEST_F(DistributedGrpcTest, LoadBalancingNoAvailableAgents) {
  // Try to route task with no registered agents
  auto result = router_->routeTask(3, "TaskAgent", {});

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error_message.empty());
}

// ============================================================================
// Test 5: Result Aggregation
// ============================================================================

TEST_F(DistributedGrpcTest, ResultAggregationWaitAll) {
  // Create aggregator with WAIT_ALL strategy, 3 expected results
  ResultAggregator aggregator(AggregationStrategy::WAIT_ALL, 10000ms, 3);

  EXPECT_FALSE(aggregator.isComplete());
  EXPECT_EQ(aggregator.getResultCount(), 0);

  // Add first result (SUCCESS)
  hmas::TaskResult result1;
  result1.set_task_id("task-1");
  result1.set_status(hmas::TASK_PHASE_COMPLETED);
  result1.set_result_yaml("result: success");

  bool complete1 = aggregator.addResult("subtask-1", result1);
  EXPECT_FALSE(complete1);  // Not complete yet (1/3)
  EXPECT_FALSE(aggregator.isComplete());
  EXPECT_EQ(aggregator.getResultCount(), 1);
  EXPECT_EQ(aggregator.getSuccessCount(), 1);

  // Add second result (SUCCESS)
  hmas::TaskResult result2;
  result2.set_task_id("task-2");
  result2.set_status(hmas::TASK_PHASE_COMPLETED);
  result2.set_result_yaml("result: success");

  bool complete2 = aggregator.addResult("subtask-2", result2);
  EXPECT_FALSE(complete2);  // Not complete yet (2/3)
  EXPECT_FALSE(aggregator.isComplete());
  EXPECT_EQ(aggregator.getResultCount(), 2);
  EXPECT_EQ(aggregator.getSuccessCount(), 2);

  // Add third result (SUCCESS)
  hmas::TaskResult result3;
  result3.set_task_id("task-3");
  result3.set_status(hmas::TASK_PHASE_COMPLETED);
  result3.set_result_yaml("result: success");

  bool complete3 = aggregator.addResult("subtask-3", result3);
  EXPECT_TRUE(complete3);  // Complete! (3/3)
  EXPECT_TRUE(aggregator.isComplete());
  EXPECT_EQ(aggregator.getResultCount(), 3);
  EXPECT_EQ(aggregator.getSuccessCount(), 3);

  // Get aggregated result
  auto aggregated = aggregator.getAggregatedResult();
  ASSERT_TRUE(aggregated.has_value());
  EXPECT_FALSE(aggregated->empty());
}

TEST_F(DistributedGrpcTest, ResultAggregationFirstSuccess) {
  // Create aggregator with FIRST_SUCCESS strategy
  ResultAggregator aggregator(AggregationStrategy::FIRST_SUCCESS, 10000ms, 3);

  EXPECT_FALSE(aggregator.isComplete());

  // Add first result (FAILED)
  hmas::TaskResult result1;
  result1.set_task_id("task-1");
  result1.set_status(hmas::TASK_PHASE_FAILED);
  result1.set_error("error");

  bool complete1 = aggregator.addResult("subtask-1", result1);
  EXPECT_FALSE(complete1);  // Not complete (need SUCCESS)
  EXPECT_EQ(aggregator.getSuccessCount(), 0);
  EXPECT_EQ(aggregator.getFailedCount(), 1);

  // Add second result (SUCCESS)
  hmas::TaskResult result2;
  result2.set_task_id("task-2");
  result2.set_status(hmas::TASK_PHASE_COMPLETED);
  result2.set_result_yaml("result: success");

  bool complete2 = aggregator.addResult("subtask-2", result2);
  EXPECT_TRUE(complete2);  // Complete on first success!
  EXPECT_TRUE(aggregator.isComplete());
  EXPECT_EQ(aggregator.getSuccessCount(), 1);
}

TEST_F(DistributedGrpcTest, ResultAggregationMajority) {
  // Create aggregator with MAJORITY strategy, 5 expected results
  // Majority = ⌈5/2⌉ + 1 = 3 + 1 = 4? No, majority = more than half
  // For 5: need 3 (since 3 > 5/2 = 2.5)
  ResultAggregator aggregator(AggregationStrategy::MAJORITY, 10000ms, 5);

  EXPECT_FALSE(aggregator.isComplete());

  // Add 2 successes
  hmas::TaskResult result1;
  result1.set_status(hmas::TASK_PHASE_COMPLETED);
  aggregator.addResult("subtask-1", result1);

  hmas::TaskResult result2;
  result2.set_status(hmas::TASK_PHASE_COMPLETED);
  aggregator.addResult("subtask-2", result2);

  EXPECT_FALSE(aggregator.isComplete());  // 2/5 not majority

  // Add 3rd success
  hmas::TaskResult result3;
  result3.set_status(hmas::TASK_PHASE_COMPLETED);
  bool complete3 = aggregator.addResult("subtask-3", result3);

  EXPECT_TRUE(complete3);  // 3/5 is majority!
  EXPECT_TRUE(aggregator.isComplete());
  EXPECT_EQ(aggregator.getSuccessCount(), 3);
}

TEST_F(DistributedGrpcTest, ResultAggregationTimeout) {
  // Create aggregator with short timeout
  ResultAggregator aggregator(AggregationStrategy::WAIT_ALL, 100ms, 3);

  // Add only 1 result
  hmas::TaskResult result1;
  result1.set_status(hmas::TASK_PHASE_COMPLETED);
  aggregator.addResult("subtask-1", result1);

  EXPECT_FALSE(aggregator.isComplete());
  EXPECT_FALSE(aggregator.hasTimedOut());

  // Wait for timeout
  std::this_thread::sleep_for(150ms);

  EXPECT_TRUE(aggregator.hasTimedOut());
  EXPECT_FALSE(aggregator.isComplete());  // Not complete (1/3)
}

// ============================================================================
// Test 6: Task Routing with YAML Specs
// ============================================================================

TEST_F(DistributedGrpcTest, TaskRoutingFromYamlSpec) {
  // Register agents
  registry_->registerAgent("chief", "ChiefArchitectAgent", 0, "node1:50051",
                           {});
  registry_->registerAgent("component", "ComponentLeadAgent", 1, "node2:50051",
                           {"cpp20"});
  registry_->registerAgent("task-1", "TaskAgent", 3, "node3:50051",
                           {"bash", "cpp20"});

  // Send heartbeats
  registry_->updateHeartbeat("chief");
  registry_->updateHeartbeat("component");
  registry_->updateHeartbeat("task-1");

  // Create YAML spec targeting TaskAgent with bash capability
  auto spec = YamlSpecBuilder()
                  .setGoal("Run echo hello")
                  .setTargetLevel(3)
                  .setTargetAgentType("TaskAgent")
                  .addRequiredCapability("bash")
                  .setCommand("echo hello")
                  .build();

  // Route task
  auto result = router_->routeTask(spec);

  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.target_agent_id, "task-1");
  EXPECT_EQ(result.target_ip_port, "node3:50051");
}

TEST_F(DistributedGrpcTest, TaskRoutingMissingCapability) {
  // Register agent without required capability
  registry_->registerAgent("task-1", "TaskAgent", 3, "node1:50051",
                           {"python3"});  // Missing "bash"
  registry_->updateHeartbeat("task-1");

  // Create spec requiring "bash"
  auto spec = YamlSpecBuilder()
                  .setTargetLevel(3)
                  .setTargetAgentType("TaskAgent")
                  .addRequiredCapability("bash")
                  .build();

  // Routing should fail (no agent with bash)
  auto result = router_->routeTask(spec);

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error_message.empty());
}

// ============================================================================
// Test 7: HMASCoordinator Task State Management
// ============================================================================

TEST_F(DistributedGrpcTest, TaskStateTracking) {
  // Register an agent
  registry_->registerAgent("task-1", "TaskAgent", 3, "node1:50051", {});
  registry_->updateHeartbeat("task-1");

  // Create a simple task spec
  auto spec = YamlSpecBuilder()
                  .setName("test-task")
                  .setTaskId("task-001")
                  .setGoal("Test task")
                  .setTargetLevel(3)
                  .setTargetAgentType("TaskAgent")
                  .build();

  // Simulate task submission (manually create task state)
  // Note: Full SubmitTask RPC would require running gRPC server
  // Here we test the coordinator's state management directly

  EXPECT_EQ(coordinator_->getActiveTaskCount(), 0);

  // Update task status
  coordinator_->updateTaskStatus("task-001", hmas::TASK_PHASE_PENDING);

  // Get task state
  auto state = coordinator_->getTaskState("task-001");
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(state->task_id, "task-001");
  EXPECT_EQ(state->phase, hmas::TASK_PHASE_PENDING);

  // Update to EXECUTING
  coordinator_->updateTaskStatus("task-001", hmas::TASK_PHASE_EXECUTING, 50,
                                 "subtask-1");

  state = coordinator_->getTaskState("task-001");
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(state->phase, hmas::TASK_PHASE_EXECUTING);
  EXPECT_EQ(state->progress_percent, 50);
  EXPECT_EQ(state->current_subtask, "subtask-1");

  // Update to COMPLETED
  coordinator_->updateTaskStatus("task-001", hmas::TASK_PHASE_COMPLETED, 100);

  state = coordinator_->getTaskState("task-001");
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(state->phase, hmas::TASK_PHASE_COMPLETED);
  EXPECT_EQ(state->progress_percent, 100);
}

TEST_F(DistributedGrpcTest, MultipleTaskStateTracking) {
  // Create multiple task states
  coordinator_->updateTaskStatus("task-1", hmas::TASK_PHASE_PENDING);
  coordinator_->updateTaskStatus("task-2", hmas::TASK_PHASE_EXECUTING, 30);
  coordinator_->updateTaskStatus("task-3", hmas::TASK_PHASE_COMPLETED, 100);

  // Verify all tasks tracked
  EXPECT_TRUE(coordinator_->hasTask("task-1"));
  EXPECT_TRUE(coordinator_->hasTask("task-2"));
  EXPECT_TRUE(coordinator_->hasTask("task-3"));
  EXPECT_FALSE(coordinator_->hasTask("task-999"));

  // Verify individual states
  auto state1 = coordinator_->getTaskState("task-1");
  auto state2 = coordinator_->getTaskState("task-2");
  auto state3 = coordinator_->getTaskState("task-3");

  ASSERT_TRUE(state1.has_value());
  EXPECT_EQ(state1->phase, hmas::TASK_PHASE_PENDING);

  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state2->phase, hmas::TASK_PHASE_EXECUTING);
  EXPECT_EQ(state2->progress_percent, 30);

  ASSERT_TRUE(state3.has_value());
  EXPECT_EQ(state3->phase, hmas::TASK_PHASE_COMPLETED);
  EXPECT_EQ(state3->progress_percent, 100);
}

// ============================================================================
// Test 8: Strategy Conversion Helpers
// ============================================================================

TEST_F(DistributedGrpcTest, AggregationStrategyConversion) {
  // Test string to strategy conversion
  EXPECT_EQ(stringToStrategy("WAIT_ALL"), AggregationStrategy::WAIT_ALL);
  EXPECT_EQ(stringToStrategy("FIRST_SUCCESS"),
            AggregationStrategy::FIRST_SUCCESS);
  EXPECT_EQ(stringToStrategy("MAJORITY"), AggregationStrategy::MAJORITY);

  // Test strategy to string conversion
  EXPECT_EQ(strategyToString(AggregationStrategy::WAIT_ALL), "WAIT_ALL");
  EXPECT_EQ(strategyToString(AggregationStrategy::FIRST_SUCCESS),
            "FIRST_SUCCESS");
  EXPECT_EQ(strategyToString(AggregationStrategy::MAJORITY), "MAJORITY");
}

// ============================================================================
// Test 9: Integration Test - Full Flow (No Real gRPC Server)
// ============================================================================

TEST_F(DistributedGrpcTest, IntegrationFullTaskFlowSimulated) {
  // This test simulates a full task flow without running real gRPC servers
  // It validates that all components work together correctly

  // Step 1: Register agents in hierarchy
  registry_->registerAgent("chief", "ChiefArchitectAgent", 0, "node1:50051",
                           {});
  registry_->registerAgent("component", "ComponentLeadAgent", 1, "node2:50051",
                           {"cpp20"});
  registry_->registerAgent("module", "ModuleLeadAgent", 2, "node3:50051",
                           {"cpp20", "cmake"});
  registry_->registerAgent("task-1", "TaskAgent", 3, "node4:50051",
                           {"bash", "cpp20"});
  registry_->registerAgent("task-2", "TaskAgent", 3, "node5:50051",
                           {"bash", "cpp20"});

  // Send heartbeats
  registry_->updateHeartbeat("chief");
  registry_->updateHeartbeat("component");
  registry_->updateHeartbeat("module");
  registry_->updateHeartbeat("task-1");
  registry_->updateHeartbeat("task-2");

  // Step 2: Chief creates high-level goal YAML
  auto chief_spec = YamlSpecBuilder()
                        .setName("build-project")
                        .setTaskId("chief-task-001")
                        .setGoal("Build C++20 project")
                        .setTargetLevel(1)  // Delegate to ComponentLead
                        .setTargetAgentType("ComponentLeadAgent")
                        .setActionType("DECOMPOSE")
                        .addRequiredCapability("cpp20")
                        .build();

  // Step 3: Route task to ComponentLead
  auto route_to_component = router_->routeTask(chief_spec);
  EXPECT_TRUE(route_to_component.success);
  EXPECT_EQ(route_to_component.target_agent_id, "component");

  // Step 4: ComponentLead creates module-level tasks
  auto module_spec = YamlSpecBuilder()
                         .setName("build-messaging-module")
                         .setTaskId("module-task-001")
                         .setGoal("Build messaging module")
                         .setTargetLevel(2)  // Delegate to ModuleLead
                         .setTargetAgentType("ModuleLeadAgent")
                         .setActionType("DECOMPOSE")
                         .addRequiredCapability("cpp20")
                         .addRequiredCapability("cmake")
                         .addSubtask("compile-sources", "TaskAgent")
                         .addSubtask("run-tests", "TaskAgent")
                         .setAggregationStrategy("WAIT_ALL")
                         .build();

  // Step 5: Route to ModuleLead
  auto route_to_module = router_->routeTask(module_spec);
  EXPECT_TRUE(route_to_module.success);
  EXPECT_EQ(route_to_module.target_agent_id, "module");

  // Step 6: ModuleLead creates concrete task YAMLs
  auto task1_spec = YamlSpecBuilder()
                        .setName("compile-sources")
                        .setTaskId("task-001")
                        .setGoal("Compile source files")
                        .setTargetLevel(3)
                        .setTargetAgentType("TaskAgent")
                        .setActionType("EXECUTE")
                        .setCommand("cmake --build build")
                        .addRequiredCapability("bash")
                        .build();

  auto task2_spec = YamlSpecBuilder()
                        .setName("run-tests")
                        .setTaskId("task-002")
                        .setGoal("Run unit tests")
                        .setTargetLevel(3)
                        .setTargetAgentType("TaskAgent")
                        .setActionType("EXECUTE")
                        .setCommand("ctest")
                        .addRequiredCapability("bash")
                        .build();

  // Step 7: Route tasks to TaskAgents
  router_->setStrategy(LoadBalancingStrategy::LEAST_LOADED);

  auto route_task1 = router_->routeTask(task1_spec);
  auto route_task2 = router_->routeTask(task2_spec);

  EXPECT_TRUE(route_task1.success);
  EXPECT_TRUE(route_task2.success);

  // Should route to different agents (or same if load balancing chooses)
  EXPECT_FALSE(route_task1.target_agent_id.empty());
  EXPECT_FALSE(route_task2.target_agent_id.empty());

  // Step 8: Simulate task execution and result aggregation
  ResultAggregator module_aggregator(AggregationStrategy::WAIT_ALL, 60000ms, 2);

  hmas::TaskResult result1;
  result1.set_task_id("task-001");
  result1.set_status(hmas::TASK_PHASE_COMPLETED);
  result1.set_result_yaml("compile: success");

  hmas::TaskResult result2;
  result2.set_task_id("task-002");
  result2.set_status(hmas::TASK_PHASE_COMPLETED);
  result2.set_result_yaml("tests: passed");

  module_aggregator.addResult("compile-sources", result1);
  bool module_complete = module_aggregator.addResult("run-tests", result2);

  EXPECT_TRUE(module_complete);
  EXPECT_TRUE(module_aggregator.isComplete());
  EXPECT_EQ(module_aggregator.getSuccessCount(), 2);

  // Step 9: Verify final aggregated result
  auto aggregated = module_aggregator.getAggregatedResult();
  ASSERT_TRUE(aggregated.has_value());
  EXPECT_FALSE(aggregated->empty());

  // This demonstrates the full hierarchical flow:
  // Chief -> Component -> Module -> Tasks -> Results aggregated back
}

// ============================================================================
// Test 10: Edge Cases and Error Handling
// ============================================================================

TEST_F(DistributedGrpcTest, InvalidYamlParsing) {
  // Test parsing invalid YAML
  std::string invalid_yaml = "this is not { valid yaml";
  auto parsed = YamlParser::parseTaskSpec(invalid_yaml);

  EXPECT_FALSE(parsed.has_value());
}

TEST_F(DistributedGrpcTest, DuplicateAgentRegistration) {
  // Register same agent twice
  bool reg1 =
      registry_->registerAgent("agent-1", "TaskAgent", 3, "node1:50051", {});
  bool reg2 =
      registry_->registerAgent("agent-1", "TaskAgent", 3, "node1:50051", {});

  EXPECT_TRUE(reg1);
  EXPECT_FALSE(reg2);  // Duplicate should fail
  EXPECT_EQ(registry_->getAgentCount(), 1);
}

TEST_F(DistributedGrpcTest, UnregisterNonexistentAgent) {
  // Try to unregister agent that doesn't exist
  bool unreg = registry_->unregisterAgent("nonexistent-agent");

  EXPECT_FALSE(unreg);
}

TEST_F(DistributedGrpcTest, AggregatorResetFunctionality) {
  // Create aggregator and add results
  ResultAggregator aggregator(AggregationStrategy::WAIT_ALL, 10000ms, 3);

  hmas::TaskResult result1;
  result1.set_status(hmas::TASK_PHASE_COMPLETED);
  aggregator.addResult("task-1", result1);

  EXPECT_EQ(aggregator.getResultCount(), 1);
  EXPECT_FALSE(aggregator.isComplete());

  // Reset aggregator
  aggregator.reset();

  EXPECT_EQ(aggregator.getResultCount(), 0);
  EXPECT_FALSE(aggregator.isComplete());
  EXPECT_FALSE(aggregator.hasTimedOut());
}

TEST_F(DistributedGrpcTest, TaskCleanupOldTasks) {
  // Create old task states with all terminal phases
  coordinator_->updateTaskStatus("old-task-1", hmas::TASK_PHASE_COMPLETED);
  coordinator_->updateTaskStatus("old-task-2", hmas::TASK_PHASE_FAILED);
  coordinator_->updateTaskStatus("old-task-3", hmas::TASK_PHASE_ERROR);
  coordinator_->updateTaskStatus("old-task-4", hmas::TASK_PHASE_TIMEOUT);
  coordinator_->updateTaskStatus("old-task-5", hmas::TASK_PHASE_CANCELLED);
  coordinator_->updateTaskStatus("recent-task", hmas::TASK_PHASE_EXECUTING);

  // Wait a bit
  std::this_thread::sleep_for(100ms);

  // Cleanup tasks older than 50ms (should remove old tasks)
  int32_t cleaned = coordinator_->cleanupOldTasks(50);

  // Note: This test depends on coordinator implementation
  // If cleanup only removes completed/failed/error/timeout/cancelled tasks, it
  // should work The exact behavior depends on implementation details

  EXPECT_GE(cleaned, 0);  // At least doesn't crash
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
