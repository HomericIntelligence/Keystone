#pragma once

#include "simulation/simulated_network.hpp"
#include "simulation/simulated_numa_node.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace keystone {
namespace simulation {

/**
 * @brief Simulated cluster for testing distributed work-stealing
 *
 * Phase D.3: Coordinates multiple SimulatedNUMANodes with a SimulatedNetwork
 * to test distributed work-stealing without requiring actual hardware.
 *
 * Example:
 * @code
 * SimulatedCluster::Config config{
 *     .num_nodes = 4,
 *     .workers_per_node = 4,
 *     .network_config = {.min_latency = 100us, .max_latency = 500us}
 * };
 * SimulatedCluster cluster(config);
 * cluster.start();
 * cluster.registerAgent("agent_A", 0);
 * cluster.submit("agent_A", []() { do_work(); });
 * @endcode
 */
class SimulatedCluster {
 public:
  /**
   * @brief Cluster configuration parameters
   */
  struct Config {
    size_t num_nodes{2};                      ///< Number of simulated nodes
    size_t workers_per_node{4};               ///< Workers per node
    SimulatedNetwork::Config network_config;  ///< Network configuration

    // Work stealing thresholds
    size_t local_queue_threshold{10};  ///< Steal remotely if local queue < this
    size_t remote_steal_batch{1};      ///< Items to steal per remote request
  };

  /**
   * @brief Aggregate statistics across all nodes
   */
  struct Stats {
    size_t total_local_steals;                  ///< Sum of local steals across all nodes
    size_t total_remote_steals;                 ///< Sum of remote steals across all nodes
    size_t total_network_messages;              ///< Total messages sent over network
    double avg_network_latency_us;              ///< Average network latency
    std::vector<size_t> queue_depths_per_node;  ///< Queue depth for each node
    double load_imbalance;                      ///< Standard deviation of queue depths
    size_t total_tasks_submitted;               ///< Total tasks submitted to cluster
  };

  /**
   * @brief Construct a simulated cluster
   * @param config Cluster configuration
   */
  explicit SimulatedCluster(Config config);

  /**
   * @brief Destructor ensures clean shutdown
   */
  ~SimulatedCluster();

  // Prevent copying
  SimulatedCluster(const SimulatedCluster&) = delete;
  SimulatedCluster& operator=(const SimulatedCluster&) = delete;

  // Prevent moving (contains atomic members which cannot be moved)
  SimulatedCluster(SimulatedCluster&&) noexcept = delete;
  SimulatedCluster& operator=(SimulatedCluster&&) noexcept = delete;

  /**
   * @brief Start all nodes in the cluster
   */
  void start();

  /**
   * @brief Shutdown all nodes in the cluster
   */
  void shutdown();

  /**
   * @brief Submit work to the cluster (routes to agent's home node)
   *
   * If agent is registered, work goes to its home node.
   * Otherwise, work is round-robin distributed.
   *
   * @param agent_id Agent identifier
   * @param work Work function to execute
   */
  void submit(const std::string& agent_id, std::function<void()> work);

  /**
   * @brief Submit work directly to a specific node
   * @param node_id Target node (must be < num_nodes)
   * @param work Work function to execute
   */
  void submitToNode(size_t node_id, std::function<void()> work);

  /**
   * @brief Register an agent on a specific node
   * @param agent_id Agent identifier
   * @param preferred_node Node to assign agent to
   */
  void registerAgent(const std::string& agent_id, size_t preferred_node);

  /**
   * @brief Unregister an agent from the cluster
   * @param agent_id Agent identifier
   */
  void unregisterAgent(const std::string& agent_id);

  /**
   * @brief Get the node an agent is registered on
   * @param agent_id Agent identifier
   * @return Node ID, or nullopt if not registered
   */
  std::optional<size_t> getAgentNode(const std::string& agent_id) const;

  /**
   * @brief Attempt to steal work from one node to another
   *
   * Simulates cross-node work stealing with network latency.
   *
   * @param from_node Source node to steal from
   * @param to_node Destination node receiving work
   * @return true if work was stolen successfully
   */
  bool stealRemoteWork(size_t from_node, size_t to_node);

  /**
   * @brief Process pending network messages for all nodes
   *
   * Called periodically to deliver messages whose latency has elapsed.
   */
  void processNetworkMessages();

  /**
   * @brief Get number of nodes in cluster
   * @return Node count
   */
  size_t getNumNodes() const { return nodes_.size(); }

  /**
   * @brief Get aggregate statistics across all nodes
   * @return Cluster statistics
   */
  Stats getStats() const;

  /**
   * @brief Reset all statistics counters
   */
  void resetStats();

  /**
   * @brief Get a specific node (for advanced access)
   * @param node_id Node index
   * @return Pointer to node, or nullptr if invalid
   */
  SimulatedNUMANode* getNode(size_t node_id);

  /**
   * @brief Get the network (for advanced access)
   * @return Pointer to network
   */
  SimulatedNetwork* getNetwork() { return network_.get(); }

 private:
  Config config_;  ///< Cluster configuration

  std::vector<std::unique_ptr<SimulatedNUMANode>> nodes_;  ///< All NUMA nodes
  std::unique_ptr<SimulatedNetwork> network_;              ///< Network layer

  // Agent -> Node mapping
  mutable std::mutex agent_map_mutex_;
  std::unordered_map<std::string, size_t> agent_node_map_;

  // Statistics
  std::atomic<size_t> total_tasks_submitted_{0};
  std::atomic<size_t> round_robin_counter_{0};  ///< For load balancing unregistered agents

  bool started_{false};

  /**
   * @brief Calculate standard deviation of queue depths
   * @param queue_depths Vector of queue depths per node
   * @return Standard deviation
   */
  double calculateStdDev(const std::vector<size_t>& queue_depths) const;
};

}  // namespace simulation
}  // namespace keystone
