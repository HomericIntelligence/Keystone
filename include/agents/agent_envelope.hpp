#pragma once

#include <map>
#include <optional>
#include <string>

#include "agents/agent_action_type.hpp"
#include "core/message.hpp"

namespace keystone {
namespace agents {

/**
 * @brief Agent-layer message envelope wrapping a transport KeystoneMessage
 *
 * AgentEnvelope sits above the transport layer and carries orchestration-level
 * metadata that does not belong in KeystoneMessage (a pure transport struct).
 * It was introduced as part of Issue #515 (SOLID/SRP: remove orchestration
 * concerns from the transport struct).
 *
 * The transport layer (MessageBus, NATS bridge) never sees this type; it only
 * passes raw KeystoneMessage objects. Agent code that needs session isolation,
 * task tracking, or orchestration action semantics wraps the incoming
 * KeystoneMessage in an AgentEnvelope.
 *
 * Construction on the receive path
 * ---------------------------------
 * Agent processMessage() implementations receive a raw core::KeystoneMessage.
 * When they need orchestration context, they call AgentEnvelope::wrap():
 *
 *   auto env = AgentEnvelope::wrap(msg);
 *   if (env.agent_action == AgentActionType::CANCEL_TASK) { ... }
 *
 * The agent_action field is populated by decoding the transport action_type
 * combined with the payload convention agreed between agents.
 */
struct AgentEnvelope {
  /// The underlying transport message
  core::KeystoneMessage transport_msg;

  /// Agent-level action type (orchestration semantics)
  std::optional<AgentActionType> agent_action;

  /// Session/context identifier for concurrent operation isolation
  std::string session_id{"default"};

  /// Optional task identifier for tracking/cancellation
  std::optional<std::string> task_id;

  /// Extensible key-value metadata (agent-layer, not serialized on wire)
  std::map<std::string, std::string> metadata;

  /**
   * @brief Wrap a raw transport KeystoneMessage in an AgentEnvelope
   *
   * Decodes agent_action from the transport message. The convention is:
   * - transport EXECUTE with payload prefix "CANCEL_TASK:" → CANCEL_TASK
   * - transport EXECUTE with payload prefix "TASK_FAILED:" → TASK_FAILED
   * - transport EXECUTE with payload prefix "DECOMPOSE:" → DECOMPOSE
   * All other messages have agent_action == std::nullopt (pure transport).
   *
   * @param msg Raw transport message
   * @return AgentEnvelope wrapping the message
   */
  static AgentEnvelope wrap(const core::KeystoneMessage& msg);

  /**
   * @brief Create an agent envelope for a new outgoing message
   *
   * @param sender Sender agent ID
   * @param receiver Receiver agent ID
   * @param action Agent-level action type
   * @param session Session identifier
   * @param data Optional payload
   * @return AgentEnvelope with transport_msg populated
   */
  static AgentEnvelope create(
      const std::string& sender, const std::string& receiver,
      AgentActionType action, const std::string& session = "default",
      const std::optional<std::string>& data = std::nullopt);

  /**
   * @brief Create a task cancellation envelope
   *
   * Cancellation is cooperative: agents check and respond gracefully.
   *
   * @param sender Sender agent ID (parent requesting cancellation)
   * @param receiver Receiver agent ID (child executing the task)
   * @param task_id_val Task identifier to cancel
   * @param session Session identifier
   * @return AgentEnvelope with CANCEL_TASK agent_action
   */
  static AgentEnvelope createCancellation(
      const std::string& sender, const std::string& receiver,
      const std::string& task_id_val, const std::string& session = "default");

  /**
   * @brief Create a task failure notification envelope
   *
   * Sent by a subordinate agent to its parent when execution fails.
   *
   * @param sender Sender agent ID (child reporting failure)
   * @param receiver Receiver agent ID (parent waiting for result)
   * @param error_msg Human-readable failure description
   * @param session Session identifier
   * @return AgentEnvelope with TASK_FAILED agent_action
   */
  static AgentEnvelope createFailure(const std::string& sender,
                                     const std::string& receiver,
                                     const std::string& error_msg,
                                     const std::string& session = "default");
};

}  // namespace agents
}  // namespace keystone
