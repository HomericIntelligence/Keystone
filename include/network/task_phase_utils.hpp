#pragma once

#include <string>

#include "hmas_coordinator.grpc.pb.h"

namespace keystone::network {

/// Returns true if phase is a terminal state (task will not transition
/// further).
inline bool isTerminalPhase(hmas::TaskPhase phase) {
  switch (phase) {
    case hmas::TASK_PHASE_COMPLETED:
    case hmas::TASK_PHASE_FAILED:
    case hmas::TASK_PHASE_ERROR:
    case hmas::TASK_PHASE_TIMEOUT:
    case hmas::TASK_PHASE_CANCELLED:
      return true;
    default:
      return false;
  }
}

/// Convert task phase enum to string representation.
/// @param phase The task phase enum value
/// @return String representation of the phase (e.g., "PENDING", "EXECUTING",
/// "COMPLETED")
inline std::string phaseToString(hmas::TaskPhase phase) {
  switch (phase) {
    case hmas::TASK_PHASE_PENDING:
      return "PENDING";
    case hmas::TASK_PHASE_PLANNING:
      return "PLANNING";
    case hmas::TASK_PHASE_WAITING:
      return "WAITING";
    case hmas::TASK_PHASE_EXECUTING:
      return "EXECUTING";
    case hmas::TASK_PHASE_SYNTHESIZING:
      return "SYNTHESIZING";
    case hmas::TASK_PHASE_COMPLETED:
      return "COMPLETED";
    case hmas::TASK_PHASE_FAILED:
      return "FAILED";
    case hmas::TASK_PHASE_TIMEOUT:
      return "TIMEOUT";
    case hmas::TASK_PHASE_CANCELLED:
      return "CANCELLED";
    case hmas::TASK_PHASE_ERROR:
      return "ERROR";
    default:
      return "UNSPECIFIED";
  }
}

/// Convert string representation to task phase enum.
/// @param phase_str String representation (e.g., "PENDING", "EXECUTING",
/// "COMPLETED")
/// @return Task phase enum value, or TASK_PHASE_UNSPECIFIED if string is
/// unrecognized
inline hmas::TaskPhase stringToPhase(const std::string& phase_str) {
  if (phase_str == "PENDING") return hmas::TASK_PHASE_PENDING;
  if (phase_str == "PLANNING") return hmas::TASK_PHASE_PLANNING;
  if (phase_str == "WAITING") return hmas::TASK_PHASE_WAITING;
  if (phase_str == "EXECUTING") return hmas::TASK_PHASE_EXECUTING;
  if (phase_str == "SYNTHESIZING") return hmas::TASK_PHASE_SYNTHESIZING;
  if (phase_str == "COMPLETED") return hmas::TASK_PHASE_COMPLETED;
  if (phase_str == "FAILED") return hmas::TASK_PHASE_FAILED;
  if (phase_str == "TIMEOUT") return hmas::TASK_PHASE_TIMEOUT;
  if (phase_str == "CANCELLED") return hmas::TASK_PHASE_CANCELLED;
  if (phase_str == "ERROR") return hmas::TASK_PHASE_ERROR;
  if (phase_str == "UNSPECIFIED") return hmas::TASK_PHASE_UNSPECIFIED;
  return hmas::TASK_PHASE_UNSPECIFIED;
}

/// Returns true if the string maps to a known (non-UNSPECIFIED) TaskPhase
/// value.
inline bool isKnownPhaseString(const std::string& phase_str) {
  if (phase_str.empty()) return false;
  if (phase_str == "UNSPECIFIED") return false;
  return stringToPhase(phase_str) != hmas::TASK_PHASE_UNSPECIFIED;
}

}  // namespace keystone::network
