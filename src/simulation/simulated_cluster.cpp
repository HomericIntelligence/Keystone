#include "simulation/simulated_cluster.hpp"

#include "concurrency/logger.hpp"

#include <cmath>
#include <numeric>
#include <stdexcept>

namespace keystone {
namespace simulation {

using namespace concurrency;

SimulatedCluster::SimulatedCluster(Config config)
    : config_(config), network_(std::make_unique<SimulatedNetwork>(config.network_config)) {
  if (config_.num_nodes == 0) {
    throw std::invalid_argument("SimulatedCluster: num_nodes must be > 0");
  }

  // Create all nodes
  nodes_.reserve(config_.num_nodes);
  for (size_t i = 0; i < config_.num_nodes; ++i) {
    nodes_.push_back(std::make_unique<SimulatedNUMANode>(i, config_.workers_per_node));
  }

  Logger::info("SimulatedCluster: Created with {} nodes, {} workers/node",
               config_.num_nodes,
               config_.workers_per_node);
}

SimulatedCluster::~SimulatedCluster() {
  if (started_) {
    shutdown();
  }
}

void SimulatedCluster::start() {
  if (started_) {
    Logger::warn("SimulatedCluster: Already started");
    return;
  }

  // Start all nodes
  for (auto& node : nodes_) {
    node->start();
  }

  started_ = true;
  Logger::info("SimulatedCluster: Started {} nodes", nodes_.size());
}

void SimulatedCluster::shutdown() {
  if (!started_) {
    Logger::warn("SimulatedCluster: Not started");
    return;
  }

  // Shutdown all nodes
  for (auto& node : nodes_) {
    node->shutdown();
  }

  started_ = false;
  Logger::info("SimulatedCluster: Shutdown complete");
}

void SimulatedCluster::submit(const std::string& agent_id, std::function<void()> work) {
  total_tasks_submitted_++;

  // Find agent's home node
  size_t node_id;
  {
    std::lock_guard<std::mutex> lock(agent_map_mutex_);
    auto it = agent_node_map_.find(agent_id);
    if (it != agent_node_map_.end()) {
      node_id = it->second;
    } else {
      node_id = round_robin_counter_.fetch_add(1) % nodes_.size();
    }
  }
  nodes_[node_id]->submit(std::move(work));
  Logger::trace("SimulatedCluster: Submitted work for agent '{}' to node {}", agent_id, node_id);
}

void SimulatedCluster::submitToNode(size_t node_id, std::function<void()> work) {
  if (node_id >= nodes_.size()) {
    throw std::out_of_range("SimulatedCluster: Invalid node_id");
  }

  total_tasks_submitted_++;
  nodes_[node_id]->submit(std::move(work));
  Logger::trace("SimulatedCluster: Submitted work to node {}", node_id);
}

void SimulatedCluster::registerAgent(const std::string& agent_id, size_t preferred_node) {
  if (preferred_node >= nodes_.size()) {
    throw std::out_of_range("SimulatedCluster: Invalid preferred_node");
  }

  {
    std::lock_guard<std::mutex> lock(agent_map_mutex_);
    agent_node_map_[agent_id] = preferred_node;
  }
  nodes_[preferred_node]->registerAgent(agent_id);

  Logger::info("SimulatedCluster: Registered agent '{}' on node {}", agent_id, preferred_node);
}

void SimulatedCluster::unregisterAgent(const std::string& agent_id) {
  size_t node_id;
  {
    std::lock_guard<std::mutex> lock(agent_map_mutex_);
    auto it = agent_node_map_.find(agent_id);
    if (it == agent_node_map_.end()) {
      Logger::warn("SimulatedCluster: Agent '{}' not registered", agent_id);
      return;
    }
    node_id = it->second;
    agent_node_map_.erase(it);
  }
  nodes_[node_id]->unregisterAgent(agent_id);

  Logger::info("SimulatedCluster: Unregistered agent '{}' from node {}", agent_id, node_id);
}

std::optional<size_t> SimulatedCluster::getAgentNode(const std::string& agent_id) const {
  std::lock_guard<std::mutex> lock(agent_map_mutex_);
  auto it = agent_node_map_.find(agent_id);
  if (it == agent_node_map_.end()) {
    return std::nullopt;
  }
  return it->second;
}

bool SimulatedCluster::stealRemoteWork(size_t from_node, size_t to_node) {
  if (from_node >= nodes_.size() || to_node >= nodes_.size()) {
    throw std::out_of_range("SimulatedCluster: Invalid node ID");
  }

  if (from_node == to_node) {
    Logger::warn("SimulatedCluster: Cannot steal from same node ({})", from_node);
    return false;
  }

  // Attempt to steal work from source node
  auto work = nodes_[from_node]->stealWork();
  if (!work.has_value()) {
    Logger::trace("SimulatedCluster: Remote steal failed ({}→{}): no work available",
                  from_node,
                  to_node);
    return false;
  }

  // Send work over network with latency
  network_->send(from_node, to_node, std::move(*work));

  Logger::debug("SimulatedCluster: Remote steal initiated ({}→{})", from_node, to_node);
  return true;
}

void SimulatedCluster::processNetworkMessages() {
  // Receive messages for all nodes
  for (size_t node_id = 0; node_id < nodes_.size(); ++node_id) {
    while (auto work = network_->receive(node_id)) {
      // Submit received work to destination node
      nodes_[node_id]->submit(std::move(*work));
      Logger::trace("SimulatedCluster: Delivered network message to node {}", node_id);
    }
  }
}

SimulatedCluster::Stats SimulatedCluster::getStats() const {
  Stats stats{};

  // Aggregate local and remote steals
  for (const auto& node : nodes_) {
    stats.total_local_steals += node->getLocalSteals();
    stats.total_remote_steals += node->getRemoteSteals();
  }

  // Network statistics
  stats.total_network_messages = network_->getTotalMessages();
  stats.avg_network_latency_us = network_->getAverageLatencyUs();

  // Queue depths per node
  stats.queue_depths_per_node.reserve(nodes_.size());
  for (const auto& node : nodes_) {
    stats.queue_depths_per_node.push_back(node->getQueueDepth());
  }

  // Calculate load imbalance (stddev of queue depths)
  stats.load_imbalance = calculateStdDev(stats.queue_depths_per_node);

  // Total tasks
  stats.total_tasks_submitted = total_tasks_submitted_.load();

  return stats;
}

void SimulatedCluster::resetStats() {
  for (auto& node : nodes_) {
    node->resetStats();
  }

  network_->resetStats();
  total_tasks_submitted_.store(0);

  Logger::debug("SimulatedCluster: Stats reset");
}

SimulatedNUMANode* SimulatedCluster::getNode(size_t node_id) {
  if (node_id >= nodes_.size()) {
    return nullptr;
  }
  return nodes_[node_id].get();
}

double SimulatedCluster::calculateStdDev(const std::vector<size_t>& queue_depths) const {
  if (queue_depths.empty()) {
    return 0.0;
  }

  // Calculate mean
  double sum = std::accumulate(queue_depths.begin(), queue_depths.end(), 0.0);
  double mean = sum / queue_depths.size();

  // Calculate variance
  double variance_sum = 0.0;
  for (size_t depth : queue_depths) {
    double diff = static_cast<double>(depth) - mean;
    variance_sum += diff * diff;
  }
  double variance = variance_sum / queue_depths.size();

  // Return standard deviation
  return std::sqrt(variance);
}

}  // namespace simulation
}  // namespace keystone
