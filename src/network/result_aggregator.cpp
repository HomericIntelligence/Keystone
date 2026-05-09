#include "network/result_aggregator.hpp"

#include <algorithm>
#include <sstream>

namespace keystone::network {

ResultAggregator::ResultAggregator(AggregationStrategy strategy,
                                   std::chrono::milliseconds timeout,
                                   size_t expected_count)
    : strategy_(strategy), timeout_(timeout), expected_count_(expected_count) {
  auto now = std::chrono::system_clock::now();
  start_time_ = now;
  deadline_ = now + timeout;
}

bool ResultAggregator::addResult(const std::string& subtask_name,
                                 const hmas::TaskResult& result) {
  // Store the result
  results_[subtask_name] = result;

  // Check if aggregation is now complete
  return isComplete();
}

bool ResultAggregator::isComplete() const {
  switch (strategy_) {
    case AggregationStrategy::WAIT_ALL:
      // All expected results must be received
      return results_.size() >= expected_count_;

    case AggregationStrategy::FIRST_SUCCESS:
      // At least one successful result
      return getSuccessCount() > 0;

    case AggregationStrategy::MAJORITY:
      // Majority of results received
      return results_.size() >= getMajorityThreshold();

    default:
      return false;
  }
}

bool ResultAggregator::hasTimedOut() const {
  return std::chrono::system_clock::now() >= deadline_;
}

std::optional<std::string> ResultAggregator::getAggregatedResult() const {
  if (!isComplete() && !hasTimedOut()) {
    return std::nullopt;
  }

  std::ostringstream oss;

  switch (strategy_) {
    case AggregationStrategy::WAIT_ALL: {
      // Combine all results
      oss << "Aggregated results (" << results_.size() << "/" << expected_count_
          << " subtasks):\n";

      for (const auto& [name, result] : results_) {
        oss << "\n[" << name << "]: ";
        if (result.status() == hmas::TASK_PHASE_COMPLETED) {
          oss << "SUCCESS\n";
          if (!result.result_yaml().empty()) {
            oss << result.result_yaml();
          }
        } else {
          oss << "FAILED\n";
          if (!result.error().empty()) {
            oss << "Error: " << result.error();
          }
        }
      }
      break;
    }

    case AggregationStrategy::FIRST_SUCCESS: {
      // Return first successful result
      for (const auto& [name, result] : results_) {
        if (isSuccessful(result)) {
          oss << "First successful result from: " << name << "\n";
          if (!result.result_yaml().empty()) {
            oss << result.result_yaml();
          }
          break;
        }
      }
      break;
    }

    case AggregationStrategy::MAJORITY: {
      // Return majority results
      size_t success_count = getSuccessCount();
      size_t fail_count = getFailedCount();

      oss << "Majority results (" << results_.size() << "/" << expected_count_
          << " subtasks):\n";
      oss << "Successful: " << success_count << "\n";
      oss << "Failed: " << fail_count << "\n";

      if (success_count >= fail_count) {
        oss << "\nOverall: SUCCESS\n";
        // Include successful results
        for (const auto& [name, result] : results_) {
          if (isSuccessful(result)) {
            oss << "\n[" << name << "]: " << result.result_yaml();
          }
        }
      } else {
        oss << "\nOverall: FAILED\n";
        // Include failures
        for (const auto& [name, result] : results_) {
          if (!isSuccessful(result)) {
            oss << "\n[" << name << "]: " << result.error();
          }
        }
      }
      break;
    }
  }

  return oss.str();
}

std::string ResultAggregator::getAggregatedResultYaml() const {
  std::ostringstream oss;

  oss << "aggregated_results:\n";
  oss << "  strategy: " << strategyToString(strategy_) << "\n";
  oss << "  total_subtasks: " << expected_count_ << "\n";
  oss << "  received_results: " << results_.size() << "\n";
  oss << "  successful: " << getSuccessCount() << "\n";
  oss << "  failed: " << getFailedCount() << "\n";
  oss << "  complete: " << (isComplete() ? "true" : "false") << "\n";
  oss << "  timed_out: " << (hasTimedOut() ? "true" : "false") << "\n";
  oss << "  elapsed_time_ms: " << getElapsedTime().count() << "\n";

  oss << "  subtasks:\n";
  for (const auto& [name, result] : results_) {
    oss << "    - name: " << name << "\n";
    oss << "      status: "
        << (result.status() == hmas::TASK_PHASE_COMPLETED ? "COMPLETED"
                                                          : "FAILED")
        << "\n";

    if (!result.result_yaml().empty()) {
      // Indent YAML result
      std::istringstream result_stream(result.result_yaml());
      std::string line;
      oss << "      result: |\n";
      while (std::getline(result_stream, line)) {
        oss << "        " << line << "\n";
      }
    }

    if (!result.error().empty()) {
      oss << "      error: " << result.error() << "\n";
    }
  }

  return oss.str();
}

size_t ResultAggregator::getSuccessCount() const {
  return std::count_if(
      results_.begin(), results_.end(),
      [this](const auto& pair) { return isSuccessful(pair.second); });
}

size_t ResultAggregator::getFailedCount() const {
  return std::count_if(
      results_.begin(), results_.end(),
      [this](const auto& pair) { return !isSuccessful(pair.second); });
}

std::optional<hmas::TaskResult> ResultAggregator::getResult(
    const std::string& subtask_name) const {
  auto it = results_.find(subtask_name);
  if (it != results_.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::chrono::milliseconds ResultAggregator::getElapsedTime() const {
  auto now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                               start_time_);
}

std::chrono::milliseconds ResultAggregator::getRemainingTime() const {
  auto now = std::chrono::system_clock::now();
  if (now >= deadline_) {
    return std::chrono::milliseconds(0);
  }
  return std::chrono::duration_cast<std::chrono::milliseconds>(deadline_ - now);
}

void ResultAggregator::reset() {
  results_.clear();
  auto now = std::chrono::system_clock::now();
  start_time_ = now;
  deadline_ = now + timeout_;
}

bool ResultAggregator::isSuccessful(const hmas::TaskResult& result) const {
  return result.status() == hmas::TASK_PHASE_COMPLETED;
}

size_t ResultAggregator::getMajorityThreshold() const {
  // Majority = ⌈N/2⌉ + 1
  // For N=5: ⌈5/2⌉ + 1 = 3 + 1 = 4? No, that's supermajority
  // Majority should be > 50%, so ⌈N/2⌉ = ceil(5/2) = 3
  return (expected_count_ / 2) + 1;
}

// Helper functions
AggregationStrategy stringToStrategy(const std::string& strategy_str) {
  if (strategy_str == "WAIT_ALL") {
    return AggregationStrategy::WAIT_ALL;
  } else if (strategy_str == "FIRST_SUCCESS") {
    return AggregationStrategy::FIRST_SUCCESS;
  } else if (strategy_str == "MAJORITY") {
    return AggregationStrategy::MAJORITY;
  }
  return AggregationStrategy::WAIT_ALL;  // Default
}

std::string strategyToString(AggregationStrategy strategy) {
  switch (strategy) {
    case AggregationStrategy::WAIT_ALL:
      return "WAIT_ALL";
    case AggregationStrategy::FIRST_SUCCESS:
      return "FIRST_SUCCESS";
    case AggregationStrategy::MAJORITY:
      return "MAJORITY";
    default:
      return "UNKNOWN";
  }
}

}  // namespace keystone::network
