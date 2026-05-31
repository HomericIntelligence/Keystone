/**
 * @file message_serializer.cpp
 * @brief Implementation of Cista-based message serialization
 */

#include "core/message_serializer.hpp"

#include <cista/serialization.h>

namespace keystone {
namespace core {

SerializableMessage SerializableMessage::fromKeystoneMessage(
    const KeystoneMessage& msg) {
  SerializableMessage smsg;

  smsg.msg_id = cista::offset::string{msg.msg_id.c_str()};
  smsg.sender_id = cista::offset::string{msg.sender_id.c_str()};
  smsg.receiver_id = cista::offset::string{msg.receiver_id.c_str()};

  smsg.action_type = static_cast<uint32_t>(msg.action_type);
  smsg.content_type = static_cast<uint32_t>(msg.content_type);

  _Pragma("GCC diagnostic push")
  _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
  smsg.command = cista::offset::string{msg.command.c_str()};
  _Pragma("GCC diagnostic pop")

  if (msg.payload.has_value()) {
    smsg.payload = cista::offset::string{msg.payload.value().c_str()};
    smsg.has_payload = true;
  } else {
    smsg.payload = cista::offset::string{""};
    smsg.has_payload = false;
  }

  // Convert timestamp to nanoseconds since epoch
  auto duration = msg.timestamp.time_since_epoch();
  smsg.timestamp_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();

  // Issue #285: Propagate correlation_id for cross-host tracing
  if (msg.correlation_id.has_value()) {
    smsg.correlation_id =
        cista::offset::string{msg.correlation_id.value().c_str()};
    smsg.has_correlation_id = true;
  } else {
    smsg.correlation_id = cista::offset::string{""};
    smsg.has_correlation_id = false;
  }

  return smsg;
}

KeystoneMessage SerializableMessage::toKeystoneMessage() const {
  KeystoneMessage msg;

  msg.msg_id = std::string{msg_id.data(), msg_id.size()};
  msg.sender_id = std::string{sender_id.data(), sender_id.size()};
  msg.receiver_id = std::string{receiver_id.data(), receiver_id.size()};

  msg.action_type = static_cast<ActionType>(action_type);
  msg.content_type = static_cast<ContentType>(content_type);

  _Pragma("GCC diagnostic push")
  _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
  msg.command = std::string{command.data(), command.size()};
  _Pragma("GCC diagnostic pop")

  if (has_payload) {
    msg.payload = std::string{payload.data(), payload.size()};
  } else {
    msg.payload = std::nullopt;
  }

  // Convert timestamp from nanoseconds since epoch
  auto duration = std::chrono::nanoseconds{timestamp_ns};
  msg.timestamp = std::chrono::system_clock::time_point{
      std::chrono::duration_cast<std::chrono::system_clock::duration>(
          duration)};

  // Initialize Phase C fields with defaults (not in serialized format yet)
  msg.priority = Priority::NORMAL;
  msg.deadline = std::nullopt;

  // Issue #285: Restore correlation_id from serialized form
  if (has_correlation_id) {
    msg.correlation_id =
        std::string{correlation_id.data(), correlation_id.size()};
  } else {
    msg.correlation_id = std::nullopt;
  }

  return msg;
}

std::vector<uint8_t> MessageSerializer::serialize(const KeystoneMessage& msg) {
  // Convert to serializable format
  auto smsg = SerializableMessage::fromKeystoneMessage(msg);

  // Serialize using Cista
  auto buffer = cista::serialize(smsg);

  return std::vector<uint8_t>(buffer.begin(), buffer.end());
}

KeystoneMessage MessageSerializer::deserialize(const uint8_t* buffer,
                                               size_t size) {
  // Deserialize using Cista
  auto smsg = cista::deserialize<SerializableMessage>(buffer, buffer + size);

  // Convert back to KeystoneMessage
  return smsg->toKeystoneMessage();
}

KeystoneMessage MessageSerializer::deserialize(
    const std::vector<uint8_t>& buffer) {
  return deserialize(buffer.data(), buffer.size());
}

const SerializableMessage* MessageSerializer::deserializeInPlace(
    const uint8_t* buffer, size_t size) {
  // Zero-copy deserialization - returns pointer into the buffer
  return cista::deserialize<SerializableMessage>(buffer, buffer + size);
}

}  // namespace core
}  // namespace keystone
