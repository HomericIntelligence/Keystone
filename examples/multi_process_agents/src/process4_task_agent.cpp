#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

#include "data_structures.hpp"
#include "queue_manager.hpp"

using namespace keystone::multi_process;

std::string executeClineSimulated(const TaskItem& task) {
  // Simulated Cline execution for demo purposes
  // In production, this would call: claude --print --agent <type> --task <desc>
  // --files <file>

  std::ostringstream result;
  result << "{\n";
  result << "  \"agent\": \"" << task.agent_type << "\",\n";
  result << "  \"file\": \"" << task.file << "\",\n";
  result << "  \"findings\": [\n";

  // Simulate different findings based on agent type
  if (task.agent_type == "code-review-specialist") {
    result << "    {\"type\": \"info\", \"line\": 42, \"message\": \"Code "
              "structure looks good\"},\n";
    result << "    {\"type\": \"warning\", \"line\": 67, \"message\": "
              "\"Consider error handling\"}\n";
  } else if (task.agent_type == "security-specialist") {
    result << "    {\"type\": \"critical\", \"line\": 23, \"message\": "
              "\"Potential SQL injection risk\"},\n";
    result << "    {\"type\": \"warning\", \"line\": 89, \"message\": \"Use "
              "parameterized queries\"}\n";
  } else if (task.agent_type == "test-engineer") {
    result << "    {\"type\": \"info\", \"message\": \"Test coverage is "
              "adequate\"},\n";
    result << "    {\"type\": \"suggestion\", \"message\": \"Add edge case "
              "tests\"}\n";
  } else {
    result << "    {\"type\": \"info\", \"message\": \"Review completed "
              "successfully\"}\n";
  }

  result << "  ],\n";
  result << "  \"summary\": \"" << task.agent_type << " review completed\"\n";
  result << "}";

  return result.str();
}

int main() {
  QueueManager qm("/tmp/keystone_queues.sock");

  // Wait for server to start
  std::this_thread::sleep_for(std::chrono::milliseconds(2000));

  // Connect to server
  int retries = 5;
  while (retries-- > 0 && !qm.connect()) {
    std::cerr << "TaskAgent: Failed to connect, retrying..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  if (!qm.isConnected()) {
    std::cerr << "TaskAgent: Failed to connect to queue server" << std::endl;
    return 1;
  }

  std::cout << "TaskAgent: Connected to queue server, waiting for tasks..."
            << std::endl;

  int processed = 0;
  int idle_count = 0;
  const int max_idle = 40;  // Exit after 40 idle iterations (8 seconds)

  while (idle_count < max_idle) {
    // Steal task
    auto task = qm.stealTask();
    if (!task) {
      idle_count++;
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      continue;
    }

    idle_count = 0;  // Reset idle count
    processed++;

    std::cout << "TaskAgent: Executing " << task->agent_type << " on "
              << task->file << " (request: " << task->request_id.substr(0, 20)
              << "...)" << std::endl;

    try {
      // Simulate Cline execution (in production: execute claude --print)
      std::string output = executeClineSimulated(*task);

      // Simulate processing time
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      // Create result
      TaskResult result;
      result.request_id = task->request_id;
      result.task_id = task->task_id;
      result.agent_type = task->agent_type;
      result.status = "complete";
      result.output = output;
      result.completed_at = std::chrono::system_clock::now();

      qm.pushTaskResult(result);
      std::cout << "  Result pushed for " << task->file << " ("
                << task->agent_type << ")" << std::endl;

    } catch (const std::exception& e) {
      // Push error result
      TaskResult result;
      result.request_id = task->request_id;
      result.task_id = task->task_id;
      result.agent_type = task->agent_type;
      result.status = "error";
      result.output = std::string("Error: ") + e.what();
      result.completed_at = std::chrono::system_clock::now();

      qm.pushTaskResult(result);
      std::cerr << "  Error processing " << task->file << ": " << e.what()
                << std::endl;
    }
  }

  std::cout << "TaskAgent: Processed " << processed << " tasks, exiting"
            << std::endl;
  qm.disconnect();
  return 0;
}
