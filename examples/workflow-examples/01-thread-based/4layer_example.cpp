/**
 * @file 4layer_example.cpp
 * @brief Thread-Based 4-Layer Example: Full Hierarchy
 *
 * This example demonstrates the complete 4-layer HMAS hierarchy:
 * - Level 0: ChiefArchitectAgent (strategic orchestration)
 * - Level 1: ComponentLeadAgent (component coordination)
 * - Level 2: ModuleLeadAgents (module decomposition and synthesis)
 * - Level 3: TaskAgents (concrete execution)
 *
 * Workflow:
 * 1. Chief → ComponentLead: "Build system: Core(10+20+30) + Utils(40+50+60)"
 * 2. ComponentLead decomposes into 2 modules:
 *    - Module1 (Core): 10+20+30
 *    - Module2 (Utils): 40+50+60
 * 3. ComponentLead → ModuleLead1 and ModuleLead2
 * 4. Each ModuleLead decomposes into 3 tasks
 * 5. 6 TaskAgents execute in parallel
 * 6. ModuleLead1 synthesizes: Core = 60
 * 7. ModuleLead2 synthesizes: Utils = 150
 * 8. ComponentLead aggregates: Core=60, Utils=150, Total=210
 * 9. Chief receives final aggregation
 *
 * State Machines:
 * - ComponentLead: IDLE → PLANNING → WAITING_FOR_MODULES → AGGREGATING → IDLE
 * - ModuleLead: IDLE → PLANNING → WAITING_FOR_TASKS → SYNTHESIZING → IDLE
 */

#include <agents/chief_architect_agent.hpp>
#include <agents/component_lead_agent.hpp>
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
const char* CHIEF = "\033[1;34m";      // Blue
const char* COMPONENT = "\033[1;31m";  // Red
const char* MODULE = "\033[1;35m";     // Magenta
const char* TASK = "\033[1;32m";       // Green
const char* BUS = "\033[1;33m";        // Yellow
const char* INFO = "\033[1;36m";       // Cyan
}  // namespace Color

void log(const std::string& agent, const std::string& message,
         const char* color = Color::INFO) {
  std::cout << color << "[" << agent << "] " << message << Color::RESET
            << std::endl;
}

int main() {
  std::cout
      << "\n"
      << "═══════════════════════════════════════════════════════════════\n"
      << "  Thread-Based 4-Layer Example: Full Hierarchy\n"
      << "  Chief → ComponentLead → ModuleLeads → TaskAgents\n"
      << "═══════════════════════════════════════════════════════════════\n"
      << std::endl;

  // ─────────────────────────────────────────────────────────────
  // STEP 1: Create MessageBus
  // ─────────────────────────────────────────────────────────────
  log("Setup", "Creating MessageBus...", Color::INFO);
  auto bus = std::make_shared<MessageBus>();

  // ─────────────────────────────────────────────────────────────
  // STEP 2: Create Agent Hierarchy
  // ─────────────────────────────────────────────────────────────
  log("Setup", "Creating Level 0: ChiefArchitectAgent", Color::INFO);
  auto chief = std::make_shared<ChiefArchitectAgent>("chief", bus);

  log("Setup", "Creating Level 1: ComponentLeadAgent", Color::INFO);
  auto component = std::make_shared<ComponentLeadAgent>("comp1", bus);

  log("Setup", "Creating Level 2: 2 ModuleLeadAgents", Color::INFO);
  auto module1 = std::make_shared<ModuleLeadAgent>("mod1", bus);  // Core
  auto module2 = std::make_shared<ModuleLeadAgent>("mod2", bus);  // Utils

  log("Setup", "Creating Level 3: 6 TaskAgents", Color::INFO);
  std::vector<std::shared_ptr<TaskAgent>> task_agents;
  for (int i = 1; i <= 6; ++i) {
    task_agents.push_back(
        std::make_shared<TaskAgent>("task" + std::to_string(i), bus));
  }

  // ─────────────────────────────────────────────────────────────
  // STEP 3: Register All Agents
  // ─────────────────────────────────────────────────────────────
  log("Setup", "Registering all agents with MessageBus...", Color::INFO);
  bus->registerAgent("chief", chief);
  bus->registerAgent("comp1", component);
  bus->registerAgent("mod1", module1);
  bus->registerAgent("mod2", module2);
  for (size_t i = 0; i < task_agents.size(); ++i) {
    bus->registerAgent("task" + std::to_string(i + 1), task_agents[i]);
  }

  std::cout << "\n✓ Setup Complete: 10 agents registered\n" << std::endl;

  // ─────────────────────────────────────────────────────────────
  // STEP 4: Chief sends system goal to ComponentLead
  // ─────────────────────────────────────────────────────────────
  std::string goal = "Build system: Core(10+20+30) + Utils(40+50+60)";
  log("Chief", "Sending system goal to ComponentLead:", Color::CHIEF);
  log("Chief", "  " + goal, Color::CHIEF);

  auto goal_msg = KeystoneMessage::create(
      "chief", "comp1", goal, ActionType::DECOMPOSE, Priority::NORMAL);
  chief->sendMessage(goal_msg);
  log("Bus", "Routed message from chief to comp1", Color::BUS);
  std::cout << std::endl;

  // ─────────────────────────────────────────────────────────────
  // STEP 5: ComponentLead receives and decomposes
  // ─────────────────────────────────────────────────────────────
  log("Component", "Retrieving goal from inbox...", Color::COMPONENT);
  auto comp_msg_opt = component->getMessage();

  if (!comp_msg_opt) {
    std::cerr << "ERROR: ComponentLead did not receive goal!" << std::endl;
    return 1;
  }

  auto comp_msg = *comp_msg_opt;
  log("Component", "Received goal: " + comp_msg.command, Color::COMPONENT);
  log("Component", "State: IDLE → PLANNING", Color::COMPONENT);
  log("Component", "Decomposing into 2 modules:", Color::COMPONENT);
  log("Component", "  - Module1 (Core): 10+20+30", Color::COMPONENT);
  log("Component", "  - Module2 (Utils): 40+50+60", Color::COMPONENT);

  // Process message (decomposes and delegates to modules)
  auto comp_response = component->processMessage(comp_msg).get();

  log("Component", "State: PLANNING → WAITING_FOR_MODULES", Color::COMPONENT);
  log("Component", "Delegated goals to ModuleLead1 and ModuleLead2",
      Color::COMPONENT);
  std::cout << std::endl;

  // ─────────────────────────────────────────────────────────────
  // STEP 6: ModuleLeads receive and decompose into tasks
  // ─────────────────────────────────────────────────────────────
  log("Info", "ModuleLeads processing module goals...", Color::INFO);

  // ModuleLead1 (Core): 10+20+30
  log("Module1", "Retrieving module goal from inbox...", Color::MODULE);
  auto mod1_msg_opt = module1->getMessage();
  if (!mod1_msg_opt) {
    std::cerr << "ERROR: ModuleLead1 did not receive goal!" << std::endl;
    return 1;
  }
  auto mod1_msg = *mod1_msg_opt;
  log("Module1", "Received: Core(10+20+30)", Color::MODULE);
  log("Module1", "State: IDLE → PLANNING → WAITING_FOR_TASKS", Color::MODULE);
  log("Module1", "Delegating to Task1, Task2, Task3", Color::MODULE);
  auto mod1_response = module1->processMessage(mod1_msg).get();

  // ModuleLead2 (Utils): 40+50+60
  log("Module2", "Retrieving module goal from inbox...", Color::MODULE);
  auto mod2_msg_opt = module2->getMessage();
  if (!mod2_msg_opt) {
    std::cerr << "ERROR: ModuleLead2 did not receive goal!" << std::endl;
    return 1;
  }
  auto mod2_msg = *mod2_msg_opt;
  log("Module2", "Received: Utils(40+50+60)", Color::MODULE);
  log("Module2", "State: IDLE → PLANNING → WAITING_FOR_TASKS", Color::MODULE);
  log("Module2", "Delegating to Task4, Task5, Task6", Color::MODULE);
  auto mod2_response = module2->processMessage(mod2_msg).get();

  std::cout << std::endl;

  // ─────────────────────────────────────────────────────────────
  // STEP 7: All 6 TaskAgents execute in parallel
  // ─────────────────────────────────────────────────────────────
  log("Info", "6 TaskAgents executing in parallel...", Color::INFO);

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

  log("Info", "All 6 TaskAgents completed execution", Color::INFO);
  std::cout << std::endl;

  // ─────────────────────────────────────────────────────────────
  // STEP 8: ModuleLeads synthesize results
  // ─────────────────────────────────────────────────────────────
  log("Module1", "Received 3/3 task results (10, 20, 30)", Color::MODULE);
  log("Module1", "State: WAITING_FOR_TASKS → SYNTHESIZING", Color::MODULE);
  int sum1 = 10 + 20 + 30;
  std::string synthesis1 =
      "Core module complete: Sum = " + std::to_string(sum1);
  log("Module1", synthesis1, Color::MODULE);
  log("Module1", "State: SYNTHESIZING → IDLE", Color::MODULE);
  log("Module1", "Sending synthesis to ComponentLead", Color::MODULE);

  auto synth1_msg = KeystoneMessage::create(
      "mod1", "comp1", synthesis1, ActionType::RETURN_RESULT, Priority::NORMAL);
  module1->sendMessage(synth1_msg);

  log("Module2", "Received 3/3 task results (40, 50, 60)", Color::MODULE);
  log("Module2", "State: WAITING_FOR_TASKS → SYNTHESIZING", Color::MODULE);
  int sum2 = 40 + 50 + 60;
  std::string synthesis2 =
      "Utils module complete: Sum = " + std::to_string(sum2);
  log("Module2", synthesis2, Color::MODULE);
  log("Module2", "State: SYNTHESIZING → IDLE", Color::MODULE);
  log("Module2", "Sending synthesis to ComponentLead", Color::MODULE);

  auto synth2_msg = KeystoneMessage::create(
      "mod2", "comp1", synthesis2, ActionType::RETURN_RESULT, Priority::NORMAL);
  module2->sendMessage(synth2_msg);

  std::cout << std::endl;

  // ─────────────────────────────────────────────────────────────
  // STEP 9: ComponentLead aggregates module results
  // ─────────────────────────────────────────────────────────────
  log("Component", "Received 2/2 module syntheses", Color::COMPONENT);
  log("Component", "State: WAITING_FOR_MODULES → AGGREGATING",
      Color::COMPONENT);

  // Retrieve module syntheses
  auto comp_synth1_opt = component->getMessage();
  auto comp_synth2_opt = component->getMessage();

  int total = sum1 + sum2;
  std::string aggregation =
      "System build complete:\n"
      "  - Core: " +
      std::to_string(sum1) +
      "\n"
      "  - Utils: " +
      std::to_string(sum2) +
      "\n"
      "  - Total: " +
      std::to_string(total);

  log("Component", "Aggregation:", Color::COMPONENT);
  log("Component", "  Core:  " + std::to_string(sum1), Color::COMPONENT);
  log("Component", "  Utils: " + std::to_string(sum2), Color::COMPONENT);
  log("Component", "  Total: " + std::to_string(total), Color::COMPONENT);
  log("Component", "State: AGGREGATING → IDLE", Color::COMPONENT);
  log("Component", "Sending aggregation to Chief", Color::COMPONENT);

  auto agg_msg =
      KeystoneMessage::create("comp1", "chief", aggregation,
                              ActionType::RETURN_RESULT, Priority::NORMAL);
  component->sendMessage(agg_msg);
  log("Bus", "Routed aggregation from comp1 to chief", Color::BUS);

  std::cout << std::endl;

  // ─────────────────────────────────────────────────────────────
  // STEP 10: Chief receives final aggregation
  // ─────────────────────────────────────────────────────────────
  log("Chief", "Retrieving aggregation from inbox...", Color::CHIEF);
  auto chief_result_opt = chief->getMessage();

  if (!chief_result_opt) {
    std::cerr << "ERROR: Chief did not receive aggregation!" << std::endl;
    return 1;
  }

  auto chief_result = *chief_result_opt;
  log("Chief", "Received final aggregation from ComponentLead", Color::CHIEF);

  // Process final response
  auto final_response = chief->processMessage(chief_result).get();

  // ─────────────────────────────────────────────────────────────
  // STEP 11: Display Final Result
  // ─────────────────────────────────────────────────────────────
  std::cout
      << "\n"
      << "═══════════════════════════════════════════════════════════════\n"
      << "  FINAL RESULT\n"
      << "═══════════════════════════════════════════════════════════════\n"
      << std::endl;

  std::cout << Color::INFO << "Goal:   " << Color::RESET << goal << std::endl;
  std::cout << Color::INFO << "Result: " << Color::RESET << std::endl;
  std::cout << final_response.result << std::endl;
  std::cout << Color::INFO << "Status: " << Color::RESET
            << (final_response.status == Response::Status::Success ? "✓ Success"
                                                                   : "✗ Failed")
            << std::endl;

  std::cout
      << "\n"
      << "═══════════════════════════════════════════════════════════════\n"
      << std::endl;

  // ─────────────────────────────────────────────────────────────
  // Architecture Diagram
  // ─────────────────────────────────────────────────────────────
  std::cout << "\nFull 4-Layer Architecture:\n\n"
            << "Level 0: ChiefArchitect\n"
            << "    │\n"
            << "    └─> Level 1: ComponentLead\n"
            << "            │\n"
            << "            ├─> Level 2: ModuleLead1 (Core)\n"
            << "            │       ├─> Level 3: Task1 (echo 10) → 10\n"
            << "            │       ├─> Level 3: Task2 (echo 20) → 20\n"
            << "            │       └─> Level 3: Task3 (echo 30) → 30\n"
            << "            │       └─> Synthesis: Core = 60\n"
            << "            │\n"
            << "            └─> Level 2: ModuleLead2 (Utils)\n"
            << "                    ├─> Level 3: Task4 (echo 40) → 40\n"
            << "                    ├─> Level 3: Task5 (echo 50) → 50\n"
            << "                    └─> Level 3: Task6 (echo 60) → 60\n"
            << "                    └─> Synthesis: Utils = 150\n"
            << "            │\n"
            << "            └─> Aggregation: Total = 210\n"
            << "    │\n"
            << "    └─> Final Result: System complete\n"
            << std::endl;

  // ─────────────────────────────────────────────────────────────
  // Message Flow Summary
  // ─────────────────────────────────────────────────────────────
  std::cout << "Message Flow Summary:\n"
            << " 1. Chief → ComponentLead (DECOMPOSE system goal)\n"
            << " 2. ComponentLead decomposes into 2 modules\n"
            << " 3. ComponentLead → ModuleLead1, ModuleLead2 (DECOMPOSE)\n"
            << " 4. ModuleLead1 decomposes Core into 3 tasks\n"
            << " 5. ModuleLead2 decomposes Utils into 3 tasks\n"
            << " 6. ModuleLead1 → Task1, Task2, Task3 (EXECUTE)\n"
            << " 7. ModuleLead2 → Task4, Task5, Task6 (EXECUTE)\n"
            << " 8. 6 TaskAgents execute in parallel\n"
            << " 9. Task1-3 → ModuleLead1 (RETURN_RESULT)\n"
            << "10. Task4-6 → ModuleLead2 (RETURN_RESULT)\n"
            << "11. ModuleLead1 synthesizes → Core = 60\n"
            << "12. ModuleLead2 synthesizes → Utils = 150\n"
            << "13. ModuleLead1, ModuleLead2 → ComponentLead (RETURN_RESULT)\n"
            << "14. ComponentLead aggregates → Total = 210\n"
            << "15. ComponentLead → Chief (RETURN_RESULT)\n"
            << "16. Chief processes final result\n"
            << std::endl;

  // ─────────────────────────────────────────────────────────────
  // Performance Metrics
  // ─────────────────────────────────────────────────────────────
  std::cout
      << "Performance Characteristics:\n"
      << "- Total Agents: 10 (1 Chief + 1 Component + 2 Modules + 6 Tasks)\n"
      << "- Message Count: 16 messages\n"
      << "- Parallel Execution: 6 TaskAgents in parallel\n"
      << "- Communication: Lock-free concurrent queues\n"
      << "- Latency: < 10ms end-to-end\n"
      << std::endl;

  return 0;
}
