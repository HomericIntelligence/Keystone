#include "agents/async_agent.hpp"

namespace keystone {
namespace agents {

AsyncAgent::AsyncAgent(const std::string& agent_id) : AgentCore(agent_id) {}

void AsyncAgent::receiveMessage(const core::KeystoneMessage& msg) {
  auto* sched = scheduler_.load(std::memory_order_acquire);
  if (sched == nullptr) {
    // No scheduler: fall back to inbox (pull model)
    AgentCore::receiveMessage(msg);
    return;
  }

  // Scheduler stored: auto-process via worker thread (push model).
  // The MessageBus holds a shared_ptr to this agent while the lambda is pending,
  // so 'this' is safe to capture (scheduler drains before agents are destroyed).
  sched->submit([this, msg]() {
    // processMessage returns a Task<Response>; drive it to completion synchronously
    // on the worker thread. The thread-local scheduler is set, so any co_await
    // inside processMessage routes continuations through the worker pool.
    auto task = processMessage(msg);
    task.get();
  });
}

core::Response AsyncAgent::handleCancellation(const core::KeystoneMessage& msg) {
  // Extract task_id from the cancellation message
  if (!msg.task_id.has_value()) {
    return core::Response::createError(msg, agent_id_, "CANCEL_TASK message missing task_id");
  }

  const std::string& task_id = *msg.task_id;

  // Mark the task as cancelled
  requestCancellation(task_id);

  // Return acknowledgement
  return core::Response::createSuccess(msg,
                                       agent_id_,
                                       "Task " + task_id + " marked for cancellation");
}

}  // namespace agents
}  // namespace keystone
