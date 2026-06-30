#include "monitoring/prometheus_exporter.hpp"

#include "concurrency/logger.hpp"
#include "core/config.hpp"  // FIX m3: Centralized configuration
#include "core/metrics.hpp"

#include <array>
#include <cstring>
#include <sstream>
#include <utility>  // FIX: For std::exchange

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace keystone {
namespace monitoring {

/**
 * @brief RAII wrapper for socket file descriptors
 *
 * FIX C3: Ensures sockets are always closed, even on error paths.
 */
class SocketHandle {
 public:
  explicit SocketHandle(int fd) : fd_(fd) {}

  ~SocketHandle() {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

  // Non-copyable, movable
  SocketHandle(const SocketHandle&) = delete;
  SocketHandle& operator=(const SocketHandle&) = delete;

  SocketHandle(SocketHandle&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

  SocketHandle& operator=(SocketHandle&& other) noexcept {
    if (this != &other) {
      if (fd_ >= 0) {
        close(fd_);
      }
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }

  int get() const { return fd_; }
  bool valid() const { return fd_ >= 0; }

 private:
  int fd_;
};

PrometheusExporter::PrometheusExporter(uint16_t port) : port_(port) {}

PrometheusExporter::~PrometheusExporter() {
  stop();
}

bool PrometheusExporter::start() {
  if (running_.load()) {
    return false;  // Already running
  }

  // Initialize logger (ensures spdlog is ready before first log call)
  concurrency::Logger::init();

  // Create socket
  server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd_ < 0) {
    concurrency::Logger::error("PrometheusExporter: Failed to create socket");
    return false;
  }

  // Set socket options (reuse address)
  int opt = 1;
  if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    concurrency::Logger::error("PrometheusExporter: Failed to set socket options");
    close(server_fd_);
    server_fd_ = -1;
    return false;
  }

  // Bind to port
  struct sockaddr_in address;
  std::memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port_);

  if (bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) {
    concurrency::Logger::error("PrometheusExporter: Failed to bind to port {}", port_);
    close(server_fd_);
    server_fd_ = -1;
    return false;
  }

  // Listen for connections
  if (listen(server_fd_, core::Config::HTTP_MAX_PENDING_CONNECTIONS) < 0) {
    concurrency::Logger::error("PrometheusExporter: Failed to listen on port {}", port_);
    close(server_fd_);
    server_fd_ = -1;
    return false;
  }

  // Start server thread
  running_.store(true);
  server_thread_ = std::make_unique<std::thread>(&PrometheusExporter::serverLoop, this);

  concurrency::Logger::info("Prometheus exporter started on port {}", port_);
  return true;
}

void PrometheusExporter::stop() {
  if (!running_.load()) {
    return;  // Not running
  }

  running_.store(false);

  // Close server socket to unblock accept()
  if (server_fd_ >= 0) {
    close(server_fd_);
    server_fd_ = -1;
  }

  // Wait for server thread to finish
  if (server_thread_ && server_thread_->joinable()) {
    server_thread_->join();
  }

  concurrency::Logger::info("Prometheus exporter stopped");
}

bool PrometheusExporter::isRunning() const {
  return running_.load();
}

uint16_t PrometheusExporter::getPort() const {
  return port_;
}

void PrometheusExporter::serverLoop() {
  while (running_.load()) {
    struct sockaddr_in client_address;
    socklen_t client_len = sizeof(client_address);

    // Accept connection
    int client_fd = accept(server_fd_, (struct sockaddr*)&client_address, &client_len);
    if (client_fd < 0) {
      if (running_.load()) {
        concurrency::Logger::error("PrometheusExporter: Accept failed");
      }
      continue;
    }

    // FIX C3: Use RAII wrapper - socket closed automatically on scope exit
    SocketHandle client_socket(client_fd);

    // FIX m4: Set socket read timeout to prevent slowloris attacks
    struct timeval timeout;
    timeout.tv_sec =
        std::chrono::duration_cast<std::chrono::seconds>(core::Config::HTTP_READ_TIMEOUT).count();
    timeout.tv_usec = 0;

    if (setsockopt(client_socket.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
      concurrency::Logger::error("PrometheusExporter: Failed to set socket read timeout");
      continue;  // Still try to handle request without timeout
    }

    // Handle request
    handleRequest(client_socket.get());

    // Socket automatically closed by SocketHandle destructor
  }
}

void PrometheusExporter::handleRequest(int client_fd) {
  // FIX C5: Use std::array for bounds safety and proper buffer handling
  std::array<char, core::Config::HTTP_REQUEST_BUFFER_SIZE> buffer;
  ssize_t bytes_read = read(client_fd, buffer.data(), buffer.size() - 1);

  // FIX m4: Validate read result and reject invalid requests
  if (bytes_read < 0) {
    // Read error (including timeout)
    return;
  }

  if (bytes_read == 0) {
    // Empty request - connection closed by client
    return;
  }

  // FIX m4: Validate minimum request size (at least "GET /")
  if (bytes_read < 5) {
    std::string bad_request = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
    [[maybe_unused]] auto result = write(client_fd, bad_request.c_str(), bad_request.size());
    return;
  }

  // FIX C5: Clamp to safe range to prevent buffer overflow
  if (bytes_read >= static_cast<ssize_t>(buffer.size())) {
    bytes_read = buffer.size() - 1;
  }

  // Check if this is a GET request to /metrics
  buffer[bytes_read] = '\0';  // ✅ Safe: bytes_read is guaranteed < buffer.size()
  std::string request(buffer.data());

  // FIX m4: Validate HTTP method (only accept GET)
  if (request.substr(0, 3) != "GET") {
    std::string method_not_allowed =
        "HTTP/1.1 405 Method Not Allowed\r\n"
        "Allow: GET\r\n"
        "Content-Length: 0\r\n\r\n";
    [[maybe_unused]] auto result = write(client_fd,
                                         method_not_allowed.c_str(),
                                         method_not_allowed.size());
    return;
  }

  bool is_metrics_request = (request.find("GET /metrics") != std::string::npos);

  if (is_metrics_request) {
    // Generate metrics
    std::string metrics = generateMetrics();

    // Send HTTP response
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: text/plain; version=0.0.4\r\n";
    response << "Content-Length: " << metrics.size() << "\r\n";
    response << "\r\n";
    response << metrics;

    std::string response_str = response.str();
    [[maybe_unused]] auto result = write(client_fd, response_str.c_str(), response_str.size());
  } else {
    // Send 404 for other paths
    std::string not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    [[maybe_unused]] auto result = write(client_fd, not_found.c_str(), not_found.size());
  }
}

std::string PrometheusExporter::generateMetrics() {
  std::ostringstream ss;

  // Get metrics from singleton
  auto& metrics = core::Metrics::getInstance();

  // Messages sent (counters by priority)
  auto priority_stats = metrics.getPriorityStats();
  ss << "# HELP hmas_messages_total Total number of messages sent by "
        "priority\n";
  ss << "# TYPE hmas_messages_total counter\n";
  ss << "hmas_messages_total{priority=\"high\"} " << priority_stats.high_count << "\n";
  ss << "hmas_messages_total{priority=\"normal\"} " << priority_stats.normal_count << "\n";
  ss << "hmas_messages_total{priority=\"low\"} " << priority_stats.low_count << "\n";

  // Messages processed (counter)
  ss << "# HELP hmas_messages_processed_total Total number of messages "
        "processed\n";
  ss << "# TYPE hmas_messages_processed_total counter\n";
  ss << "hmas_messages_processed_total " << metrics.getTotalMessagesProcessed() << "\n";

  // Message latency (gauge - average)
  auto avg_latency = metrics.getAverageLatencyUs();
  if (avg_latency.has_value()) {
    ss << "# HELP hmas_message_latency_microseconds Average message processing "
          "latency\n";
    ss << "# TYPE hmas_message_latency_microseconds gauge\n";
    ss << "hmas_message_latency_microseconds " << avg_latency.value() << "\n";
  }

  // Queue depth (gauge - maximum across all agents)
  ss << "# HELP hmas_queue_depth_max Maximum queue depth across all agents\n";
  ss << "# TYPE hmas_queue_depth_max gauge\n";
  ss << "hmas_queue_depth_max " << metrics.getMaxQueueDepth() << "\n";

  // Worker utilization (gauge - percentage)
  ss << "# HELP hmas_worker_utilization_percent Worker utilization "
        "percentage\n";
  ss << "# TYPE hmas_worker_utilization_percent gauge\n";
  ss << "hmas_worker_utilization_percent " << metrics.getWorkerUtilization() << "\n";

  // Messages per second (gauge)
  ss << "# HELP hmas_messages_per_second Message throughput\n";
  ss << "# TYPE hmas_messages_per_second gauge\n";
  ss << "hmas_messages_per_second " << metrics.getMessagesPerSecond() << "\n";

  // Deadline misses (counter)
  ss << "# HELP hmas_deadline_misses_total Total number of deadline misses\n";
  ss << "# TYPE hmas_deadline_misses_total counter\n";
  ss << "hmas_deadline_misses_total " << metrics.getTotalDeadlineMisses() << "\n";

  // Deadline miss time (gauge - average)
  auto avg_miss = metrics.getAverageDeadlineMissMs();
  if (avg_miss.has_value()) {
    ss << "# HELP hmas_deadline_miss_milliseconds Average deadline miss time\n";
    ss << "# TYPE hmas_deadline_miss_milliseconds gauge\n";
    ss << "hmas_deadline_miss_milliseconds " << avg_miss.value() << "\n";
  }

  // Uptime (gauge - seconds since start)
  static auto start_time = std::chrono::steady_clock::now();
  auto now = std::chrono::steady_clock::now();
  auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
  ss << "# HELP hmas_uptime_seconds HMAS uptime in seconds\n";
  ss << "# TYPE hmas_uptime_seconds gauge\n";
  ss << "hmas_uptime_seconds " << uptime_seconds << "\n";

  // In-flight task claim count (gauge - TaskClaimer advance_dag_tracked tasks)
  ss << "# HELP keystone_task_claimer_in_flight_count Number of "
        "advance_dag_tracked "
        "tasks currently executing in the TaskClaimer\n";
  ss << "# TYPE keystone_task_claimer_in_flight_count gauge\n";
  ss << "keystone_task_claimer_in_flight_count " << metrics.getInFlightCount() << "\n";

  // Health status (gauge - always 1 if responding)
  ss << "# HELP hmas_up HMAS health status (1=up, 0=down)\n";
  ss << "# TYPE hmas_up gauge\n";
  ss << "hmas_up 1\n";

  return ss.str();
}

}  // namespace monitoring
}  // namespace keystone
