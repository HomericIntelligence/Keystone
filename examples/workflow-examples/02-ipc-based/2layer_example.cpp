/**
 * @file 2layer_example.cpp
 * @brief IPC-Based 2-Layer Example: Chief Process ← → Task Process
 *
 * This example demonstrates inter-process communication using shared memory.
 * Two separate OS processes communicate via Boost.Interprocess shared memory.
 *
 * Architecture:
 * ┌──────────────┐              ┌──────────────┐
 * │  Process 1   │              │  Process 2   │
 * │              │              │              │
 * │ ┌──────────┐ │              │ ┌──────────┐ │
 * │ │  Chief   │ │              │ │   Task   │ │
 * │ │  Agent   │ │              │ │  Agent   │ │
 * │ └────┬─────┘ │              │ └────┬─────┘ │
 * └──────┼───────┘              └──────┼───────┘
 *        │                             │
 *        └─────────────┬───────────────┘
 *                      ↓
 *        ┌──────────────────────────┐
 *        │  Shared Memory Segment   │
 *        │  "HMASSharedMemory"      │
 *        │                          │
 *        │  ┌────────────────────┐  │
 *        │  │  MessageBus (IPC)  │  │
 *        │  │  - queue_chief     │  │
 *        │  │  - queue_task1     │  │
 *        │  │  - Named mutexes   │  │
 *        │  │  - Semaphores      │  │
 *        │  └────────────────────┘  │
 *        └──────────────────────────┘
 *
 * Usage:
 *   Terminal 1: ./ipc_2layer --role=task
 *   Terminal 2: ./ipc_2layer --role=chief
 *
 * Build Requirements:
 *   - Boost.Interprocess library
 *   - C++20 compiler
 *   - CMake with -DENABLE_IPC=ON
 */

#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

// Boost.Interprocess for shared memory IPC
#include <agents/chief_architect_agent.hpp>
#include <agents/task_agent.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/deque.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/named_semaphore.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <core/message.hpp>

using namespace projectkeystone;
using namespace boost::interprocess;

// ═══════════════════════════════════════════════════════════════
// IPC Message Queue in Shared Memory
// ═══════════════════════════════════════════════════════════════

/**
 * @brief Message structure that can be stored in shared memory
 *
 * Note: std::string cannot be directly stored in shared memory.
 * We use fixed-size char arrays instead.
 */
struct IPCMessage {
  char msg_id[64];
  char sender_id[64];
  char receiver_id[64];
  char command[256];
  char payload[1024];
  int action_type;  // Maps to ActionType enum
  int priority;     // Maps to Priority enum
  bool is_response;
  int status;  // For responses: Success or Error

  // Convert from KeystoneMessage
  static IPCMessage from_keystone(const KeystoneMessage& msg) {
    IPCMessage ipc_msg{};
    strncpy(ipc_msg.msg_id, msg.msg_id.c_str(), 63);
    strncpy(ipc_msg.sender_id, msg.sender_id.c_str(), 63);
    strncpy(ipc_msg.receiver_id, msg.receiver_id.c_str(), 63);
    strncpy(ipc_msg.command, msg.command.c_str(), 255);
    if (msg.payload) {
      strncpy(ipc_msg.payload, msg.payload->c_str(), 1023);
    }
    ipc_msg.action_type = static_cast<int>(msg.action_type);
    ipc_msg.priority = static_cast<int>(msg.priority);
    ipc_msg.is_response = false;
    return ipc_msg;
  }

  // Convert from Response
  static IPCMessage from_response(const Response& resp) {
    IPCMessage ipc_msg{};
    strncpy(ipc_msg.msg_id, resp.msg_id.c_str(), 63);
    strncpy(ipc_msg.sender_id, resp.sender_id.c_str(), 63);
    strncpy(ipc_msg.receiver_id, resp.receiver_id.c_str(), 63);
    strncpy(ipc_msg.payload, resp.result.c_str(), 1023);
    ipc_msg.is_response = true;
    ipc_msg.status = static_cast<int>(resp.status);
    return ipc_msg;
  }

  // Convert to KeystoneMessage
  KeystoneMessage to_keystone() const {
    auto msg = KeystoneMessage::create(sender_id, receiver_id, command,
                                       static_cast<ActionType>(action_type),
                                       static_cast<Priority>(priority));
    msg.msg_id = msg_id;
    if (strlen(payload) > 0) {
      msg.payload = std::string(payload);
    }
    return msg;
  }

  // Convert to Response
  Response to_response() const {
    Response resp;
    resp.msg_id = msg_id;
    resp.sender_id = sender_id;
    resp.receiver_id = receiver_id;
    resp.result = payload;
    resp.status = static_cast<Response::Status>(status);
    resp.timestamp = std::chrono::system_clock::now();
    return resp;
  }
};

/**
 * @brief Allocator for shared memory containers
 */
using ShmemAllocator =
    allocator<IPCMessage, managed_shared_memory::segment_manager>;

/**
 * @brief Deque of messages in shared memory
 */
using IPCMessageQueue = deque<IPCMessage, ShmemAllocator>;

// ═══════════════════════════════════════════════════════════════
// IPC Helper Functions
// ═══════════════════════════════════════════════════════════════

constexpr const char* SHARED_MEMORY_NAME = "HMASSharedMemory";
constexpr size_t SHARED_MEMORY_SIZE = 65536;  // 64KB

/**
 * @brief Initialize shared memory segment (first process)
 */
managed_shared_memory create_shared_memory() {
  // Remove any existing segment
  shared_memory_object::remove(SHARED_MEMORY_NAME);

  // Create new segment
  return managed_shared_memory(create_only, SHARED_MEMORY_NAME,
                               SHARED_MEMORY_SIZE);
}

/**
 * @brief Open existing shared memory segment (subsequent processes)
 */
managed_shared_memory open_shared_memory() {
  return managed_shared_memory(open_only, SHARED_MEMORY_NAME);
}

/**
 * @brief Create message queue in shared memory
 */
IPCMessageQueue* create_message_queue(managed_shared_memory& segment,
                                      const std::string& queue_name) {
  const ShmemAllocator alloc(segment.get_segment_manager());
  return segment.construct<IPCMessageQueue>(queue_name.c_str())(alloc);
}

/**
 * @brief Find existing message queue in shared memory
 */
IPCMessageQueue* find_message_queue(managed_shared_memory& segment,
                                    const std::string& queue_name) {
  auto result = segment.find<IPCMessageQueue>(queue_name.c_str());
  return result.first;
}

/**
 * @brief Send message via IPC
 */
void send_ipc_message(managed_shared_memory& segment,
                      const std::string& receiver_id, const IPCMessage& msg) {
  std::string queue_name = "queue_" + receiver_id;
  std::string mutex_name = "mutex_" + receiver_id;
  std::string sem_name = "sem_" + receiver_id;

  // Find or create mutex
  auto mutex =
      segment.find_or_construct<interprocess_mutex>(mutex_name.c_str())();

  // Find queue
  auto queue = find_message_queue(segment, queue_name);
  if (!queue) {
    throw std::runtime_error("Queue not found: " + queue_name);
  }

  // Lock and push message
  {
    scoped_lock<interprocess_mutex> lock(*mutex);
    queue->push_back(msg);
  }

  // Signal semaphore to wake up receiver
  try {
    named_semaphore sem(open_or_create, sem_name.c_str(), 0);
    sem.post();
  } catch (interprocess_exception& e) {
    std::cerr << "Semaphore error: " << e.what() << std::endl;
  }

  std::cout << "[IPC] Sent message to " << receiver_id << std::endl;
}

/**
 * @brief Receive message via IPC (blocking)
 */
IPCMessage receive_ipc_message(managed_shared_memory& segment,
                               const std::string& agent_id,
                               int timeout_ms = 5000) {
  std::string queue_name = "queue_" + agent_id;
  std::string mutex_name = "mutex_" + agent_id;
  std::string sem_name = "sem_" + agent_id;

  // Open semaphore
  named_semaphore sem(open_or_create, sem_name.c_str(), 0);

  // Wait for message with timeout
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  if (!sem.timed_wait(boost::posix_time::microsec_clock::universal_time() +
                      boost::posix_time::milliseconds(timeout_ms))) {
    throw std::runtime_error("Timeout waiting for message");
  }

  // Find mutex and queue
  auto mutex = segment.find<interprocess_mutex>(mutex_name.c_str()).first;
  auto queue = find_message_queue(segment, queue_name);

  if (!queue) {
    throw std::runtime_error("Queue not found: " + queue_name);
  }

  // Lock and pop message
  scoped_lock<interprocess_mutex> lock(*mutex);

  if (queue->empty()) {
    throw std::runtime_error("Queue is empty after semaphore signal");
  }

  IPCMessage msg = queue->front();
  queue->pop_front();

  std::cout << "[IPC] Received message for " << agent_id << std::endl;

  return msg;
}

// ═══════════════════════════════════════════════════════════════
// Agent Process Functions
// ═══════════════════════════════════════════════════════════════

/**
 * @brief Run Chief process
 */
void run_chief_process() {
  std::cout << "\n"
            << "═══════════════════════════════════════════════════════\n"
            << "  IPC 2-Layer Example: Chief Process\n"
            << "═══════════════════════════════════════════════════════\n"
            << std::endl;

  // Create shared memory segment
  std::cout << "[Chief] Creating shared memory segment..." << std::endl;
  auto segment = create_shared_memory();

  // Create Chief's message queue
  std::cout << "[Chief] Creating message queue..." << std::endl;
  create_message_queue(segment, "queue_chief");

  // Create Chief agent (no MessageBus for IPC mode)
  std::cout << "[Chief] Initializing ChiefArchitectAgent..." << std::endl;
  auto chief = std::make_shared<ChiefArchitectAgent>("chief", nullptr);

  // Wait for Task process to be ready
  std::cout << "[Chief] Waiting for Task process to register..." << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // Send command to TaskAgent
  std::string command = "echo 'Hello from IPC!'";
  std::cout << "[Chief] Sending command to TaskAgent: " << command << std::endl;

  auto goal_msg = KeystoneMessage::create(
      "chief", "task1", command, ActionType::EXECUTE, Priority::NORMAL);

  IPCMessage ipc_msg = IPCMessage::from_keystone(goal_msg);
  send_ipc_message(segment, "task1", ipc_msg);

  // Wait for response
  std::cout << "[Chief] Waiting for response from TaskAgent..." << std::endl;
  IPCMessage response_ipc = receive_ipc_message(segment, "chief", 10000);
  Response response = response_ipc.to_response();

  std::cout << "[Chief] Received response:" << std::endl;
  std::cout << "  Result: " << response.result << std::endl;
  std::cout << "  Status: "
            << (response.status == Response::Status::Success ? "Success"
                                                             : "Error")
            << std::endl;

  std::cout << "\n"
            << "═══════════════════════════════════════════════════════\n"
            << "  Chief Process Complete\n"
            << "═══════════════════════════════════════════════════════\n"
            << std::endl;

  // Cleanup
  std::this_thread::sleep_for(std::chrono::seconds(1));
  shared_memory_object::remove(SHARED_MEMORY_NAME);
  named_semaphore::remove("sem_chief");
  named_semaphore::remove("sem_task1");
}

/**
 * @brief Run Task process
 */
void run_task_process() {
  std::cout << "\n"
            << "═══════════════════════════════════════════════════════\n"
            << "  IPC 2-Layer Example: Task Process\n"
            << "═══════════════════════════════════════════════════════\n"
            << std::endl;

  // Wait for Chief to create shared memory
  std::cout << "[Task] Waiting for shared memory..." << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // Open shared memory segment
  std::cout << "[Task] Opening shared memory segment..." << std::endl;
  auto segment = open_shared_memory();

  // Create Task's message queue
  std::cout << "[Task] Creating message queue..." << std::endl;
  create_message_queue(segment, "queue_task1");

  // Create TaskAgent
  std::cout << "[Task] Initializing TaskAgent..." << std::endl;
  auto task = std::make_shared<TaskAgent>("task1", nullptr);

  std::cout << "[Task] Ready. Waiting for commands..." << std::endl;

  // Wait for command
  IPCMessage command_ipc = receive_ipc_message(segment, "task1", 30000);
  KeystoneMessage command_msg = command_ipc.to_keystone();

  std::cout << "[Task] Received command: " << command_msg.command << std::endl;
  std::cout << "[Task] Executing..." << std::endl;

  // Execute command using TaskAgent
  auto response = task->processMessage(command_msg).get();

  std::cout << "[Task] Execution complete" << std::endl;
  std::cout << "  Result: " << response.result << std::endl;

  // Send response back to Chief
  std::cout << "[Task] Sending response to Chief..." << std::endl;
  IPCMessage response_ipc = IPCMessage::from_response(response);
  send_ipc_message(segment, "chief", response_ipc);

  std::cout << "\n"
            << "═══════════════════════════════════════════════════════\n"
            << "  Task Process Complete\n"
            << "═══════════════════════════════════════════════════════\n"
            << std::endl;
}

// ═══════════════════════════════════════════════════════════════
// Main Entry Point
// ═══════════════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " --role=<chief|task>" << std::endl;
    std::cerr << "\nExample:\n";
    std::cerr << "  Terminal 1: " << argv[0] << " --role=task\n";
    std::cerr << "  Terminal 2: " << argv[0] << " --role=chief\n";
    return 1;
  }

  std::string arg = argv[1];
  std::string role;

  if (arg.find("--role=") == 0) {
    role = arg.substr(7);
  } else {
    std::cerr << "Invalid argument: " << arg << std::endl;
    return 1;
  }

  try {
    if (role == "chief") {
      run_chief_process();
    } else if (role == "task") {
      run_task_process();
    } else {
      std::cerr << "Invalid role: " << role << std::endl;
      std::cerr << "Valid roles: chief, task" << std::endl;
      return 1;
    }
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}

/**
 * Expected Output:
 *
 * ===== Terminal 1 (Task) =====
 * ═══════════════════════════════════════════════════════
 *   IPC 2-Layer Example: Task Process
 * ═══════════════════════════════════════════════════════
 *
 * [Task] Waiting for shared memory...
 * [Task] Opening shared memory segment...
 * [Task] Creating message queue...
 * [Task] Initializing TaskAgent...
 * [Task] Ready. Waiting for commands...
 * [IPC] Received message for task1
 * [Task] Received command: echo 'Hello from IPC!'
 * [Task] Executing...
 * [Task] Execution complete
 *   Result: Hello from IPC!
 * [Task] Sending response to Chief...
 * [IPC] Sent message to chief
 *
 * ═══════════════════════════════════════════════════════
 *   Task Process Complete
 * ═══════════════════════════════════════════════════════
 *
 * ===== Terminal 2 (Chief) =====
 * ═══════════════════════════════════════════════════════
 *   IPC 2-Layer Example: Chief Process
 * ═══════════════════════════════════════════════════════
 *
 * [Chief] Creating shared memory segment...
 * [Chief] Creating message queue...
 * [Chief] Initializing ChiefArchitectAgent...
 * [Chief] Waiting for Task process to register...
 * [Chief] Sending command to TaskAgent: echo 'Hello from IPC!'
 * [IPC] Sent message to task1
 * [Chief] Waiting for response from TaskAgent...
 * [IPC] Received message for chief
 * [Chief] Received response:
 *   Result: Hello from IPC!
 *   Status: Success
 *
 * ═══════════════════════════════════════════════════════
 *   Chief Process Complete
 * ═══════════════════════════════════════════════════════
 */
