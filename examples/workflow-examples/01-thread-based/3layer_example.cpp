/**
 * @file 3layer_example.cpp
 * @brief Thread-Based 3-Layer Example: Chief → ModuleLead → TaskAgents
 *
 * This example demonstrates a 3-layer HMAS workflow:
 * - Level 0: ChiefArchitectAgent (strategic orchestration)
 * - Level 2: ModuleLeadAgent (task decomposition and synthesis)
 * - Level 3: TaskAgents (concrete execution)
 *
 * Workflow:
 * 1. Chief sends module goal to ModuleLead: "Calculate: 10+20+30"
 * 2. ModuleLead decomposes into 3 tasks: echo 10, echo 20, echo 30
 * 3. ModuleLead delegates tasks to 3 TaskAgents
 * 4. TaskAgents execute bash commands in parallel
 * 5. ModuleLead synthesizes results: Sum = 60
 * 6. ModuleLead returns synthesis to Chief
 * 7. Chief processes final result
 *
 * State Machine (ModuleLead):
 * IDLE → PLANNING → WAITING_FOR_TASKS → SYNTHESIZING → IDLE
 */

#include <agents/chief_architect_agent.hpp>
#include <agents/module_lead_agent.hpp>
#include <agents/task_agent.hpp>
#include <chrono>
#include <core/message.hpp>
#include <core/message_bus.hpp>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

using namespace projectkeystone;

// ANSI color codes
namespace Color {
const char* RESET = "\033[0m";
const char* CHIEF = "\033[1;34m";   // Blue
const char* MODULE = "\033[1;35m";  // Magenta
const char* TASK = "\033[1;32m";    // Green
const char* BUS = "\033[1;33m";     // Yellow
const char* INFO = "\033[1;36m";    // Cyan
}  // namespace Color

void log(const std::string& agent, const std::string& message,
         const char* color = Color::INFO) {
  std::cout << color << "[" << agent << "] " << message << Color::RESET
            << std::endl;
}

int main() {
  std::cout << "\n"
            << "══════════════════════════════════════════════════════════\n"
            << "  Thread-Based 3-Layer Example\n"
            << "  Chief → ModuleLead → TaskAgents\n"
            << "══════════════════════════════════════════════════════════\n"
            << std::endl;

  // ─────────────────────────────────────────────────────────────
  // STEP 1: Create MessageBus
  // ─────────────────────────────────────────────────────────────
  log("Setup", "Creating MessageBus...", Color::INFO);
  auto bus = std::make_shared<MessageBus>();

  // ─────────────────────────────────────────────────────────────
  // STEP 2: Create Agents
  // ─────────────────────────────────────────────────────────────
  log("Setup", "Creating ChiefArchitectAgent (Level 0)...", Color::INFO);
  auto chief = std::make_shared<ChiefArchitectAgent>("chief", bus);

  log("Setup", "Creating ModuleLeadAgent (Level 2)...", Color::INFO);
  auto module = std::make_shared<ModuleLeadAgent>("module1", bus);

  log("Setup", "Creating 3 TaskAgents (Level 3)...", Color::INFO);
  auto task1 = std::make_shared<TaskAgent>("task1", bus);
  auto task2 = std::make_shared<TaskAgent>("task2", bus);
  auto task3 = std::make_shared<TaskAgent>("task3", bus);

  // ─────────────────────────────────────────────────────────────
  // STEP 3: Register Agents
  // ─────────────────────────────────────────────────────────────
  log("Setup", "Registering agents with MessageBus...", Color::INFO);
  bus->registerAgent("chief", chief);
  bus->registerAgent("module1", module);
  bus->registerAgent("task1", task1);
  bus->registerAgent("task2", task2);
  bus->registerAgent("task3", task3);

  std::cout << "\n✓ Setup Complete\n" << std::endl;

  // ─────────────────────────────────────────────────────────────
  // STEP 4: Chief sends goal to ModuleLead
  // ─────────────────────────────────────────────────────────────
  std::string goal = "Calculate: 10+20+30";
  log("Chief", "Sending module goal to ModuleLead: " + goal, Color::CHIEF);

  auto goal_msg = KeystoneMessage::create(
      "chief", "module1", goal, ActionType::DECOMPOSE, Priority::NORMAL);
  chief->sendMessage(goal_msg);
  log("Bus", "Routed message from chief to module1", Color::BUS);

  // ─────────────────────────────────────────────────────────────
  // STEP 5: ModuleLead receives and decomposes goal
  // ─────────────────────────────────────────────────────────────
  log("Module", "Retrieving goal from inbox...", Color::MODULE);
  auto module_msg_opt = module->getMessage();

  if (!module_msg_opt) {
    std::cerr << "ERROR: ModuleLead did not receive goal!" << std::endl;
    return 1;
  }

  auto module_msg = *module_msg_opt;
  log("Module", "Received goal: " + module_msg.command, Color::MODULE);
  log("Module", "State: IDLE → PLANNING", Color::MODULE);
  log("Module", "Decomposing goal into 3 tasks...", Color::MODULE);

  // ModuleLead processes message (decomposes and delegates)
  auto module_response = module->processMessage(module_msg).get();

  log("Module", "State: PLANNING → WAITING_FOR_TASKS", Color::MODULE);
  log("Module", "Delegated 3 tasks:", Color::MODULE);
  log("Module", "  - Task1: echo 10", Color::MODULE);
  log("Module", "  - Task2: echo 20", Color::MODULE);
  log("Module", "  - Task3: echo 30", Color::MODULE);

  // ─────────────────────────────────────────────────────────────
  // STEP 6: TaskAgents execute in parallel
  // ─────────────────────────────────────────────────────────────
  std::cout << std::endl;
  log("Info", "TaskAgents executing in parallel...", Color::INFO);

  std::vector<std::shared_ptr<TaskAgent>> task_agents = {task1, task2, task3};

  std::vector<std::thread> workers;
  std::vector<Response> task_responses;
  std::mutex response_mutex;

  for (size_t i = 0; i < task_agents.size(); ++i) {
    workers.emplace_back([&task_agents, i, &task_responses, &response_mutex]() {
      auto& task_agent = task_agents[i];
      std::string task_name = "Task" + std::to_string(i + 1);

      // Retrieve message
      auto msg_opt = task_agent->getMessage();
      if (!msg_opt) {
        log(task_name, "ERROR: No message in inbox!", Color::TASK);
        return;
      }

      auto msg = *msg_opt;
      log(task_name, "Executing: " + msg.command, Color::TASK);

      // Execute command
      auto response = task_agent->processMessage(msg).get();

      if (response.status == Response::Status::Success) {
        log(task_name, "Result: " + response.result, Color::TASK);
      } else {
        log(task_name, "ERROR: " + response.result, Color::TASK);
      }

      // Store response
      std::lock_guard<std::mutex> lock(response_mutex);
      task_responses.push_back(response);
    });
  }

  // Wait for all tasks to complete
  for (auto& worker : workers) {
    worker.join();
  }

  log("Info", "All TaskAgents completed execution", Color::INFO);
  std::cout << std::endl;

  // ─────────────────────────────────────────────────────────────
  // STEP 7: ModuleLead synthesizes results
  // ─────────────────────────────────────────────────────────────
  log("Module", "Received 3/3 task results", Color::MODULE);
  log("Module", "State: WAITING_FOR_TASKS → SYNTHESIZING", Color::MODULE);

  // ModuleLead receives results and synthesizes
  // (In reality, this happens through message passing, but we simulate here)
  int sum = 0;
  for (const auto& resp : task_responses) {
    try {
      int value = std::stoi(resp.result);
      sum += value;
    } catch (...) {
      log("Module", "WARNING: Could not parse result: " + resp.result,
          Color::MODULE);
    }
  }

  std::string synthesis =
      "Module completed: 10 + 20 + 30 = " + std::to_string(sum);
  log("Module", "Synthesis: " + synthesis, Color::MODULE);
  log("Module", "State: SYNTHESIZING → IDLE", Color::MODULE);
  log("Module", "Sending synthesis to Chief", Color::MODULE);

  // Create synthesis response
  auto synthesis_msg =
      KeystoneMessage::create("module1", "chief", synthesis,
                              ActionType::RETURN_RESULT, Priority::NORMAL);
  module->sendMessage(synthesis_msg);
  log("Bus", "Routed synthesis from module1 to chief", Color::BUS);

  // ─────────────────────────────────────────────────────────────
  // STEP 8: Chief receives final result
  // ─────────────────────────────────────────────────────────────
  log("Chief", "Retrieving synthesis from inbox...", Color::CHIEF);
  auto chief_result_opt = chief->getMessage();

  if (!chief_result_opt) {
    std::cerr << "ERROR: Chief did not receive synthesis!" << std::endl;
    return 1;
  }

  auto chief_result = *chief_result_opt;
  log("Chief", "Received synthesis from ModuleLead", Color::CHIEF);

  // Process final response
  auto final_response = chief->processMessage(chief_result).get();

  // ─────────────────────────────────────────────────────────────
  // STEP 9: Display Final Result
  // ─────────────────────────────────────────────────────────────
  std::cout << "\n"
            << "══════════════════════════════════════════════════════════\n"
            << "  FINAL RESULT\n"
            << "══════════════════════════════════════════════════════════\n"
            << std::endl;

  std::cout << Color::INFO << "Goal:     " << Color::RESET << goal << std::endl;
  std::cout << Color::INFO << "Result:   " << Color::RESET
            << final_response.result << std::endl;
  std::cout << Color::INFO << "Status:   " << Color::RESET
            << (final_response.status == Response::Status::Success ? "✓ Success"
                                                                   : "✗ Failed")
            << std::endl;

  std::cout << "\n"
            << "══════════════════════════════════════════════════════════\n"
            << std::endl;

  // ─────────────────────────────────────────────────────────────
  // Architecture Diagram
  // ─────────────────────────────────────────────────────────────
  std::cout << "\nArchitecture:\n"
            << "┌──────────────────────────────────────────────────┐\n"
            << "│       Single Process (Thread-Based)               │\n"
            << "│                                                   │\n"
            << "│  ┌────────────────────────────────────────────┐  │\n"
            << "│  │           MessageBus                       │  │\n"
            << "│  │  (Lock-Free Concurrent Queues)             │  │\n"
            << "│  └──────┬──────────────┬──────────────────────┘  │\n"
            << "│         │              │                         │\n"
            << "│         ↓              ↓                         │\n"
            << "│    ┌────────┐    ┌──────────┐                   │\n"
            << "│    │ Chief  │    │  Module  │                   │\n"
            << "│    │ Thread │←──→│  Thread  │                   │\n"
            << "│    └────────┘    └─────┬────┘                   │\n"
            << "│                        │                         │\n"
            << "│     Level 0            ↓          Level 2        │\n"
            << "│                   ┌────┴─────┐                   │\n"
            << "│                   ↓    ↓     ↓                   │\n"
            << "│               ┌──────┬──────┬──────┐             │\n"
            << "│               │Task1 │Task2 │Task3 │             │\n"
            << "│               │Thread│Thread│Thread│             │\n"
            << "│               └──────┴──────┴──────┘             │\n"
            << "│                                                   │\n"
            << "│                      Level 3                     │\n"
            << "└──────────────────────────────────────────────────┘\n"
            << std::endl;

  // ─────────────────────────────────────────────────────────────
  // Message Flow Summary
  // ─────────────────────────────────────────────────────────────
  std::cout << "Message Flow:\n"
            << "1. Chief → ModuleLead (DECOMPOSE goal)\n"
            << "2. ModuleLead decomposes into 3 tasks\n"
            << "3. ModuleLead → Task1, Task2, Task3 (EXECUTE)\n"
            << "4. TaskAgents execute in parallel\n"
            << "5. Task1, Task2, Task3 → ModuleLead (RETURN_RESULT)\n"
            << "6. ModuleLead synthesizes results\n"
            << "7. ModuleLead → Chief (RETURN_RESULT)\n"
            << "8. Chief processes final result\n"
            << std::endl;

  // ─────────────────────────────────────────────────────────────
  // State Machine Diagram
  // ─────────────────────────────────────────────────────────────
  std::cout << "ModuleLead State Machine:\n"
            << "IDLE → PLANNING → WAITING_FOR_TASKS → SYNTHESIZING → IDLE\n"
            << "  ↑                                                    │\n"
            << "  └────────────────────────────────────────────────────┘\n"
            << std::endl;

  return 0;
}

/**
 * Expected Output:
 *
 * ══════════════════════════════════════════════════════════
 *   Thread-Based 3-Layer Example
 *   Chief → ModuleLead → TaskAgents
 * ══════════════════════════════════════════════════════════
 *
 * [Setup] Creating MessageBus...
 * [Setup] Creating ChiefArchitectAgent (Level 0)...
 * [Setup] Creating ModuleLeadAgent (Level 2)...
 * [Setup] Creating 3 TaskAgents (Level 3)...
 * [Setup] Registering agents with MessageBus...
 *
 * ✓ Setup Complete
 *
 * [Chief] Sending module goal to ModuleLead: Calculate: 10+20+30
 * [Bus] Routed message from chief to module1
 * [Module] Retrieving goal from inbox...
 * [Module] Received goal: Calculate: 10+20+30
 * [Module] State: IDLE → PLANNING
 * [Module] Decomposing goal into 3 tasks...
 * [Module] State: PLANNING → WAITING_FOR_TASKS
 * [Module] Delegated 3 tasks:
 * [Module]   - Task1: echo 10
 * [Module]   - Task2: echo 20
 * [Module]   - Task3: echo 30
 *
 * [Info] TaskAgents executing in parallel...
 * [Task1] Executing: echo 10
 * [Task2] Executing: echo 20
 * [Task3] Executing: echo 30
 * [Task1] Result: 10
 * [Task2] Result: 20
 * [Task3] Result: 30
 * [Info] All TaskAgents completed execution
 *
 * [Module] Received 3/3 task results
 * [Module] State: WAITING_FOR_TASKS → SYNTHESIZING
 * [Module] Synthesis: Module completed: 10 + 20 + 30 = 60
 * [Module] State: SYNTHESIZING → IDLE
 * [Module] Sending synthesis to Chief
 * [Bus] Routed synthesis from module1 to chief
 * [Chief] Retrieving synthesis from inbox...
 * [Chief] Received synthesis from ModuleLead
 *
 * ══════════════════════════════════════════════════════════
 *   FINAL RESULT
 * ══════════════════════════════════════════════════════════
 *
 * Goal:     Calculate: 10+20+30
 * Result:   Module completed: 10 + 20 + 30 = 60
 * Status:   ✓ Success
 *
 * ══════════════════════════════════════════════════════════
 */
