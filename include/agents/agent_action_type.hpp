#pragma once

#include <string>

namespace keystone {
namespace agents {

/**
 * @brief Agent-layer action types for HMAS orchestration
 *
 * These action types belong to the agent layer (ProjectAgamemnon and
 * the legacy agent shim in Keystone) rather than to the transport layer.
 * They were extracted from core::ActionType per Issue #515 (SOLID/SRP
 * violation: KeystoneMessage carried orchestration semantics).
 *
 * Transport-level signals (EXECUTE, RETURN_RESULT, SHUTDOWN) remain in
 * core::ActionType. Orchestration-level signals live here.
 */
enum class AgentActionType {
  DECOMPOSE,    ///< Decompose a goal into subtasks/subgoals
  CANCEL_TASK,  ///< Cancel a running task
  TASK_FAILED   ///< Report task failure to parent agent
};

/**
 * @brief Convert AgentActionType to string
 */
inline std::string agentActionTypeToString(AgentActionType type) {
  switch (type) {
    case AgentActionType::DECOMPOSE:
      return "DECOMPOSE";
    case AgentActionType::CANCEL_TASK:
      return "CANCEL_TASK";
    case AgentActionType::TASK_FAILED:
      return "TASK_FAILED";
    default:
      return "UNKNOWN";
  }
}

}  // namespace agents
}  // namespace keystone
