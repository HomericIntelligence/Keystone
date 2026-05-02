#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

#include "data_structures.hpp"
#include "queue_manager.hpp"

using namespace keystone::multi_process;

std::vector<std::string> determineReviewTypes(const ModuleTask& task) {
  std::vector<std::string> types;

  // Always code review
  types.push_back("code-review-specialist");

  // Check file patterns for additional reviews
  for (const auto& file : task.files) {
    // Security-sensitive files
    if (file.find("auth") != std::string::npos ||
        file.find("crypto") != std::string::npos ||
        file.find("security") != std::string::npos) {
      if (std::find(types.begin(), types.end(), "security-specialist") ==
          types.end()) {
        types.push_back("security-specialist");
      }
    }

    // Test files
    if (file.find("test") != std::string::npos ||
        file.find("Test") != std::string::npos) {
      if (std::find(types.begin(), types.end(), "test-engineer") ==
          types.end()) {
        types.push_back("test-engineer");
      }
    }

    // Documentation files
    if (file.find(".md") != std::string::npos ||
        file.find("README") != std::string::npos ||
        file.find("doc") != std::string::npos) {
      if (std::find(types.begin(), types.end(), "documentation-engineer") ==
          types.end()) {
        types.push_back("documentation-engineer");
      }
    }
  }

  return types;
}

ModuleResult aggregateTaskResults(const ModuleTask& module_task,
                                  const std::vector<TaskResult>& task_results) {
  ModuleResult result;
  result.request_id = module_task.request_id;
  result.task_id = module_task.task_id;
  result.module = module_task.module;
  result.task_results = task_results;

  int completed = 0;
  for (const auto& tr : task_results) {
    if (tr.status == "complete") completed++;
  }

  result.summary = std::to_string(completed) + "/" +
                   std::to_string(task_results.size()) +
                   " reviews completed successfully";
  result.completed_at = std::chrono::system_clock::now();
  return result;
}

int main() {
  QueueManager qm("/tmp/keystone_queues.sock");

  // Wait for server to start
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));

  // Connect to server
  int retries = 5;
  while (retries-- > 0 && !qm.connect()) {
    std::cerr << "ModuleLead: Failed to connect, retrying..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  if (!qm.isConnected()) {
    std::cerr << "ModuleLead: Failed to connect to queue server" << std::endl;
    return 1;
  }

  std::cout
      << "ModuleLead: Connected to queue server, waiting for module tasks..."
      << std::endl;

  int processed = 0;
  int idle_count = 0;
  const int max_idle = 30;  // Exit after 30 idle iterations (6 seconds)

  while (idle_count < max_idle) {
    // Steal module task
    auto module_task = qm.stealModule();
    if (!module_task) {
      idle_count++;
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      continue;
    }

    idle_count = 0;  // Reset idle count
    processed++;

    std::cout << "ModuleLead: Processing module " << module_task->module
              << " in domain " << module_task->domain
              << " (request: " << module_task->request_id.substr(0, 20)
              << "...)" << std::endl;

    // Determine required review types
    auto review_types = determineReviewTypes(*module_task);
    std::cout << "  Selected " << review_types.size() << " review types: ";
    for (size_t i = 0; i < review_types.size(); ++i) {
      std::cout << review_types[i];
      if (i < review_types.size() - 1) std::cout << ", ";
    }
    std::cout << std::endl;

    // Create task items for each file + review type
    std::vector<TaskItem> task_items;
    for (const auto& file : module_task->files) {
      for (const auto& agent_type : review_types) {
        TaskItem item;
        item.request_id = module_task->request_id;
        item.task_id = module_task->task_id + "_task_" + agent_type;
        item.file = file;
        item.agent_type = agent_type;
        item.task_description = "Review " + file + " with " + agent_type;
        item.created_at = std::chrono::system_clock::now();

        task_items.push_back(item);
        qm.pushTask(item);
        std::cout << "  Pushed task: " << file << " -> " << agent_type
                  << std::endl;
      }
    }

    // Wait for task results
    std::vector<TaskResult> task_results;
    while (task_results.size() < task_items.size()) {
      auto result = qm.pollTaskResult();
      if (result && result->request_id == module_task->request_id) {
        task_results.push_back(*result);
        std::cout << "  Received task result: " << result->agent_type << " ("
                  << result->status << ")" << std::endl;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Aggregate and push module result
    ModuleResult module_result =
        aggregateTaskResults(*module_task, task_results);
    qm.pushModuleResult(module_result);

    std::cout << "ModuleLead: Completed module " << module_task->module
              << std::endl;
  }

  std::cout << "ModuleLead: Processed " << processed << " modules, exiting"
            << std::endl;
  qm.disconnect();
  return 0;
}
