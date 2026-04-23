/**
 * @file nats_listener.cpp
 * @brief NATS JetStream listener with startup error handling and retry logic
 */

#include "network/nats_listener.hpp"

#include "concurrency/logger.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <thread>

namespace keystone {
namespace network {

using namespace concurrency;

NatsListener::NatsListener(Config config, std::unique_ptr<INatsConnection> connection)
    : config_(std::move(config)), connection_(std::move(connection)) {
  if (!connection_) {
    throw std::invalid_argument("NatsListener: connection must not be null");
  }
  if (config_.server_url.empty()) {
    throw std::invalid_argument("NatsListener: server_url must not be empty");
  }
  if (config_.subject.empty()) {
    throw std::invalid_argument("NatsListener: subject must not be empty");
  }
  if (config_.stream_name.empty()) {
    throw std::invalid_argument("NatsListener: stream_name must not be empty");
  }
  if (config_.durable_name.empty()) {
    throw std::invalid_argument("NatsListener: durable_name must not be empty");
  }
}

NatsListener::~NatsListener() {
  stop();
}

void NatsListener::setMessageHandler(NatsMessageHandler handler) {
  handler_ = std::move(handler);
}

void NatsListener::start() {
  uint32_t attempt = 0;

  while (!stop_requested_.load(std::memory_order_acquire)) {
    attempt_count_.store(attempt + 1, std::memory_order_release);

    try {
      Logger::info("NatsListener: Connecting to {} (attempt {})", config_.server_url, attempt + 1);
      connection_->connect(config_.server_url);

      Logger::info("NatsListener: Subscribing to stream '{}' subject '{}'",
                   config_.stream_name,
                   config_.subject);
      connection_->subscribe(config_.subject, config_.stream_name, config_.durable_name, handler_);

      connected_.store(true, std::memory_order_release);
      Logger::info("NatsListener: Subscribed successfully on attempt {}", attempt + 1);
      return;

    } catch (const std::exception& ex) {
      NatsStartupError category = classifyException(ex);

      switch (category) {
        case NatsStartupError::StreamNotFound: {
          if (config_.auto_create_stream) {
            Logger::warn("NatsListener: Stream '{}' not found — auto-creating with subject '{}'",
                         config_.stream_name,
                         config_.subject);
            try {
              connection_->createStream(config_.stream_name, config_.subject);
              Logger::info("NatsListener: Stream '{}' created successfully", config_.stream_name);
            } catch (const std::exception& create_ex) {
              Logger::error("NatsListener: Failed to create stream '{}': {}",
                            config_.stream_name,
                            create_ex.what());
            }
            // Retry immediately after create attempt
            ++attempt;
            continue;
          }

          logStreamNotFoundInstructions();
          break;
        }

        case NatsStartupError::ConnectionFailed: {
          Logger::warn("NatsListener: Connection to {} failed (attempt {}): {}",
                       config_.server_url,
                       attempt + 1,
                       ex.what());
          break;
        }

        case NatsStartupError::Permanent: {
          Logger::error("NatsListener: Permanent error — will not retry: {}", ex.what());
          throw;
        }
      }

      auto delay = computeBackoff(attempt);
      Logger::info("NatsListener: Retrying in {}ms (attempt {})", delay.count(), attempt + 1);
      ++attempt;

      if (sleepInterruptible(delay)) {
        Logger::info("NatsListener: Shutdown requested during backoff — exiting");
        return;
      }
    }
  }

  Logger::info("NatsListener: Shutdown requested — exiting retry loop");
}

void NatsListener::stop() {
  stop_requested_.store(true, std::memory_order_release);
  connected_.store(false, std::memory_order_release);

  if (connection_) {
    try {
      connection_->disconnect();
    } catch (const std::exception& ex) {
      Logger::warn("NatsListener: Error during disconnect: {}", ex.what());
    }
  }
}

// --- private helpers ---

NatsStartupError NatsListener::classifyException(const std::exception& ex) {
  // std::domain_error signals stream-not-found (as documented on INatsConnection)
  if (dynamic_cast<const std::domain_error*>(&ex) != nullptr) {
    return NatsStartupError::StreamNotFound;
  }
  // std::system_error signals transient connectivity issues
  if (dynamic_cast<const std::system_error*>(&ex) != nullptr) {
    return NatsStartupError::ConnectionFailed;
  }
  // Everything else (std::runtime_error from auth failures, bad config) is permanent
  return NatsStartupError::Permanent;
}

std::chrono::milliseconds NatsListener::computeBackoff(uint32_t attempt) const {
  if (attempt == 0) {
    return config_.initial_backoff_ms;
  }
  double ms = static_cast<double>(config_.initial_backoff_ms.count()) *
              std::pow(config_.backoff_multiplier, static_cast<double>(attempt));
  ms = std::min(ms, static_cast<double>(config_.max_backoff_ms.count()));
  return std::chrono::milliseconds(static_cast<int64_t>(ms));
}

bool NatsListener::sleepInterruptible(std::chrono::milliseconds duration) const {
  auto remaining = duration;
  while (remaining.count() > 0) {
    if (stop_requested_.load(std::memory_order_acquire)) {
      return true;
    }
    auto slice = std::min(remaining, config_.shutdown_poll_ms);
    std::this_thread::sleep_for(slice);
    remaining -= slice;
  }
  return stop_requested_.load(std::memory_order_acquire);
}

void NatsListener::logStreamNotFoundInstructions() const {
  Logger::warn("NatsListener: JetStream stream '{}' does not exist for subject '{}'.",
               config_.stream_name,
               config_.subject);
  Logger::warn("  To create the stream manually, run:");
  Logger::warn("    nats stream add {} --subjects='{}' --storage=file --replicas=1",
               config_.stream_name,
               config_.subject);
  Logger::warn(
      "  Or set auto_create_stream=true in NatsListener::Config to let "
      "Keystone create it automatically.");
  Logger::warn("  Will retry until the stream is available or shutdown is requested.");
}

}  // namespace network
}  // namespace keystone
