#pragma once

#include <grpcpp/grpcpp.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "core/i_message_router.hpp"
#include "hmas_coordinator.grpc.pb.h"
#include "network/service_registry.hpp"
#include "network/task_phase_utils.hpp"
#include "network/task_router.hpp"
#include "network/yaml_parser.hpp"

namespace keystone {
namespace network {

/// Task state tracking
struct TaskState {
  std::string task_id;
  std::string parent_task_id;
  std::string assigned_agent_id;
  std::string assigned_node_ip_port;
  hmas::TaskPhase phase{hmas::TASK_PHASE_PENDING};
  int32_t progress_percent{0};
  std::string current_subtask;
  std::chrono::system_clock::time_point created_at;
  std::chrono::system_clock::time_point updated_at;
  HierarchicalTaskSpec spec;  // Full task spec
};

/// HMASCoordinator gRPC Service Implementation
class HMASCoordinatorServiceImpl final : public hmas::HMASCoordinator::Service {
 public:
  /// Constructor
  /// @param registry Service registry for agent discovery
  /// @param router Task router for agent selection
  explicit HMASCoordinatorServiceImpl(std::shared_ptr<ServiceRegistry> registry,
                                      std::shared_ptr<TaskRouter> router);

  /// Destructor
  ~HMASCoordinatorServiceImpl() override;

  // gRPC Service method implementations

  /// Submit a task for execution
  grpc::Status SubmitTask(grpc::ServerContext* context,
                          const hmas::TaskRequest* request,
                          hmas::TaskResponse* response) override;

  /// Stream task status updates (server-side streaming)
  grpc::Status StreamTaskStatus(
      grpc::ServerContext* context, const hmas::TaskStatusRequest* request,
      grpc::ServerWriter<hmas::TaskStatusUpdate>* writer) override;

  /// Get final task result
  grpc::Status GetTaskResult(grpc::ServerContext* context,
                             const hmas::TaskResultRequest* request,
                             hmas::TaskResult* response) override;

  /// Submit task result back to parent
  grpc::Status SubmitResult(grpc::ServerContext* context,
                            const hmas::TaskResult* request,
                            hmas::ResultAcknowledgement* response) override;

  /// Cancel a running task
  grpc::Status CancelTask(grpc::ServerContext* context,
                          const hmas::CancelRequest* request,
                          hmas::CancelResponse* response) override;

  /// Get task progress (synchronous polling)
  grpc::Status GetTaskProgress(grpc::ServerContext* context,
                               const hmas::TaskProgressRequest* request,
                               hmas::TaskProgress* response) override;

  // Task state management (non-RPC methods)

  /// Update task status
  /// @param task_id Task identifier
  /// @param phase New phase
  /// @param progress Progress percentage (0-100)
  /// @param current_subtask Currently executing subtask name
  void updateTaskStatus(const std::string& task_id, hmas::TaskPhase phase,
                        int32_t progress = 0,
                        const std::string& current_subtask = "");

  /// Get task state
  /// @param task_id Task identifier
  /// @return Task state if found, nullopt otherwise
  std::optional<TaskState> getTaskState(const std::string& task_id) const;

  /// Check if task exists
  bool hasTask(const std::string& task_id) const;

  /// Get number of active tasks
  size_t getActiveTaskCount() const;

  /// Clean up completed/failed tasks older than threshold
  /// @param age_threshold_ms Age threshold in milliseconds
  /// @return Number of tasks cleaned up
  int32_t cleanupOldTasks(
      int64_t age_threshold_ms = 3600000);  // Default: 1 hour

  /// Set the message router used to notify agents of task lifecycle events.
  /// When set, CancelTask sends a CANCEL_TASK KeystoneMessage to the assigned
  /// agent.
  /// @param router Pointer to IMessageRouter (must outlive this object; may be
  /// nullptr)
  void setMessageRouter(core::IMessageRouter* router);

 private:
  /// Generate unique task ID
  std::string generateTaskId() const;

  std::shared_ptr<ServiceRegistry> registry_;
  std::shared_ptr<TaskRouter> router_;

  /// Optional message router for agent notifications (e.g., cancellation).
  /// Not owned by this object.
  core::IMessageRouter* message_router_{nullptr};

  /// Active tasks map: task_id -> TaskState
  std::unordered_map<std::string, TaskState> active_tasks_;

  /// Task results map: task_id -> TaskResult
  std::unordered_map<std::string, hmas::TaskResult> task_results_;

  /// Mutex for thread-safe access
  mutable std::mutex mutex_;
};

}  // namespace network
}  // namespace keystone
