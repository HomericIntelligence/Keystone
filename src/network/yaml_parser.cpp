#include "network/yaml_parser.hpp"

#include <regex>
#include <sstream>
#include <stdexcept>

#include "network/task_phase_utils.hpp"

namespace keystone::network {

// Parse YAML string
std::optional<HierarchicalTaskSpec> YamlParser::parseTaskSpec(
    const std::string& yaml_str) {
  try {
    YAML::Node node = YAML::Load(yaml_str);
    return parseTaskSpec(node);
  } catch (const YAML::Exception& e) {
    return std::nullopt;
  }
}

// Parse YAML node
std::optional<HierarchicalTaskSpec> YamlParser::parseTaskSpec(
    const YAML::Node& node) {
  if (!validateTaskSpec(node)) {
    return std::nullopt;
  }

  try {
    HierarchicalTaskSpec spec;

    // Top-level fields
    spec.api_version = node["apiVersion"].as<std::string>();
    spec.kind = node["kind"].as<std::string>();

    // Parse sections
    spec.metadata = parseMetadata(node["metadata"]);
    spec.routing = parseRouting(node["spec"]["routing"]);
    spec.hierarchy = parseHierarchy(node["spec"]["hierarchy"]);
    spec.action = parseAction(node["spec"]["action"]);
    spec.payload = parsePayload(node["spec"]["payload"]);

    // Optional tasks array
    if (node["spec"]["tasks"]) {
      spec.tasks = parseTasks(node["spec"]["tasks"]);
    }

    // Aggregation config
    if (node["spec"]["aggregation"]) {
      spec.aggregation = parseAggregation(node["spec"]["aggregation"]);
    }

    // Status (optional, may not exist yet)
    if (node["status"]) {
      spec.status = parseStatus(node["status"]);
    }

    return spec;
  } catch (const YAML::Exception& e) {
    return std::nullopt;
  } catch (const std::exception& e) {
    return std::nullopt;
  }
}

// Generate YAML string
std::string YamlParser::generateTaskSpec(const HierarchicalTaskSpec& spec) {
  YAML::Node root;

  root["apiVersion"] = spec.api_version;
  root["kind"] = spec.kind;
  root["metadata"] = generateMetadata(spec.metadata);

  YAML::Node spec_node;
  spec_node["routing"] = generateRouting(spec.routing);
  spec_node["hierarchy"] = generateHierarchy(spec.hierarchy);
  spec_node["action"] = generateAction(spec.action);
  spec_node["payload"] = generatePayload(spec.payload);

  if (!spec.tasks.empty()) {
    spec_node["tasks"] = generateTasks(spec.tasks);
  }

  spec_node["aggregation"] = generateAggregation(spec.aggregation);

  root["spec"] = spec_node;
  root["status"] = generateStatus(spec.status);

  YAML::Emitter emitter;
  emitter << root;
  return emitter.c_str();
}

// Basic validation
bool YamlParser::validateTaskSpec(const YAML::Node& node) {
  if (!node.IsMap()) {
    return false;
  }

  // Check required top-level fields
  if (!node["apiVersion"] || !node["kind"] || !node["metadata"] ||
      !node["spec"]) {
    return false;
  }

  // Check required metadata fields
  const auto& metadata = node["metadata"];
  if (!metadata["name"] || !metadata["taskId"] || !metadata["createdAt"]) {
    return false;
  }

  // Check required spec fields
  const auto& spec = node["spec"];
  if (!spec["routing"] || !spec["hierarchy"] || !spec["action"] ||
      !spec["payload"]) {
    return false;
  }

  return true;
}

// Parse duration string (e.g., "15m", "2h", "30s")
std::optional<int64_t> YamlParser::parseDuration(
    const std::string& duration_str) {
  std::regex duration_regex(R"((\d+)(ms|s|m|h))");
  std::smatch match;

  if (!std::regex_match(duration_str, match, duration_regex)) {
    return std::nullopt;
  }

  int64_t value = std::stoll(match[1].str());
  std::string unit = match[2].str();

  if (unit == "ms") {
    return value;
  } else if (unit == "s") {
    return value * 1000;
  } else if (unit == "m") {
    return value * 60 * 1000;
  } else if (unit == "h") {
    return value * 60 * 60 * 1000;
  }

  return std::nullopt;
}

// Format duration to string
std::string YamlParser::formatDuration(int64_t duration_ms) {
  if (duration_ms < 1000) {
    return std::to_string(duration_ms) + "ms";
  } else if (duration_ms < 60000) {
    return std::to_string(duration_ms / 1000) + "s";
  } else if (duration_ms < 3600000) {
    return std::to_string(duration_ms / 60000) + "m";
  } else {
    return std::to_string(duration_ms / 3600000) + "h";
  }
}

// Parse metadata
TaskMetadata YamlParser::parseMetadata(const YAML::Node& node) {
  TaskMetadata metadata;
  metadata.name = node["name"].as<std::string>();
  metadata.task_id = node["taskId"].as<std::string>();
  metadata.created_at = node["createdAt"].as<std::string>();

  if (node["parentTaskId"]) {
    metadata.parent_task_id = node["parentTaskId"].as<std::string>();
  }
  if (node["sessionId"]) {
    metadata.session_id = node["sessionId"].as<std::string>();
  }
  if (node["deadline"]) {
    metadata.deadline = node["deadline"].as<std::string>();
  }

  return metadata;
}

// Parse routing
TaskRouting YamlParser::parseRouting(const YAML::Node& node) {
  TaskRouting routing;
  routing.origin_node = node["originNode"].as<std::string>();
  routing.target_level = node["targetLevel"].as<int>();
  routing.target_agent_type = node["targetAgentType"].as<std::string>();

  if (node["targetAgentId"]) {
    routing.target_agent_id = node["targetAgentId"].as<std::string>();
  }

  return routing;
}

// Parse hierarchy
TaskHierarchy YamlParser::parseHierarchy(const YAML::Node& node) {
  TaskHierarchy hierarchy;
  hierarchy.level0_goal = node["level0_goal"].as<std::string>();

  if (node["level1_directive"]) {
    hierarchy.level1_directive = node["level1_directive"].as<std::string>();
  }
  if (node["level2_module"]) {
    hierarchy.level2_module = node["level2_module"].as<std::string>();
  }
  if (node["level3_task"]) {
    hierarchy.level3_task = node["level3_task"].as<std::string>();
  }

  return hierarchy;
}

// Parse action
TaskAction YamlParser::parseAction(const YAML::Node& node) {
  TaskAction action;
  action.type = node["type"].as<std::string>();

  if (node["contentType"]) {
    action.content_type = node["contentType"].as<std::string>();
  }
  if (node["priority"]) {
    action.priority = node["priority"].as<std::string>();
  }

  return action;
}

// Parse payload
TaskPayload YamlParser::parsePayload(const YAML::Node& node) {
  TaskPayload payload;
  payload.command = node["command"].as<std::string>();

  if (node["data"]) {
    payload.data = node["data"].as<std::string>();
  }
  if (node["metadata"] && node["metadata"]["estimatedDuration"]) {
    payload.estimated_duration =
        node["metadata"]["estimatedDuration"].as<std::string>();
  }
  if (node["metadata"] && node["metadata"]["requiredCapabilities"]) {
    for (const auto& cap : node["metadata"]["requiredCapabilities"]) {
      payload.required_capabilities.push_back(cap.as<std::string>());
    }
  }

  return payload;
}

// Parse tasks
std::vector<SubtaskSpec> YamlParser::parseTasks(const YAML::Node& node) {
  std::vector<SubtaskSpec> tasks;

  for (const auto& task_node : node) {
    SubtaskSpec task;
    task.name = task_node["name"].as<std::string>();
    task.agent_type = task_node["agentType"].as<std::string>();
    task.depends = task_node["depends"].as<std::string>();

    if (task_node["spec"]) {
      task.spec = task_node["spec"];
    }

    tasks.push_back(task);
  }

  return tasks;
}

// Parse aggregation
AggregationConfig YamlParser::parseAggregation(const YAML::Node& node) {
  AggregationConfig config;

  if (node["strategy"]) {
    config.strategy = node["strategy"].as<std::string>();
  }
  if (node["timeout"]) {
    config.timeout = node["timeout"].as<std::string>();
  }
  if (node["retryPolicy"]) {
    config.retry_policy = parseRetryPolicy(node["retryPolicy"]);
  }

  return config;
}

// Parse retry policy
RetryPolicy YamlParser::parseRetryPolicy(const YAML::Node& node) {
  RetryPolicy policy;

  if (node["maxRetries"]) {
    policy.max_retries = node["maxRetries"].as<int>();
  }
  if (node["backoff"]) {
    policy.backoff = node["backoff"].as<std::string>();
  }
  if (node["initialDelay"]) {
    policy.initial_delay = node["initialDelay"].as<std::string>();
  }
  if (node["maxDelay"]) {
    policy.max_delay = node["maxDelay"].as<std::string>();
  }

  return policy;
}

// Parse status
TaskStatus YamlParser::parseStatus(const YAML::Node& node) {
  TaskStatus status;

  if (node["phase"]) {
    const std::string phase_str = node["phase"].as<std::string>();
    // Validate phase string against the known TaskPhase enum values.
    // An empty string defaults to "PENDING" (struct default); any non-empty
    // string that does not map to a valid phase is rejected.
    if (!phase_str.empty() && !isKnownPhaseString(phase_str)) {
      throw std::runtime_error("Unknown task phase string: '" + phase_str +
                               "'");
    }
    status.phase = phase_str;
  }
  if (node["startTime"]) {
    status.start_time = node["startTime"].as<std::string>();
  }
  if (node["completionTime"]) {
    status.completion_time = node["completionTime"].as<std::string>();
  }
  if (node["result"]) {
    status.result = node["result"].as<std::string>();
  }
  if (node["error"]) {
    status.error = node["error"].as<std::string>();
  }
  if (node["subtasks"]) {
    status.subtasks = parseSubtasks(node["subtasks"]);
  }

  return status;
}

// Parse subtasks
std::vector<SubtaskStatus> YamlParser::parseSubtasks(const YAML::Node& node) {
  std::vector<SubtaskStatus> subtasks;

  for (const auto& subtask_node : node) {
    SubtaskStatus subtask;
    subtask.name = subtask_node["name"].as<std::string>();
    subtask.status = subtask_node["status"].as<std::string>();
    if (subtask_node["assignedNode"]) {
      subtask.assigned_node = subtask_node["assignedNode"].as<std::string>();
    }
    subtasks.push_back(subtask);
  }

  return subtasks;
}

// Generate metadata
YAML::Node YamlParser::generateMetadata(const TaskMetadata& metadata) {
  YAML::Node node;
  node["name"] = metadata.name;
  node["taskId"] = metadata.task_id;
  node["createdAt"] = metadata.created_at;

  if (metadata.parent_task_id.has_value()) {
    node["parentTaskId"] = metadata.parent_task_id.value();
  }
  if (metadata.session_id.has_value()) {
    node["sessionId"] = metadata.session_id.value();
  }
  if (metadata.deadline.has_value()) {
    node["deadline"] = metadata.deadline.value();
  }

  return node;
}

// Generate routing
YAML::Node YamlParser::generateRouting(const TaskRouting& routing) {
  YAML::Node node;
  node["originNode"] = routing.origin_node;
  node["targetLevel"] = routing.target_level;
  node["targetAgentType"] = routing.target_agent_type;

  if (routing.target_agent_id.has_value()) {
    node["targetAgentId"] = routing.target_agent_id.value();
  }

  return node;
}

// Generate hierarchy
YAML::Node YamlParser::generateHierarchy(const TaskHierarchy& hierarchy) {
  YAML::Node node;
  node["level0_goal"] = hierarchy.level0_goal;

  if (hierarchy.level1_directive.has_value()) {
    node["level1_directive"] = hierarchy.level1_directive.value();
  }
  if (hierarchy.level2_module.has_value()) {
    node["level2_module"] = hierarchy.level2_module.value();
  }
  if (hierarchy.level3_task.has_value()) {
    node["level3_task"] = hierarchy.level3_task.value();
  }

  return node;
}

// Generate action
YAML::Node YamlParser::generateAction(const TaskAction& action) {
  YAML::Node node;
  node["type"] = action.type;
  node["contentType"] = action.content_type;
  node["priority"] = action.priority;
  return node;
}

// Generate payload
YAML::Node YamlParser::generatePayload(const TaskPayload& payload) {
  YAML::Node node;
  node["command"] = payload.command;

  if (!payload.data.empty()) {
    node["data"] = payload.data;
  }

  if (payload.estimated_duration.has_value() ||
      !payload.required_capabilities.empty()) {
    YAML::Node metadata;

    if (payload.estimated_duration.has_value()) {
      metadata["estimatedDuration"] = payload.estimated_duration.value();
    }

    if (!payload.required_capabilities.empty()) {
      for (const auto& cap : payload.required_capabilities) {
        metadata["requiredCapabilities"].push_back(cap);
      }
    }

    node["metadata"] = metadata;
  }

  return node;
}

// Generate tasks
YAML::Node YamlParser::generateTasks(const std::vector<SubtaskSpec>& tasks) {
  YAML::Node node;

  for (const auto& task : tasks) {
    YAML::Node task_node;
    task_node["name"] = task.name;
    task_node["agentType"] = task.agent_type;
    task_node["depends"] = task.depends;

    if (task.spec.IsDefined() && !task.spec.IsNull()) {
      task_node["spec"] = task.spec;
    }

    node.push_back(task_node);
  }

  return node;
}

// Generate aggregation
YAML::Node YamlParser::generateAggregation(
    const AggregationConfig& aggregation) {
  YAML::Node node;
  node["strategy"] = aggregation.strategy;
  node["timeout"] = aggregation.timeout;
  node["retryPolicy"] = generateRetryPolicy(aggregation.retry_policy);
  return node;
}

// Generate retry policy
YAML::Node YamlParser::generateRetryPolicy(const RetryPolicy& policy) {
  YAML::Node node;
  node["maxRetries"] = policy.max_retries;
  node["backoff"] = policy.backoff;
  node["initialDelay"] = policy.initial_delay;
  node["maxDelay"] = policy.max_delay;
  return node;
}

// Generate status
YAML::Node YamlParser::generateStatus(const TaskStatus& status) {
  YAML::Node node;
  node["phase"] = status.phase;

  if (status.start_time.has_value()) {
    node["startTime"] = status.start_time.value();
  }
  if (status.completion_time.has_value()) {
    node["completionTime"] = status.completion_time.value();
  }
  if (status.result.has_value()) {
    node["result"] = status.result.value();
  }
  if (status.error.has_value()) {
    node["error"] = status.error.value();
  }
  if (!status.subtasks.empty()) {
    node["subtasks"] = generateSubtasks(status.subtasks);
  }

  return node;
}

// Generate subtasks
YAML::Node YamlParser::generateSubtasks(
    const std::vector<SubtaskStatus>& subtasks) {
  YAML::Node node;

  for (const auto& subtask : subtasks) {
    YAML::Node subtask_node;
    subtask_node["name"] = subtask.name;
    subtask_node["status"] = subtask.status;
    if (!subtask.assigned_node.empty()) {
      subtask_node["assignedNode"] = subtask.assigned_node;
    }
    node.push_back(subtask_node);
  }

  return node;
}

}  // namespace keystone::network
