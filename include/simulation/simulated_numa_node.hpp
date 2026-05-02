#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>

#include "concurrency/work_stealing_scheduler.hpp"

namespace keystone {
namespace simulation {

/**
 * @brief Simulated NUMA node for testing distributed work-stealing
 *
 * Phase D.3: Represents a single NUMA node with its own WorkStealingScheduler
 * and affinity for specific agents. Tracks local vs remote work stealing
 * statistics for performance analysis.
 *
 * Example:
 * @code
 * SimulatedNUMANode node0(0, 4);  // Node 0 with 4 workers
 * node0.start();
 * node0.registerAgent("agent_A");
 * node0.submit([]() { std::cout << "Work on node 0\n"; });
 * @endcode
 */
class SimulatedNUMANode {
 public:
  /**
   * @brief Construct a simulated NUMA node
   * @param node_id Unique identifier for this node (0-N)
   * @param num_workers Number of worker threads in this node's scheduler
   */
  SimulatedNUMANode(size_t node_id, size_t num_workers);

  /**
   * @brief Destructor ensures clean shutdown
   */
  ~SimulatedNUMANode();

  // Prevent copying (unique ownership of scheduler)
  SimulatedNUMANode(const SimulatedNUMANode&) = delete;
  SimulatedNUMANode& operator=(const SimulatedNUMANode&) = delete;

  // Prevent moving (contains atomic members which cannot be moved)
  SimulatedNUMANode(SimulatedNUMANode&&) noexcept = delete;
  SimulatedNUMANode& operator=(SimulatedNUMANode&&) noexcept = delete;

  /**
   * @brief Start the scheduler for this node
   */
  void start();

  /**
   * @brief Shutdown the scheduler for this node
   */
  void shutdown();

  /**
   * @brief Submit work to this node's scheduler
   * @param work Function to execute
   */
  void submit(std::function<void()> work);

  /**
   * @brief Submit work to a specific worker on this node
   * @param worker_index Target worker (must be < num_workers)
   * @param work Function to execute
   */
  void submitToWorker(size_t worker_index, std::function<void()> work);

  /**
   * @brief Register an agent as having affinity to this node
   * @param agent_id Agent identifier
   */
  void registerAgent(const std::string& agent_id);

  /**
   * @brief Unregister an agent from this node
   * @param agent_id Agent identifier
   */
  void unregisterAgent(const std::string& agent_id);

  /**
   * @brief Check if an agent has affinity to this node
   * @param agent_id Agent identifier
   * @return true if agent is registered on this node
   */
  bool hasAgent(const std::string& agent_id) const;

  /**
   * @brief Attempt to steal work from this node
   *
   * Called by a remote node attempting cross-node work stealing.
   * Increments remote steal counter if successful.
   *
   * @return WorkItem if steal succeeded, nullopt otherwise
   */
  std::optional<std::function<void()>> stealWork();

  /**
   * @brief Record a local steal (within same node)
   */
  void recordLocalSteal();

  /**
   * @brief Get node identifier
   * @return Node ID
   */
  size_t getNodeId() const { return node_id_; }

  /**
   * @brief Get number of workers in this node
   * @return Worker count
   */
  size_t getNumWorkers() const;

  /**
   * @brief Get approximate queue depth across all workers
   * @return Total pending work items
   */
  size_t getQueueDepth() const;

  /**
   * @brief Get count of local steals (within node)
   * @return Number of local steals
   */
  size_t getLocalSteals() const { return local_steals_.load(); }

  /**
   * @brief Get count of remote steals (from other nodes)
   * @return Number of remote steals
   */
  size_t getRemoteSteals() const { return remote_steals_.load(); }

  /**
   * @brief Get list of agents registered on this node
   * @return Set of agent IDs
   */
  std::unordered_set<std::string> getLocalAgents() const;

  /**
   * @brief Reset statistics counters
   */
  void resetStats();

 private:
  size_t node_id_;  ///< Unique node identifier
  std::unique_ptr<concurrency::WorkStealingScheduler>
      scheduler_;                    ///< Thread pool for this node
  mutable std::mutex agents_mutex_;  ///< Guards local_agents_
  std::unordered_set<std::string>
      local_agents_;  ///< Agents with affinity to this node

  std::atomic<size_t> local_steals_{0};   ///< Count of intra-node steals
  std::atomic<size_t> remote_steals_{0};  ///< Count of cross-node steals
};

}  // namespace simulation
}  // namespace keystone
