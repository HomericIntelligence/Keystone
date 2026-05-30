#pragma once

#include "core/message.hpp"

namespace keystone {
namespace core {

/**
 * @brief Minimal transport-facing interface for any entity that can receive a
 *        message.
 *
 * This is the single point of contact the transport core (MessageBus,
 * IAgentRegistry) requires from a delivery target. By depending on this
 * one-method abstraction instead of the concrete agents::AgentCore, the
 * transport core no longer depends on the agents layer at all.
 *
 * AgentCore implements this interface, so existing agents register and receive
 * messages exactly as before. Any non-agent sink (test stubs, bridges, future
 * non-agent consumers) can also be registered on a MessageBus without the
 * transport core depending on the concrete agent type.
 *
 * Prerequisite root-fix for the ADR-015/016 agent + Python removal: deletion
 * PRs were stalling because the core still referenced the agent layer.
 */
class IMessageSink {
 public:
  virtual ~IMessageSink() = default;

  /**
   * @brief Receive a message destined for this sink.
   *
   * Signature must match agents::AgentCore::receiveMessage exactly so AgentCore
   * can override it.
   *
   * @param msg Message to deliver to this sink.
   */
  virtual void receiveMessage(const KeystoneMessage& msg) = 0;
};

}  // namespace core
}  // namespace keystone
