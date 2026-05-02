/**
 * @file 3layer_example.cpp
 * @brief Network-Based 3-Layer Example: Distributed Coordination
 *
 * This example extends the 2-layer network pattern to include ModuleLead.
 * Five agents run on separate machines:
 * - Machine A: Chief
 * - Machine B: ModuleLead
 * - Machine C: TaskAgent #1
 * - Machine D: TaskAgent #2
 * - Machine E: TaskAgent #3
 * - Machine F: ServiceRegistry
 *
 * Workflow:
 * 1. All agents register with ServiceRegistry
 * 2. Chief queries for ModuleLead
 * 3. Chief sends goal to ModuleLead via gRPC
 * 4. ModuleLead queries for TaskAgents
 * 5. ModuleLead sends tasks to 3 TaskAgents (parallel gRPC calls)
 * 6. TaskAgents execute and respond
 * 7. ModuleLead synthesizes results
 * 8. ModuleLead sends synthesis to Chief
 *
 * Note: This is a documentation template showing the architecture.
 *       Full implementation extends 2layer_example.cpp patterns.
 */

#ifdef ENABLE_GRPC

#include <iostream>

int main() {
  std::cout << "\n"
            << "═══════════════════════════════════════════════════════\n"
            << "  Network 3-Layer Example: Distributed Coordination\n"
            << "═══════════════════════════════════════════════════════\n"
            << "\nMachine Layout:\n"
            << "  Machine A (192.168.1.10):  Chief\n"
            << "  Machine B (192.168.1.11):  ModuleLead\n"
            << "  Machine C (192.168.1.12):  TaskAgent #1\n"
            << "  Machine D (192.168.1.13):  TaskAgent #2\n"
            << "  Machine E (192.168.1.14):  TaskAgent #3\n"
            << "  Machine F (192.168.1.20):  ServiceRegistry\n"
            << "\nDeployment:\n"
            << "  # Machine F:\n"
            << "  ./service_registry --port=50051\n"
            << "\n  # Machines C, D, E:\n"
            << "  ./network_3layer --role=task --id=1 \\\n"
            << "    --address=192.168.1.12:50100 \\\n"
            << "    --registry=192.168.1.20:50051\n"
            << "\n  # Machine B:\n"
            << "  ./network_3layer --role=module \\\n"
            << "    --address=192.168.1.11:50200 \\\n"
            << "    --registry=192.168.1.20:50051\n"
            << "\n  # Machine A:\n"
            << "  ./network_3layer --role=chief \\\n"
            << "    --address=192.168.1.10:50300 \\\n"
            << "    --registry=192.168.1.20:50051\n"
            << "\nExpected Flow:\n"
            << " 1. TaskAgents register with ServiceRegistry\n"
            << " 2. ModuleLead registers with ServiceRegistry\n"
            << " 3. Chief queries for ModuleLead capability\n"
            << " 4. Chief → ModuleLead: Calculate 10+20+30 (gRPC)\n"
            << " 5. ModuleLead queries for TaskAgent capability\n"
            << " 6. ModuleLead decomposes into 3 tasks\n"
            << " 7. ModuleLead → Task1: echo 10 (gRPC)\n"
            << " 8. ModuleLead → Task2: echo 20 (gRPC)\n"
            << " 9. ModuleLead → Task3: echo 30 (gRPC)\n"
            << "10. TaskAgents execute in parallel\n"
            << "11. Task1 → ModuleLead: 10 (gRPC response)\n"
            << "12. Task2 → ModuleLead: 20 (gRPC response)\n"
            << "13. Task3 → ModuleLead: 30 (gRPC response)\n"
            << "14. ModuleLead synthesizes: Sum = 60\n"
            << "15. ModuleLead → Chief: Synthesis (gRPC response)\n"
            << "16. Chief: Final result = 60\n"
            << "\nImplementation extends 2layer_example.cpp:\n"
            << "  - Add ModuleLead gRPC service\n"
            << "  - Implement parallel task delegation\n"
            << "  - Add result synthesis logic\n"
            << "  - Use load balancing (ROUND_ROBIN)\n"
            << "═══════════════════════════════════════════════════════\n"
            << std::endl;

  return 0;
}

#else  // !ENABLE_GRPC

int main() {
  std::cout << "Network examples require gRPC. Build with -DENABLE_GRPC=ON\n";
  return 1;
}

#endif  // ENABLE_GRPC
