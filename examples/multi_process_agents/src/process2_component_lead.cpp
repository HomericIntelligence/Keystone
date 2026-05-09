#include <chrono>
#include <iostream>
#include <thread>

#include "data_structures.hpp"
#include "queue_manager.hpp"

using namespace keystone::multi_process;

std::vector<ModuleTask> decomposeToModules(const DomainTask& domain_task) {
  std::vector<ModuleTask> tasks;

  // Group files by module (simplified: one file per module for demo)
  for (const auto& file : domain_task.files) {
    // Extract module name from file path (e.g., "src/auth/login.cpp" ->
    // "login")
    std::string module_name = file;
    size_t last_slash = file.find_last_of('/');
    if (last_slash != std::string::npos) {
      module_name = file.substr(last_slash + 1);
    }
    size_t last_dot = module_name.find_last_of('.');
    if (last_dot != std::string::npos) {
      module_name = module_name.substr(0, last_dot);
    }

    ModuleTask task;
    task.request_id = domain_task.request_id;
    task.task_id = domain_task.task_id + "_module_" + module_name;
    task.domain = domain_task.domain;
    task.module = module_name;
    task.files = {file};
    task.description = "Review " + module_name + " module";
    task.created_at = std::chrono::system_clock::now();
    tasks.push_back(task);
  }

  return tasks;
}

DomainResult aggregateModuleResults(
    const DomainTask& domain_task,
    const std::vector<ModuleResult>& module_results) {
  DomainResult result;
  result.request_id = domain_task.request_id;
  result.task_id = domain_task.task_id;
  result.domain = domain_task.domain;
  result.module_results = module_results;
  result.summary = "Reviewed " + std::to_string(module_results.size()) +
                   " modules successfully";
  result.completed_at = std::chrono::system_clock::now();
  return result;
}

int main() {
  QueueManager qm("/tmp/keystone_queues.sock");

  // Wait for server to start
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // Connect to server
  int retries = 5;
  while (retries-- > 0 && !qm.connect()) {
    std::cerr << "ComponentLead: Failed to connect, retrying..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  if (!qm.isConnected()) {
    std::cerr << "ComponentLead: Failed to connect to queue server"
              << std::endl;
    return 1;
  }

  std::cout
      << "ComponentLead: Connected to queue server, waiting for domain tasks..."
      << std::endl;

  int processed = 0;
  int idle_count = 0;
  const int max_idle = 20;  // Exit after 20 idle iterations (4 seconds)

  while (idle_count < max_idle) {
    // Steal domain task
    auto domain_task = qm.stealDomain();
    if (!domain_task) {
      idle_count++;
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      continue;
    }

    idle_count = 0;  // Reset idle count
    processed++;

    std::cout << "ComponentLead: Processing domain " << domain_task->domain
              << " (request: " << domain_task->request_id.substr(0, 20)
              << "...)" << std::endl;

    // Decompose into modules
    auto module_tasks = decomposeToModules(*domain_task);
    std::cout << "  Decomposed into " << module_tasks.size() << " modules"
              << std::endl;

    // Push module tasks
    for (const auto& task : module_tasks) {
      qm.pushModule(task);
      std::cout << "  Pushed module: " << task.module << std::endl;
    }

    // Wait for module results
    std::vector<ModuleResult> module_results;
    while (module_results.size() < module_tasks.size()) {
      auto result = qm.pollModuleResult();
      if (result && result->request_id == domain_task->request_id) {
        module_results.push_back(*result);
        std::cout << "  Received module result: " << result->module
                  << std::endl;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Aggregate and push domain result
    DomainResult domain_result =
        aggregateModuleResults(*domain_task, module_results);
    qm.pushDomainResult(domain_result);

    std::cout << "ComponentLead: Completed domain " << domain_task->domain
              << std::endl;
  }

  std::cout << "ComponentLead: Processed " << processed << " domains, exiting"
            << std::endl;
  qm.disconnect();
  return 0;
}
