#include "network/grpc_client.hpp"

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace keystone::network {

namespace {

/// Read file contents into a string
/// @param file_path Path to the file
/// @return File contents as string
/// @throws std::runtime_error if file cannot be read
std::string readFileContents(const std::string& file_path) {
  std::ifstream file(file_path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open file: " + file_path);
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();

  if (file.fail() && !file.eof()) {
    throw std::runtime_error("Failed to read file: " + file_path);
  }

  return buffer.str();
}

/// Get environment variable value
/// @param name Environment variable name
/// @return Value if set, empty string otherwise
std::string getEnvVar(const char* name) {
  const char* value = std::getenv(name);
  return value ? std::string(value) : std::string();
}

}  // anonymous namespace

// HMASCoordinatorClient implementation
HMASCoordinatorClient::HMASCoordinatorClient(const GrpcClientConfig& config)
    : config_(config) {
  channel_ = grpc::CreateChannel(config_.server_address, createCredentials());
  stub_ = hmas::HMASCoordinator::NewStub(channel_);
}

hmas::TaskResponse HMASCoordinatorClient::submitTask(
    const std::string& yaml_spec, const std::string& session_id,
    int64_t deadline_unix_ms, hmas::TaskPriority priority,
    const std::string& parent_task_id) {
  hmas::TaskRequest request;
  request.set_yaml_spec(yaml_spec);
  request.set_session_id(session_id);
  request.set_deadline_unix_ms(deadline_unix_ms);
  request.set_priority(priority);
  if (!parent_task_id.empty()) {
    request.set_parent_task_id(parent_task_id);
  }

  hmas::TaskResponse response;
  auto context = createContext(config_.timeout_ms);

  grpc::Status status = stub_->SubmitTask(context.get(), request, &response);

  if (!status.ok()) {
    throw std::runtime_error("SubmitTask RPC failed: " +
                             status.error_message());
  }

  return response;
}

hmas::ResultAcknowledgement HMASCoordinatorClient::submitResult(
    const hmas::TaskResult& result) {
  hmas::ResultAcknowledgement response;
  auto context = createContext(config_.timeout_ms);

  grpc::Status status = stub_->SubmitResult(context.get(), result, &response);

  if (!status.ok()) {
    throw std::runtime_error("SubmitResult RPC failed: " +
                             status.error_message());
  }

  return response;
}

hmas::TaskResult HMASCoordinatorClient::getTaskResult(
    const std::string& task_id, int64_t timeout_ms) {
  hmas::TaskResultRequest request;
  request.set_task_id(task_id);
  request.set_timeout_ms(timeout_ms);

  hmas::TaskResult response;
  auto context = createContext(config_.timeout_ms);

  grpc::Status status = stub_->GetTaskResult(context.get(), request, &response);

  if (!status.ok()) {
    throw std::runtime_error("GetTaskResult RPC failed: " +
                             status.error_message());
  }

  return response;
}

hmas::TaskProgress HMASCoordinatorClient::getTaskProgress(
    const std::string& task_id, bool include_subtasks) {
  hmas::TaskProgressRequest request;
  request.set_task_id(task_id);
  request.set_include_subtasks(include_subtasks);

  hmas::TaskProgress response;
  auto context = createContext(config_.timeout_ms);

  grpc::Status status =
      stub_->GetTaskProgress(context.get(), request, &response);

  if (!status.ok()) {
    throw std::runtime_error("GetTaskProgress RPC failed: " +
                             status.error_message());
  }

  return response;
}

hmas::CancelResponse HMASCoordinatorClient::cancelTask(
    const std::string& task_id, const std::string& reason) {
  hmas::CancelRequest request;
  request.set_task_id(task_id);
  request.set_reason(reason);

  hmas::CancelResponse response;
  auto context = createContext(config_.timeout_ms);

  grpc::Status status = stub_->CancelTask(context.get(), request, &response);

  if (!status.ok()) {
    throw std::runtime_error("CancelTask RPC failed: " +
                             status.error_message());
  }

  return response;
}

std::shared_ptr<grpc::ChannelCredentials>
HMASCoordinatorClient::createCredentials() {
  if (config_.enable_tls) {
    // Get CA certificate path from environment variable
    std::string ca_path = getEnvVar("KEYSTONE_TLS_CA_PATH");

    // If path not in environment, fall back to config
    if (ca_path.empty()) {
      ca_path = config_.tls_ca_path;
    }

    // Validate that CA path is set
    if (ca_path.empty()) {
      throw std::runtime_error(
          "TLS enabled but CA certificate path not provided. "
          "Set KEYSTONE_TLS_CA_PATH environment variable.");
    }

    try {
      // Read CA certificate file
      std::string ca_contents = readFileContents(ca_path);

      // Setup SSL credentials
      grpc::SslCredentialsOptions ssl_opts;
      ssl_opts.pem_root_certs = ca_contents;

      return grpc::SslCredentials(ssl_opts);
    } catch (const std::exception& e) {
      throw std::runtime_error(std::string("Failed to load TLS credentials: ") +
                               e.what());
    }
  } else {
    return grpc::InsecureChannelCredentials();
  }
}

std::unique_ptr<grpc::ClientContext> HMASCoordinatorClient::createContext(
    int timeout_ms) {
  auto context = std::make_unique<grpc::ClientContext>();

  if (timeout_ms > 0) {
    context->set_deadline(std::chrono::system_clock::now() +
                          std::chrono::milliseconds(timeout_ms));
  }

  return context;
}

// ServiceRegistryClient implementation
ServiceRegistryClient::ServiceRegistryClient(const GrpcClientConfig& config)
    : config_(config) {
  channel_ = grpc::CreateChannel(config_.server_address, createCredentials());
  stub_ = hmas::ServiceRegistry::NewStub(channel_);
}

hmas::RegistrationResponse ServiceRegistryClient::registerAgent(
    const hmas::AgentRegistration& registration) {
  hmas::RegistrationResponse response;
  auto context = createContext(config_.timeout_ms);

  grpc::Status status =
      stub_->RegisterAgent(context.get(), registration, &response);

  if (!status.ok()) {
    throw std::runtime_error("RegisterAgent RPC failed: " +
                             status.error_message());
  }

  return response;
}

hmas::HeartbeatResponse ServiceRegistryClient::heartbeat(
    const std::string& agent_id, float cpu_usage_percent, float memory_usage_mb,
    int active_tasks) {
  hmas::HeartbeatRequest request;
  request.set_agent_id(agent_id);
  request.set_timestamp_unix_ms(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  request.set_cpu_usage_percent(cpu_usage_percent);
  request.set_memory_usage_mb(memory_usage_mb);
  request.set_active_tasks(active_tasks);

  hmas::HeartbeatResponse response;
  auto context = createContext(config_.timeout_ms);

  grpc::Status status = stub_->Heartbeat(context.get(), request, &response);

  if (!status.ok()) {
    throw std::runtime_error("Heartbeat RPC failed: " + status.error_message());
  }

  return response;
}

hmas::UnregisterResponse ServiceRegistryClient::unregisterAgent(
    const std::string& agent_id, const std::string& reason) {
  hmas::UnregisterRequest request;
  request.set_agent_id(agent_id);
  request.set_reason(reason);

  hmas::UnregisterResponse response;
  auto context = createContext(config_.timeout_ms);

  grpc::Status status =
      stub_->UnregisterAgent(context.get(), request, &response);

  if (!status.ok()) {
    throw std::runtime_error("UnregisterAgent RPC failed: " +
                             status.error_message());
  }

  return response;
}

hmas::AgentList ServiceRegistryClient::queryAgents(
    const hmas::AgentQuery& query) {
  hmas::AgentList response;
  auto context = createContext(config_.timeout_ms);

  grpc::Status status = stub_->QueryAgents(context.get(), query, &response);

  if (!status.ok()) {
    throw std::runtime_error("QueryAgents RPC failed: " +
                             status.error_message());
  }

  return response;
}

hmas::AgentInfo ServiceRegistryClient::getAgent(const std::string& agent_id) {
  hmas::GetAgentRequest request;
  request.set_agent_id(agent_id);

  hmas::AgentInfo response;
  auto context = createContext(config_.timeout_ms);

  grpc::Status status = stub_->GetAgent(context.get(), request, &response);

  if (!status.ok()) {
    throw std::runtime_error("GetAgent RPC failed: " + status.error_message());
  }

  return response;
}

hmas::AgentList ServiceRegistryClient::listAllAgents(bool only_alive) {
  hmas::ListAllAgentsRequest request;
  request.set_only_alive(only_alive);

  hmas::AgentList response;
  auto context = createContext(config_.timeout_ms);

  grpc::Status status = stub_->ListAllAgents(context.get(), request, &response);

  if (!status.ok()) {
    throw std::runtime_error("ListAllAgents RPC failed: " +
                             status.error_message());
  }

  return response;
}

std::shared_ptr<grpc::ChannelCredentials>
ServiceRegistryClient::createCredentials() {
  if (config_.enable_tls) {
    // Get CA certificate path from environment variable
    std::string ca_path = getEnvVar("KEYSTONE_TLS_CA_PATH");

    // If path not in environment, fall back to config
    if (ca_path.empty()) {
      ca_path = config_.tls_ca_path;
    }

    // Validate that CA path is set
    if (ca_path.empty()) {
      throw std::runtime_error(
          "TLS enabled but CA certificate path not provided. "
          "Set KEYSTONE_TLS_CA_PATH environment variable.");
    }

    try {
      // Read CA certificate file
      std::string ca_contents = readFileContents(ca_path);

      // Setup SSL credentials
      grpc::SslCredentialsOptions ssl_opts;
      ssl_opts.pem_root_certs = ca_contents;

      return grpc::SslCredentials(ssl_opts);
    } catch (const std::exception& e) {
      throw std::runtime_error(std::string("Failed to load TLS credentials: ") +
                               e.what());
    }
  } else {
    return grpc::InsecureChannelCredentials();
  }
}

std::unique_ptr<grpc::ClientContext> ServiceRegistryClient::createContext(
    int timeout_ms) {
  auto context = std::make_unique<grpc::ClientContext>();

  if (timeout_ms > 0) {
    context->set_deadline(std::chrono::system_clock::now() +
                          std::chrono::milliseconds(timeout_ms));
  }

  return context;
}

}  // namespace keystone::network
