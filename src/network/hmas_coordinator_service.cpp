#include "network/hmas_coordinator_service.hpp"

#include <chrono>
#include <random>
#include <sstream>

#include "agents/agent_envelope.hpp"
#include "core/message.hpp"
#include "core/subject_validator.hpp"

namespace keystone::network {

HMASCoordinatorServiceImpl::HMASCoordinatorServiceImpl(
    std::shared_ptr<ServiceRegistry> registry,
    std::shared_ptr<TaskRouter> router,
    std::shared_ptr<core::MessageBus> message_bus)
    : registry_(std::move(registry)),
      router_(std::move(router)),
      message_bus_(std::move(message_bus)) {}

HMASCoordinatorServiceImpl::~HMASCoordinatorServiceImpl() = default;

grpc::Status HMASCoordinatorServiceImpl::SubmitTask(
    grpc::ServerContext* context, const hmas::TaskRequest* request,
    hmas::TaskResponse* response) {
  (void)context;  // Unused

  std::lock_guard<std::mutex> lock(mutex_);

  // Validate incoming subject tokens at the gRPC service boundary.
  // parent_task_id is optional — only validate when non-empty.
  if (!request->parent_task_id().empty()) {
    try {
      keystone::core::validateNatsSubjectToken(request->parent_task_id(),
                                               "parent_task_id");
    } catch (const std::invalid_argument& e) {
      response->set_accepted(false);
      response->set_error(std::string("Invalid parent_task_id: ") + e.what());
      return grpc::Status::OK;
    }
  }

  // Parse YAML spec
  auto spec_opt = YamlParser::parseTaskSpec(request->yaml_spec());
  if (!spec_opt.has_value()) {
    response->set_accepted(false);
    response->set_error("Failed to parse YAML task specification");
    return grpc::Status::OK;
  }

  auto spec = spec_opt.value();

  // Generate task ID if not provided
  std::string task_id =
      spec.metadata.task_id.empty() ? generateTaskId() : spec.metadata.task_id;

  // Validate the task_id token before routing — rejects path traversal and
  // other characters that are unsafe in NATS subject positions.
  try {
    keystone::core::validateNatsSubjectToken(task_id, "task_id");
  } catch (const std::invalid_argument& e) {
    response->set_accepted(false);
    response->set_error(std::string("Invalid task_id: ") + e.what());
    return grpc::Status::OK;
  }

  // Route task to appropriate agent
  auto routing_result = router_->routeTask(spec);

  if (!routing_result.success) {
    response->set_accepted(false);
    response->set_error("Failed to route task: " +
                        routing_result.error_message);
    return grpc::Status::OK;
  }

  // Create task state
  TaskState state;
  state.task_id = task_id;
  state.parent_task_id = request->parent_task_id();
  state.assigned_agent_id = routing_result.target_agent_id;
  state.assigned_node_ip_port = routing_result.target_ip_port;
  state.phase = hmas::TASK_PHASE_PENDING;
  state.progress_percent = 0;
  state.created_at = std::chrono::system_clock::now();
  state.updated_at = state.created_at;
  state.spec = spec;

  // Store task state
  active_tasks_[task_id] = state;

  // Build response
  response->set_task_id(task_id);
  response->set_accepted(true);
  response->set_assigned_node(routing_result.target_ip_port);
  response->set_assigned_agent_id(routing_result.target_agent_id);
  response->set_accepted_at_unix_ms(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          state.created_at.time_since_epoch())
          .count());

  return grpc::Status::OK;
}

grpc::Status HMASCoordinatorServiceImpl::StreamTaskStatus(
    grpc::ServerContext* context, const hmas::TaskStatusRequest* request,
    grpc::ServerWriter<hmas::TaskStatusUpdate>* writer) {
  (void)context;  // Unused

  const std::string& task_id = request->task_id();

  // Validate task_id at the service boundary before any internal lookup.
  try {
    keystone::core::validateNatsSubjectToken(task_id, "task_id");
  } catch (const std::invalid_argument& e) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, e.what());
  }

  // Stream status updates until task is complete or client disconnects
  while (!context->IsCancelled()) {
    hmas::TaskStatusUpdate update;

    {
      std::lock_guard<std::mutex> lock(mutex_);

      auto it = active_tasks_.find(task_id);
      if (it == active_tasks_.end()) {
        // Task not found or completed
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "Task not found");
      }

      const auto& state = it->second;

      update.set_task_id(task_id);
      update.set_phase(state.phase);
      update.set_progress_percent(state.progress_percent);
      update.set_current_subtask(state.current_subtask);
      update.set_updated_at_unix_ms(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              state.updated_at.time_since_epoch())
              .count());

      // Check if task is in terminal state
      if (isTerminalPhase(state.phase)) {
        writer->Write(update);
        break;  // Task done, stop streaming
      }
    }

    // Write update
    if (!writer->Write(update)) {
      // Client disconnected
      break;
    }

    // Wait before next update (poll interval: 500ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  return grpc::Status::OK;
}

grpc::Status HMASCoordinatorServiceImpl::GetTaskResult(
    grpc::ServerContext* context, const hmas::TaskResultRequest* request,
    hmas::TaskResult* response) {
  (void)context;  // Unused

  const std::string& task_id = request->task_id();

  // Validate task_id at the service boundary.
  try {
    keystone::core::validateNatsSubjectToken(task_id, "task_id");
  } catch (const std::invalid_argument& e) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, e.what());
  }

  int64_t timeout_ms = request->timeout_ms();

  auto deadline =
      std::chrono::system_clock::now() + std::chrono::milliseconds(timeout_ms);

  // Poll for result with timeout
  while (timeout_ms == 0 || std::chrono::system_clock::now() < deadline) {
    {
      std::lock_guard<std::mutex> lock(mutex_);

      // Check if result is available
      auto result_it = task_results_.find(task_id);
      if (result_it != task_results_.end()) {
        *response = result_it->second;
        return grpc::Status::OK;
      }

      // Check if task is in terminal state
      auto state_it = active_tasks_.find(task_id);
      if (state_it != active_tasks_.end()) {
        const hmas::TaskPhase current_phase = state_it->second.phase;
        if (isTerminalPhase(current_phase) &&
            current_phase != hmas::TASK_PHASE_COMPLETED) {
          // Task reached a non-success terminal state — synthesize an error
          // result. Provide a phase-specific error message so callers can
          // distinguish infrastructure errors (TASK_PHASE_ERROR) from logical
          // failures.
          response->set_task_id(task_id);
          response->set_status(current_phase);
          switch (current_phase) {
            case hmas::TASK_PHASE_ERROR:
              response->set_error(
                  "Task terminated with infrastructure or unexpected error "
                  "(TASK_PHASE_ERROR)");
              break;
            case hmas::TASK_PHASE_FAILED:
              response->set_error(
                  "Task failed during execution (TASK_PHASE_FAILED)");
              break;
            case hmas::TASK_PHASE_TIMEOUT:
              response->set_error(
                  "Task exceeded its deadline (TASK_PHASE_TIMEOUT)");
              break;
            case hmas::TASK_PHASE_CANCELLED:
              response->set_error(
                  "Task was cancelled before completion "
                  "(TASK_PHASE_CANCELLED)");
              break;
            default:
              response->set_error("Task ended without result");
              break;
          }
          return grpc::Status::OK;
        }
      }
    }

    if (timeout_ms == 0) {
      break;  // Non-blocking mode, return immediately
    }

    // Wait before polling again
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // Timeout or not found
  if (timeout_ms > 0) {
    return grpc::Status(grpc::StatusCode::DEADLINE_EXCEEDED,
                        "Timeout waiting for task result");
  } else {
    return grpc::Status(grpc::StatusCode::NOT_FOUND,
                        "Task result not available");
  }
}

grpc::Status HMASCoordinatorServiceImpl::SubmitResult(
    grpc::ServerContext* context, const hmas::TaskResult* request,
    hmas::ResultAcknowledgement* response) {
  (void)context;  // Unused

  // Validate task_id at the service boundary.
  try {
    keystone::core::validateNatsSubjectToken(request->task_id(), "task_id");
  } catch (const std::invalid_argument& e) {
    response->set_acknowledged(false);
    response->set_message(std::string("Invalid task_id: ") + e.what());
    return grpc::Status::OK;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  const std::string& task_id = request->task_id();

  // Store result
  task_results_[task_id] = *request;

  // Update task state to completed
  auto state_it = active_tasks_.find(task_id);
  if (state_it != active_tasks_.end()) {
    state_it->second.phase = request->status();
    state_it->second.progress_percent = 100;
    state_it->second.updated_at = std::chrono::system_clock::now();
  }

  // If this task has a parent, we may need to forward result
  // (This would be handled by the agent layer, not here)

  response->set_acknowledged(true);
  response->set_message("Result received and stored");

  return grpc::Status::OK;
}

grpc::Status HMASCoordinatorServiceImpl::CancelTask(
    grpc::ServerContext* context, const hmas::CancelRequest* request,
    hmas::CancelResponse* response) {
  (void)context;  // Unused

  // Validate task_id at the service boundary.
  try {
    keystone::core::validateNatsSubjectToken(request->task_id(), "task_id");
  } catch (const std::invalid_argument& e) {
    response->set_cancelled(false);
    response->set_message(std::string("Invalid task_id: ") + e.what());
    response->set_current_phase(hmas::TASK_PHASE_UNSPECIFIED);
    return grpc::Status::OK;
  }

  const std::string& task_id = request->task_id();

  // Use a nested scope so the mutex is released before we call into MessageBus,
  // preventing a potential lock-order inversion with locks held inside the
  // agent/scheduler layer.
  std::string assigned_agent_id;
  bool task_cancelled = false;
  hmas::TaskPhase previous_phase = hmas::TASK_PHASE_UNSPECIFIED;

  {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = active_tasks_.find(task_id);
    if (it == active_tasks_.end()) {
      response->set_cancelled(false);
      response->set_message("Task not found");
      response->set_current_phase(hmas::TASK_PHASE_UNSPECIFIED);
      return grpc::Status::OK;
    }

    auto& state = it->second;

    // Check if task can be cancelled (not already in terminal state)
    if (isTerminalPhase(state.phase)) {
      response->set_cancelled(false);
      response->set_message("Task already in terminal state");
      response->set_current_phase(state.phase);
      return grpc::Status::OK;
    }

    // Mark as cancelled
    previous_phase = state.phase;
    state.phase = hmas::TASK_PHASE_CANCELLED;
    state.updated_at = std::chrono::system_clock::now();

    // Capture data needed for the out-of-lock notification.
    assigned_agent_id = state.assigned_agent_id;
    task_cancelled = true;
  }  // mutex released here

  response->set_cancelled(task_cancelled);
  response->set_message("Task cancelled successfully");
  response->set_current_phase(previous_phase);

  // Notify the assigned agent to stop execution cooperatively.
  // This is done after releasing the mutex to avoid lock-order inversion.
  if (message_bus_ && !assigned_agent_id.empty()) {
    // Issue #515: CANCEL_TASK moved from core::ActionType to
    // agents::AgentActionType; use AgentEnvelope::createCancellation to build
    // the transport message.
    auto cancel_env = agents::AgentEnvelope::createCancellation(
        "coordinator",      // sender
        assigned_agent_id,  // receiver — the agent executing the task
        task_id             // task being cancelled
    );
    message_bus_->routeMessage(cancel_env.transport_msg);
  }

  return grpc::Status::OK;
}

grpc::Status HMASCoordinatorServiceImpl::GetTaskProgress(
    grpc::ServerContext* context, const hmas::TaskProgressRequest* request,
    hmas::TaskProgress* response) {
  (void)context;  // Unused

  // Validate task_id at the service boundary.
  try {
    keystone::core::validateNatsSubjectToken(request->task_id(), "task_id");
  } catch (const std::invalid_argument& e) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, e.what());
  }

  std::lock_guard<std::mutex> lock(mutex_);

  const std::string& task_id = request->task_id();

  auto it = active_tasks_.find(task_id);
  if (it == active_tasks_.end()) {
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "Task not found");
  }

  const auto& state = it->second;

  response->set_task_id(task_id);
  response->set_phase(state.phase);
  response->set_progress_percent(state.progress_percent);
  response->set_current_subtask(state.current_subtask);
  response->set_updated_at_unix_ms(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          state.updated_at.time_since_epoch())
          .count());

  // Check if complete
  response->set_is_complete(isTerminalPhase(state.phase));

  return grpc::Status::OK;
}

// Non-RPC methods

void HMASCoordinatorServiceImpl::updateTaskStatus(
    const std::string& task_id, hmas::TaskPhase phase, int32_t progress,
    const std::string& current_subtask) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = active_tasks_.find(task_id);
  if (it != active_tasks_.end()) {
    it->second.phase = phase;
    it->second.progress_percent = progress;
    it->second.current_subtask = current_subtask;
    it->second.updated_at = std::chrono::system_clock::now();
  }
}

std::optional<TaskState> HMASCoordinatorServiceImpl::getTaskState(
    const std::string& task_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = active_tasks_.find(task_id);
  if (it != active_tasks_.end()) {
    return it->second;
  }

  return std::nullopt;
}

bool HMASCoordinatorServiceImpl::hasTask(const std::string& task_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_tasks_.find(task_id) != active_tasks_.end();
}

size_t HMASCoordinatorServiceImpl::getActiveTaskCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_tasks_.size();
}

int32_t HMASCoordinatorServiceImpl::cleanupOldTasks(int64_t age_threshold_ms) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto now = std::chrono::system_clock::now();
  int32_t removed_count = 0;

  // Clean up old completed/failed tasks
  for (auto it = active_tasks_.begin(); it != active_tasks_.end();) {
    const auto& state = it->second;

    // Only clean up terminal states
    if (isTerminalPhase(state.phase)) {
      auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                     now - state.updated_at)
                     .count();

      if (age > age_threshold_ms) {
        // Also clean up result
        task_results_.erase(it->first);

        it = active_tasks_.erase(it);
        ++removed_count;
        continue;
      }
    }

    ++it;
  }

  return removed_count;
}

std::string HMASCoordinatorServiceImpl::generateTaskId() const {
  // Simple task ID generation: timestamp + random suffix
  auto now = std::chrono::system_clock::now();
  auto timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now.time_since_epoch())
                          .count();

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int32_t> dist(1000, 9999);

  std::ostringstream oss;
  oss << "task-" << timestamp_ms << "-" << dist(gen);
  return oss.str();
}

}  // namespace keystone::network
