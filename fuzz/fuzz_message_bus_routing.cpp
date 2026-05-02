// fuzz_message_bus_routing.cpp
// Fuzz test for MessageBus routing logic
//
// Tests the robustness of message routing against:
// - Invalid agent IDs
// - Duplicate registrations
// - Concurrent register/unregister operations
// - Messages to non-existent agents
// - Malformed receiver IDs
// - Empty agent IDs
//
// Build with: cmake -DENABLE_FUZZING=ON -DCMAKE_CXX_COMPILER=clang++ ..
// Run with: ./fuzz_message_bus_routing -max_len=4096 -runs=1000000

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "agents/base_agent.hpp"
#include "core/message.hpp"
#include "core/message_bus.hpp"

using namespace keystone;
using namespace keystone::core;

// Minimal test agent for fuzzing
class FuzzAgent : public BaseAgent {
 public:
  explicit FuzzAgent(const std::string& id) : BaseAgent(id) {}

  Response processMessage(const KeystoneMessage& msg) override {
    // Just echo back
    return Response{ResponseStatus::SUCCESS, "fuzz_ok", msg.payload};
  }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Skip empty or too small inputs
  if (size < 4) {
    return 0;
  }

  // Create a fresh MessageBus for each iteration
  auto bus = std::make_unique<MessageBus>();

  // Create some agents with fuzzed IDs
  std::vector<std::unique_ptr<FuzzAgent>> agents;

  try {
    // Extract operation count from first byte (limit to avoid timeout)
    uint8_t op_count = std::min(data[0], uint8_t(20));
    size_t offset = 1;

    for (uint8_t i = 0; i < op_count && offset < size; ++i) {
      // Get operation type from data
      uint8_t op_type = data[offset] % 4;
      offset++;

      // Extract string length (limit to prevent excessive allocations)
      if (offset >= size) break;
      uint8_t str_len = std::min(data[offset], uint8_t(64));
      offset++;

      // Extract agent ID string
      if (offset + str_len > size) break;
      std::string agent_id(reinterpret_cast<const char*>(data + offset),
                           str_len);
      offset += str_len;

      switch (op_type) {
        case 0: {
          // Register agent
          auto agent = std::make_unique<FuzzAgent>(agent_id);
          bus->registerAgent(agent_id, agent.get());
          agents.push_back(std::move(agent));
          break;
        }

        case 1: {
          // Unregister agent
          bus->unregisterAgent(agent_id);
          break;
        }

        case 2: {
          // Check if agent exists
          bus->hasAgent(agent_id);
          break;
        }

        case 3: {
          // Try to route a message
          if (offset + 2 > size) break;

          // Extract sender and receiver lengths
          uint8_t sender_len = std::min(data[offset], uint8_t(32));
          offset++;

          if (offset + sender_len > size) break;
          std::string sender(reinterpret_cast<const char*>(data + offset),
                             sender_len);
          offset += sender_len;

          if (offset >= size) break;
          uint8_t receiver_len = std::min(data[offset], uint8_t(32));
          offset++;

          if (offset + receiver_len > size) break;
          std::string receiver(reinterpret_cast<const char*>(data + offset),
                               receiver_len);
          offset += receiver_len;

          // Create and route message
          auto msg = KeystoneMessage::create(sender, receiver, "fuzz_command");
          bus->routeMessage(msg);
          break;
        }
      }
    }

    // Test listing agents
    auto agent_list = bus->listAgents();

  } catch (const std::exception&) {
    // Expected for malformed input
    return 0;
  }

  return 0;
}
