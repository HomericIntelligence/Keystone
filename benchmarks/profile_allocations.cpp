/**
 * @file profile_allocations.cpp
 * @brief Simple profiling harness for valgrind massif
 *
 * Runs a focused workload for memory profiling
 */

#include "core/message.hpp"

#include <cstdint>
#include <vector>

using namespace keystone::core;

int main() {
  // Simulate realistic workload for profiling
  const int32_t num_messages = 10000;

  std::vector<KeystoneMessage> messages;
  messages.reserve(static_cast<size_t>(num_messages));

  // Create many messages (typical hot path)
  for (int32_t i = 0; i < num_messages; ++i) {
    messages.push_back(
        KeystoneMessage::create("sender-agent-001", "receiver-agent-002", "EXECUTE"));
  }

  // Clear to measure deallocation
  messages.clear();

  // Test with payloads
  for (int32_t i = 0; i < num_messages; ++i) {
    auto msg = KeystoneMessage::create("sender",
                                       "receiver",
                                       "EXECUTE",
                                       "payload-data-" + std::to_string(i));
    messages.push_back(std::move(msg));
  }

  return 0;
}
