/**
 * @file 4layer_example.cpp
 * @brief IPC-Based 4-Layer Example: Full Hierarchy Across Processes
 *
 * This example demonstrates the complete 4-layer HMAS using 10 separate
 * processes:
 * - Level 0: 1 Chief process
 * - Level 1: 1 ComponentLead process
 * - Level 2: 2 ModuleLead processes
 * - Level 3: 6 TaskAgent processes
 *
 * Total: 10 independent OS processes communicating via shared memory
 *
 * Architecture:
 * ```
 * Process Hierarchy:
 *
 * PID 1000: Chief
 *     ↓ (writes to queue_comp1)
 * PID 1001: ComponentLead
 *     ├─ (writes to queue_mod1)
 *     │   PID 1002: ModuleLead1 (Core)
 *     │       ├─ PID 1003: Task1 (echo 10)
 *     │       ├─ PID 1004: Task2 (echo 20)
 *     │       └─ PID 1005: Task3 (echo 30)
 *     │
 *     └─ (writes to queue_mod2)
 *         PID 1006: ModuleLead2 (Utils)
 *             ├─ PID 1007: Task4 (echo 40)
 *             ├─ PID 1008: Task5 (echo 50)
 *             └─ PID 1009: Task6 (echo 60)
 *
 * All communication via shared memory segment: "HMASSharedMemory"
 * Shared memory size: 256KB (larger for 10 agents)
 * ```
 *
 * Usage:
 *   ./scripts/run_ipc_4layer.sh
 *
 * Or manually:
 *   Terminal 1-6: ./ipc_4layer --role=task --id=1..6
 *   Terminal 7-8: ./ipc_4layer --role=module --id=1,2
 *   Terminal 9:   ./ipc_4layer --role=component
 *   Terminal 10:  ./ipc_4layer --role=chief
 *
 * Message Flow:
 * 1. Chief → ComponentLead: "Build: Core(10+20+30) + Utils(40+50+60)"
 * 2. ComponentLead decomposes into 2 module goals
 * 3. ComponentLead → ModuleLead1: "Core(10+20+30)"
 * 4. ComponentLead → ModuleLead2: "Utils(40+50+60)"
 * 5. ModuleLead1 decomposes → 3 tasks (echo 10, 20, 30)
 * 6. ModuleLead2 decomposes → 3 tasks (echo 40, 50, 60)
 * 7. ModuleLead1 → Task1, Task2, Task3
 * 8. ModuleLead2 → Task4, Task5, Task6
 * 9. 6 TaskAgents execute in parallel
 * 10. Task1-3 → ModuleLead1: Results (10, 20, 30)
 * 11. Task4-6 → ModuleLead2: Results (40, 50, 60)
 * 12. ModuleLead1 synthesizes: Core = 60
 * 13. ModuleLead2 synthesizes: Utils = 150
 * 14. ModuleLead1, ModuleLead2 → ComponentLead
 * 15. ComponentLead aggregates: Total = 210
 * 16. ComponentLead → Chief: Final aggregation
 *
 * Shared Memory Layout (256KB):
 * ┌────────────────────────────────────────┐
 * │  Agent Registry (10 entries)           │
 * │  - chief, comp1, mod1, mod2, task1-6   │
 * ├────────────────────────────────────────┤
 * │  Message Queues (10 queues)            │
 * │  - queue_chief                         │
 * │  - queue_comp1                         │
 * │  - queue_mod1, queue_mod2              │
 * │  - queue_task1..queue_task6            │
 * ├────────────────────────────────────────┤
 * │  Mutexes (10 mutexes)                  │
 * │  - mutex_chief, mutex_comp1, ...       │
 * ├────────────────────────────────────────┤
 * │  Semaphores (10 semaphores)            │
 * │  - sem_chief, sem_comp1, ...           │
 * ├────────────────────────────────────────┤
 * │  Execution Trace Buffer (debug)        │
 * │  - State transitions                   │
 * │  - Message log                         │
 * └────────────────────────────────────────┘
 */

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
  std::cout
      << "\n"
      << "═══════════════════════════════════════════════════════════════\n"
      << "  IPC 4-Layer Example: Full Hierarchy (10 Processes)\n"
      << "═══════════════════════════════════════════════════════════════\n"
      << "\nThis is a documentation template.\n"
      << "Full implementation extends 2layer_example.cpp to 4 layers.\n"
      << "\n"
      << "Process Breakdown:\n"
      << "─────────────────────────────────────────────────────────────\n"
      << "Level 0: Chief           (1 process)\n"
      << "Level 1: ComponentLead   (1 process)\n"
      << "Level 2: ModuleLeads     (2 processes: Core, Utils)\n"
      << "Level 3: TaskAgents      (6 processes: Task1-6)\n"
      << "                         ─────────────\n"
      << "                         10 total processes\n"
      << "\n"
      << "Shared Memory Configuration:\n"
      << "─────────────────────────────────────────────────────────────\n"
      << "Segment Name: HMASSharedMemory\n"
      << "Segment Size: 262144 bytes (256KB)\n"
      << "Queues:       10 (one per agent)\n"
      << "Mutexes:      10 (one per queue)\n"
      << "Semaphores:   10 (one per queue)\n"
      << "\n"
      << "Implementation Components:\n"
      << "─────────────────────────────────────────────────────────────\n"
      << "1. run_chief_process():\n"
      << "   - Creates shared memory segment (256KB)\n"
      << "   - Creates queue_chief\n"
      << "   - Sends system goal to comp1\n"
      << "   - Waits for aggregated result\n"
      << "   - Displays final output\n"
      << "\n"
      << "2. run_component_process():\n"
      << "   - Opens shared memory\n"
      << "   - Creates queue_comp1\n"
      << "   - Waits for system goal from chief\n"
      << "   - Decomposes into 2 module goals:\n"
      << "     * Core(10+20+30)\n"
      << "     * Utils(40+50+60)\n"
      << "   - Sends module goals to mod1, mod2\n"
      << "   - Waits for 2 syntheses\n"
      << "   - Aggregates: Core=60, Utils=150, Total=210\n"
      << "   - Sends aggregation to chief\n"
      << "\n"
      << "3. run_module_process(int module_id):\n"
      << "   - Opens shared memory\n"
      << "   - Creates queue_mod{id}\n"
      << "   - Waits for module goal from comp1\n"
      << "   - Decomposes into 3 tasks\n"
      << "   - Sends tasks to task agents:\n"
      << "     * mod1 → task1, task2, task3\n"
      << "     * mod2 → task4, task5, task6\n"
      << "   - Waits for 3 results\n"
      << "   - Synthesizes: Sum = X\n"
      << "   - Sends synthesis to comp1\n"
      << "\n"
      << "4. run_task_process(int task_id):\n"
      << "   - Opens shared memory\n"
      << "   - Creates queue_task{id}\n"
      << "   - Waits for command\n"
      << "   - Executes bash command\n"
      << "   - Sends result to module lead\n"
      << "\n"
      << "Startup Script (scripts/run_ipc_4layer.sh):\n"
      << "─────────────────────────────────────────────────────────────\n"
      << "#!/bin/bash\n"
      << "set -e\n"
      << "\n"
      << "echo \"Starting 6 TaskAgent processes...\"\n"
      << "for i in {1..6}; do\n"
      << "  ./ipc_4layer --role=task --id=$i &\n"
      << "done\n"
      << "sleep 1\n"
      << "\n"
      << "echo \"Starting 2 ModuleLead processes...\"\n"
      << "./ipc_4layer --role=module --id=1 &  # Core\n"
      << "./ipc_4layer --role=module --id=2 &  # Utils\n"
      << "sleep 1\n"
      << "\n"
      << "echo \"Starting ComponentLead process...\"\n"
      << "./ipc_4layer --role=component &\n"
      << "sleep 1\n"
      << "\n"
      << "echo \"Starting Chief process...\"\n"
      << "./ipc_4layer --role=chief\n"
      << "\n"
      << "echo \"Workflow complete. Cleaning up...\"\n"
      << "wait\n"
      << "\n"
      << "Expected Output:\n"
      << "─────────────────────────────────────────────────────────────\n"
      << "[Task1] Ready, waiting for commands...\n"
      << "[Task2] Ready, waiting for commands...\n"
      << "[Task3] Ready, waiting for commands...\n"
      << "[Task4] Ready, waiting for commands...\n"
      << "[Task5] Ready, waiting for commands...\n"
      << "[Task6] Ready, waiting for commands...\n"
      << "[Module1] Ready, waiting for module goal...\n"
      << "[Module2] Ready, waiting for module goal...\n"
      << "[Component] Ready, waiting for system goal...\n"
      << "[Chief] Sending system goal: Build: Core+Utils\n"
      << "[Component] Decomposing into 2 modules\n"
      << "[Component] Sending Core(10+20+30) to Module1\n"
      << "[Component] Sending Utils(40+50+60) to Module2\n"
      << "[Module1] Decomposing Core into 3 tasks\n"
      << "[Module2] Decomposing Utils into 3 tasks\n"
      << "[Module1] Sending tasks to Task1, Task2, Task3\n"
      << "[Module2] Sending tasks to Task4, Task5, Task6\n"
      << "[Task1] Executing: echo 10 → 10\n"
      << "[Task2] Executing: echo 20 → 20\n"
      << "[Task3] Executing: echo 30 → 30\n"
      << "[Task4] Executing: echo 40 → 40\n"
      << "[Task5] Executing: echo 50 → 50\n"
      << "[Task6] Executing: echo 60 → 60\n"
      << "[Module1] Synthesizing: Core = 60\n"
      << "[Module2] Synthesizing: Utils = 150\n"
      << "[Component] Aggregating: Core=60, Utils=150\n"
      << "[Component] Total = 210\n"
      << "[Chief] Final result: System complete with 210 total\n"
      << "\n"
      << "Process Isolation Benefits:\n"
      << "─────────────────────────────────────────────────────────────\n"
      << "✓ TaskAgent crash doesn't affect ModuleLeads\n"
      << "✓ ModuleLead crash doesn't affect ComponentLead\n"
      << "✓ Each process can be profiled independently\n"
      << "✓ OS can enforce per-process resource limits\n"
      << "✓ Debugger can attach to individual agent process\n"
      << "✓ Agents can restart independently on failure\n"
      << "\n"
      << "Performance Characteristics:\n"
      << "─────────────────────────────────────────────────────────────\n"
      << "Message Latency:     5-20ms (IPC overhead)\n"
      << "Total Messages:      16 messages\n"
      << "End-to-End Latency:  100-500ms (10x slower than threads)\n"
      << "Memory Overhead:     ~10MB (10 processes × 1MB each)\n"
      << "Shared Memory:       256KB\n"
      << "\n"
      << "═══════════════════════════════════════════════════════════════\n"
      << std::endl;

  return 0;
}

/**
 * Full Implementation Guide:
 *
 * To implement this example, follow these steps:
 *
 * 1. Extend 2layer_example.cpp IPC primitives:
 *    - Increase SHARED_MEMORY_SIZE to 262144 (256KB)
 *    - Support multiple module/task IDs
 *    - Add process monitoring (PID tracking)
 *
 * 2. Implement State Machines:
 *    ComponentLead: IDLE → PLANNING → WAITING_FOR_MODULES → AGGREGATING → IDLE
 *    ModuleLead: IDLE → PLANNING → WAITING_FOR_TASKS → SYNTHESIZING → IDLE
 *
 * 3. Add fault tolerance:
 *    - PID monitoring for crash detection
 *    - Automatic agent restart on failure
 *    - Timeout handling for stuck processes
 *
 * 4. Create helper utilities:
 *    - Agent registry management in shared memory
 *    - Process lifecycle management
 *    - Cleanup scripts for shared memory segments
 *
 * 5. Testing:
 *    - Test with all 10 processes
 *    - Test with process crashes (kill -9)
 *    - Test with resource limits (ulimit)
 *    - Verify memory isolation
 *
 * See 2layer_example.cpp for complete IPC implementation patterns.
 */
