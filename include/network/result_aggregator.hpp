#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "hmas_coordinator.pb.h"

namespace keystone {
namespace network {

/// Aggregation strategy for subtask results
enum class AggregationStrategy {
  WAIT_ALL,       // Wait for all subtasks to complete
  FIRST_SUCCESS,  // Return on first successful result
  MAJORITY        // Wait for majority (⌈N/2⌉ + 1) of results
};

/// Result aggregator for combining subtask results
class ResultAggregator {
 public:
  /// Constructor
  /// @param strategy Aggregation strategy
  /// @param timeout Timeout duration
  /// @param expected_count Number of expected subtask results
  ResultAggregator(AggregationStrategy strategy,
                   std::chrono::milliseconds timeout, size_t expected_count);

  /// Add a subtask result
  /// @param subtask_name Name of the subtask
  /// @param result Task result to add
  /// @return true if aggregation is now complete, false otherwise
  bool addResult(const std::string& subtask_name,
                 const hmas::TaskResult& result);

  /// Check if aggregation is complete
  /// @return true if enough results received per strategy
  bool isComplete() const;

  /// Check if aggregation has timed out
  /// @return true if current time exceeds deadline
  bool hasTimedOut() const;

  /// Get aggregated result
  /// @return Aggregated result string, or nullopt if not complete
  std::optional<std::string> getAggregatedResult() const;

  /// Get aggregated result YAML
  /// @return Aggregated result as YAML string
  std::string getAggregatedResultYaml() const;

  /// Get number of results received
  size_t getResultCount() const { return results_.size(); }

  /// Get number of successful results
  size_t getSuccessCount() const;

  /// Get number of failed results
  size_t getFailedCount() const;

  /// Get expected count
  size_t getExpectedCount() const { return expected_count_; }

  /// Get strategy
  AggregationStrategy getStrategy() const { return strategy_; }

  /// Get individual result
  /// @param subtask_name Name of subtask
  /// @return Result if found, nullopt otherwise
  std::optional<hmas::TaskResult> getResult(
      const std::string& subtask_name) const;

  /// Get all results
  /// @return Map of subtask_name -> TaskResult
  const std::unordered_map<std::string, hmas::TaskResult>& getAllResults()
      const {
    return results_;
  }

  /// Get elapsed time since creation
  std::chrono::milliseconds getElapsedTime() const;

  /// Get remaining time until timeout
  std::chrono::milliseconds getRemainingTime() const;

  /// Reset aggregator (clear all results, restart timer)
  void reset();

 private:
  /// Check if a result is successful
  bool isSuccessful(const hmas::TaskResult& result) const;

  /// Calculate majority threshold
  size_t getMajorityThreshold() const;

  AggregationStrategy strategy_;
  std::chrono::milliseconds timeout_;
  std::chrono::system_clock::time_point deadline_;
  std::chrono::system_clock::time_point start_time_;
  size_t expected_count_;
  std::unordered_map<std::string, hmas::TaskResult> results_;
};

/// Helper function to convert string to AggregationStrategy
AggregationStrategy stringToStrategy(const std::string& strategy_str);

/// Helper function to convert AggregationStrategy to string
std::string strategyToString(AggregationStrategy strategy);

}  // namespace network
}  // namespace keystone
