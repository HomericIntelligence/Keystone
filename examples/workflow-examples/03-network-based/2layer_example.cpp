/**
 * @file 2layer_example.cpp
 * @brief Network-Based 2-Layer Example: Chief (Machine A) → TaskAgent (Machine
 * B)
 *
 * This example demonstrates distributed HMAS using gRPC for network
 * communication. Two agents run on separate machines and communicate via gRPC
 * Protocol Buffers.
 *
 * Architecture:
 * ┌──────────────────────────┐              ┌──────────────────────────┐
 * │  Machine A               │              │  Machine B               │
 * │  IP: 192.168.1.10        │              │  IP: 192.168.1.11        │
 * │                          │              │                          │
 * │  ┌────────────────────┐  │              │  ┌────────────────────┐  │
 * │  │ ChiefArchitect     │  │              │  │  TaskAgent         │  │
 * │  │                    │  │              │  │                    │  │
 * │  │  gRPC Client   ────┼──┼──────────────┼─>│  gRPC Server      │  │
 * │  │                    │  │              │  │  Port: 50100       │  │
 * │  └────────────────────┘  │              │  └────────────────────┘  │
 * │         │                │              │         │                │
 * └─────────┼────────────────┘              └─────────┼────────────────┘
 *           │                                         │
 *           └─────────────────┬───────────────────────┘
 *                             │
 *                             ↓
 *               ┌──────────────────────────┐
 *               │  Machine C               │
 *               │  IP: 192.168.1.20        │
 *               │                          │
 *               │  ┌────────────────────┐  │
 *               │  │  ServiceRegistry   │  │
 *               │  │  gRPC Server       │  │
 *               │  │  Port: 50051       │  │
 *               │  │                    │  │
 *               │  │  - RegisterAgent() │  │
 *               │  │  - QueryAgents()   │  │
 *               │  │  - Heartbeat()     │  │
 *               │  └────────────────────┘  │
 *               └──────────────────────────┘
 *
 * Requirements:
 *   - gRPC and Protocol Buffers installed
 *   - Build with -DENABLE_GRPC=ON
 *   - Network connectivity between machines
 *
 * Usage:
 *   # Machine C (ServiceRegistry):
 *   ./service_registry --port=50051
 *
 *   # Machine B (TaskAgent):
 *   ./network_2layer --role=task \
 *                    --address=192.168.1.11:50100 \
 *                    --registry=192.168.1.20:50051
 *
 *   # Machine A (Chief):
 *   ./network_2layer --role=chief \
 *                    --address=192.168.1.10:50200 \
 *                    --registry=192.168.1.20:50051
 */

#ifdef ENABLE_GRPC

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

// gRPC includes
#include <grpcpp/grpcpp.h>

// ProjectKeystone includes (assuming Phase 8 is enabled)
#include <agents/chief_architect_agent.hpp>
#include <agents/task_agent.hpp>
#include <network/grpc_agent_service.hpp>
#include <network/service_registry_client.hpp>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

using namespace projectkeystone;

// ═══════════════════════════════════════════════════════════════
// Configuration
// ═══════════════════════════════════════════════════════════════

struct NetworkConfig {
  std::string role;              // "chief" or "task"
  std::string agent_address;     // This agent's gRPC address
  std::string registry_address;  // ServiceRegistry address
};

// ═══════════════════════════════════════════════════════════════
// TaskAgent Process (Machine B)
// ═══════════════════════════════════════════════════════════════

/**
 * @brief Run TaskAgent with gRPC server
 */
void run_task_agent(const NetworkConfig& config) {
  std::cout << "\n"
            << "═══════════════════════════════════════════════════════\n"
            << "  Network 2-Layer Example: TaskAgent Process\n"
            << "  Address: " << config.agent_address << "\n"
            << "  Registry: " << config.registry_address << "\n"
            << "═══════════════════════════════════════════════════════\n"
            << std::endl;

  // Create TaskAgent
  std::cout << "[Task] Creating TaskAgent..." << std::endl;
  auto task_agent = std::make_shared<TaskAgent>("task1", nullptr);

  // Create gRPC service for this agent
  std::cout << "[Task] Creating gRPC service..." << std::endl;
  GrpcAgentService grpc_service(task_agent);

  // Build and start gRPC server
  std::cout << "[Task] Starting gRPC server on " << config.agent_address
            << std::endl;
  ServerBuilder builder;
  builder.AddListeningPort(config.agent_address,
                           grpc::InsecureServerCredentials());
  builder.RegisterService(&grpc_service);
  std::unique_ptr<Server> server(builder.BuildAndStart());

  if (!server) {
    std::cerr << "ERROR: Failed to start gRPC server" << std::endl;
    return;
  }

  std::cout << "[Task] gRPC server started successfully" << std::endl;

  // Register with ServiceRegistry
  std::cout << "[Task] Registering with ServiceRegistry..." << std::endl;
  auto channel = grpc::CreateChannel(config.registry_address,
                                     grpc::InsecureChannelCredentials());
  ServiceRegistryClient registry_client(channel);

  AgentInfo info;
  info.agent_id = "task1";
  info.agent_type = "TaskAgent";
  info.address = config.agent_address;
  info.capabilities.push_back("bash_execution");
  info.metadata["version"] = "1.0.0";

  bool registered = registry_client.RegisterAgent(info);
  if (!registered) {
    std::cerr << "ERROR: Failed to register with ServiceRegistry" << std::endl;
    return;
  }

  std::cout << "[Task] Successfully registered with ServiceRegistry"
            << std::endl;

  // Start heartbeat thread
  std::cout << "[Task] Starting heartbeat thread..." << std::endl;
  std::atomic<bool> running{true};
  std::thread heartbeat_thread([&]() {
    while (running) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      registry_client.Heartbeat("task1");
    }
  });

  std::cout << "[Task] Ready to process commands" << std::endl;

  // Wait for server to finish
  server->Wait();

  // Cleanup
  running = false;
  heartbeat_thread.join();

  std::cout << "[Task] TaskAgent process shutting down" << std::endl;
}

// ═══════════════════════════════════════════════════════════════
// Chief Process (Machine A)
// ═══════════════════════════════════════════════════════════════

/**
 * @brief Run Chief with gRPC client
 */
void run_chief(const NetworkConfig& config) {
  std::cout << "\n"
            << "═══════════════════════════════════════════════════════\n"
            << "  Network 2-Layer Example: Chief Process\n"
            << "  Address: " << config.agent_address << "\n"
            << "  Registry: " << config.registry_address << "\n"
            << "═══════════════════════════════════════════════════════\n"
            << std::endl;

  // Wait for TaskAgent to register
  std::cout << "[Chief] Waiting for TaskAgent to register..." << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // Connect to ServiceRegistry
  std::cout << "[Chief] Connecting to ServiceRegistry..." << std::endl;
  auto channel = grpc::CreateChannel(config.registry_address,
                                     grpc::InsecureChannelCredentials());
  ServiceRegistryClient registry_client(channel);

  // Query for TaskAgents
  std::cout << "[Chief] Querying for TaskAgents..." << std::endl;
  AgentQuery query;
  query.agent_type = "TaskAgent";

  auto agents = registry_client.QueryAgents(query);
  if (agents.empty()) {
    std::cerr << "ERROR: No TaskAgents found!" << std::endl;
    return;
  }

  std::cout << "[Chief] Found " << agents.size() << " TaskAgent(s)"
            << std::endl;

  // Get TaskAgent address
  std::string task_address = agents[0].address;
  std::cout << "[Chief] TaskAgent address: " << task_address << std::endl;

  // Create gRPC channel to TaskAgent
  std::cout << "[Chief] Creating gRPC channel to TaskAgent..." << std::endl;
  auto task_channel =
      grpc::CreateChannel(task_address, grpc::InsecureChannelCredentials());
  auto task_stub = AgentService::NewStub(task_channel);

  // Send command to TaskAgent
  std::string command = "echo 'Hello from distributed HMAS!'";
  std::cout << "[Chief] Sending command to TaskAgent: " << command << std::endl;

  ClientContext context;
  Message msg;
  msg.set_msg_id(generate_uuid());
  msg.set_sender_id("chief");
  msg.set_receiver_id("task1");
  msg.set_command(command);
  msg.set_action_type(ActionType::EXECUTE);

  Response response;
  Status status = task_stub->ProcessMessage(&context, msg, &response);

  if (!status.ok()) {
    std::cerr << "ERROR: gRPC call failed: " << status.error_message()
              << std::endl;
    return;
  }

  std::cout << "[Chief] Received response from TaskAgent:" << std::endl;
  std::cout << "  Result: " << response.result() << std::endl;
  std::cout << "  Status: "
            << (response.status() == Response::SUCCESS ? "Success" : "Error")
            << std::endl;

  std::cout << "\n"
            << "═══════════════════════════════════════════════════════\n"
            << "  Chief Process Complete\n"
            << "═══════════════════════════════════════════════════════\n"
            << std::endl;
}

// ═══════════════════════════════════════════════════════════════
// Main Entry Point
// ═══════════════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
  NetworkConfig config;

  // Parse command-line arguments
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg.find("--role=") == 0) {
      config.role = arg.substr(7);
    } else if (arg.find("--address=") == 0) {
      config.agent_address = arg.substr(10);
    } else if (arg.find("--registry=") == 0) {
      config.registry_address = arg.substr(11);
    }
  }

  // Validate configuration
  if (config.role.empty() || config.agent_address.empty() ||
      config.registry_address.empty()) {
    std::cerr << "Usage: " << argv[0] << "\n"
              << "  --role=<chief|task>\n"
              << "  --address=<ip:port>\n"
              << "  --registry=<registry-ip:port>\n"
              << "\nExample:\n"
              << "  # TaskAgent:\n"
              << "  " << argv[0] << " --role=task --address=192.168.1.11:50100"
              << " --registry=192.168.1.20:50051\n"
              << "\n  # Chief:\n"
              << "  " << argv[0] << " --role=chief --address=192.168.1.10:50200"
              << " --registry=192.168.1.20:50051\n";
    return 1;
  }

  try {
    if (config.role == "task") {
      run_task_agent(config);
    } else if (config.role == "chief") {
      run_chief(config);
    } else {
      std::cerr << "Invalid role: " << config.role << std::endl;
      std::cerr << "Valid roles: chief, task" << std::endl;
      return 1;
    }
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}

#else  // !ENABLE_GRPC

int main() {
  std::cout << "\n"
            << "Network-based examples require gRPC support.\n"
            << "Please rebuild with: cmake -DENABLE_GRPC=ON ..\n"
            << std::endl;
  return 1;
}

#endif  // ENABLE_GRPC

/**
 * Expected Output:
 *
 * ===== Terminal 1 (ServiceRegistry on Machine C) =====
 * [Registry] Starting ServiceRegistry on port 50051...
 * [Registry] ServiceRegistry ready
 *
 * ===== Terminal 2 (TaskAgent on Machine B) =====
 * ═══════════════════════════════════════════════════════
 *   Network 2-Layer Example: TaskAgent Process
 *   Address: 192.168.1.11:50100
 *   Registry: 192.168.1.20:50051
 * ═══════════════════════════════════════════════════════
 *
 * [Task] Creating TaskAgent...
 * [Task] Creating gRPC service...
 * [Task] Starting gRPC server on 192.168.1.11:50100
 * [Task] gRPC server started successfully
 * [Task] Registering with ServiceRegistry...
 * [Task] Successfully registered with ServiceRegistry
 * [Task] Starting heartbeat thread...
 * [Task] Ready to process commands
 * [Task] Received gRPC call: ProcessMessage
 * [Task] Executing command: echo 'Hello from distributed HMAS!'
 * [Task] Command result: Hello from distributed HMAS!
 * [Task] Sending response to Chief
 *
 * ===== Terminal 3 (Chief on Machine A) =====
 * ═══════════════════════════════════════════════════════
 *   Network 2-Layer Example: Chief Process
 *   Address: 192.168.1.10:50200
 *   Registry: 192.168.1.20:50051
 * ═══════════════════════════════════════════════════════
 *
 * [Chief] Waiting for TaskAgent to register...
 * [Chief] Connecting to ServiceRegistry...
 * [Chief] Querying for TaskAgents...
 * [Chief] Found 1 TaskAgent(s)
 * [Chief] TaskAgent address: 192.168.1.11:50100
 * [Chief] Creating gRPC channel to TaskAgent...
 * [Chief] Sending command to TaskAgent: echo 'Hello from distributed HMAS!'
 * [Chief] Received response from TaskAgent:
 *   Result: Hello from distributed HMAS!
 *   Status: Success
 *
 * ═══════════════════════════════════════════════════════
 *   Chief Process Complete
 * ═══════════════════════════════════════════════════════
 *
 * Key Takeaways:
 * 1. TaskAgent runs on separate machine (192.168.1.11)
 * 2. Chief runs on separate machine (192.168.1.10)
 * 3. ServiceRegistry coordinates on third machine (192.168.1.20)
 * 4. Communication via gRPC over network
 * 5. Service discovery enables dynamic agent lookup
 * 6. Heartbeat monitoring maintains agent health
 */
