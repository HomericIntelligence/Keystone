#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "data_structures.hpp"

namespace keystone {
namespace multi_process {

/**
 * QueueManager handles inter-process communication via file-based queues.
 * Uses flock for atomic operations and JSON serialization for cross-process
 * data sharing.
 *
 * All processes (1, 2, 3, 4) share the same queue files in /tmp:
 * - /tmp/keystone_domain_queue.dat
 * - /tmp/keystone_module_queue.dat
 * - /tmp/keystone_task_queue.dat
 * - /tmp/keystone_task_result_queue.dat
 * - /tmp/keystone_module_result_queue.dat
 * - /tmp/keystone_domain_result_queue.dat
 */
class QueueManager {
 public:
  explicit QueueManager(const std::string& socket_path);
  ~QueueManager();

  // Delete copy/move constructors
  QueueManager(const QueueManager&) = delete;
  QueueManager& operator=(const QueueManager&) = delete;
  QueueManager(QueueManager&&) = delete;
  QueueManager& operator=(QueueManager&&) = delete;

  // Server-side operations (Process 1 only) - deprecated but kept for API
  // compatibility
  void startServer();
  void stopServer();

  // Client-side operations (Processes 2, 3, 4) - deprecated but kept for API
  // compatibility
  bool connect();
  void disconnect();

  // Push operations (create work)
  void pushDomain(const DomainTask& task);
  void pushModule(const ModuleTask& task);
  void pushTask(const TaskItem& task);

  // Steal operations (steal work from queues)
  std::optional<DomainTask> stealDomain();
  std::optional<ModuleTask> stealModule();
  std::optional<TaskItem> stealTask();

  // Result operations
  void pushTaskResult(const TaskResult& result);
  void pushModuleResult(const ModuleResult& result);
  void pushDomainResult(const DomainResult& result);

  std::optional<TaskResult> pollTaskResult();
  std::optional<ModuleResult> pollModuleResult();
  std::optional<DomainResult> pollDomainResult();

  // Utility
  bool isServer() const { return is_server_; }
  bool isConnected() const {
    return true;
  }  // Always connected with file-based queues

 private:
  std::string socket_path_;  // Kept for API compatibility, not used
  bool is_server_{false};

  // File paths for shared queues
  static constexpr const char* DOMAIN_QUEUE_FILE =
      "/tmp/keystone_domain_queue.dat";
  static constexpr const char* MODULE_QUEUE_FILE =
      "/tmp/keystone_module_queue.dat";
  static constexpr const char* TASK_QUEUE_FILE = "/tmp/keystone_task_queue.dat";
  static constexpr const char* TASK_RESULT_QUEUE_FILE =
      "/tmp/keystone_task_result_queue.dat";
  static constexpr const char* MODULE_RESULT_QUEUE_FILE =
      "/tmp/keystone_module_result_queue.dat";
  static constexpr const char* DOMAIN_RESULT_QUEUE_FILE =
      "/tmp/keystone_domain_result_queue.dat";

  // File-based queue operations
  template <typename T>
  void pushToFile(const std::string& file_path, const T& item);

  template <typename T>
  std::optional<T> stealFromFile(const std::string& file_path);

  // JSON serialization helpers
  nlohmann::json toJson(const DomainTask& task);
  nlohmann::json toJson(const ModuleTask& task);
  nlohmann::json toJson(const TaskItem& task);
  nlohmann::json toJson(const TaskResult& result);
  nlohmann::json toJson(const ModuleResult& result);
  nlohmann::json toJson(const DomainResult& result);

  DomainTask domainTaskFromJson(const nlohmann::json& j);
  ModuleTask moduleTaskFromJson(const nlohmann::json& j);
  TaskItem taskItemFromJson(const nlohmann::json& j);
  TaskResult taskResultFromJson(const nlohmann::json& j);
  ModuleResult moduleResultFromJson(const nlohmann::json& j);
  DomainResult domainResultFromJson(const nlohmann::json& j);

  // File locking helpers
  int openAndLockFile(const std::string& file_path, int lock_type);
  void unlockAndCloseFile(int fd);
};

}  // namespace multi_process
}  // namespace keystone
