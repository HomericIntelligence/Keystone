/**
 * @file test_message_serializer.cpp
 * @brief Unit tests for MessageSerializer (Cista-based)
 */

// KeystoneMessage::command is [[deprecated]]; test files intentionally access
// it to verify backward-compat behaviour.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <gtest/gtest.h>

#include "core/message.hpp"
#include "core/message_serializer.hpp"

using namespace keystone::core;

// Test: Serialize and deserialize basic message
TEST(MessageSerializerTest, BasicSerializeDeserialize) {
  // Create a message
  auto msg = KeystoneMessage::create("agent1", "agent2", ActionType::EXECUTE,
                                     "test payload");

  // Serialize
  auto buffer = MessageSerializer::serialize(msg);
  EXPECT_GT(buffer.size(), 0);

  // Deserialize
  auto deserialized = MessageSerializer::deserialize(buffer);

  // Verify all fields
  EXPECT_EQ(deserialized.msg_id, msg.msg_id);
  EXPECT_EQ(deserialized.sender_id, msg.sender_id);
  EXPECT_EQ(deserialized.receiver_id, msg.receiver_id);
  EXPECT_EQ(deserialized.action_type, msg.action_type);
  EXPECT_EQ(deserialized.content_type, msg.content_type);
  EXPECT_EQ(deserialized.payload, msg.payload);
}

// Test: Serialize message without payload
TEST(MessageSerializerTest, SerializeWithoutPayload) {
  auto msg = KeystoneMessage::create("agent1", "agent2", ActionType::SHUTDOWN,
                                     std::nullopt);

  // Serialize and deserialize
  auto buffer = MessageSerializer::serialize(msg);
  auto deserialized = MessageSerializer::deserialize(buffer);

  // Verify no payload
  EXPECT_FALSE(deserialized.payload.has_value());
  EXPECT_EQ(deserialized.action_type, ActionType::SHUTDOWN);
}

// Test: Serialize different action types
TEST(MessageSerializerTest, DifferentActionTypes) {
  ActionType types[] = {ActionType::EXECUTE, ActionType::RETURN_RESULT,
                        ActionType::SHUTDOWN};

  for (auto type : types) {
    auto msg = KeystoneMessage::create("agent1", "agent2", type);

    auto buffer = MessageSerializer::serialize(msg);
    auto deserialized = MessageSerializer::deserialize(buffer);

    EXPECT_EQ(deserialized.action_type, type);
  }
}

// Test: Serialize different content types
TEST(MessageSerializerTest, DifferentContentTypes) {
  auto msg1 = KeystoneMessage::create("agent1", "agent2", ActionType::EXECUTE,
                                      "text data", ContentType::TEXT_PLAIN);

  auto msg2 = KeystoneMessage::create("agent1", "agent2", ActionType::EXECUTE,
                                      "binary data", ContentType::BINARY_CISTA);

  auto buffer1 = MessageSerializer::serialize(msg1);
  auto buffer2 = MessageSerializer::serialize(msg2);

  auto deserialized1 = MessageSerializer::deserialize(buffer1);
  auto deserialized2 = MessageSerializer::deserialize(buffer2);

  EXPECT_EQ(deserialized1.content_type, ContentType::TEXT_PLAIN);
  EXPECT_EQ(deserialized2.content_type, ContentType::BINARY_CISTA);
}

// Test: Large payload
TEST(MessageSerializerTest, LargePayload) {
  std::string large_payload(10000, 'x');  // 10KB payload

  auto msg = KeystoneMessage::create("agent1", "agent2",
                                     ActionType::RETURN_RESULT, large_payload);

  auto buffer = MessageSerializer::serialize(msg);
  auto deserialized = MessageSerializer::deserialize(buffer);

  EXPECT_EQ(deserialized.payload.value(), large_payload);
}

// Test: Zero-copy deserialization
TEST(MessageSerializerTest, ZeroCopyDeserialize) {
  auto msg = KeystoneMessage::create("agent1", "agent2", ActionType::EXECUTE,
                                     "payload");

  auto buffer = MessageSerializer::serialize(msg);

  // Zero-copy deserialize
  const auto* smsg =
      MessageSerializer::deserializeInPlace(buffer.data(), buffer.size());

  ASSERT_NE(smsg, nullptr);
  EXPECT_EQ(std::string(smsg->sender_id.data(), smsg->sender_id.size()),
            "agent1");
  EXPECT_EQ(std::string(smsg->receiver_id.data(), smsg->receiver_id.size()),
            "agent2");
  EXPECT_EQ(smsg->action_type, static_cast<uint32_t>(ActionType::EXECUTE));
}

// Test: Timestamp preservation
TEST(MessageSerializerTest, TimestampPreservation) {
  auto msg = KeystoneMessage::create("agent1", "agent2", ActionType::EXECUTE);

  auto original_timestamp = msg.timestamp;

  auto buffer = MessageSerializer::serialize(msg);
  auto deserialized = MessageSerializer::deserialize(buffer);

  // Timestamps should be equal (within reasonable precision)
  auto diff = std::chrono::abs(deserialized.timestamp - original_timestamp);
  EXPECT_LT(diff, std::chrono::microseconds(1));
}

// Test: Special characters in strings
TEST(MessageSerializerTest, SpecialCharacters) {
  auto msg = KeystoneMessage::create("agent-1.test", "agent@2#special",
                                     ActionType::EXECUTE,
                                     "payload with\nnewlines\tand\ttabs");

  auto buffer = MessageSerializer::serialize(msg);
  auto deserialized = MessageSerializer::deserialize(buffer);

  EXPECT_EQ(deserialized.sender_id, msg.sender_id);
  EXPECT_EQ(deserialized.receiver_id, msg.receiver_id);
  EXPECT_EQ(deserialized.payload, msg.payload);
}

// Test: Backward compatibility with legacy create()
TEST(MessageSerializerTest, LegacyCreateCompatibility) {
  auto msg = KeystoneMessage::create("agent1", "agent2",
                                     "echo hello",  // legacy command
                                     "some data");

  // Should have default values for new fields
  EXPECT_EQ(msg.action_type, ActionType::EXECUTE);
  EXPECT_EQ(msg.content_type, ContentType::TEXT_PLAIN);

  // Should serialize and deserialize correctly
  auto buffer = MessageSerializer::serialize(msg);
  auto deserialized = MessageSerializer::deserialize(buffer);

  EXPECT_EQ(deserialized.action_type, ActionType::EXECUTE);
  EXPECT_EQ(deserialized.content_type, ContentType::TEXT_PLAIN);
  EXPECT_EQ(deserialized.command, "echo hello");
}

#pragma GCC diagnostic pop
