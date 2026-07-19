#pragma once

#include <nats.h>

#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <thread>

namespace keystone {
namespace network {

/// Configuration for NATSListener.
struct NATSListenerConfig {
  std::string subject;       ///< NATS subject pattern, e.g. "hi.tasks.>"
  std::string durable_name;  ///< Durable consumer name for JetStream
  int max_ack_pending{1};    ///< Max unacked messages per AGENTS.md rate-limit
  int max_attempts{
      3};  ///< Maximum subscribe attempts before giving up (issue #331)
};

/// Callback invoked when a terminal task event advances the DAG.
using AdvanceDagCallback =
    std::function<void(std::string_view team_id, std::string_view task_id)>;

/// Result of parsing a NATS subject token.
enum class SubjectVerdict {
  kMalformed,        ///< Fewer than 5 dot-separated parts — nak
  kUnsafeToken,      ///< team_id or task_id contains disallowed chars — nak
  kUnknownVerb,      ///< Verb not in the known set — ack, no DAG advance
  kNonTerminalVerb,  ///< Known verb but not terminal (e.g. "updated") — ack
  kTerminal,  ///< Terminal verb ("completed"/"failed") — invoke callback
};

/// Parsed fields extracted from a NATS subject.
struct SubjectClassification {
  SubjectVerdict verdict{SubjectVerdict::kMalformed};
  std::string_view team_id;
  std::string_view task_id;
  std::string_view verb;
};

/// NATSListener subscribes to a JetStream durable consumer using a pull-based
/// fetch loop that honors MaxAckPending = 1 rate-limiting per AGENTS.md spec.
/// Every message is explicitly acked or nacked before the next fetch.
/// The listener runs on its own thread and can be cleanly stopped via stop().
class NATSListener {
 public:
  /// Construct listener with configuration and DAG-advance callback.
  NATSListener(NATSListenerConfig cfg, AdvanceDagCallback cb);

  NATSListener(const NATSListener&) = delete;
  NATSListener& operator=(const NATSListener&) = delete;
  NATSListener(NATSListener&&) = delete;
  NATSListener& operator=(NATSListener&&) = delete;

  ~NATSListener();

  /// Subscribe to the configured subject on the given JetStream context and
  /// start the pull-based fetch loop on an internal thread.
  /// @param js JetStream context (must outlive the listener or until stop() is
  /// called)
  /// @return NATS_OK on success.
  natsStatus start(jsCtx* js);

  /// Signal the loop to stop and wait for the thread to join.
  /// Safe to call multiple times. The subscription is unsubscribed and
  /// destroyed during this call.
  void stop();

  /// Parse a NATS subject into a SubjectClassification.
  /// Exposed as public static for direct unit testing without a NATS server.
  static SubjectClassification classify_subject(
      std::string_view subject) noexcept;

 private:
  /// Pull-based fetch loop running on listener_thread_.
  /// Repeatedly calls natsSubscription_Fetch(sub, 1, timeout_ms) to get one
  /// message at a time, calls handle_message(), then loops back to fetch the
  /// next message. Exits cleanly when stopped_ becomes true.
  void pull_loop() noexcept;

  void handle_message(natsMsg* msg) noexcept;

  NATSListenerConfig cfg_;
  AdvanceDagCallback callback_;
  natsSubscription* sub_{nullptr};
  std::atomic<bool> stopped_{false};
  std::thread listener_thread_;  ///< Pull loop runs here
  jsCtx* js_{nullptr};           ///< Cached JetStream context
};

}  // namespace network
}  // namespace keystone
