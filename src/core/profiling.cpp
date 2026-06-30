#include "core/profiling.hpp"

#include <algorithm>
#include <cstdlib>  // For getenv
#include <iomanip>
#include <sstream>

namespace keystone {
namespace core {

// Static helper to get global section data
std::map<std::string, ProfilingSession::SectionData>& ProfilingSession::getSectionData() {
  static std::map<std::string, SectionData> data;
  return data;
}

std::shared_mutex& ProfilingSession::getGlobalMutex() {
  static std::shared_mutex mutex;
  return mutex;
}

bool ProfilingSession::checkEnabled() {
  static bool enabled = []() {
    const char* env = std::getenv("KEYSTONE_PROFILE");
    return env != nullptr && std::string(env) == "1";
  }();
  return enabled;
}

bool ProfilingSession::isEnabled() {
  return checkEnabled();
}

ProfilingSession::ProfilingSession(const std::string& section_name, bool enabled)
    : section_name_(section_name),
      start_time_(std::chrono::steady_clock::now()),
      enabled_(enabled),
      ended_(false) {}

ProfilingSession ProfilingSession::start(const std::string& section_name) {
  return ProfilingSession(section_name, checkEnabled());
}

void ProfilingSession::end() {
  if (!enabled_ || ended_) {
    return;  // Already ended or profiling disabled
  }

  ended_ = true;

  auto end_time = std::chrono::steady_clock::now();
  auto duration_us = std::chrono::duration<double, std::micro>(end_time - start_time_).count();

  recordDuration(section_name_, duration_us);
}

ProfilingSession::~ProfilingSession() {
  end();  // Safe to call multiple times
}

ProfilingSession::ProfilingSession(ProfilingSession&& other) noexcept
    : section_name_(std::move(other.section_name_)),
      start_time_(other.start_time_),
      enabled_(other.enabled_),
      ended_(other.ended_) {
  other.ended_ = true;  // Prevent double-end
}

ProfilingSession& ProfilingSession::operator=(ProfilingSession&& other) noexcept {
  if (this != &other) {
    end();  // End current session
    section_name_ = std::move(other.section_name_);
    start_time_ = other.start_time_;
    enabled_ = other.enabled_;
    ended_ = other.ended_;
    other.ended_ = true;
  }
  return *this;
}

void ProfilingSession::recordDuration(const std::string& section_name, double duration_us) {
  // SECURITY FIX: Use-after-free prevention
  // Hold shared_lock during entire section access to prevent map rehashing
  // which would invalidate section pointers.

  // First, check if section exists (read-only, shared lock)
  {
    std::shared_lock<std::shared_mutex> global_lock(getGlobalMutex());
    auto& data = getSectionData();
    auto it = data.find(section_name);

    if (it != data.end()) {
      // Section exists - record duration while holding shared_lock
      // This prevents map modifications (including rehashing)
      std::lock_guard<std::mutex> section_lock(it->second.mutex);
      it->second.durations_us.push_back(duration_us);
      return;
    }
  }  // Release shared_lock

  // Section doesn't exist - need write lock to create it
  {
    std::unique_lock<std::shared_mutex> global_lock(getGlobalMutex());
    auto& data = getSectionData();

    // Double-check after acquiring write lock (another thread may have created
    // it)
    auto it = data.find(section_name);
    if (it == data.end()) {
      // Create new section
      // Use piecewise_construct because SectionData contains non-movable
      // std::mutex
      it = data.emplace(std::piecewise_construct,
                        std::forward_as_tuple(section_name),
                        std::forward_as_tuple())
               .first;
    }

    // Record duration while still holding unique_lock
    // Safe: map cannot be modified by other threads
    std::lock_guard<std::mutex> section_lock(it->second.mutex);
    it->second.durations_us.push_back(duration_us);
  }
}

// Internal helper: Assumes global shared_lock already held by caller
// FIX SAFE-001: Caller must hold shared_lock, this acquires section.mutex
// This is safe because lock order is: shared_lock (read) → section.mutex
std::optional<ProfilingSession::SectionStats> ProfilingSession::getStatsUnlocked(
    const std::string& section_name) {
  auto& data = getSectionData();
  auto it = data.find(section_name);
  if (it == data.end()) {
    return std::nullopt;
  }

  auto& section = it->second;

  // Acquire section lock while holding shared_lock (read-only on map)
  // This is safe: shared_lock allows concurrent readers
  std::lock_guard<std::mutex> section_lock(section.mutex);

  if (section.durations_us.empty()) {
    return std::nullopt;
  }

  // Calculate statistics
  auto durations = section.durations_us;  // Copy for sorting
  std::sort(durations.begin(), durations.end());

  SectionStats stats;
  stats.sample_count = durations.size();
  stats.min_us = durations.front();
  stats.max_us = durations.back();

  // Mean
  double sum = 0.0;
  for (double d : durations) {
    sum += d;
  }
  stats.mean_us = sum / durations.size();

  // Percentiles
  auto percentile = [&](double p) -> double {
    auto index = static_cast<size_t>(p * static_cast<double>(durations.size() - 1));
    return durations[index];
  };

  stats.p50_us = percentile(0.50);
  stats.p95_us = percentile(0.95);
  stats.p99_us = percentile(0.99);

  return stats;
}

// Public API: Acquires shared_lock (allows concurrent reads) then calls
// getStatsUnlocked FIX SAFE-001: Use shared_lock for read-only access to map
std::optional<ProfilingSession::SectionStats> ProfilingSession::getStats(
    const std::string& section_name) {
  std::shared_lock<std::shared_mutex> global_lock(getGlobalMutex());
  return getStatsUnlocked(section_name);
}

std::string ProfilingSession::generateReport() {
  // FIX SAFE-001: Use shared_lock for read-only access to map
  std::shared_lock<std::shared_mutex> global_lock(getGlobalMutex());

  auto& data = getSectionData();

  if (data.empty()) {
    return "No profiling data collected.\n"
           "Set KEYSTONE_PROFILE=1 environment variable to enable profiling.\n";
  }

  std::ostringstream oss;
  oss << "\n=== Performance Profiling Report ===\n\n";
  oss << std::left << std::setw(30) << "Section" << std::right << std::setw(10) << "Samples"
      << std::setw(12) << "Min (µs)" << std::setw(12) << "Mean (µs)" << std::setw(12) << "P50 (µs)"
      << std::setw(12) << "P95 (µs)" << std::setw(12) << "P99 (µs)" << std::setw(12) << "Max (µs)"
      << "\n";
  oss << std::string(112, '-') << "\n";

  for (const auto& [section_name, section_data] : data) {
    auto stats_opt = getStatsUnlocked(section_name);
    if (!stats_opt)
      continue;

    const auto& stats = *stats_opt;

    oss << std::left << std::setw(30) << section_name << std::right << std::setw(10)
        << stats.sample_count << std::setw(12) << std::fixed << std::setprecision(2) << stats.min_us
        << std::setw(12) << std::fixed << std::setprecision(2) << stats.mean_us << std::setw(12)
        << std::fixed << std::setprecision(2) << stats.p50_us << std::setw(12) << std::fixed
        << std::setprecision(2) << stats.p95_us << std::setw(12) << std::fixed
        << std::setprecision(2) << stats.p99_us << std::setw(12) << std::fixed
        << std::setprecision(2) << stats.max_us << "\n";
  }

  oss << std::string(112, '-') << "\n";
  oss << "\nAll durations in microseconds (µs)\n";

  return oss.str();
}

void ProfilingSession::reset() {
  // FIX SAFE-001: Use unique_lock for write operation
  std::unique_lock<std::shared_mutex> global_lock(getGlobalMutex());
  getSectionData().clear();
}

}  // namespace core
}  // namespace keystone
