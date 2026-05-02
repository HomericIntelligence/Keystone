#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace keystone {
namespace multi_process {

// Task structures (all include request_id for tracking)
struct DomainTask {
  std::string request_id;  // Top-level request identifier
  std::string task_id;     // Domain-specific task ID
  std::string domain;
  std::vector<std::string> files;
  std::string description;
  std::chrono::system_clock::time_point created_at;
};

struct ModuleTask {
  std::string request_id;  // Top-level request identifier
  std::string task_id;     // Module-specific task ID
  std::string domain;
  std::string module;
  std::vector<std::string> files;
  std::string description;
  std::chrono::system_clock::time_point created_at;
};

struct TaskItem {
  std::string request_id;  // Top-level request identifier
  std::string task_id;     // Task-specific task ID
  std::string file;
  std::string agent_type;
  std::string task_description;
  std::chrono::system_clock::time_point created_at;
};

// Result structures (all include request_id for correlation)
struct TaskResult {
  std::string request_id;  // Top-level request identifier
  std::string task_id;
  std::string agent_type;
  std::string status;
  std::string output;  // JSON string from Cline
  std::chrono::system_clock::time_point completed_at;
};

struct ModuleResult {
  std::string request_id;  // Top-level request identifier
  std::string task_id;
  std::string module;
  std::vector<TaskResult> task_results;
  std::string summary;
  std::chrono::system_clock::time_point completed_at;
};

struct DomainResult {
  std::string request_id;  // Top-level request identifier
  std::string task_id;
  std::string domain;
  std::vector<ModuleResult> module_results;
  std::string summary;
  std::chrono::system_clock::time_point completed_at;
};

}  // namespace multi_process
}  // namespace keystone
