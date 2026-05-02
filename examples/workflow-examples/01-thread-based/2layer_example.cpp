/**
 * @file 2layer_example.cpp
 * @brief Thread-Based 2-Layer Example: Chief → TaskAgent
 *
 * This example demonstrates the simplest HMAS workflow with just 2 layers:
 * - Level 0: ChiefArchitectAgent (strategic orchestration)
 * - Level 3: TaskAgent (concrete execution)
 *
 * Workflow:
 * 1. Chief sends a command to TaskAgent
 * 2. TaskAgent executes the bash command
 * 3. TaskAgent returns the result to Chief
 * 4. Chief processes the result
 *
 * Communication:
 * - Single process with multiple threads
 * - Lock-free concurrent queues for message passing
 * - MessageBus routes messages by agent ID
 * - C++20 coroutines for async processing
 */

#include <agents/chief_architect_agent.hpp>
#include <agents/task_agent.hpp>
#include <chrono>
#include <core/message.hpp>
#include <core/message_bus.hpp>
#include <iostream>
#include <memory>

using namespace projectkeystone;

// ANSI color codes for better output readability
namespace Color {
const char* RESET = "\033[0m";
const char* CHIEF = "\033[1;34m";  // Blue
const char* TASK = "\033[1;32m";   // Green
const char* BUS = "\033[1;33m";    // Yellow
const char* INFO = "\033[1;36m";   // Cyan
}  // namespace Color

/**
 * @brief Print a formatted log message with timestamp
 */
void log(const std::string& agent, const std::string& message,
         const char* color = Color::INFO) {
  auto now = std::chrono::system_clock::now();
  auto time = std::chrono::system_clock::to_time_t(now);
  std::cout << color << "[" << agent << "] " << message << Color::RESET
            << std::endl;
}

int main() {
  std::cout << "\n"
            << "═══════════════════════════════════════════════════════\n"
            << "  Thread-Based 2-Layer Example: Chief → TaskAgent\n"
            << "═══════════════════════════════════════════════════════\n"
            << std::endl;

  // ─────────────────────────────────────────────────────────────
  // STEP 1: Create MessageBus
  // ─────────────────────────────────────────────────────────────
  log("Setup", "Creating MessageBus...", Color::INFO);
  auto bus = std::make_shared<MessageBus>();

  // ─────────────────────────────────────────────────────────────
  // STEP 2: Create Agents
  // ─────────────────────────────────────────────────────────────
  log("Setup", "Creating ChiefArchitectAgent...", Color::INFO);
  auto chief = std::make_shared<ChiefArchitectAgent>("chief", bus);

  log("Setup", "Creating TaskAgent...", Color::INFO);
  auto task = std::make_shared<TaskAgent>("task1", bus);

  // ─────────────────────────────────────────────────────────────
  // STEP 3: Register Agents with MessageBus
  // ─────────────────────────────────────────────────────────────
  log("Setup", "Registering agents with MessageBus...", Color::INFO);
  bus->registerAgent("chief", chief);
  bus->registerAgent("task1", task);

  std::cout << "\n✓ Setup Complete\n" << std::endl;

  // ─────────────────────────────────────────────────────────────
  // STEP 4: Chief sends command to TaskAgent
  // ─────────────────────────────────────────────────────────────
  std::string command = "echo 'Hello from HMAS!'";
  log("Chief", "Sending command to TaskAgent: " + command, Color::CHIEF);

  auto goal_msg = KeystoneMessage::create("chief",              // sender_id
                                          "task1",              // receiver_id
                                          command,              // command
                                          ActionType::EXECUTE,  // action type
                                          Priority::NORMAL      // priority
  );

  chief->sendMessage(goal_msg);
  log("Bus", "Routed message " + goal_msg.msg_id + " from chief to task1",
      Color::BUS);

  // ─────────────────────────────────────────────────────────────
  // STEP 5: TaskAgent receives and processes message
  // ─────────────────────────────────────────────────────────────
  log("Task", "Retrieving message from inbox...", Color::TASK);
  auto task_msg_opt = task->getMessage();

  if (!task_msg_opt) {
    std::cerr << "ERROR: TaskAgent did not receive message!" << std::endl;
    return 1;
  }

  auto task_msg = *task_msg_opt;
  log("Task", "Received command: " + task_msg.command, Color::TASK);
  log("Task", "Executing bash command...", Color::TASK);

  // Process message asynchronously using C++20 coroutines
  auto task_response = task->processMessage(task_msg).get();

  if (task_response.status == Response::Status::Success) {
    log("Task", "Command executed successfully", Color::TASK);
    log("Task", "Result: " + task_response.result, Color::TASK);
    log("Task", "Sending result back to chief", Color::TASK);
  } else {
    log("Task", "Command execution failed: " + task_response.result,
        Color::TASK);
  }

  log("Bus", "Routed response " + task_response.msg_id + " from task1 to chief",
      Color::BUS);

  // ─────────────────────────────────────────────────────────────
  // STEP 6: Chief receives result from TaskAgent
  // ─────────────────────────────────────────────────────────────
  log("Chief", "Retrieving response from inbox...", Color::CHIEF);
  auto chief_msg_opt = chief->getMessage();

  if (!chief_msg_opt) {
    std::cerr << "ERROR: Chief did not receive response!" << std::endl;
    return 1;
  }

  auto chief_msg = *chief_msg_opt;
  log("Chief", "Received result from TaskAgent", Color::CHIEF);

  // Process response
  auto chief_response = chief->processMessage(chief_msg).get();

  // ─────────────────────────────────────────────────────────────
  // STEP 7: Display Final Result
  // ─────────────────────────────────────────────────────────────
  std::cout << "\n"
            << "═══════════════════════════════════════════════════════\n"
            << "  FINAL RESULT\n"
            << "═══════════════════════════════════════════════════════\n"
            << std::endl;

  std::cout << Color::INFO << "Command:  " << Color::RESET << command
            << std::endl;
  std::cout << Color::INFO << "Result:   " << Color::RESET
            << chief_response.result << std::endl;
  std::cout << Color::INFO << "Status:   " << Color::RESET
            << (chief_response.status == Response::Status::Success ? "✓ Success"
                                                                   : "✗ Failed")
            << std::endl;

  std::cout << "\n"
            << "═══════════════════════════════════════════════════════\n"
            << std::endl;

  // ─────────────────────────────────────────────────────────────
  // Architecture Diagram
  // ─────────────────────────────────────────────────────────────
  std::cout << "\nArchitecture:\n"
            << "┌─────────────────────────────────────────┐\n"
            << "│       Single Process (Thread-Based)      │\n"
            << "│                                          │\n"
            << "│  ┌────────────────────────────────────┐ │\n"
            << "│  │         MessageBus                 │ │\n"
            << "│  │  (Lock-Free Concurrent Queues)     │ │\n"
            << "│  └──────────┬─────────────────────────┘ │\n"
            << "│             │                           │\n"
            << "│             │                           │\n"
            << "│      ┌──────┴───────┐                  │\n"
            << "│      ↓              ↓                  │\n"
            << "│  ┌────────┐    ┌─────────┐             │\n"
            << "│  │ Chief  │    │  Task   │             │\n"
            << "│  │ Thread │ ←→ │ Thread  │             │\n"
            << "│  └────────┘    └─────────┘             │\n"
            << "│                                          │\n"
            << "│  Level 0       Level 3                  │\n"
            << "└─────────────────────────────────────────┘\n"
            << std::endl;

  // ─────────────────────────────────────────────────────────────
  // Message Flow Summary
  // ─────────────────────────────────────────────────────────────
  std::cout << "Message Flow:\n"
            << "1. Chief → MessageBus → TaskAgent (EXECUTE command)\n"
            << "2. TaskAgent executes bash command\n"
            << "3. TaskAgent → MessageBus → Chief (RETURN_RESULT)\n"
            << "4. Chief processes result\n"
            << std::endl;

  return 0;
}

/**
 * Expected Output:
 * ═══════════════════════════════════════════════════════
 *   Thread-Based 2-Layer Example: Chief → TaskAgent
 * ═══════════════════════════════════════════════════════
 *
 * [Setup] Creating MessageBus...
 * [Setup] Creating ChiefArchitectAgent...
 * [Setup] Creating TaskAgent...
 * [Setup] Registering agents with MessageBus...
 *
 * ✓ Setup Complete
 *
 * [Chief] Sending command to TaskAgent: echo 'Hello from HMAS!'
 * [Bus] Routed message msg_001 from chief to task1
 * [Task] Retrieving message from inbox...
 * [Task] Received command: echo 'Hello from HMAS!'
 * [Task] Executing bash command...
 * [Task] Command executed successfully
 * [Task] Result: Hello from HMAS!
 * [Task] Sending result back to chief
 * [Bus] Routed response msg_002 from task1 to chief
 * [Chief] Retrieving response from inbox...
 * [Chief] Received result from TaskAgent
 *
 * ═══════════════════════════════════════════════════════
 *   FINAL RESULT
 * ═══════════════════════════════════════════════════════
 *
 * Command:  echo 'Hello from HMAS!'
 * Result:   Hello from HMAS!
 * Status:   ✓ Success
 *
 * ═══════════════════════════════════════════════════════
 *
 * Architecture:
 * ┌─────────────────────────────────────────┐
 * │       Single Process (Thread-Based)      │
 * │                                          │
 * │  ┌────────────────────────────────────┐ │
 * │  │         MessageBus                 │ │
 * │  │  (Lock-Free Concurrent Queues)     │ │
 * │  └──────────┬─────────────────────────┘ │
 * │             │                           │
 * │             │                           │
 * │      ┌──────┴───────┐                  │
 * │      ↓              ↓                  │
 * │  ┌────────┐    ┌─────────┐             │
 * │  │ Chief  │    │  Task   │             │
 * │  │ Thread │ ←→ │ Thread  │             │
 * │  └────────┘    └─────────┘             │
 * │                                          │
 * │  Level 0       Level 3                  │
 * └─────────────────────────────────────────┘
 *
 * Message Flow:
 * 1. Chief → MessageBus → TaskAgent (EXECUTE command)
 * 2. TaskAgent executes bash command
 * 3. TaskAgent → MessageBus → Chief (RETURN_RESULT)
 * 4. Chief processes result
 */
