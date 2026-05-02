#pragma once

#ifdef ENABLE_GRPC

#include <gtest/gtest.h>

#include <memory>
#include <string>

// Forward declarations
namespace keystone {
namespace network {
class GrpcServer;
class ServiceRegistry;
class ServiceRegistryServiceImpl;
class HMASCoordinatorServiceImpl;
class TaskRouter;
}  // namespace network
}  // namespace keystone

namespace test {

/**
 * @brief Test fixture for gRPC tests requiring in-process servers
 *
 * This fixture provides running gRPC servers (HMASCoordinator and
 * ServiceRegistry) on ephemeral ports for integration testing without
 * external Docker dependencies.
 *
 * Usage:
 *   TEST_F(GrpcTestFixture, MyTest) {
 *     auto coord_addr = getCoordinatorAddress();
 *     auto registry_addr = getRegistryAddress();
 *     // Test code using servers...
 *   }
 */
class GrpcTestFixture : public ::testing::Test {
 protected:
  /**
   * @brief Set up test servers before each test
   *
   * Starts in-process gRPC servers on ephemeral ports (OS-assigned).
   * Servers are ready to accept connections after SetUp() completes.
   */
  void SetUp() override;

  /**
   * @brief Clean up servers after each test
   *
   * Stops servers gracefully with 1s timeout.
   */
  void TearDown() override;

  /**
   * @brief Get HMASCoordinator server address
   * @return Address in format "localhost:PORT"
   */
  std::string getCoordinatorAddress() const;

  /**
   * @brief Get ServiceRegistry server address
   * @return Address in format "localhost:PORT"
   */
  std::string getRegistryAddress() const;

  /**
   * @brief Get shared ServiceRegistry instance (for test assertions)
   * @return Shared pointer to ServiceRegistry
   */
  std::shared_ptr<keystone::network::ServiceRegistry> getRegistry() const {
    return registry_;
  }

 private:
  // Service implementations
  std::shared_ptr<keystone::network::ServiceRegistry> registry_;
  std::shared_ptr<keystone::network::TaskRouter> router_;
  std::shared_ptr<keystone::network::ServiceRegistryServiceImpl>
      registry_service_;
  std::shared_ptr<keystone::network::HMASCoordinatorServiceImpl>
      coordinator_service_;

  // gRPC servers
  std::unique_ptr<keystone::network::GrpcServer> coordinator_server_;
  std::unique_ptr<keystone::network::GrpcServer> registry_server_;

  // Server ports (ephemeral, assigned by OS)
  int32_t coordinator_port_ = 0;
  int32_t registry_port_ = 0;
};

}  // namespace test

#endif  // ENABLE_GRPC
