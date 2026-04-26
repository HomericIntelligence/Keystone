#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace keystone {
namespace monitoring {

/**
 * @brief Prometheus metrics exporter
 *
 * Exposes HMAS metrics in Prometheus text format via HTTP endpoint.
 * Runs on configurable port (default: 9090) and serves /metrics endpoint.
 *
 * Exported metrics:
 * - hmas_messages_total{priority} - Counter of messages by priority
 * - hmas_messages_processed_total - Counter of processed messages
 * - hmas_message_latency_microseconds - Gauge of average latency
 * - hmas_queue_depth{agent_id} - Gauge of queue depth per agent
 * - hmas_queue_depth_max - Gauge of maximum queue depth
 * - hmas_worker_utilization_percent - Gauge of worker utilization
 * - hmas_deadline_misses_total - Counter of deadline misses
 * - hmas_deadline_miss_milliseconds - Gauge of average miss time
 * - keystone_task_claimer_in_flight_count - Gauge of active advance_dag_tracked tasks
 */
class PrometheusExporter {
 public:
  /**
   * @brief Construct exporter with configuration
   * @param port HTTP server port (default: 9090)
   */
  explicit PrometheusExporter(uint16_t port = 9090);

  /**
   * @brief Destructor - stops server if running
   */
  ~PrometheusExporter();

  // Prevent copying
  PrometheusExporter(const PrometheusExporter&) = delete;
  PrometheusExporter& operator=(const PrometheusExporter&) = delete;

  /**
   * @brief Start HTTP server in background thread
   * @return true if started successfully, false if already running or error
   */
  bool start();

  /**
   * @brief Stop HTTP server
   */
  void stop();

  /**
   * @brief Check if server is running
   */
  bool isRunning() const;

  /**
   * @brief Get current port
   */
  uint16_t getPort() const;

  /**
   * @brief Generate Prometheus metrics in text format
   *
   * Queries Metrics singleton and formats as Prometheus exposition format.
   * Called by HTTP server to serve /metrics endpoint.
   *
   * @return Metrics in Prometheus text format
   */
  static std::string generateMetrics();

 private:
  /**
   * @brief HTTP server loop (runs in background thread)
   */
  void serverLoop();

  /**
   * @brief Handle HTTP request and send response
   * @param client_fd Client socket file descriptor
   */
  void handleRequest(int client_fd);

  uint16_t port_;
  std::atomic<bool> running_{false};
  std::unique_ptr<std::thread> server_thread_;
  int server_fd_{-1};
};

}  // namespace monitoring
}  // namespace keystone
