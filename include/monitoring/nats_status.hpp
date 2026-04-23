#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

namespace keystone {
namespace monitoring {

enum class NatsConnectionState {
  kConnected,
  kDisconnected,
  kReconnecting,
};

/**
 * @brief Thread-safe NATS connection state tracker
 *
 * Updated by NATS client callbacks; read by HealthCheckServer for /v1/health.
 * setConnected() records the last-success timestamp; the other setters update
 * state only (a reconnect or disconnect does not reset the success clock).
 */
class NatsStatusTracker {
 public:
  NatsStatusTracker() = default;

  // Non-copyable, non-movable (atomic + mutex members)
  NatsStatusTracker(const NatsStatusTracker&) = delete;
  NatsStatusTracker& operator=(const NatsStatusTracker&) = delete;

  void setConnected();
  void setDisconnected();
  void setReconnecting();

  NatsConnectionState state() const;

  // Returns 0 if setConnected() has never been called.
  int64_t lastSuccessEpochMs() const;

 private:
  std::atomic<NatsConnectionState> state_{NatsConnectionState::kDisconnected};
  mutable std::mutex timestamp_mutex_;
  std::chrono::system_clock::time_point last_success_time_{};
  bool has_success_{false};
};

}  // namespace monitoring
}  // namespace keystone
