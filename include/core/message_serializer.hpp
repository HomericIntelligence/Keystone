#pragma once

#include <cista/containers.h>
#include <cista/containers/hash_map.h>
#include <cista/serialization.h>

#include <cstdint>
#include <vector>

#include "core/message.hpp"

namespace keystone {
namespace core {

/**
 * @brief Cista-compatible message structure for zero-copy serialization
 *
 * This struct mirrors KeystoneMessage but uses Cista types for serialization.
 * We use cista::offset types for pointer-independent serialization.
 */
struct SerializableMessage {
  cista::offset::string msg_id;
  cista::offset::string sender_id;
  cista::offset::string receiver_id;

  uint32_t action_type;   // Serialized as uint32_t
  uint32_t content_type;  // Serialized as uint32_t
  cista::offset::string session_id;
  cista::offset::hash_map<cista::offset::string, cista::offset::string>
      metadata;

  cista::offset::string command;
  cista::offset::string payload;
  bool has_payload;
  int64_t timestamp_ns;  // Timestamp as nanoseconds since epoch

  // Issue #285: Cross-host tracing correlation ID
  cista::offset::string correlation_id;
  bool has_correlation_id{false};

  /**
   * @brief Convert KeystoneMessage to SerializableMessage
   */
  static SerializableMessage fromKeystoneMessage(const KeystoneMessage& msg);

  /**
   * @brief Convert SerializableMessage to KeystoneMessage
   */
  KeystoneMessage toKeystoneMessage() const;
};

/**
 * @brief MessageSerializer - Zero-copy serialization using Cista
 *
 * Provides high-performance serialization and deserialization of
 * KeystoneMessage using the Cista library for zero-copy operations.
 *
 * Features:
 * - Zero-copy deserialization (in-place access)
 * - Type-safe serialization
 * - Compact binary format
 * - Fast performance (<1μs for typical messages)
 */
class MessageSerializer {
 public:
  /**
   * @brief Serialize a KeystoneMessage to binary format
   *
   * @param msg The message to serialize
   * @return std::vector<uint8_t> Binary representation
   */
  static std::vector<uint8_t> serialize(const KeystoneMessage& msg);

  /**
   * @brief Deserialize a binary buffer to KeystoneMessage
   *
   * Makes a copy of the data for safety.
   *
   * @param buffer Binary data buffer
   * @param size Size of the buffer
   * @return KeystoneMessage Deserialized message
   */
  static KeystoneMessage deserialize(const uint8_t* buffer, size_t size);

  /**
   * @brief Deserialize from vector
   *
   * @param buffer Binary data vector
   * @return KeystoneMessage Deserialized message
   */
  static KeystoneMessage deserialize(const std::vector<uint8_t>& buffer);

  /**
   * @brief Zero-copy deserialization (in-place access)
   *
   * WARNING: The returned pointer is valid only as long as the buffer exists
   * and is not modified. Use this for performance-critical paths.
   *
   * @param buffer Binary data buffer
   * @param size Size of the buffer
   * @return const SerializableMessage* Pointer to deserialized message
   */
  static const SerializableMessage* deserializeInPlace(const uint8_t* buffer,
                                                       size_t size);
};

}  // namespace core
}  // namespace keystone
