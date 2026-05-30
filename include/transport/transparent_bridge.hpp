/**
 * @file transparent_bridge.hpp
 * @brief TransparentBridge — automatic promotion of off-host messages between
 *        MessageBus and NATS JetStream (Issue #512).
 *
 * The bridge wires the outbound and inbound paths:
 * - Outbound: MessageBus calls its nats_publisher_ callback when local agent
 *   lookup fails.  TransparentBridge registers that callback via attach() so
 *   that serialized KeystoneMessage bytes are published to the per-agent NATS
 *   subject (hi.agents.<receiver_id>).
 * - Inbound: A pull-based fetch loop (modelled on NATSListener::pull_loop())
 *   subscribes to cfg_.inbound_subject, deserializes arriving payloads, and
 *   calls MessageBus::routeMessage() to deliver them to locally registered
 *   agents.
 *
 * Lifetime contract:
 * - MessageBus& and NatsConnection& must outlive this object.
 * - Call attach() once after NatsConnection::connect() succeeds.
 * - Call stop() before destroying either the bridge or the NatsConnection.
 *   stop() is idempotent.
 */

#pragma once

#include <atomic>
#include <string>
#include <thread>

#include <nats.h>

// Forward declarations — avoid pulling in full nats.h types in callers.
namespace keystone {
namespace core {
class MessageBus;
}  // namespace core
namespace transport {
class NatsConnection;
}  // namespace transport
}  // namespace keystone

namespace keystone {
namespace transport {

/**
 * @brief Configuration for TransparentBridge.
 */
struct BridgeConfig {
  /// NATS subject to subscribe on for inbound messages.
  std::string inbound_subject{"hi.agents.>"};

  /// Durable JetStream consumer name.
  std::string durable_name{"keystone-bridge"};

  /// MaxAckPending per CLAUDE.md rate-limiting spec.
  int max_ack_pending{1};

  /// Maximum subscribe attempts before giving up (mirrors NATSListener).
  int max_attempts{3};
};

/**
 * @brief Automatic bridge between local MessageBus and NATS JetStream.
 *
 * After attach() is called the bridge:
 * 1. Registers an outbound NATS publisher with MessageBus so that messages for
 *    unregistered (off-host) agents are serialized and published automatically.
 * 2. Starts an inbound pull loop that subscribes to BridgeConfig::inbound_subject,
 *    deserializes each payload, and routes the resulting KeystoneMessage into
 *    the local MessageBus.
 *
 * No component needs to know whether its peer is local or remote.
 */
class TransparentBridge {
 public:
  /**
   * @param bus  Local message bus.  Must outlive this object.
   * @param conn NATS connection.  Must outlive this object.
   * @param cfg  Optional configuration override.
   */
  TransparentBridge(core::MessageBus& bus, NatsConnection& conn, BridgeConfig cfg = {});

  ~TransparentBridge();

  // Non-copyable, non-movable.
  TransparentBridge(const TransparentBridge&) = delete;
  TransparentBridge& operator=(const TransparentBridge&) = delete;
  TransparentBridge(TransparentBridge&&) = delete;
  TransparentBridge& operator=(TransparentBridge&&) = delete;

  /**
   * @brief Wire the bridge into MessageBus and start the inbound pull loop.
   *
   * Must be called once after NatsConnection::connect() succeeds.  Internally
   * calls conn_.jsContext() — do NOT pass a raw jsCtx* here; the NatsConnection
   * owns the context lifetime.
   *
   * @return NATS_OK on success; an natsStatus error code otherwise.
   *         On failure the outbound publisher may still be registered even if
   *         the inbound subscription fails; check the return value.
   */
  natsStatus attach();

  /**
   * @brief Stop the inbound pull loop and unregister the outbound publisher.
   *
   * Idempotent — safe to call multiple times or if attach() was never called.
   * Must be called before NatsConnection::disconnect().
   */
  void stop();

 private:
  /// Inbound pull-based fetch loop (runs on inbound_thread_).
  void inbound_loop() noexcept;

  core::MessageBus& bus_;
  NatsConnection& conn_;
  BridgeConfig cfg_;

  natsSubscription* sub_{nullptr};
  std::atomic<bool> stopped_{false};
  std::thread inbound_thread_;
};

// ---------------------------------------------------------------------------
// Subject derivation utility
// ---------------------------------------------------------------------------

/**
 * @brief Derive the NATS subject for a given agent receiver ID.
 *
 * Maps receiver_id → "hi.agents.<receiver_id>".
 * The pattern is consistent with the NATS subject schema in CLAUDE.md
 * (homeric-agents stream: hi.agents.>).
 *
 * @param receiver_id Agent identifier (must be a safe NATS token).
 * @return NATS subject string.
 */
std::string deriveNatsSubject(std::string_view receiver_id);

}  // namespace transport
}  // namespace keystone
