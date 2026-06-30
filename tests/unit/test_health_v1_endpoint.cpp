#include "monitoring/health_check_server.hpp"
#include "monitoring/nats_status.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace keystone::monitoring;

class HealthV1EndpointTest : public ::testing::Test {
 protected:
  void SetUp() override { port_ = 0; }

  void TearDown() override {
    if (server_) {
      server_->stop();
    }
  }

  std::string sendRequest(const std::string& path) {
    int32_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
      return "";
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      close(sock);
      return "";
    }

    std::string request = "GET " + path + " HTTP/1.1\r\nHost: localhost\r\n\r\n";
    if (write(sock, request.c_str(), request.size()) < 0) {
      close(sock);
      return "";
    }

    char buffer[4096];
    ssize_t bytes = read(sock, buffer, sizeof(buffer) - 1);
    close(sock);
    if (bytes <= 0) {
      return "";
    }
    buffer[bytes] = '\0';
    return std::string(buffer);
  }

  int getStatusCode(const std::string& response) {
    size_t start = response.find("HTTP/1.1 ");
    if (start == std::string::npos)
      return 0;
    start += 9;
    size_t end = response.find(' ', start);
    if (end == std::string::npos)
      return 0;
    try {
      return std::stoi(response.substr(start, end - start));
    } catch (...) {
      return 0;
    }
  }

  std::string getBody(const std::string& response) {
    size_t pos = response.find("\r\n\r\n");
    if (pos == std::string::npos)
      return "";
    return response.substr(pos + 4);
  }

  int port_;
  std::unique_ptr<HealthCheckServer> server_;
};

TEST_F(HealthV1EndpointTest, NullTrackerReturns200Healthy) {
  server_ = std::make_unique<HealthCheckServer>(port_, nullptr, nullptr);
  ASSERT_TRUE(server_->start());
  port_ = server_->getPort();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::string response = sendRequest("/v1/health");
  ASSERT_FALSE(response.empty());
  EXPECT_EQ(getStatusCode(response), 200);

  std::string body = getBody(response);
  EXPECT_NE(body.find("\"status\":\"healthy\""), std::string::npos);
  EXPECT_NE(body.find("\"state\":\"unknown\""), std::string::npos);
}

TEST_F(HealthV1EndpointTest, ConnectedReturns200) {
  NatsStatusTracker tracker;
  tracker.setConnected();

  server_ = std::make_unique<HealthCheckServer>(port_, nullptr, &tracker);
  ASSERT_TRUE(server_->start());
  port_ = server_->getPort();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::string response = sendRequest("/v1/health");
  ASSERT_FALSE(response.empty());
  EXPECT_EQ(getStatusCode(response), 200);

  std::string body = getBody(response);
  EXPECT_NE(body.find("\"status\":\"healthy\""), std::string::npos);
  EXPECT_NE(body.find("\"state\":\"connected\""), std::string::npos);
  // last_success_epoch_ms must be non-zero
  EXPECT_EQ(body.find("\"last_success_epoch_ms\":0"), std::string::npos);
}

TEST_F(HealthV1EndpointTest, DisconnectedReturns503) {
  NatsStatusTracker tracker;
  // tracker starts disconnected — no setDisconnected() needed

  server_ = std::make_unique<HealthCheckServer>(port_, nullptr, &tracker);
  ASSERT_TRUE(server_->start());
  port_ = server_->getPort();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::string response = sendRequest("/v1/health");
  ASSERT_FALSE(response.empty());
  EXPECT_EQ(getStatusCode(response), 503);

  std::string body = getBody(response);
  EXPECT_NE(body.find("\"status\":\"degraded\""), std::string::npos);
  EXPECT_NE(body.find("\"state\":\"disconnected\""), std::string::npos);
}

TEST_F(HealthV1EndpointTest, ReconnectingReturns503) {
  NatsStatusTracker tracker;
  tracker.setConnected();
  tracker.setReconnecting();

  server_ = std::make_unique<HealthCheckServer>(port_, nullptr, &tracker);
  ASSERT_TRUE(server_->start());
  port_ = server_->getPort();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::string response = sendRequest("/v1/health");
  ASSERT_FALSE(response.empty());
  EXPECT_EQ(getStatusCode(response), 503);

  std::string body = getBody(response);
  EXPECT_NE(body.find("\"status\":\"degraded\""), std::string::npos);
  EXPECT_NE(body.find("\"state\":\"reconnecting\""), std::string::npos);
}

TEST_F(HealthV1EndpointTest, StateTransitionConnectedToDisconnected) {
  NatsStatusTracker tracker;
  tracker.setConnected();

  server_ = std::make_unique<HealthCheckServer>(port_, nullptr, &tracker);
  ASSERT_TRUE(server_->start());
  port_ = server_->getPort();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Initially connected → 200
  EXPECT_EQ(getStatusCode(sendRequest("/v1/health")), 200);

  // Transition to disconnected → 503
  tracker.setDisconnected();
  EXPECT_EQ(getStatusCode(sendRequest("/v1/health")), 503);

  // Reconnect → 200
  tracker.setConnected();
  EXPECT_EQ(getStatusCode(sendRequest("/v1/health")), 200);
}

TEST_F(HealthV1EndpointTest, ExistingEndpointsUnaffected) {
  NatsStatusTracker tracker;
  tracker.setConnected();

  server_ = std::make_unique<HealthCheckServer>(port_, nullptr, &tracker);
  ASSERT_TRUE(server_->start());
  port_ = server_->getPort();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // /healthz still works
  EXPECT_EQ(getStatusCode(sendRequest("/healthz")), 200);
  EXPECT_EQ(getBody(sendRequest("/healthz")), "{\"status\":\"healthy\"}");

  // /ready still works
  EXPECT_EQ(getStatusCode(sendRequest("/ready")), 200);
}

TEST_F(HealthV1EndpointTest, V1HealthNotFoundWhenPathDiffers) {
  server_ = std::make_unique<HealthCheckServer>(port_);
  ASSERT_TRUE(server_->start());
  port_ = server_->getPort();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_EQ(getStatusCode(sendRequest("/v1/healthz")), 404);
}

TEST_F(HealthV1EndpointTest, LastSuccessEpochMsInBody) {
  NatsStatusTracker tracker;
  tracker.setConnected();
  int64_t expected_ms = tracker.lastSuccessEpochMs();
  ASSERT_GT(expected_ms, 0);

  server_ = std::make_unique<HealthCheckServer>(port_, nullptr, &tracker);
  ASSERT_TRUE(server_->start());
  port_ = server_->getPort();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::string body = getBody(sendRequest("/v1/health"));
  EXPECT_NE(body.find(std::to_string(expected_ms)), std::string::npos);
}
