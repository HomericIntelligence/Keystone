#include "monitoring/nats_status.hpp"

namespace keystone {
namespace monitoring {

void NatsStatusTracker::setConnected() {
  state_.store(NatsConnectionState::kConnected, std::memory_order_release);
  std::lock_guard<std::mutex> lock(timestamp_mutex_);
  last_success_time_ = std::chrono::system_clock::now();
  has_success_ = true;
}

void NatsStatusTracker::setDisconnected() {
  state_.store(NatsConnectionState::kDisconnected, std::memory_order_release);
}

void NatsStatusTracker::setReconnecting() {
  state_.store(NatsConnectionState::kReconnecting, std::memory_order_release);
}

NatsConnectionState NatsStatusTracker::state() const {
  return state_.load(std::memory_order_acquire);
}

int64_t NatsStatusTracker::lastSuccessEpochMs() const {
  std::lock_guard<std::mutex> lock(timestamp_mutex_);
  if (!has_success_) {
    return 0;
  }
  auto epoch = last_success_time_.time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();
}

}  // namespace monitoring
}  // namespace keystone
