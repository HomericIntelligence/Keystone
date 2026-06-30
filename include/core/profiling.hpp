#pragma once

#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

namespace keystone {
namespace core {

/**
 * @brief Performance profiling infrastructure for HMAS
 *
 * Phase D: Lightweight profiling system controlled by environment variable.
 *
 * Usage:
 * @code
 * // Enable profiling: export KEYSTONE_PROFILE=1
 *
 * void processMessage(const KeystoneMessage& msg) {
 *     auto session = ProfilingSession::start("processMessage");
 *     // ... do work ...
 *     session.end();  // Or let RAII handle it
 * }
 * @endcode
 *
 * Features:
 * - Zero overhead when disabled (environment variable check)
 * - RAII-based automatic session end
 * - Per-section duration histograms
 * - Thread-safe recording
 * - Generate performance reports
 */
class ProfilingSession {
 public:
  /**
   * @brief Start a profiling session for a named code section
   * @param section_name Name of the code section being profiled
   * @return Session object (use RAII or call end() manually)
   *
   * If KEYSTONE_PROFILE environment variable is not set to "1",
   * this returns an inactive session (zero overhead).
   */
  static ProfilingSession start(const std::string& section_name);

  /**
   * @brief End the profiling session and record duration
   *
   * Called automatically by destructor if not called manually.
   * Safe to call multiple times (subsequent calls ignored).
   */
  void end();

  /**
   * @brief Check if profiling is globally enabled
   * @return true if KEYSTONE_PROFILE=1 environment variable is set
   */
  static bool isEnabled();

  /**
   * @brief Get profiling statistics for a named section
   * @param section_name Name of the profiled section
   * @return Stats if section has been profiled, nullopt otherwise
   */
  struct SectionStats {
    size_t sample_count;  ///< Number of samples
    double min_us;        ///< Minimum duration (microseconds)
    double max_us;        ///< Maximum duration (microseconds)
    double mean_us;       ///< Mean duration (microseconds)
    double p50_us;        ///< 50th percentile (median)
    double p95_us;        ///< 95th percentile
    double p99_us;        ///< 99th percentile
  };
  static std::optional<SectionStats> getStats(const std::string& section_name);

  /**
   * @brief Generate human-readable profiling report
   * @return Formatted report string with all profiled sections
   */
  static std::string generateReport();

  /**
   * @brief Reset all profiling data
   */
  static void reset();

  // RAII: Destructor ends session if not already ended
  ~ProfilingSession();

  // Movable but not copyable
  ProfilingSession(ProfilingSession&& other) noexcept;
  ProfilingSession& operator=(ProfilingSession&& other) noexcept;
  ProfilingSession(const ProfilingSession&) = delete;
  ProfilingSession& operator=(const ProfilingSession&) = delete;

 private:
  // Private constructor - use start() factory method
  ProfilingSession(const std::string& section_name, bool enabled);

  std::string section_name_;
  std::chrono::steady_clock::time_point start_time_;
  bool enabled_;
  bool ended_;

  // Global profiling state
  static bool checkEnabled();
  static void recordDuration(const std::string& section_name, double duration_us);

  // Data storage
  struct SectionData {
    std::vector<double> durations_us;  ///< All recorded durations
    std::mutex mutex;                  ///< Protect concurrent access
  };

  static std::map<std::string, SectionData>& getSectionData();
  static std::shared_mutex& getGlobalMutex();

  // Internal version that assumes global mutex already held (shared or unique)
  static std::optional<SectionStats> getStatsUnlocked(const std::string& section_name);
};

}  // namespace core
}  // namespace keystone
