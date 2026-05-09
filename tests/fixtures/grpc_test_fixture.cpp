/**
 * @file grpc_test_fixture.cpp
 * @brief Implementation of gRPC test fixture for in-process testing
 *
 * Provides running gRPC servers (HMASCoordinator and ServiceRegistry) on
 * ephemeral ports for integration testing without external Docker dependencies.
 *
 * Phase 2.2: Technical debt resolution - enable disabled gRPC tests
 * Issue #53
 */

#ifdef ENABLE_GRPC

#include "fixtures/grpc_test_fixture.hpp"

#include <grpcpp/grpcpp.h>

#include "network/grpc_server.hpp"
#include "network/hmas_coordinator_service.hpp"
#include "network/service_registry.hpp"
#include "network/task_router.hpp"

namespace test {

void GrpcTestFixture::SetUp() {
  // Create ServiceRegistry instance
  registry_ = std::make_shared<keystone::network::ServiceRegistry>(3000);

  // Create TaskRouter instance
  router_ = std::make_shared<keystone::network::TaskRouter>(registry_);

  // Create service implementations
  registry_service_ =
      std::make_shared<keystone::network::ServiceRegistryServiceImpl>(
          registry_);
  coordinator_service_ =
      std::make_shared<keystone::network::HMASCoordinatorServiceImpl>(registry_,
                                                                      router_);

  // Configure servers with ephemeral ports (port 0 = OS-assigned)
  keystone::network::GrpcServerConfig coordinator_config;
  coordinator_config.server_address = "localhost:0";  // Ephemeral port
  coordinator_config.num_threads = 2;

  keystone::network::GrpcServerConfig registry_config;
  registry_config.server_address = "localhost:0";  // Ephemeral port
  registry_config.num_threads = 2;

  // Create and start coordinator server
  coordinator_server_ =
      std::make_unique<keystone::network::GrpcServer>(coordinator_config);
  coordinator_server_->registerService(coordinator_service_.get());

  if (!coordinator_server_->start()) {
    throw std::runtime_error("Failed to start HMASCoordinator test server");
  }

  // Extract actual port assigned by OS
  std::string coord_addr = coordinator_server_->getAddress();
  size_t colon_pos = coord_addr.find_last_of(':');
  if (colon_pos != std::string::npos) {
    coordinator_port_ = std::stoi(coord_addr.substr(colon_pos + 1));
  }

  // Create and start registry server
  registry_server_ =
      std::make_unique<keystone::network::GrpcServer>(registry_config);
  registry_server_->registerService(registry_service_.get());

  if (!registry_server_->start()) {
    throw std::runtime_error("Failed to start ServiceRegistry test server");
  }

  // Extract actual port assigned by OS
  std::string reg_addr = registry_server_->getAddress();
  colon_pos = reg_addr.find_last_of(':');
  if (colon_pos != std::string::npos) {
    registry_port_ = std::stoi(reg_addr.substr(colon_pos + 1));
  }

  // Give servers a moment to fully initialize
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void GrpcTestFixture::TearDown() {
  // Stop servers gracefully with 1s timeout
  if (coordinator_server_) {
    coordinator_server_->stop(1000);
  }
  if (registry_server_) {
    registry_server_->stop(1000);
  }

  // Clean up shared pointers
  coordinator_service_.reset();
  registry_service_.reset();
  router_.reset();
  registry_.reset();
}

std::string GrpcTestFixture::getCoordinatorAddress() const {
  if (coordinator_port_ == 0) {
    throw std::runtime_error(
        "Coordinator port not initialized. Call SetUp() first.");
  }
  return "localhost:" + std::to_string(coordinator_port_);
}

std::string GrpcTestFixture::getRegistryAddress() const {
  if (registry_port_ == 0) {
    throw std::runtime_error(
        "Registry port not initialized. Call SetUp() first.");
  }
  return "localhost:" + std::to_string(registry_port_);
}

}  // namespace test

#endif  // ENABLE_GRPC
