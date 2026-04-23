#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <string_view>

#include "nats/nats.h"

namespace keystone {
namespace network {

/// Configuration for NATSListener.
struct NATSListenerConfig {
  std::string subject;       ///< NATS subject pattern, e.g. "hi.tasks.>"
  std::string durable_name;  ///< Durable consumer name for JetStream
  int max_ack_pending{1};    ///< Max unacked messages per CLAUDE.md rate-limit
};

/// Callback invoked when a terminal task event advances the DAG.
using AdvanceDagCallback = std::function<void(std::string_view team_id, std::string_view task_id)>;

/// Result of parsing a NATS subject token.
enum class SubjectVerdict {
  kMalformed,        ///< Fewer than 5 dot-separated parts — nak
  kUnsafeToken,      ///< team_id or task_id contains disallowed chars — nak
  kUnknownVerb,      ///< Verb not in the known set — ack, no DAG advance
  kNonTerminalVerb,  ///< Known verb but not terminal (e.g. "updated") — ack
  kTerminal,         ///< Terminal verb ("completed"/"failed") — invoke callback
};

/// Parsed fields extracted from a NATS subject.
struct SubjectClassification {
  SubjectVerdict verdict{SubjectVerdict::kMalformed};
  std::string_view team_id;
  std::string_view task_id;
  std::string_view verb;
};

/// NATSListener subscribes to a JetStream durable consumer and drives DAG
/// advancement on every terminal task event.  Every code path through the
/// message handler explicitly calls natsMsg_Ack or natsMsg_Nak before
/// returning so that messages are never left unacknowledged (issue #86).
class NATSListener {
 public:
  /// Construct listener with configuration and DAG-advance callback.
  NATSListener(NATSListenerConfig cfg, AdvanceDagCallback cb);

  NATSListener(const NATSListener&) = delete;
  NATSListener& operator=(const NATSListener&) = delete;
  NATSListener(NATSListener&&) = delete;
  NATSListener& operator=(NATSListener&&) = delete;

  ~NATSListener();

  /// Subscribe to the configured subject on the given JetStream context.
  /// @return NATS_OK on success.
  natsStatus start(jsCtx* js);

  /// Unsubscribe and release the subscription.  Safe to call multiple times.
  void stop();

  /// Parse a NATS subject into a SubjectClassification.
  /// Exposed as public static for direct unit testing without a NATS server.
  static SubjectClassification classify_subject(std::string_view subject) noexcept;

 private:
  static void on_msg(natsConnection* nc, natsSubscription* sub, natsMsg* msg, void* userdata);

  void handle_message(natsMsg* msg) noexcept;

  NATSListenerConfig cfg_;
  AdvanceDagCallback callback_;
  natsSubscription* sub_{nullptr};
  std::atomic<bool> stopped_{false};
};

}  // namespace network
}  // namespace keystone
