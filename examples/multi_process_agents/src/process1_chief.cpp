#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>

#include "data_structures.hpp"
#include "queue_manager.hpp"
#include "utils.hpp"

using namespace keystone::multi_process;
using json = nlohmann::json;

std::vector<DomainTask> decomposePR(const json& pr,
                                    const std::string& request_id) {
  std::vector<DomainTask> tasks;

  for (const auto& domain_info : pr["domains"]) {
    DomainTask task;
    task.request_id = request_id;
    task.task_id =
        request_id + "_domain_" + domain_info["name"].get<std::string>();
    task.domain = domain_info["name"];
    task.files = domain_info["files"].get<std::vector<std::string>>();
    task.description = "Review " + task.domain + " domain";
    task.created_at = std::chrono::system_clock::now();
    tasks.push_back(task);
  }

  return tasks;
}

void outputFinalReport(const std::string& request_id,
                       const std::vector<DomainResult>& results) {
  std::cout << "\n=== Final Code Review Report ===" << std::endl;
  std::cout << "Request ID: " << request_id << std::endl;
  std::cout << "Domains reviewed: " << results.size() << std::endl;

  for (const auto& domain_result : results) {
    std::cout << "\nDomain: " << domain_result.domain << std::endl;
    std::cout << "  Modules: " << domain_result.module_results.size()
              << std::endl;

    int total_tasks = 0;
    for (const auto& module_result : domain_result.module_results) {
      total_tasks += module_result.task_results.size();
    }
    std::cout << "  Total reviews: " << total_tasks << std::endl;
    std::cout << "  Status: " << domain_result.summary << std::endl;
  }

  std::cout << "\n=== Review Complete ===" << std::endl;
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <pr_json_file>" << std::endl;
    return 1;
  }

  // Read PR JSON
  std::ifstream file(argv[1]);
  if (!file.is_open()) {
    std::cerr << "Failed to open " << argv[1] << std::endl;
    return 1;
  }

  json pr;
  file >> pr;
  file.close();

  // Generate request ID
  std::string request_id = generateRequestId();
  std::cout << "ChiefArchitect: Starting request " << request_id << std::endl;
  std::cout << "PR #" << pr["pr_id"] << ": " << pr["title"] << std::endl;

  // Connect to queues
  QueueManager qm("/tmp/keystone_queues.sock");

  try {
    qm.startServer();
    std::cout << "ChiefArchitect: Queue server started" << std::endl;
  } catch (const std::exception& e) {
    std::cerr << "Failed to start server: " << e.what() << std::endl;
    return 1;
  }

  // Wait a bit for workers to connect
  std::cout << "ChiefArchitect: Waiting for workers to connect..." << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(3));

  // Decompose PR into domains
  auto domain_tasks = decomposePR(pr, request_id);
  std::cout << "ChiefArchitect: Decomposed PR into " << domain_tasks.size()
            << " domains" << std::endl;

  // Push domain tasks
  for (const auto& task : domain_tasks) {
    qm.pushDomain(task);
    std::cout << "  Pushed domain: " << task.domain << " (" << task.files.size()
              << " files)" << std::endl;
  }

  // Wait for results
  std::cout << "ChiefArchitect: Waiting for results..." << std::endl;
  std::vector<DomainResult> results;

  while (results.size() < domain_tasks.size()) {
    auto result = qm.pollDomainResult();
    if (result) {
      results.push_back(*result);
      std::cout << "  Received result for domain: " << result->domain
                << std::endl;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  // Output final report
  outputFinalReport(request_id, results);

  qm.stopServer();
  return 0;
}
