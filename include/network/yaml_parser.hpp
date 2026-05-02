#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "yaml-cpp/yaml.h"

namespace keystone {
namespace network {

/// Retry policy configuration
struct RetryPolicy {
  int max_retries{3};
  std::string backoff{"exponential"};  // constant, linear, exponential
  std::string initial_delay{"1s"};
  std::string max_delay{"30s"};
};

/// Aggregation configuration
struct AggregationConfig {
  std::string strategy{"WAIT_ALL"};  // WAIT_ALL, FIRST_SUCCESS, MAJORITY
  std::string timeout{"25m"};
  RetryPolicy retry_policy;
};

/// Subtask specification in DAG
struct SubtaskSpec {
  std::string name;
  std::string agent_type;
  std::string depends;  // Dependency expression (e.g., "task-a && task-b")
  YAML::Node spec;      // Subtask-specific specification (flexible)
};

/// Subtask status
struct SubtaskStatus {
  std::string name;
  std::string status;  // PENDING, EXECUTING, COMPLETED, FAILED
  std::string assigned_node;
};

/// Task payload
struct TaskPayload {
  std::string command;
  std::string data;
  std::optional<std::string> estimated_duration;
  std::vector<std::string> required_capabilities;
};

/// Task action
struct TaskAction {
  std::string type;  // DECOMPOSE, EXECUTE, RETURN_RESULT, SHUTDOWN
  std::string content_type{"TEXT_PLAIN"};  // TEXT_PLAIN, BINARY_CISTA, JSON
  std::string priority{"NORMAL"};          // LOW, NORMAL, HIGH, CRITICAL
};

/// Hierarchical context
struct TaskHierarchy {
  std::string level0_goal;
  std::optional<std::string> level1_directive;
  std::optional<std::string> level2_module;
  std::optional<std::string> level3_task;
};

/// Task routing information
struct TaskRouting {
  std::string origin_node;
  int target_level;
  std::string target_agent_type;
  std::optional<std::string> target_agent_id;
};

/// Task metadata
struct TaskMetadata {
  std::string name;
  std::string task_id;
  std::optional<std::string> parent_task_id;
  std::optional<std::string> session_id;
  std::string created_at;
  std::optional<std::string> deadline;
};

/// Task status
struct TaskStatus {
  std::string phase{
      "PENDING"};  // PENDING, PLANNING, WAITING, EXECUTING, SYNTHESIZING,
                   // COMPLETED, FAILED, ERROR, TIMEOUT, CANCELLED
  std::optional<std::string> start_time;
  std::optional<std::string> completion_time;
  std::optional<std::string> result;
  std::optional<std::string> error;
  std::vector<SubtaskStatus> subtasks;
};

/// Complete hierarchical task specification
struct HierarchicalTaskSpec {
  std::string api_version;
  std::string kind;
  TaskMetadata metadata;

  // Spec section
  TaskRouting routing;
  TaskHierarchy hierarchy;
  TaskAction action;
  TaskPayload payload;
  std::vector<SubtaskSpec> tasks;
  AggregationConfig aggregation;

  // Status section
  TaskStatus status;
};

/// YAML Parser for HierarchicalTaskSpec
class YamlParser {
 public:
  /// Parse YAML string into HierarchicalTaskSpec
  /// @param yaml_str YAML string to parse
  /// @return Parsed task spec, or nullopt if parsing failed
  static std::optional<HierarchicalTaskSpec> parseTaskSpec(
      const std::string& yaml_str);

  /// Parse YAML node into HierarchicalTaskSpec
  /// @param node YAML node to parse
  /// @return Parsed task spec, or nullopt if parsing failed
  static std::optional<HierarchicalTaskSpec> parseTaskSpec(
      const YAML::Node& node);

  /// Generate YAML string from HierarchicalTaskSpec
  /// @param spec Task spec to serialize
  /// @return YAML string representation
  static std::string generateTaskSpec(const HierarchicalTaskSpec& spec);

  /// Validate YAML against schema (basic validation)
  /// @param node YAML node to validate
  /// @return true if valid, false otherwise
  static bool validateTaskSpec(const YAML::Node& node);

  /// Parse duration string (e.g., "15m", "2h", "30s") to milliseconds
  /// @param duration_str Duration string
  /// @return Duration in milliseconds, or nullopt if invalid
  static std::optional<int64_t> parseDuration(const std::string& duration_str);

  /// Format duration from milliseconds to string (e.g., "15m")
  /// @param duration_ms Duration in milliseconds
  /// @return Formatted duration string
  static std::string formatDuration(int64_t duration_ms);

 private:
  // Helper methods for parsing subsections
  static TaskMetadata parseMetadata(const YAML::Node& node);
  static TaskRouting parseRouting(const YAML::Node& node);
  static TaskHierarchy parseHierarchy(const YAML::Node& node);
  static TaskAction parseAction(const YAML::Node& node);
  static TaskPayload parsePayload(const YAML::Node& node);
  static std::vector<SubtaskSpec> parseTasks(const YAML::Node& node);
  static AggregationConfig parseAggregation(const YAML::Node& node);
  static RetryPolicy parseRetryPolicy(const YAML::Node& node);
  static TaskStatus parseStatus(const YAML::Node& node);
  static std::vector<SubtaskStatus> parseSubtasks(const YAML::Node& node);

  // Helper methods for generating YAML
  static YAML::Node generateMetadata(const TaskMetadata& metadata);
  static YAML::Node generateRouting(const TaskRouting& routing);
  static YAML::Node generateHierarchy(const TaskHierarchy& hierarchy);
  static YAML::Node generateAction(const TaskAction& action);
  static YAML::Node generatePayload(const TaskPayload& payload);
  static YAML::Node generateTasks(const std::vector<SubtaskSpec>& tasks);
  static YAML::Node generateAggregation(const AggregationConfig& aggregation);
  static YAML::Node generateRetryPolicy(const RetryPolicy& policy);
  static YAML::Node generateStatus(const TaskStatus& status);
  static YAML::Node generateSubtasks(
      const std::vector<SubtaskStatus>& subtasks);
};

}  // namespace network
}  // namespace keystone
