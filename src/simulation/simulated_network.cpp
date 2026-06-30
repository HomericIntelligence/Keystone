#include "simulation/simulated_network.hpp"

#include "concurrency/logger.hpp"

#include <algorithm>

namespace keystone {
namespace simulation {

using namespace concurrency;

SimulatedNetwork::SimulatedNetwork() : SimulatedNetwork(Config{}) {}

SimulatedNetwork::SimulatedNetwork(Config config)
    : config_(config),
      rng_(std::random_device{}()),
      latency_dist_(config.min_latency.count(), config.max_latency.count()),
      loss_dist_(0.0, 1.0) {
  Logger::info("SimulatedNetwork: Created (latency: {}-{}µs, packet_loss: {}%)",
               config_.min_latency.count(),
               config_.max_latency.count(),
               config_.packet_loss_rate * 100.0);
}

void SimulatedNetwork::send(size_t from_node, size_t to_node, std::function<void()> work) {
  total_messages_++;

  // Phase 5.2: Check for network partition
  if (!canCommunicate(from_node, to_node)) {
    partition_dropped_messages_++;
    Logger::debug("SimulatedNetwork: Message dropped due to partition ({}→{})", from_node, to_node);
    return;
  }

  // Check for packet loss
  if (shouldDropPacket()) {
    dropped_messages_++;
    Logger::debug("SimulatedNetwork: Packet dropped ({}→{})", from_node, to_node);
    return;
  }

  // Generate random latency
  auto latency = generateLatency();
  auto now = std::chrono::steady_clock::now();
  auto deliver_at = now + latency;

  // Create network message
  NetworkMessage msg{.from_node = from_node,
                     .to_node = to_node,
                     .work = std::move(work),
                     .sent_at = now,
                     .deliver_at = deliver_at};

  // Enqueue for delivery
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    message_queues_[to_node].push(std::move(msg));
  }

  Logger::trace("SimulatedNetwork: Message sent ({}→{}, latency={}µs)",
                from_node,
                to_node,
                latency.count());
}

std::optional<std::function<void()>> SimulatedNetwork::receive(size_t node_id) {
  std::lock_guard<std::mutex> lock(queue_mutex_);

  auto it = message_queues_.find(node_id);
  if (it == message_queues_.end() || it->second.empty()) {
    return std::nullopt;
  }

  auto& queue = it->second;
  auto& msg = queue.front();

  // Check if latency period has elapsed
  auto now = std::chrono::steady_clock::now();
  if (now < msg.deliver_at) {
    // Message not ready yet
    return std::nullopt;
  }

  // Deliver message
  auto latency_us =
      std::chrono::duration_cast<std::chrono::microseconds>(now - msg.sent_at).count();

  total_latency_us_ += latency_us;
  delivered_messages_++;

  auto work = std::move(msg.work);
  queue.pop();

  Logger::trace("SimulatedNetwork: Message delivered (node={}, latency={}µs)", node_id, latency_us);

  return work;
}

double SimulatedNetwork::getAverageLatencyUs() const {
  size_t delivered = delivered_messages_.load();
  if (delivered == 0) {
    return 0.0;
  }
  return static_cast<double>(total_latency_us_.load()) / delivered;
}

size_t SimulatedNetwork::getPendingMessages() const {
  std::lock_guard<std::mutex> lock(queue_mutex_);

  size_t total = 0;
  for (const auto& [node_id, queue] : message_queues_) {
    total += queue.size();
  }
  return total;
}

void SimulatedNetwork::resetStats() {
  total_messages_.store(0);
  delivered_messages_.store(0);
  dropped_messages_.store(0);
  total_latency_us_.store(0);
  partition_dropped_messages_.store(0);  // Phase 5.2

  Logger::debug("SimulatedNetwork: Stats reset");
}

std::chrono::microseconds SimulatedNetwork::generateLatency() {
  std::lock_guard<std::mutex> lock(rng_mutex_);
  int latency_us = latency_dist_(rng_);
  return std::chrono::microseconds(latency_us);
}

bool SimulatedNetwork::shouldDropPacket() {
  if (config_.packet_loss_rate <= 0.0) {
    return false;
  }
  std::lock_guard<std::mutex> lock(rng_mutex_);
  return loss_dist_(rng_) < config_.packet_loss_rate;
}

// ============================================================================
// PHASE 5.2: Network Partition Implementation
// ============================================================================

void SimulatedNetwork::createPartition(const std::vector<size_t>& partition_a,
                                       const std::vector<size_t>& partition_b) {
  std::lock_guard<std::mutex> lock(partition_mutex_);

  partition_a_ = partition_a;
  partition_b_ = partition_b;
  is_partitioned_.store(true);

  Logger::info("SimulatedNetwork: Partition created - A={} nodes, B={} nodes",
               partition_a.size(),
               partition_b.size());
}

void SimulatedNetwork::healPartition() {
  std::lock_guard<std::mutex> lock(partition_mutex_);

  partition_a_.clear();
  partition_b_.clear();
  is_partitioned_.store(false);

  Logger::info("SimulatedNetwork: Partition healed - full connectivity restored");
}

bool SimulatedNetwork::isPartitioned() const {
  return is_partitioned_.load();
}

bool SimulatedNetwork::canCommunicate(size_t from_node, size_t to_node) const {
  // If no partition, all nodes can communicate
  if (!is_partitioned_.load()) {
    return true;
  }

  // Check partition membership
  std::lock_guard<std::mutex> lock(partition_mutex_);

  // Check if both nodes are in partition A
  bool from_in_a = std::find(partition_a_.begin(), partition_a_.end(), from_node) !=
                   partition_a_.end();
  bool to_in_a = std::find(partition_a_.begin(), partition_a_.end(), to_node) != partition_a_.end();

  if (from_in_a && to_in_a) {
    return true;  // Both in partition A
  }

  // Check if both nodes are in partition B
  bool from_in_b = std::find(partition_b_.begin(), partition_b_.end(), from_node) !=
                   partition_b_.end();
  bool to_in_b = std::find(partition_b_.begin(), partition_b_.end(), to_node) != partition_b_.end();

  if (from_in_b && to_in_b) {
    return true;  // Both in partition B
  }

  // Nodes are in different partitions - cannot communicate
  return false;
}

}  // namespace simulation
}  // namespace keystone
