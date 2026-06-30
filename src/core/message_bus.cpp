#include "core/message_bus.hpp"

#include "concurrency/work_stealing_scheduler.hpp"
#include "core/message_serializer.hpp"
#include "core/metrics.hpp"
#include "core/subject_validator.hpp"

#include <stdexcept>

namespace keystone {
namespace core {

void MessageBus::setScheduler(concurrency::WorkStealingScheduler* scheduler) {
  // FIX C5: Use atomic store for thread-safe access (no mutex needed)
  scheduler_.store(scheduler, std::memory_order_release);
}

concurrency::WorkStealingScheduler* MessageBus::getScheduler() const {
  // FIX C5: Use atomic load for thread-safe access (no mutex needed)
  return scheduler_.load(std::memory_order_acquire);
}

void MessageBus::registerAgent(const std::string& agent_id, std::shared_ptr<IMessageSink> agent) {
  // FIX C2: Use shared_ptr for safe lifetime management
  if (!agent) {
    throw std::invalid_argument("Cannot register null agent");
  }

  // Issue #113: Reject agent IDs with path traversal or injection characters.
  validateSubjectToken(agent_id, "agent_id");

  std::lock_guard<std::mutex> lock(registry_mutex_);

  // FIX P2-10: Enforce maximum agent limit to prevent DoS
  if (agents_.size() >= Config::MAX_AGENTS) {
    throw std::runtime_error("Maximum agent count exceeded: " + std::to_string(Config::MAX_AGENTS));
  }

  // Phase A2: Intern the agent_id string to get integer ID
  uint32_t int_id = interning_.intern(agent_id);

  // Check for duplicate registration using integer ID
  if (agents_.find(int_id) != agents_.end()) {
    throw std::runtime_error("Agent ID already registered: " + agent_id);
  }

  // Store agent using integer ID
  agents_[int_id] = agent;
}

void MessageBus::unregisterAgent(const std::string& agent_id) {
  std::lock_guard<std::mutex> lock(registry_mutex_);

  // Phase A2: Lookup integer ID first
  auto int_id = interning_.tryGetId(agent_id);
  if (!int_id) {
    return;  // Agent not registered
  }

  // Erase using integer ID
  agents_.erase(*int_id);
}

bool MessageBus::routeMessage(const KeystoneMessage& msg) {
  // FIX C1: Deadlock prevention - lookup agent, then release lock
  // before making external calls (agent->receiveMessage or scheduler->submit)
  // FIX C2: Use shared_ptr to keep agent alive during async routing
  // FIX C5: Scheduler loaded atomically (no mutex needed)
  // Issue #512: nats_publisher_ captured under its own mutex for off-host
  // forwarding.
  // Phase A2: Use integer ID for O(1) lookup
  std::shared_ptr<IMessageSink> agent;

  {
    std::lock_guard<std::mutex> lock(registry_mutex_);

    // Phase A2: Convert receiver_id string to integer ID (read-only operation)
    auto int_id = interning_.tryGetId(msg.receiver_id);
    if (!int_id) {
      // Receiver not registered locally — forward to NATS if a publisher is
      // set (Issue #512: TransparentBridge outbound path).
      std::function<void(std::string_view, std::span<const std::byte>)> pub;
      {
        std::lock_guard<std::mutex> pub_lock(nats_publisher_mutex_);
        pub = nats_publisher_;
      }
      if (pub) {
        // Serialize the full message and publish to the per-agent subject.
        const std::string subject = "hi.agents." + msg.receiver_id;
        const std::vector<uint8_t> bytes = MessageSerializer::serialize(msg);
        const auto* byte_ptr = reinterpret_cast<const std::byte*>(bytes.data());
        pub(subject, std::span<const std::byte>(byte_ptr, bytes.size()));
      }
      return false;  // Receiver not found locally
    }

    // Lookup agent using integer ID (O(1) integer hash vs O(log n) string
    // compare)
    auto it = agents_.find(*int_id);
    if (it == agents_.end()) {
      // Agent interned but not registered — same off-host forwarding path.
      std::function<void(std::string_view, std::span<const std::byte>)> pub;
      {
        std::lock_guard<std::mutex> pub_lock(nats_publisher_mutex_);
        pub = nats_publisher_;
      }
      if (pub) {
        const std::string subject = "hi.agents." + msg.receiver_id;
        const std::vector<uint8_t> bytes = MessageSerializer::serialize(msg);
        const auto* byte_ptr = reinterpret_cast<const std::byte*>(bytes.data());
        pub(subject, std::span<const std::byte>(byte_ptr, bytes.size()));
      }
      return false;  // Receiver not found
    }
    agent = it->second;  // Ref count incremented - agent kept alive
  }  // ✅ Lock released before external calls

  // Load scheduler atomically (thread-safe)
  concurrency::WorkStealingScheduler* sched = scheduler_.load(std::memory_order_acquire);

  // Record message sent to metrics for tracking
  Metrics::getInstance().recordMessageSent(msg.msg_id, msg.priority);

  // Async routing if scheduler present (Phase A Week 3+)
  if (sched) {
    // FIX C2: Lambda captures shared_ptr, keeping agent alive
    sched->submit([agent, msg]() { agent->receiveMessage(msg); });
    return true;
  }

  // Synchronous delivery for backward compatibility (Phase 1-3)
  agent->receiveMessage(msg);
  return true;
}

bool MessageBus::hasAgent(const std::string& agent_id) const {
  std::lock_guard<std::mutex> lock(registry_mutex_);

  // Phase A2: Lookup using integer ID for O(1) performance
  auto int_id = interning_.tryGetId(agent_id);
  if (!int_id) {
    return false;  // Not interned, so not registered
  }

  return agents_.find(*int_id) != agents_.end();
}

std::vector<std::string> MessageBus::listAgents() const {
  std::lock_guard<std::mutex> lock(registry_mutex_);

  std::vector<std::string> agent_ids;
  agent_ids.reserve(agents_.size());

  // Phase A2: Iterate over integer IDs and convert back to strings
  for (const auto& [int_id, _] : agents_) {
    auto str_id = interning_.tryGetString(int_id);
    if (str_id) {
      agent_ids.push_back(*str_id);
    }
  }

  return agent_ids;
}

void MessageBus::setNatsPublisher(
    std::function<void(std::string_view subject, std::span<const std::byte> payload)> publisher) {
  std::lock_guard<std::mutex> lock(nats_publisher_mutex_);
  nats_publisher_ = std::move(publisher);
}

std::function<void(std::string_view subject, std::span<const std::byte> payload)>
MessageBus::getNatsPublisher() const {
  std::lock_guard<std::mutex> lock(nats_publisher_mutex_);
  return nats_publisher_;
}

}  // namespace core
}  // namespace keystone
