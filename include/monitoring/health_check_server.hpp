#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "monitoring/nats_status.hpp"

namespace keystone {
namespace monitoring {

/**
 * @brief Health check server for Kubernetes liveness and readiness probes
 *
 * Exposes health check endpoints via HTTP on port 8080:
 * - GET /healthz - Liveness probe (is process alive?)
 * - GET /ready - Readiness probe (is process ready for traffic?)
 *
 * Kubernetes deployment expectations:
 * - Liveness: Returns 200 OK with {"status":"healthy"} if process is alive
 * - Readiness: Returns 200 OK with {"status":"ready"} if ready for traffic
 *
 * Implementation follows PrometheusExporter pattern for consistency.
 */
class HealthCheckServer {
 public:
  /**
   * @brief Readiness check function type
   *
   * User-supplied function that determines if the system is ready.
   * - Return true if ready for traffic
   * - Return false if not ready (will return 503 Service Unavailable)
   */
  using ReadinessCheck = std::function<bool()>;

  /**
   * @brief NatsConnection connectivity check function type (issue #204)
   *
   * Lambda/callback that returns true when the NATS connection is established.
   * Supplied by the caller to decouple HealthCheckServer from NatsConnection.
   * When set, the readiness probe returns 503 until this callback returns true.
   */
  using NatsConnectionCheck = std::function<bool()>;

  /**
   * @brief Construct health check server with configuration
   * @param port HTTP server port (default: 8080 for Kubernetes)
   * @param readiness_check Optional custom readiness check function
   * @param nats_status Optional NATS status tracker (non-owning); used by
   * /v1/health
   * @param nats_connection_check Optional NATS connection check (issue #204);
   *        when supplied the readiness probe is not ready until this returns
   * true
   */
  explicit HealthCheckServer(
      uint16_t port = 8080, ReadinessCheck readiness_check = nullptr,
      NatsStatusTracker* nats_status = nullptr,
      NatsConnectionCheck nats_connection_check = nullptr);

  /**
   * @brief Destructor - stops server if running
   */
  ~HealthCheckServer();

  // Prevent copying
  HealthCheckServer(const HealthCheckServer&) = delete;
  HealthCheckServer& operator=(const HealthCheckServer&) = delete;

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
   * @brief Set readiness check function
   *
   * Allows dynamic configuration of readiness logic.
   * If not set, readiness will always return true.
   */
  void setReadinessCheck(ReadinessCheck check);

  /**
   * @brief Set NATS connection check function (issue #204)
   *
   * Wires a NatsConnection::isConnected()-style lambda into the readiness
   * probe.  When set, /ready returns 503 until the callback returns true.
   * Pass nullptr to clear (readiness probe ignores NATS connectivity again).
   */
  void setNatsConnectionCheck(NatsConnectionCheck check);

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

  /**
   * @brief Generate liveness response
   * @return JSON response: {"status":"healthy"}
   */
  static std::string generateLivenessResponse();

  /**
   * @brief Generate readiness response
   * @param ready True if system is ready
   * @return JSON response: {"status":"ready"} or {"status":"not_ready"}
   */
  static std::string generateReadinessResponse(bool ready);

  /**
   * @brief Generate /v1/health response JSON body
   * @param nats_status Optional NATS tracker (may be nullptr)
   * @return JSON body string
   */
  static std::string generateV1HealthResponse(
      const NatsStatusTracker* nats_status);

  std::atomic<uint16_t> port_;
  std::atomic<bool> running_{false};
  std::unique_ptr<std::thread> server_thread_;
  std::atomic<int> server_fd_{-1};
  mutable std::mutex readiness_mutex_;
  ReadinessCheck readiness_check_;
  NatsConnectionCheck
      nats_connection_check_;  // issue #204: gates /ready on NATS connectivity
  NatsStatusTracker* nats_status_{nullptr};  // non-owning
};

}  // namespace monitoring
}  // namespace keystone
