#include "queue_manager.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace keystone {
namespace multi_process {

QueueManager::QueueManager(const std::string& socket_path)
    : socket_path_(socket_path) {}

QueueManager::~QueueManager() {
  // Cleanup handled by OS when process exits
}

void QueueManager::startServer() {
  is_server_ = true;
  // No-op for file-based queues, kept for API compatibility
}

void QueueManager::stopServer() {
  is_server_ = false;
  // Clean up queue files if we're the server
  if (is_server_) {
    unlink(DOMAIN_QUEUE_FILE);
    unlink(MODULE_QUEUE_FILE);
    unlink(TASK_QUEUE_FILE);
    unlink(TASK_RESULT_QUEUE_FILE);
    unlink(MODULE_RESULT_QUEUE_FILE);
    unlink(DOMAIN_RESULT_QUEUE_FILE);
  }
}

bool QueueManager::connect() {
  // No-op for file-based queues, kept for API compatibility
  return true;
}

void QueueManager::disconnect() {
  // No-op for file-based queues, kept for API compatibility
}

// File locking helpers
int QueueManager::openAndLockFile(const std::string& file_path, int lock_type) {
  int fd = open(file_path.c_str(), O_RDWR | O_CREAT, 0666);
  if (fd < 0) {
    throw std::runtime_error("Failed to open file: " + file_path);
  }

  if (flock(fd, lock_type) < 0) {
    close(fd);
    throw std::runtime_error("Failed to lock file: " + file_path);
  }

  return fd;
}

void QueueManager::unlockAndCloseFile(int fd) {
  flock(fd, LOCK_UN);
  close(fd);
}

// JSON serialization for DomainTask
nlohmann::json QueueManager::toJson(const DomainTask& task) {
  nlohmann::json j;
  j["request_id"] = task.request_id;
  j["task_id"] = task.task_id;
  j["domain"] = task.domain;
  j["files"] = task.files;
  j["description"] = task.description;
  j["created_at"] = std::chrono::system_clock::to_time_t(task.created_at);
  return j;
}

DomainTask QueueManager::domainTaskFromJson(const nlohmann::json& j) {
  DomainTask task;
  task.request_id = j["request_id"];
  task.task_id = j["task_id"];
  task.domain = j["domain"];
  task.files = j["files"].get<std::vector<std::string>>();
  task.description = j["description"];
  task.created_at = std::chrono::system_clock::from_time_t(j["created_at"]);
  return task;
}

// JSON serialization for ModuleTask
nlohmann::json QueueManager::toJson(const ModuleTask& task) {
  nlohmann::json j;
  j["request_id"] = task.request_id;
  j["task_id"] = task.task_id;
  j["domain"] = task.domain;
  j["module"] = task.module;
  j["files"] = task.files;
  j["description"] = task.description;
  j["created_at"] = std::chrono::system_clock::to_time_t(task.created_at);
  return j;
}

ModuleTask QueueManager::moduleTaskFromJson(const nlohmann::json& j) {
  ModuleTask task;
  task.request_id = j["request_id"];
  task.task_id = j["task_id"];
  task.domain = j["domain"];
  task.module = j["module"];
  task.files = j["files"].get<std::vector<std::string>>();
  task.description = j["description"];
  task.created_at = std::chrono::system_clock::from_time_t(j["created_at"]);
  return task;
}

// JSON serialization for TaskItem
nlohmann::json QueueManager::toJson(const TaskItem& task) {
  nlohmann::json j;
  j["request_id"] = task.request_id;
  j["task_id"] = task.task_id;
  j["file"] = task.file;
  j["agent_type"] = task.agent_type;
  j["task_description"] = task.task_description;
  j["created_at"] = std::chrono::system_clock::to_time_t(task.created_at);
  return j;
}

TaskItem QueueManager::taskItemFromJson(const nlohmann::json& j) {
  TaskItem task;
  task.request_id = j["request_id"];
  task.task_id = j["task_id"];
  task.file = j["file"];
  task.agent_type = j["agent_type"];
  task.task_description = j["task_description"];
  task.created_at = std::chrono::system_clock::from_time_t(j["created_at"]);
  return task;
}

// JSON serialization for TaskResult
nlohmann::json QueueManager::toJson(const TaskResult& result) {
  nlohmann::json j;
  j["request_id"] = result.request_id;
  j["task_id"] = result.task_id;
  j["agent_type"] = result.agent_type;
  j["status"] = result.status;
  j["output"] = result.output;
  j["completed_at"] = std::chrono::system_clock::to_time_t(result.completed_at);
  return j;
}

TaskResult QueueManager::taskResultFromJson(const nlohmann::json& j) {
  TaskResult result;
  result.request_id = j["request_id"];
  result.task_id = j["task_id"];
  result.agent_type = j["agent_type"];
  result.status = j["status"];
  result.output = j["output"];
  result.completed_at =
      std::chrono::system_clock::from_time_t(j["completed_at"]);
  return result;
}

// JSON serialization for ModuleResult
nlohmann::json QueueManager::toJson(const ModuleResult& result) {
  nlohmann::json j;
  j["request_id"] = result.request_id;
  j["task_id"] = result.task_id;
  j["module"] = result.module;
  j["summary"] = result.summary;
  j["completed_at"] = std::chrono::system_clock::to_time_t(result.completed_at);

  nlohmann::json task_results_json = nlohmann::json::array();
  for (const auto& tr : result.task_results) {
    task_results_json.push_back(toJson(tr));
  }
  j["task_results"] = task_results_json;

  return j;
}

ModuleResult QueueManager::moduleResultFromJson(const nlohmann::json& j) {
  ModuleResult result;
  result.request_id = j["request_id"];
  result.task_id = j["task_id"];
  result.module = j["module"];
  result.summary = j["summary"];
  result.completed_at =
      std::chrono::system_clock::from_time_t(j["completed_at"]);

  if (j.contains("task_results") && j["task_results"].is_array()) {
    for (const auto& tr_json : j["task_results"]) {
      result.task_results.push_back(taskResultFromJson(tr_json));
    }
  }

  return result;
}

// JSON serialization for DomainResult
nlohmann::json QueueManager::toJson(const DomainResult& result) {
  nlohmann::json j;
  j["request_id"] = result.request_id;
  j["task_id"] = result.task_id;
  j["domain"] = result.domain;
  j["summary"] = result.summary;
  j["completed_at"] = std::chrono::system_clock::to_time_t(result.completed_at);

  nlohmann::json module_results_json = nlohmann::json::array();
  for (const auto& mr : result.module_results) {
    module_results_json.push_back(toJson(mr));
  }
  j["module_results"] = module_results_json;

  return j;
}

DomainResult QueueManager::domainResultFromJson(const nlohmann::json& j) {
  DomainResult result;
  result.request_id = j["request_id"];
  result.task_id = j["task_id"];
  result.domain = j["domain"];
  result.summary = j["summary"];
  result.completed_at =
      std::chrono::system_clock::from_time_t(j["completed_at"]);

  if (j.contains("module_results") && j["module_results"].is_array()) {
    for (const auto& mr_json : j["module_results"]) {
      result.module_results.push_back(moduleResultFromJson(mr_json));
    }
  }

  return result;
}

// Generic push operation - append JSON to file
template <typename T>
void QueueManager::pushToFile(const std::string& file_path, const T& item) {
  // Open and lock file for exclusive write
  int fd = openAndLockFile(file_path, LOCK_EX);

  try {
    // Read existing content
    std::string existing_content;
    off_t file_size = lseek(fd, 0, SEEK_END);
    if (file_size > 0) {
      lseek(fd, 0, SEEK_SET);
      std::vector<char> buffer(file_size);
      read(fd, buffer.data(), file_size);
      existing_content = std::string(buffer.begin(), buffer.end());
    }

    // Parse existing JSON array or create new one
    nlohmann::json queue_array;
    if (!existing_content.empty()) {
      try {
        queue_array = nlohmann::json::parse(existing_content);
      } catch (...) {
        // If parsing fails, start fresh
        queue_array = nlohmann::json::array();
      }
    } else {
      queue_array = nlohmann::json::array();
    }

    // Append new item
    queue_array.push_back(toJson(item));

    // Write back to file
    std::string new_content = queue_array.dump();
    lseek(fd, 0, SEEK_SET);
    ftruncate(fd, 0);
    write(fd, new_content.c_str(), new_content.size());

    unlockAndCloseFile(fd);
  } catch (...) {
    unlockAndCloseFile(fd);
    throw;
  }
}

// Generic steal operation - read and remove first item from file
template <typename T>
std::optional<T> QueueManager::stealFromFile(const std::string& file_path) {
  // Open and lock file for exclusive write
  int fd = openAndLockFile(file_path, LOCK_EX);

  try {
    // Read existing content
    std::string existing_content;
    off_t file_size = lseek(fd, 0, SEEK_END);
    if (file_size == 0) {
      unlockAndCloseFile(fd);
      return std::nullopt;
    }

    lseek(fd, 0, SEEK_SET);
    std::vector<char> buffer(file_size);
    read(fd, buffer.data(), file_size);
    existing_content = std::string(buffer.begin(), buffer.end());

    // Parse JSON array
    nlohmann::json queue_array;
    try {
      queue_array = nlohmann::json::parse(existing_content);
    } catch (...) {
      unlockAndCloseFile(fd);
      return std::nullopt;
    }

    if (!queue_array.is_array() || queue_array.empty()) {
      unlockAndCloseFile(fd);
      return std::nullopt;
    }

    // Extract first item
    nlohmann::json first_item = queue_array[0];
    queue_array.erase(queue_array.begin());

    // Write back remaining items
    std::string new_content = queue_array.dump();
    lseek(fd, 0, SEEK_SET);
    ftruncate(fd, 0);
    write(fd, new_content.c_str(), new_content.size());

    unlockAndCloseFile(fd);

    // Deserialize and return the item
    // Note: This requires specialization for each type
    return std::nullopt;  // Will be specialized below
  } catch (...) {
    unlockAndCloseFile(fd);
    throw;
  }
}

// Push operations
void QueueManager::pushDomain(const DomainTask& task) {
  pushToFile(DOMAIN_QUEUE_FILE, task);
}

void QueueManager::pushModule(const ModuleTask& task) {
  pushToFile(MODULE_QUEUE_FILE, task);
}

void QueueManager::pushTask(const TaskItem& task) {
  pushToFile(TASK_QUEUE_FILE, task);
}

void QueueManager::pushTaskResult(const TaskResult& result) {
  pushToFile(TASK_RESULT_QUEUE_FILE, result);
}

void QueueManager::pushModuleResult(const ModuleResult& result) {
  pushToFile(MODULE_RESULT_QUEUE_FILE, result);
}

void QueueManager::pushDomainResult(const DomainResult& result) {
  pushToFile(DOMAIN_RESULT_QUEUE_FILE, result);
}

// Steal operations - specialized implementation for each type
std::optional<DomainTask> QueueManager::stealDomain() {
  int fd = openAndLockFile(DOMAIN_QUEUE_FILE, LOCK_EX);

  try {
    // Read existing content
    off_t file_size = lseek(fd, 0, SEEK_END);
    if (file_size == 0) {
      unlockAndCloseFile(fd);
      return std::nullopt;
    }

    lseek(fd, 0, SEEK_SET);
    std::vector<char> buffer(file_size);
    read(fd, buffer.data(), file_size);
    std::string existing_content(buffer.begin(), buffer.end());

    // Parse JSON array
    nlohmann::json queue_array = nlohmann::json::parse(existing_content);
    if (!queue_array.is_array() || queue_array.empty()) {
      unlockAndCloseFile(fd);
      return std::nullopt;
    }

    // Extract first item
    nlohmann::json first_item = queue_array[0];
    queue_array.erase(queue_array.begin());

    // Write back remaining items
    std::string new_content = queue_array.dump();
    lseek(fd, 0, SEEK_SET);
    ftruncate(fd, 0);
    write(fd, new_content.c_str(), new_content.size());

    unlockAndCloseFile(fd);

    return domainTaskFromJson(first_item);
  } catch (...) {
    unlockAndCloseFile(fd);
    return std::nullopt;
  }
}

std::optional<ModuleTask> QueueManager::stealModule() {
  int fd = openAndLockFile(MODULE_QUEUE_FILE, LOCK_EX);

  try {
    // Read existing content
    off_t file_size = lseek(fd, 0, SEEK_END);
    if (file_size == 0) {
      unlockAndCloseFile(fd);
      return std::nullopt;
    }

    lseek(fd, 0, SEEK_SET);
    std::vector<char> buffer(file_size);
    read(fd, buffer.data(), file_size);
    std::string existing_content(buffer.begin(), buffer.end());

    // Parse JSON array
    nlohmann::json queue_array = nlohmann::json::parse(existing_content);
    if (!queue_array.is_array() || queue_array.empty()) {
      unlockAndCloseFile(fd);
      return std::nullopt;
    }

    // Extract first item
    nlohmann::json first_item = queue_array[0];
    queue_array.erase(queue_array.begin());

    // Write back remaining items
    std::string new_content = queue_array.dump();
    lseek(fd, 0, SEEK_SET);
    ftruncate(fd, 0);
    write(fd, new_content.c_str(), new_content.size());

    unlockAndCloseFile(fd);

    return moduleTaskFromJson(first_item);
  } catch (...) {
    unlockAndCloseFile(fd);
    return std::nullopt;
  }
}

std::optional<TaskItem> QueueManager::stealTask() {
  int fd = openAndLockFile(TASK_QUEUE_FILE, LOCK_EX);

  try {
    // Read existing content
    off_t file_size = lseek(fd, 0, SEEK_END);
    if (file_size == 0) {
      unlockAndCloseFile(fd);
      return std::nullopt;
    }

    lseek(fd, 0, SEEK_SET);
    std::vector<char> buffer(file_size);
    read(fd, buffer.data(), file_size);
    std::string existing_content(buffer.begin(), buffer.end());

    // Parse JSON array
    nlohmann::json queue_array = nlohmann::json::parse(existing_content);
    if (!queue_array.is_array() || queue_array.empty()) {
      unlockAndCloseFile(fd);
      return std::nullopt;
    }

    // Extract first item
    nlohmann::json first_item = queue_array[0];
    queue_array.erase(queue_array.begin());

    // Write back remaining items
    std::string new_content = queue_array.dump();
    lseek(fd, 0, SEEK_SET);
    ftruncate(fd, 0);
    write(fd, new_content.c_str(), new_content.size());

    unlockAndCloseFile(fd);

    return taskItemFromJson(first_item);
  } catch (...) {
    unlockAndCloseFile(fd);
    return std::nullopt;
  }
}

std::optional<TaskResult> QueueManager::pollTaskResult() {
  int fd = openAndLockFile(TASK_RESULT_QUEUE_FILE, LOCK_EX);

  try {
    // Read existing content
    off_t file_size = lseek(fd, 0, SEEK_END);
    if (file_size == 0) {
      unlockAndCloseFile(fd);
      return std::nullopt;
    }

    lseek(fd, 0, SEEK_SET);
    std::vector<char> buffer(file_size);
    read(fd, buffer.data(), file_size);
    std::string existing_content(buffer.begin(), buffer.end());

    // Parse JSON array
    nlohmann::json queue_array = nlohmann::json::parse(existing_content);
    if (!queue_array.is_array() || queue_array.empty()) {
      unlockAndCloseFile(fd);
      return std::nullopt;
    }

    // Extract first item
    nlohmann::json first_item = queue_array[0];
    queue_array.erase(queue_array.begin());

    // Write back remaining items
    std::string new_content = queue_array.dump();
    lseek(fd, 0, SEEK_SET);
    ftruncate(fd, 0);
    write(fd, new_content.c_str(), new_content.size());

    unlockAndCloseFile(fd);

    return taskResultFromJson(first_item);
  } catch (...) {
    unlockAndCloseFile(fd);
    return std::nullopt;
  }
}

std::optional<ModuleResult> QueueManager::pollModuleResult() {
  int fd = openAndLockFile(MODULE_RESULT_QUEUE_FILE, LOCK_EX);

  try {
    // Read existing content
    off_t file_size = lseek(fd, 0, SEEK_END);
    if (file_size == 0) {
      unlockAndCloseFile(fd);
      return std::nullopt;
    }

    lseek(fd, 0, SEEK_SET);
    std::vector<char> buffer(file_size);
    read(fd, buffer.data(), file_size);
    std::string existing_content(buffer.begin(), buffer.end());

    // Parse JSON array
    nlohmann::json queue_array = nlohmann::json::parse(existing_content);
    if (!queue_array.is_array() || queue_array.empty()) {
      unlockAndCloseFile(fd);
      return std::nullopt;
    }

    // Extract first item
    nlohmann::json first_item = queue_array[0];
    queue_array.erase(queue_array.begin());

    // Write back remaining items
    std::string new_content = queue_array.dump();
    lseek(fd, 0, SEEK_SET);
    ftruncate(fd, 0);
    write(fd, new_content.c_str(), new_content.size());

    unlockAndCloseFile(fd);

    return moduleResultFromJson(first_item);
  } catch (...) {
    unlockAndCloseFile(fd);
    return std::nullopt;
  }
}

std::optional<DomainResult> QueueManager::pollDomainResult() {
  int fd = openAndLockFile(DOMAIN_RESULT_QUEUE_FILE, LOCK_EX);

  try {
    // Read existing content
    off_t file_size = lseek(fd, 0, SEEK_END);
    if (file_size == 0) {
      unlockAndCloseFile(fd);
      return std::nullopt;
    }

    lseek(fd, 0, SEEK_SET);
    std::vector<char> buffer(file_size);
    read(fd, buffer.data(), file_size);
    std::string existing_content(buffer.begin(), buffer.end());

    // Parse JSON array
    nlohmann::json queue_array = nlohmann::json::parse(existing_content);
    if (!queue_array.is_array() || queue_array.empty()) {
      unlockAndCloseFile(fd);
      return std::nullopt;
    }

    // Extract first item
    nlohmann::json first_item = queue_array[0];
    queue_array.erase(queue_array.begin());

    // Write back remaining items
    std::string new_content = queue_array.dump();
    lseek(fd, 0, SEEK_SET);
    ftruncate(fd, 0);
    write(fd, new_content.c_str(), new_content.size());

    unlockAndCloseFile(fd);

    return domainResultFromJson(first_item);
  } catch (...) {
    unlockAndCloseFile(fd);
    return std::nullopt;
  }
}

}  // namespace multi_process
}  // namespace keystone
