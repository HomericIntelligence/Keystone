#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace keystone {
namespace core {

/**
 * @brief Message priority levels
 *
 * Phase C: Priority-based message processing.
 * Higher priority messages are processed before lower priority ones.
 */
enum class Priority {
  HIGH = 0,    ///< Urgent, time-sensitive messages
  NORMAL = 1,  ///< Standard priority (default)
  LOW = 2      ///< Background, non-urgent messages
};

/**
 * @brief Convert Priority to string
 */
inline std::string priorityToString(Priority priority) {
  switch (priority) {
    case Priority::HIGH:
      return "HIGH";
    case Priority::NORMAL:
      return "NORMAL";
    case Priority::LOW:
      return "LOW";
    default:
      return "UNKNOWN";
  }
}

/**
 * @brief Transport-level action types for the KIM protocol
 *
 * These are the action types the pure transport layer understands.
 * Orchestration-level actions (CANCEL_TASK, TASK_FAILED, DECOMPOSE) have been
 * moved to agents::AgentActionType per Issue #515 (SOLID/SRP: KeystoneMessage
 * is a pure transport struct and must not carry orchestration semantics).
 */
enum class ActionType {
  EXECUTE,        ///< Execute a concrete task or command
  RETURN_RESULT,  ///< Return the result of a computation
  SHUTDOWN,       ///< Graceful shutdown signal
};

/**
 * @brief Content type for message payload
 *
 * Indicates the format/encoding of the message payload.
 */
enum class ContentType {
  TEXT_PLAIN,   ///< Plain text payload (default)
  BINARY_CISTA  ///< Binary payload serialized with Cista
};

/**
 * @brief Convert ActionType to string
 */
inline std::string actionTypeToString(ActionType type) {
  switch (type) {
    case ActionType::EXECUTE:
      return "EXECUTE";
    case ActionType::RETURN_RESULT:
      return "RETURN_RESULT";
    case ActionType::SHUTDOWN:
      return "SHUTDOWN";
    default:
      return "UNKNOWN";
  }
}

/**
 * @brief Convert ContentType to string
 */
inline std::string contentTypeToString(ContentType type) {
  switch (type) {
    case ContentType::TEXT_PLAIN:
      return "TEXT_PLAIN";
    case ContentType::BINARY_CISTA:
      return "BINARY_CISTA";
    default:
      return "UNKNOWN";
  }
}

/**
 * @brief Keystone Interchange Message (KIM) protocol — pure transport struct
 *
 * Carries routing headers and an opaque payload between transport endpoints.
 * This struct is intentionally free of orchestration concerns:
 * - No session_id  (moved to agents::AgentEnvelope per Issue #515)
 * - No task_id     (moved to agents::AgentEnvelope per Issue #515)
 * - No metadata    (moved to agents::AgentEnvelope per Issue #515)
 * - No CANCEL_TASK / TASK_FAILED action types (moved to agents::AgentActionType)
 *
 * Agents that require orchestration-level semantics wrap an incoming
 * KeystoneMessage in an agents::AgentEnvelope.
 */
struct KeystoneMessage {
  // Core identifiers
  std::string msg_id;       ///< Unique message identifier (UUID)
  std::string sender_id;    ///< ID of the sending agent
  std::string receiver_id;  ///< ID of the receiving agent

  // Transport action and encoding
  ActionType action_type;    ///< Type of action requested/performed
  ContentType content_type;  ///< Format of the payload

  // Phase C: Priority field
  Priority priority;  ///< Message priority (HIGH/NORMAL/LOW)

  // Phase C: Deadline scheduling
  std::optional<std::chrono::system_clock::time_point> deadline;  ///< Optional processing deadline

  // Issue #285: Cross-host tracing
  std::optional<std::string> correlation_id;  ///< Optional correlation ID for distributed tracing

  // Payload and timing
  [[deprecated(
      "command is a legacy/convenience field; use payload with ActionType "
      "instead")]]
  std::string command;                              ///< Command string (legacy)
  std::optional<std::string> payload;               ///< Optional payload data
  std::chrono::system_clock::time_point timestamp;  ///< Message timestamp

  // Declare special members out-of-line so their definitions (in message.cpp)
  // can suppress -Wdeprecated-declarations for the internal move/copy of the
  // deprecated 'command' field.  External code that reads/writes 'command'
  // directly will still receive the deprecation diagnostic.
  KeystoneMessage();
  KeystoneMessage(const KeystoneMessage&);
  KeystoneMessage(KeystoneMessage&&) noexcept;
  KeystoneMessage& operator=(const KeystoneMessage&);
  KeystoneMessage& operator=(KeystoneMessage&&) noexcept;
  ~KeystoneMessage();

  /**
   * @brief Create a new message with generated ID (legacy interface)
   *
   * Maintains backward compatibility with existing code.
   * Uses ActionType::EXECUTE and ContentType::TEXT_PLAIN by default.
   *
   * @param sender Sender agent ID
   * @param receiver Receiver agent ID
   * @param cmd Command string (stored in legacy 'command' field)
   * @param data Optional payload data
   * @return KeystoneMessage New message with auto-generated ID
   */
  static KeystoneMessage create(const std::string& sender,
                                const std::string& receiver,
                                const std::string& cmd,
                                const std::optional<std::string>& data = std::nullopt);

  /**
   * @brief Create a new transport message with action type
   *
   * @param sender Sender agent ID
   * @param receiver Receiver agent ID
   * @param action Transport action type
   * @param data Optional payload data
   * @param content Content type (default: TEXT_PLAIN)
   * @return KeystoneMessage New message with auto-generated ID
   */
  static KeystoneMessage create(const std::string& sender,
                                const std::string& receiver,
                                ActionType action,
                                const std::optional<std::string>& data = std::nullopt,
                                ContentType content = ContentType::TEXT_PLAIN);

  /**
   * @brief Set deadline relative to current time
   *
   * @param duration_ms Deadline in milliseconds from now
   */
  void setDeadlineFromNow(std::chrono::milliseconds duration_ms);

  /**
   * @brief Check if message has missed its deadline
   *
   * @return true if deadline exists and has passed, false otherwise
   */
  bool hasDeadlinePassed() const;

  /**
   * @brief Get time remaining until deadline
   *
   * @return milliseconds until deadline, nullopt if no deadline set
   */
  std::optional<std::chrono::milliseconds> getTimeUntilDeadline() const;
};

/**
 * @brief Response to a KeystoneMessage
 */
struct Response {
  std::string msg_id;       ///< ID of the original message
  std::string sender_id;    ///< ID of the responding agent
  std::string receiver_id;  ///< ID of the original sender
  enum class Status { Success, Error } status;
  std::string result;  ///< Result data or error message
  std::chrono::system_clock::time_point timestamp;

  /**
   * @brief Create a success response
   *
   * @param original_msg The message being responded to
   * @param sender The responding agent ID
   * @param result_data The result data
   * @return Response Success response
   */
  static Response createSuccess(const KeystoneMessage& original_msg,
                                const std::string& sender,
                                const std::string& result_data);

  /**
   * @brief Create an error response
   *
   * @param original_msg The message being responded to
   * @param sender The responding agent ID
   * @param error_msg The error message
   * @return Response Error response
   */
  static Response createError(const KeystoneMessage& original_msg,
                              const std::string& sender,
                              const std::string& error_msg);
};

}  // namespace core
}  // namespace keystone
