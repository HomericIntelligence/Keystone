#pragma once

#include "hmas_coordinator.grpc.pb.h"

namespace keystone::network {

/// Returns true if phase is a terminal state (task will not transition further).
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

}  // namespace keystone::network
