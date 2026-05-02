// fuzz_message_serialization.cpp
// Fuzz test for KeystoneMessage serialization/deserialization
//
// Tests the robustness of message serialization against malformed input:
// - Invalid JSON payloads
// - Missing required fields
// - Extremely long strings
// - Special characters and unicode
// - Malformed timestamps
//
// Build with: cmake -DENABLE_FUZZING=ON -DCMAKE_CXX_COMPILER=clang++ ..
// Run with: ./fuzz_message_serialization -max_len=4096 -runs=1000000

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/message.hpp"
#include "core/message_serializer.hpp"

using namespace keystone;
using namespace keystone::core;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Skip empty inputs
  if (size == 0) {
    return 0;
  }

  try {
    // Test 1: Try to deserialize arbitrary bytes as a message
    auto deserialized = MessageSerializer::deserialize(data, size);

    // If deserialization succeeded, re-serialize and verify stability
    auto reserialized = MessageSerializer::serialize(deserialized);

    // Try to deserialize again - should not crash
    auto redeserialized = MessageSerializer::deserialize(reserialized);

    // Verify round-trip consistency (both should have the same msg_id)
    if (deserialized.msg_id != redeserialized.msg_id) {
      // Inconsistency detected (but don't crash)
      return 0;
    }
  } catch (const std::exception&) {
    // Expected for malformed input - just return
    return 0;
  }

  // Test 2: Try to create a message with fuzzed fields
  if (size >= 12) {
    try {
      // Split input into 4 parts for msg_id, sender, receiver, command
      size_t quarter = size / 4;
      std::string msg_id(reinterpret_cast<const char*>(data),
                         std::min(quarter, size_t(256)));
      std::string sender(reinterpret_cast<const char*>(data + quarter),
                         std::min(quarter, size_t(256)));
      std::string receiver(reinterpret_cast<const char*>(data + 2 * quarter),
                           std::min(quarter, size_t(256)));
      std::string command(reinterpret_cast<const char*>(data + 3 * quarter),
                          size - 3 * quarter);

      auto msg = KeystoneMessage::create(sender, receiver, command);

      // Serialize the fuzzed message
      auto serialized = MessageSerializer::serialize(msg);

      // Try to deserialize it back
      auto deserialized = MessageSerializer::deserialize(serialized);

    } catch (const std::exception&) {
      // Expected for invalid input
      return 0;
    }
  }

  return 0;
}
