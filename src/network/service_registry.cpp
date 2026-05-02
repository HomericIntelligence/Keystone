#include "network/service_registry.hpp"

#include <algorithm>
#include <chrono>

namespace keystone::network {

ServiceRegistry::ServiceRegistry(int32_t heartbeat_timeout_ms)
    : heartbeat_timeout_(heartbeat_timeout_ms) {}

ServiceRegistry::~ServiceRegistry() = default;

bool ServiceRegistry::registerAgent(
    const std::string& agent_id, const std::string& agent_type, int32_t level,
    const std::string& ip_port, const std::vector<std::string>& capabilities) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Check if agent already exists
  if (agents_.find(agent_id) != agents_.end()) {
    return false;
  }

  // Create registration info
  AgentRegistrationInfo info;
  info.agent_id = agent_id;
  info.agent_type = agent_type;
  info.level = level;
  info.ip_port = ip_port;
  info.capabilities = capabilities;
  info.last_heartbeat = std::chrono::system_clock::now();
  info.cpu_usage_percent = 0.0f;
  info.memory_usage_mb = 0.0f;
  info.active_tasks = 0;

  agents_[agent_id] = std::move(info);
  return true;
}

bool ServiceRegistry::updateHeartbeat(const std::string& agent_id,
                                      float cpu_usage_percent,
                                      float memory_usage_mb,
                                      int32_t active_tasks) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = agents_.find(agent_id);
  if (it == agents_.end()) {
    return false;
  }

  it->second.last_heartbeat = std::chrono::system_clock::now();
  it->second.cpu_usage_percent = cpu_usage_percent;
  it->second.memory_usage_mb = memory_usage_mb;
  it->second.active_tasks = active_tasks;

  return true;
}

bool ServiceRegistry::unregisterAgent(const std::string& agent_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  return agents_.erase(agent_id) > 0;
}

std::optional<AgentRegistrationInfo> ServiceRegistry::getAgent(
    const std::string& agent_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = agents_.find(agent_id);
  if (it == agents_.end()) {
    return std::nullopt;
  }

  return it->second;
}

std::vector<AgentRegistrationInfo> ServiceRegistry::queryAgents(
    const std::string& agent_type, int32_t level,
    const std::vector<std::string>& required_capabilities, int32_t max_results,
    bool only_alive) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<AgentRegistrationInfo> results;

  for (const auto& [id, info] : agents_) {
    // Filter by liveness
    if (only_alive && !isAgentAlive(id)) {
      continue;
    }

    // Filter by agent type
    if (!agent_type.empty() && info.agent_type != agent_type) {
      continue;
    }

    // Filter by level (-1 means all levels)
    if (level != -1 && info.level != level) {
      continue;
    }

    // Filter by capabilities
    if (!required_capabilities.empty() &&
        !hasRequiredCapabilities(info, required_capabilities)) {
      continue;
    }

    results.push_back(info);

    // Check max results limit
    if (max_results > 0 &&
        static_cast<int32_t>(results.size()) >= max_results) {
      break;
    }
  }

  return results;
}

std::vector<AgentRegistrationInfo> ServiceRegistry::listAllAgents(
    bool only_alive) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<AgentRegistrationInfo> results;

  for (const auto& [id, info] : agents_) {
    if (only_alive && !isAgentAlive(id)) {
      continue;
    }
    results.push_back(info);
  }

  return results;
}

bool ServiceRegistry::isAgentAlive(const std::string& agent_id) const {
  // Note: Caller must hold mutex lock
  auto it = agents_.find(agent_id);
  if (it == agents_.end()) {
    return false;
  }

  auto now = std::chrono::system_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - it->second.last_heartbeat);

  return elapsed < heartbeat_timeout_;
}

int32_t ServiceRegistry::cleanupDeadAgents() {
  std::lock_guard<std::mutex> lock(mutex_);

  int32_t removed_count = 0;
  auto now = std::chrono::system_clock::now();

  for (auto it = agents_.begin(); it != agents_.end();) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - it->second.last_heartbeat);

    if (elapsed >= heartbeat_timeout_) {
      it = agents_.erase(it);
      ++removed_count;
    } else {
      ++it;
    }
  }

  return removed_count;
}

size_t ServiceRegistry::getAliveAgentCount() const {
  std::lock_guard<std::mutex> lock(mutex_);

  size_t count = 0;
  for (const auto& [id, info] : agents_) {
    if (isAgentAlive(id)) {
      ++count;
    }
  }

  return count;
}

bool ServiceRegistry::hasRequiredCapabilities(
    const AgentRegistrationInfo& agent,
    const std::vector<std::string>& required_capabilities) const {
  // Agent must have ALL required capabilities
  for (const auto& required_cap : required_capabilities) {
    bool has_capability =
        std::find(agent.capabilities.begin(), agent.capabilities.end(),
                  required_cap) != agent.capabilities.end();
    if (!has_capability) {
      return false;
    }
  }
  return true;
}

// Static conversion methods
hmas::AgentInfo ServiceRegistry::toProtoAgentInfo(
    const AgentRegistrationInfo& info) {
  hmas::AgentInfo proto_info;
  proto_info.set_agent_id(info.agent_id);
  proto_info.set_agent_type(info.agent_type);
  proto_info.set_level(info.level);
  proto_info.set_ip_port(info.ip_port);

  for (const auto& cap : info.capabilities) {
    proto_info.add_capabilities(cap);
  }

  auto timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          info.last_heartbeat.time_since_epoch())
                          .count();
  proto_info.set_last_heartbeat_unix_ms(timestamp_ms);
  proto_info.set_cpu_usage_percent(info.cpu_usage_percent);
  proto_info.set_memory_usage_mb(info.memory_usage_mb);

  return proto_info;
}

AgentRegistrationInfo ServiceRegistry::fromProtoRegistration(
    const hmas::AgentRegistration& reg) {
  AgentRegistrationInfo info;
  info.agent_id = reg.agent_id();
  info.agent_type = reg.agent_type();
  info.level = reg.level();
  info.ip_port = reg.ip_port();

  for (int32_t i = 0; i < reg.capabilities_size(); ++i) {
    info.capabilities.push_back(reg.capabilities(i));
  }

  if (reg.timestamp_unix_ms() > 0) {
    info.last_heartbeat = std::chrono::system_clock::time_point(
        std::chrono::milliseconds(reg.timestamp_unix_ms()));
  } else {
    info.last_heartbeat = std::chrono::system_clock::now();
  }

  info.cpu_usage_percent = 0.0f;
  info.memory_usage_mb = 0.0f;
  info.active_tasks = 0;

  return info;
}

// gRPC Service Implementation
ServiceRegistryServiceImpl::ServiceRegistryServiceImpl(
    std::shared_ptr<ServiceRegistry> registry)
    : registry_(std::move(registry)) {}

grpc::Status ServiceRegistryServiceImpl::RegisterAgent(
    grpc::ServerContext* context, const hmas::AgentRegistration* request,
    hmas::RegistrationResponse* response) {
  (void)context;  // Unused

  std::vector<std::string> capabilities;
  for (int32_t i = 0; i < request->capabilities_size(); ++i) {
    capabilities.push_back(request->capabilities(i));
  }

  bool success = registry_->registerAgent(
      request->agent_id(), request->agent_type(), request->level(),
      request->ip_port(), capabilities);

  response->set_success(success);
  if (success) {
    response->set_message("Agent registered successfully");
    response->set_assigned_id(request->agent_id());
  } else {
    response->set_message("Agent ID already exists");
  }

  return grpc::Status::OK;
}

grpc::Status ServiceRegistryServiceImpl::Heartbeat(
    grpc::ServerContext* context, const hmas::HeartbeatRequest* request,
    hmas::HeartbeatResponse* response) {
  (void)context;  // Unused

  bool success = registry_->updateHeartbeat(
      request->agent_id(), request->cpu_usage_percent(),
      request->memory_usage_mb(), request->active_tasks());

  response->set_acknowledged(success);
  response->set_server_timestamp_unix_ms(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());

  return grpc::Status::OK;
}

grpc::Status ServiceRegistryServiceImpl::UnregisterAgent(
    grpc::ServerContext* context, const hmas::UnregisterRequest* request,
    hmas::UnregisterResponse* response) {
  (void)context;  // Unused

  bool success = registry_->unregisterAgent(request->agent_id());

  response->set_success(success);
  if (success) {
    response->set_message("Agent unregistered successfully");
  } else {
    response->set_message("Agent not found");
  }

  return grpc::Status::OK;
}

grpc::Status ServiceRegistryServiceImpl::QueryAgents(
    grpc::ServerContext* context, const hmas::AgentQuery* request,
    hmas::AgentList* response) {
  (void)context;  // Unused

  std::vector<std::string> required_capabilities;
  for (int32_t i = 0; i < request->required_capabilities_size(); ++i) {
    required_capabilities.push_back(request->required_capabilities(i));
  }

  auto agents = registry_->queryAgents(
      request->agent_type(), request->level(), required_capabilities,
      request->max_results(), request->only_alive());

  response->set_total_count(static_cast<int32_t>(agents.size()));

  for (const auto& agent : agents) {
    auto* agent_info = response->add_agents();
    *agent_info = ServiceRegistry::toProtoAgentInfo(agent);
  }

  return grpc::Status::OK;
}

grpc::Status ServiceRegistryServiceImpl::GetAgent(
    grpc::ServerContext* context, const hmas::GetAgentRequest* request,
    hmas::AgentInfo* response) {
  (void)context;  // Unused

  auto agent = registry_->getAgent(request->agent_id());

  if (!agent.has_value()) {
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "Agent not found");
  }

  *response = ServiceRegistry::toProtoAgentInfo(agent.value());

  return grpc::Status::OK;
}

grpc::Status ServiceRegistryServiceImpl::ListAllAgents(
    grpc::ServerContext* context, const hmas::ListAllAgentsRequest* request,
    hmas::AgentList* response) {
  (void)context;  // Unused

  auto agents = registry_->listAllAgents(request->only_alive());

  response->set_total_count(static_cast<int32_t>(agents.size()));

  for (const auto& agent : agents) {
    auto* agent_info = response->add_agents();
    *agent_info = ServiceRegistry::toProtoAgentInfo(agent);
  }

  return grpc::Status::OK;
}

}  // namespace keystone::network
