#include "monitoring/health_check_server.hpp"

#include <atomic>
#include <chrono>
#include <thread>

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace keystone::monitoring;

// FIXED: P1-001 - HealthCheckServer tests previously hung indefinitely
// Issue: Tests hung during execution due to blocking accept() not being
// properly interrupted by close() on this platform.
//
// Resolution: Implemented poll() with 100ms timeout in serverLoop() instead
// of blocking accept(). This allows periodic checking of running_ flag and
// enables clean shutdown without hanging. All 11 tests now enabled.
//
// Fix location: src/monitoring/health_check_server.cpp:146-206

// FIXED: Flaky test issue - port allocation conflicts
// Issue: Tests used rand() % 1000 which could assign same port to multiple tests running in
// parallel Resolution: Use port 0 (OS-assigned port) and query actual port via getPort() Each test
// gets a unique OS-assigned port, eliminating conflicts entirely
class HealthCheckServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Use port 0 = "any available port" - OS will assign a free port
    // After server->start(), call server->getPort() to get the actual port
    port_ = 0;
  }

  void TearDown() override {
    // Ensure server is stopped
    if (server_) {
      server_->stop();
    }
  }

  /**
   * @brief Send HTTP GET request to server
   * @param path Request path (e.g., "/healthz")
   * @return Response body (empty if error)
   */
  std::string sendRequest(const std::string& path) {
    // Create socket
    int32_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
      return "";
    }

    // Connect to server
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
      close(sock);
      return "";
    }

    // Send GET request
    std::string request = "GET " + path + " HTTP/1.1\r\nHost: localhost\r\n\r\n";
    if (write(sock, request.c_str(), request.size()) < 0) {
      close(sock);
      return "";
    }

    // Read response
    char buffer[4096];
    ssize_t bytes = read(sock, buffer, sizeof(buffer) - 1);
    close(sock);

    if (bytes <= 0) {
      return "";
    }

    buffer[bytes] = '\0';
    return std::string(buffer);
  }

  /**
   * @brief Extract HTTP status code from response
   */
  int32_t getStatusCode(const std::string& response) {
    if (response.empty())
      return 0;

    // Look for "HTTP/1.1 200 OK" pattern
    size_t start = response.find("HTTP/1.1 ");
    if (start == std::string::npos)
      return 0;

    start += 9;  // Skip "HTTP/1.1 "
    size_t end = response.find(" ", start);
    if (end == std::string::npos)
      return 0;

    try {
      return std::stoi(response.substr(start, end - start));
    } catch (...) {
      return 0;
    }
  }

  /**
   * @brief Extract response body (after headers)
   */
  std::string getBody(const std::string& response) {
    size_t body_start = response.find("\r\n\r\n");
    if (body_start == std::string::npos)
      return "";
    return response.substr(body_start + 4);
  }

  int32_t port_;
  std::unique_ptr<HealthCheckServer> server_;
};

/**
 * @brief Test server start and stop
 */
TEST_F(HealthCheckServerTest, StartStop) {
  server_ = std::make_unique<HealthCheckServer>(port_);

  EXPECT_FALSE(server_->isRunning());
  EXPECT_EQ(server_->getPort(), port_);

  // Start server
  EXPECT_TRUE(server_->start());
  EXPECT_TRUE(server_->isRunning());

  // Give server time to bind
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Try to start again (should fail - already running)
  EXPECT_FALSE(server_->start());

  // Stop server
  server_->stop();
  EXPECT_FALSE(server_->isRunning());

  // Stop again (should be safe)
  server_->stop();
  EXPECT_FALSE(server_->isRunning());
}

/**
 * @brief Test liveness endpoint (/healthz)
 */
TEST_F(HealthCheckServerTest, LivenessEndpoint) {
  server_ = std::make_unique<HealthCheckServer>(port_);
  ASSERT_TRUE(server_->start());
  port_ = server_->getPort();  // Get actual assigned port

  // Give server time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Request /healthz
  std::string response = sendRequest("/healthz");
  ASSERT_FALSE(response.empty());

  // Check status code
  EXPECT_EQ(getStatusCode(response), 200);

  // Check body
  std::string body = getBody(response);
  EXPECT_EQ(body, "{\"status\":\"healthy\"}");

  server_->stop();
}

/**
 * @brief Test readiness endpoint (/ready) - always ready (default)
 */
TEST_F(HealthCheckServerTest, ReadinessEndpointDefaultReady) {
  server_ = std::make_unique<HealthCheckServer>(port_);
  ASSERT_TRUE(server_->start());
  port_ = server_->getPort();  // Get actual assigned port

  // Give server time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Request /ready (should be ready by default)
  std::string response = sendRequest("/ready");
  ASSERT_FALSE(response.empty());

  // Check status code
  EXPECT_EQ(getStatusCode(response), 200);

  // Check body
  std::string body = getBody(response);
  EXPECT_EQ(body, "{\"status\":\"ready\"}");

  server_->stop();
}

/**
 * @brief Test readiness endpoint with custom check (ready)
 */
TEST_F(HealthCheckServerTest, ReadinessEndpointCustomReady) {
  bool is_ready = true;
  auto readiness_check = [&is_ready]() {
    return is_ready;
  };

  server_ = std::make_unique<HealthCheckServer>(port_, readiness_check);
  ASSERT_TRUE(server_->start());
  port_ = server_->getPort();  // Get actual assigned port

  // Give server time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Request /ready (should be ready)
  std::string response = sendRequest("/ready");
  ASSERT_FALSE(response.empty());

  EXPECT_EQ(getStatusCode(response), 200);
  EXPECT_EQ(getBody(response), "{\"status\":\"ready\"}");

  server_->stop();
}

/**
 * @brief Test readiness endpoint with custom check (not ready)
 */
TEST_F(HealthCheckServerTest, ReadinessEndpointCustomNotReady) {
  bool is_ready = false;
  auto readiness_check = [&is_ready]() {
    return is_ready;
  };

  server_ = std::make_unique<HealthCheckServer>(port_, readiness_check);
  ASSERT_TRUE(server_->start());
  port_ = server_->getPort();  // Get actual assigned port

  // Give server time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Request /ready (should NOT be ready)
  std::string response = sendRequest("/ready");
  ASSERT_FALSE(response.empty());

  EXPECT_EQ(getStatusCode(response), 503);  // Service Unavailable
  EXPECT_EQ(getBody(response), "{\"status\":\"not_ready\"}");

  server_->stop();
}

/**
 * @brief Test readiness state transition
 */
TEST_F(HealthCheckServerTest, ReadinessStateTransition) {
  bool is_ready = false;
  auto readiness_check = [&is_ready]() {
    return is_ready;
  };

  server_ = std::make_unique<HealthCheckServer>(port_, readiness_check);
  ASSERT_TRUE(server_->start());
  port_ = server_->getPort();  // Get actual assigned port

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Initially not ready
  std::string response1 = sendRequest("/ready");
  EXPECT_EQ(getStatusCode(response1), 503);

  // Transition to ready
  is_ready = true;

  std::string response2 = sendRequest("/ready");
  EXPECT_EQ(getStatusCode(response2), 200);

  // Transition back to not ready
  is_ready = false;

  std::string response3 = sendRequest("/ready");
  EXPECT_EQ(getStatusCode(response3), 503);

  server_->stop();
}

/**
 * @brief Test setReadinessCheck() method
 */
TEST_F(HealthCheckServerTest, SetReadinessCheck) {
  server_ = std::make_unique<HealthCheckServer>(port_);
  ASSERT_TRUE(server_->start());
  port_ = server_->getPort();  // Get actual assigned port

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Initially ready (default)
  std::string response1 = sendRequest("/ready");
  EXPECT_EQ(getStatusCode(response1), 200);

  // Set custom check (not ready)
  bool is_ready = false;
  server_->setReadinessCheck([&is_ready]() { return is_ready; });

  std::string response2 = sendRequest("/ready");
  EXPECT_EQ(getStatusCode(response2), 503);

  // Update state to ready
  is_ready = true;

  std::string response3 = sendRequest("/ready");
  EXPECT_EQ(getStatusCode(response3), 200);

  server_->stop();
}

/**
 * @brief Test invalid endpoint (404)
 */
TEST_F(HealthCheckServerTest, InvalidEndpoint) {
  server_ = std::make_unique<HealthCheckServer>(port_);
  ASSERT_TRUE(server_->start());
  port_ = server_->getPort();  // Get actual assigned port

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Request unknown endpoint
  std::string response = sendRequest("/unknown");
  ASSERT_FALSE(response.empty());

  EXPECT_EQ(getStatusCode(response), 404);
  EXPECT_EQ(getBody(response), "{\"error\":\"endpoint not found\"}");

  server_->stop();
}

/**
 * @brief Test POST method (405 Method Not Allowed)
 */
TEST_F(HealthCheckServerTest, InvalidMethod) {
  server_ = std::make_unique<HealthCheckServer>(port_);
  ASSERT_TRUE(server_->start());
  port_ = server_->getPort();  // Get actual assigned port

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Create socket
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(sock, 0);

  // Connect
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port_);
  server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  ASSERT_GE(connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)), 0);

  // Send POST request (not allowed)
  std::string request = "POST /healthz HTTP/1.1\r\nHost: localhost\r\n\r\n";
  ssize_t written = write(sock, request.c_str(), request.size());
  ASSERT_GT(written, 0);

  // Read response
  char buffer[4096];
  ssize_t bytes = read(sock, buffer, sizeof(buffer) - 1);
  close(sock);

  ASSERT_GT(bytes, 0);
  buffer[bytes] = '\0';
  std::string response(buffer);

  EXPECT_EQ(getStatusCode(response), 405);  // Method Not Allowed

  server_->stop();
}

/**
 * @brief Test concurrent requests
 */
TEST_F(HealthCheckServerTest, ConcurrentRequests) {
  server_ = std::make_unique<HealthCheckServer>(port_);
  ASSERT_TRUE(server_->start());
  port_ = server_->getPort();  // Get actual assigned port

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Send 10 concurrent requests
  std::vector<std::thread> threads;
  std::atomic<int32_t> success_count{0};

  for (int32_t i = 0; i < 10; ++i) {
    threads.emplace_back([this, &success_count]() {
      std::string response = sendRequest("/healthz");
      if (getStatusCode(response) == 200) {
        success_count++;
      }
    });
  }

  // Wait for all threads
  for (auto& t : threads) {
    t.join();
  }

  // All requests should succeed
  EXPECT_EQ(success_count.load(), 10);

  server_->stop();
}

/**
 * @brief Test server restart
 */
TEST_F(HealthCheckServerTest, ServerRestart) {
  server_ = std::make_unique<HealthCheckServer>(port_);

  // Start
  ASSERT_TRUE(server_->start());
  port_ = server_->getPort();  // Get actual assigned port
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Test endpoint works
  std::string response1 = sendRequest("/healthz");
  EXPECT_EQ(getStatusCode(response1), 200);

  // Stop
  server_->stop();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Start again
  port_ = 0;  // Reset to request new port
  server_ = std::make_unique<HealthCheckServer>(port_);
  ASSERT_TRUE(server_->start());
  port_ = server_->getPort();  // Get new assigned port
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Test endpoint works again
  std::string response2 = sendRequest("/healthz");
  EXPECT_EQ(getStatusCode(response2), 200);

  server_->stop();
}
