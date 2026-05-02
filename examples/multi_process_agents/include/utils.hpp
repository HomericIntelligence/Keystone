#pragma once

#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>

namespace keystone {
namespace multi_process {

inline std::string generateRequestId() {
  // Timestamp component
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::tm tm = *std::localtime(&time_t);

  std::ostringstream timestamp;
  timestamp << std::put_time(&tm, "%Y%m%d_%H%M%S");

  // Simple random component (since we don't have uuid library)
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 0xFFFFFF);

  std::ostringstream uuid;
  uuid << std::hex << std::setfill('0');
  uuid << std::setw(6) << dis(gen);
  uuid << std::setw(6) << dis(gen);

  return timestamp.str() + "_" + uuid.str();
}

inline std::string formatTime(const std::chrono::system_clock::time_point& tp) {
  auto time_t = std::chrono::system_clock::to_time_t(tp);
  std::tm tm = *std::localtime(&time_t);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  return std::string(buf);
}

}  // namespace multi_process
}  // namespace keystone
