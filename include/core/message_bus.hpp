#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "agent_id_interning.hpp"
#include "i_agent_registry.hpp"
#include "i_message_router.hpp"
#include "i_scheduler_integration.hpp"
#include "message.hpp"
#include "message_sink.hpp"

// Forward declarations (must be outside namespace keystone to avoid nesting)
namespace keystone {
namespace concurrency {
class WorkStealingScheduler;
}
}  // namespace keystone

namespace keystone {
namespace core {

/**
 * @brief Central message routing hub for agent communication
 *
 * MessageBus decouples agents from each other, enabling:
 * - Dynamic agent registration/discovery (IAgentRegistry)
 * - Automatic message routing by agent ID (IMessageRouter)
 * - Async message processing via WorkStealingScheduler (ISchedulerIntegration)
 *
 * Architecture:
 * - Implements 3 separate interfaces following Interface Segregation Principle
 * - Without scheduler: Synchronous routing (Phase 1-3 behavior)
 * - With scheduler: Async routing via work-stealing threads
 *
 * Interface Segregation (Issue #46):
 * This class implements all 3 interfaces but consumers should use the specific
 * interface they need (IAgentRegistry for setup, IMessageRouter for routing,
 * ISchedulerIntegration for async configuration).
 */
class MessageBus : public IAgentRegistry,
                   public IMessageRouter,
                   public ISchedulerIntegration {
 public:
  MessageBus() = default;
  ~MessageBus() = default;

  // Non-copyable, non-movable
  MessageBus(const MessageBus&) = delete;
  MessageBus& operator=(const MessageBus&) = delete;
  MessageBus(MessageBus&&) = delete;
  MessageBus& operator=(MessageBus&&) = delete;

  // ========================================================================
  // ISchedulerIntegration interface implementation
  // ========================================================================

  /**
   * @brief Set the work-stealing scheduler for async message routing
   *
   * When scheduler is set, routeMessage() submits messages as work items
   * to the scheduler instead of synchronous delivery.
   *
   * @param scheduler Pointer to scheduler (must outlive MessageBus)
   */
  void setScheduler(concurrency::WorkStealingScheduler* scheduler) override;

  /**
   * @brief Get the current scheduler (may be nullptr)
   */
  concurrency::WorkStealingScheduler* getScheduler() const override;

  // ========================================================================
  // IAgentRegistry interface implementation
  // ========================================================================

  /**
   * @brief Register an agent with the bus
   *
   * FIX C2: Changed to shared_ptr for safe lifetime management.
   * Prevents use-after-free in async routing scenarios.
   *
   * @param agent_id Unique identifier for the agent
   * @param agent Shared pointer to the agent (lifetime managed by shared_ptr)
   * @throws std::runtime_error if agent_id already registered
   */
  void registerAgent(const std::string& agent_id,
                     std::shared_ptr<IMessageSink> agent) override;

  /**
   * @brief Register an agent with compile-time interface verification (Issue
   * #24)
   *
   * This templated version uses C++20 concepts to verify at compile time
   * that the agent implements the required interface:
   * - getAgentId() -> string
   * - sendMessage(KeystoneMessage) -> void
   * - receiveMessage(KeystoneMessage) -> void
   * - processMessage(KeystoneMessage) -> Task<Response>
   *
   * Benefits:
   * - Compile-time errors for missing methods
   * - Better error messages than SFINAE
   * - Self-documenting code (concept is the contract)
   * - Enables generic agent algorithms
   *
   * Example:
   * @code
   * // MyAgent satisfies the AgentLike concept (getAgentId() + IMessageSink).
   * auto agent = std::make_shared<MyAgent>("worker-1");
   * bus.registerAgent(agent);  // Compile error if interface incomplete
   * @endcode
   *
   * @tparam A Agent type exposing getAgentId() and convertible to IMessageSink
   * @param agent Shared pointer to the agent
   * @throws std::runtime_error if agent_id already registered
   */
  template <typename A>
    requires requires(const A& a) {
      { a.getAgentId() } -> std::convertible_to<std::string>;
      requires std::convertible_to<std::shared_ptr<A>,
                                   std::shared_ptr<IMessageSink>>;
    }
  void registerAgent(std::shared_ptr<A> agent) {
    if (!agent) {
      throw std::runtime_error("MessageBus::registerAgent: null agent pointer");
    }

    // Use the agent's ID
    std::string agent_id = agent->getAgentId();

    // Upcast to the transport-facing sink interface for storage.
    std::shared_ptr<IMessageSink> sink = agent;

    // Delegate to the existing implementation
    registerAgent(agent_id, sink);
  }

  /**
   * @brief Unregister an agent from the bus
   *
   * @param agent_id Agent identifier to unregister
   */
  void unregisterAgent(const std::string& agent_id) override;

  // ========================================================================
  // IMessageRouter interface implementation
  // ========================================================================

  /**
   * @brief Route a message to the appropriate agent
   *
   * Behavior depends on scheduler:
   * - No scheduler: Synchronous delivery via agent->receiveMessage()
   * - With scheduler: Async delivery via scheduler->submit()
   *
   * @param msg Message to route (uses msg.receiver_id for routing)
   * @return true if message was delivered/submitted, false if receiver not
   * found
   */
  bool routeMessage(const KeystoneMessage& msg) override;

  /**
   * @brief Check if an agent is registered
   *
   * @param agent_id Agent identifier to check
   * @return true if agent is registered
   */
  bool hasAgent(const std::string& agent_id) const override;

  /**
   * @brief Get list of all registered agent IDs
   *
   * @return std::vector<std::string> List of agent IDs
   */
  std::vector<std::string> listAgents() const override;

  // ========================================================================
  // Bridge wiring for transparent NATS forwarding (Issues #206, #333)
  // ========================================================================

  /**
   * @brief Set NATS publisher callback for off-host message forwarding
   *
   * When set, MessageBus will forward messages destined for off-host agents
   * via the provided publisher function. This enables transparent bridging
   * between local MessageBus and NATS JetStream.
   *
   * The publisher is called on the caller's thread after local agent lookup
   * fails. It must be safe to call repeatedly and must not block indefinitely.
   *
   * @param publisher Callback function: (subject, payload) -> void
   *                  Called when message needs off-host forwarding.
   *                  Can be null to disable NATS forwarding.
   */
  void setNatsPublisher(std::function<void(std::string_view subject,
                                           std::span<const std::byte> payload)>
                            publisher);

  /**
   * @brief Get current NATS publisher callback (may be nullptr)
   */
  std::function<void(std::string_view subject,
                     std::span<const std::byte> payload)>
  getNatsPublisher() const;

 private:
  mutable std::mutex registry_mutex_;

  // Phase A2: Agent ID interning for O(1) integer-based lookups
  AgentIdInterning interning_;

  // FIX C2: Use shared_ptr for safe lifetime management in async scenarios
  // Phase A2: Registry now uses integer IDs (interned from string IDs)
  std::unordered_map<uint32_t, std::shared_ptr<IMessageSink>> agents_;

  // FIX C5: Atomic scheduler pointer for thread-safe access without
  // registry_mutex
  std::atomic<concurrency::WorkStealingScheduler*> scheduler_{nullptr};

  // Issue #206/#333: NATS publisher for transparent bridge forwarding
  mutable std::mutex nats_publisher_mutex_;
  std::function<void(std::string_view subject,
                     std::span<const std::byte> payload)>
      nats_publisher_;
};

}  // namespace core
}  // namespace keystone
