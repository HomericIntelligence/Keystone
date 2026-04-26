#include "monitoring/health_check_server.hpp"

#include "concurrency/logger.hpp"
#include "core/config.hpp"

#include <array>
#include <cstring>
#include <sstream>
#include <utility>  // For std::exchange

#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace keystone {
namespace monitoring {

/**
 * @brief RAII wrapper for socket file descriptors
 *
 * Ensures sockets are always closed, even on error paths.
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

HealthCheckServer::HealthCheckServer(uint16_t port,
                                     ReadinessCheck readiness_check,
                                     NatsStatusTracker* nats_status,
                                     NatsConnectionCheck nats_connection_check)
    : port_(port),
      server_fd_(-1),
      readiness_check_(std::move(readiness_check)),
      nats_connection_check_(std::move(nats_connection_check)),
      nats_status_(nats_status) {}

HealthCheckServer::~HealthCheckServer() {
  stop();
}

bool HealthCheckServer::start() {
  if (running_.load()) {
    return false;  // Already running
  }

  // Create socket
  server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd_.load() < 0) {
    concurrency::Logger::error("HealthCheckServer: Failed to create socket");
    return false;
  }

  // Set socket options (reuse address)
  int opt = 1;
  if (setsockopt(server_fd_.load(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    concurrency::Logger::error("HealthCheckServer: Failed to set socket options");
    close(server_fd_.load());
    server_fd_ = -1;
    return false;
  }

  // Bind to port
  struct sockaddr_in address;
  std::memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port_.load());

  if (::bind(server_fd_.load(), (struct sockaddr*)&address, sizeof(address)) < 0) {
    concurrency::Logger::error("HealthCheckServer: Failed to bind to port {}", port_.load());
    close(server_fd_.load());
    server_fd_ = -1;
    return false;
  }

  // FIX: If port 0 was requested, query the actual assigned port
  if (port_ == 0) {
    struct sockaddr_in actual_address;
    socklen_t len = sizeof(actual_address);
    if (getsockname(server_fd_.load(), (struct sockaddr*)&actual_address, &len) == 0) {
      port_ = ntohs(actual_address.sin_port);
    } else {
      concurrency::Logger::error("HealthCheckServer: Failed to get assigned port");
      close(server_fd_.load());
      server_fd_ = -1;
      return false;
    }
  }

  // Listen for connections
  if (listen(server_fd_.load(), core::Config::HTTP_MAX_PENDING_CONNECTIONS) < 0) {
    concurrency::Logger::error("HealthCheckServer: Failed to listen on port {}", port_.load());
    close(server_fd_.load());
    server_fd_ = -1;
    return false;
  }

  // Start server thread
  running_.store(true);
  server_thread_ = std::make_unique<std::thread>(&HealthCheckServer::serverLoop, this);

  concurrency::Logger::info("Health check server started on port {}", port_.load());
  return true;
}

void HealthCheckServer::stop() {
  if (!running_.load()) {
    return;  // Not running
  }

  running_.store(false);

  // Close server socket to unblock accept()
  int fd = server_fd_.exchange(-1);
  if (fd >= 0) {
    close(fd);
  }

  // Wait for server thread to finish
  if (server_thread_ && server_thread_->joinable()) {
    server_thread_->join();
  }

  concurrency::Logger::info("Health check server stopped");
}

bool HealthCheckServer::isRunning() const {
  return running_.load();
}

uint16_t HealthCheckServer::getPort() const {
  return port_;
}

void HealthCheckServer::setReadinessCheck(ReadinessCheck check) {
  std::lock_guard<std::mutex> lock(readiness_mutex_);
  readiness_check_ = std::move(check);
}

void HealthCheckServer::setNatsConnectionCheck(NatsConnectionCheck check) {
  std::lock_guard<std::mutex> lock(readiness_mutex_);
  nats_connection_check_ = std::move(check);
}

void HealthCheckServer::serverLoop() {
  while (running_.load()) {
    // Use poll() with timeout to check for incoming connections
    // This allows periodic checking of running_ flag without blocking indefinitely
    struct pollfd pfd;
    pfd.fd = server_fd_.load();
    pfd.events = POLLIN;
    pfd.revents = 0;

    // Poll with 100ms timeout - allows responsive shutdown
    int poll_result = poll(&pfd, 1, 100);

    if (poll_result < 0) {
      // Poll error
      if (running_.load()) {
        concurrency::Logger::error("HealthCheckServer: Poll failed");
      }
      break;
    }

    if (poll_result == 0) {
      // Timeout - no incoming connections, loop again to check running_ flag
      continue;
    }

    // Data available - proceed with accept()
    struct sockaddr_in client_address;
    socklen_t client_len = sizeof(client_address);

    int client_fd = accept(server_fd_.load(), (struct sockaddr*)&client_address, &client_len);
    if (client_fd < 0) {
      if (running_.load()) {
        concurrency::Logger::error("HealthCheckServer: Accept failed");
      }
      continue;
    }

    // Use RAII wrapper - socket closed automatically on scope exit
    SocketHandle client_socket(client_fd);

    // Set socket read timeout to prevent slowloris attacks
    struct timeval timeout;
    timeout.tv_sec =
        std::chrono::duration_cast<std::chrono::seconds>(core::Config::HTTP_READ_TIMEOUT).count();
    timeout.tv_usec = 0;

    if (setsockopt(client_socket.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
      concurrency::Logger::error("HealthCheckServer: Failed to set socket read timeout");
      continue;  // Still try to handle request without timeout
    }

    // Handle request
    handleRequest(client_socket.get());

    // Socket automatically closed by SocketHandle destructor
  }
}

void HealthCheckServer::handleRequest(int client_fd) {
  // Use std::array for bounds safety
  std::array<char, core::Config::HTTP_REQUEST_BUFFER_SIZE> buffer;
  ssize_t bytes_read = read(client_fd, buffer.data(), buffer.size() - 1);

  // Validate read result and reject invalid requests
  if (bytes_read < 0) {
    // Read error (including timeout)
    return;
  }

  if (bytes_read == 0) {
    // Empty request - connection closed by client
    return;
  }

  // Validate minimum request size (at least "GET /")
  if (bytes_read < 5) {
    std::string bad_request = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
    [[maybe_unused]] auto result = write(client_fd, bad_request.c_str(), bad_request.size());
    return;
  }

  // Clamp to safe range to prevent buffer overflow
  if (bytes_read >= static_cast<ssize_t>(buffer.size())) {
    bytes_read = buffer.size() - 1;
  }

  buffer[bytes_read] = '\0';
  std::string request(buffer.data());

  // Validate HTTP method (only accept GET)
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

  // Check which endpoint is requested
  bool is_liveness = (request.find("GET /healthz") != std::string::npos);
  bool is_readiness = (request.find("GET /ready") != std::string::npos);
  bool is_v1_health = (request.find("GET /v1/health ") != std::string::npos);

  if (is_v1_health) {
    std::string body = generateV1HealthResponse(nats_status_);
    NatsConnectionState nats_state = nats_status_ ? nats_status_->state()
                                                  : NatsConnectionState::kDisconnected;
    bool healthy = (nats_status_ == nullptr) || (nats_state == NatsConnectionState::kConnected);
    std::string status_line = healthy ? "HTTP/1.1 200 OK\r\n"
                                      : "HTTP/1.1 503 Service Unavailable\r\n";

    std::ostringstream response;
    response << status_line;
    response << "Content-Type: application/json\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "\r\n";
    response << body;

    std::string response_str = response.str();
    [[maybe_unused]] auto result = write(client_fd, response_str.c_str(), response_str.size());

  } else if (is_liveness) {
    // Liveness probe - always return 200 OK if process is alive
    std::string body = generateLivenessResponse();

    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: application/json\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "\r\n";
    response << body;

    std::string response_str = response.str();
    [[maybe_unused]] auto result = write(client_fd, response_str.c_str(), response_str.size());

  } else if (is_readiness) {
    // Readiness probe - check if system is ready
    bool ready = true;
    {
      std::lock_guard<std::mutex> lock(readiness_mutex_);
      // issue #204: gate on NATS connection readiness first
      if (nats_connection_check_ && !nats_connection_check_()) {
        ready = false;
      }
      if (ready && readiness_check_) {
        ready = readiness_check_();
      }
    }

    std::string body = generateReadinessResponse(ready);
    std::string status_line = ready ? "HTTP/1.1 200 OK\r\n"
                                    : "HTTP/1.1 503 Service Unavailable\r\n";

    std::ostringstream response;
    response << status_line;
    response << "Content-Type: application/json\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "\r\n";
    response << body;

    std::string response_str = response.str();
    [[maybe_unused]] auto result = write(client_fd, response_str.c_str(), response_str.size());

  } else {
    // Send 404 for other paths
    std::string not_found =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 27\r\n"
        "\r\n"
        "{\"error\":\"endpoint not found\"}";
    [[maybe_unused]] auto result = write(client_fd, not_found.c_str(), not_found.size());
  }
}

std::string HealthCheckServer::generateLivenessResponse() {
  return "{\"status\":\"healthy\"}";
}

std::string HealthCheckServer::generateReadinessResponse(bool ready) {
  if (ready) {
    return "{\"status\":\"ready\"}";
  } else {
    return "{\"status\":\"not_ready\"}";
  }
}

std::string HealthCheckServer::generateV1HealthResponse(const NatsStatusTracker* nats_status) {
  std::ostringstream body;

  if (nats_status == nullptr) {
    body << "{\"status\":\"healthy\","
         << "\"nats\":{\"state\":\"unknown\",\"last_success_epoch_ms\":0}}";
    return body.str();
  }

  NatsConnectionState st = nats_status->state();
  int64_t last_ms = nats_status->lastSuccessEpochMs();

  const char* state_str = "disconnected";
  if (st == NatsConnectionState::kConnected) {
    state_str = "connected";
  } else if (st == NatsConnectionState::kReconnecting) {
    state_str = "reconnecting";
  }

  bool healthy = (st == NatsConnectionState::kConnected);
  const char* overall = healthy ? "healthy" : "degraded";

  body << "{\"status\":\"" << overall << "\",\"nats\":{\"state\":\"" << state_str
       << "\",\"last_success_epoch_ms\":" << last_ms << "}}";
  return body.str();
}

}  // namespace monitoring
}  // namespace keystone
