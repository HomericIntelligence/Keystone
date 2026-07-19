/**
 * @file test_message.cpp
 * @brief Unit tests for KeystoneMessage and Response factory helpers.
 *
 * Covers the message factory overloads, deadline helpers, and Response
 * success/error construction that make up the pure-transport message value
 * type (src/core/message.cpp). These are in-memory value operations — no
 * broker or scheduler required.
 */

#include "core/message.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <thread>

#include <gtest/gtest.h>

using namespace keystone::core;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// KeystoneMessage::create — legacy (command) overload
// ---------------------------------------------------------------------------

TEST(MessageFactory, LegacyCreateSetsDefaultsAndFields) {
  auto msg = KeystoneMessage::create("alice", "bob", "ping", "payload-data");

  EXPECT_EQ(msg.sender_id, "alice");
  EXPECT_EQ(msg.receiver_id, "bob");
  ASSERT_TRUE(msg.payload.has_value());
  EXPECT_EQ(*msg.payload, "payload-data");

  // Legacy overload defaults action/content and priority.
  EXPECT_EQ(msg.action_type, ActionType::EXECUTE);
  EXPECT_EQ(msg.content_type, ContentType::TEXT_PLAIN);
  EXPECT_EQ(msg.priority, Priority::NORMAL);

  // Auto-generated UUID: 32 hex + 4 dashes = 36 chars.
  EXPECT_EQ(msg.msg_id.size(), 36u);
  EXPECT_FALSE(msg.hasDeadlinePassed());
}

TEST(MessageFactory, LegacyCreateWithoutPayloadIsNullopt) {
  auto msg = KeystoneMessage::create("alice", "bob", "ping");
  EXPECT_FALSE(msg.payload.has_value());
}

TEST(MessageFactory, GeneratedIdsAreUnique) {
  auto a = KeystoneMessage::create("s", "r", "cmd");
  auto b = KeystoneMessage::create("s", "r", "cmd");
  EXPECT_NE(a.msg_id, b.msg_id);
}

// ---------------------------------------------------------------------------
// KeystoneMessage::create — enhanced (ActionType) overload
// ---------------------------------------------------------------------------

TEST(MessageFactory, EnhancedCreateSetsActionAndContent) {
  auto msg = KeystoneMessage::create(
      "alice", "bob", ActionType::SHUTDOWN, std::nullopt, ContentType::BINARY_CISTA);

  EXPECT_EQ(msg.sender_id, "alice");
  EXPECT_EQ(msg.receiver_id, "bob");
  EXPECT_EQ(msg.action_type, ActionType::SHUTDOWN);
  EXPECT_EQ(msg.content_type, ContentType::BINARY_CISTA);
  EXPECT_EQ(msg.priority, Priority::NORMAL);
  EXPECT_FALSE(msg.payload.has_value());
  EXPECT_FALSE(msg.deadline.has_value());
  EXPECT_EQ(msg.msg_id.size(), 36u);
}

TEST(MessageFactory, EnhancedCreateDefaultsToTextPlain) {
  auto msg = KeystoneMessage::create("alice", "bob", ActionType::RETURN_RESULT, "result");
  EXPECT_EQ(msg.content_type, ContentType::TEXT_PLAIN);
  ASSERT_TRUE(msg.payload.has_value());
  EXPECT_EQ(*msg.payload, "result");
}

// ---------------------------------------------------------------------------
// Copy / move special members (defined out-of-line in message.cpp)
// ---------------------------------------------------------------------------

TEST(MessageFactory, CopyAndMovePreserveFields) {
  auto original = KeystoneMessage::create("alice", "bob", ActionType::EXECUTE, "data");
  original.priority = Priority::HIGH;

  KeystoneMessage copied(original);
  EXPECT_EQ(copied.msg_id, original.msg_id);
  EXPECT_EQ(copied.priority, Priority::HIGH);

  KeystoneMessage assigned;
  assigned = original;
  EXPECT_EQ(assigned.receiver_id, "bob");

  const std::string moved_id = original.msg_id;
  KeystoneMessage moved(std::move(original));
  EXPECT_EQ(moved.msg_id, moved_id);

  KeystoneMessage move_assigned;
  move_assigned = std::move(copied);
  EXPECT_EQ(move_assigned.sender_id, "alice");
}

// ---------------------------------------------------------------------------
// Deadline helpers
// ---------------------------------------------------------------------------

TEST(MessageDeadline, NoDeadlineByDefault) {
  auto msg = KeystoneMessage::create("s", "r", "cmd");
  EXPECT_FALSE(msg.hasDeadlinePassed());
  EXPECT_FALSE(msg.getTimeUntilDeadline().has_value());
}

TEST(MessageDeadline, FutureDeadlineNotPassedAndHasTimeRemaining) {
  auto msg = KeystoneMessage::create("s", "r", "cmd");
  msg.setDeadlineFromNow(10s);

  EXPECT_FALSE(msg.hasDeadlinePassed());

  auto remaining = msg.getTimeUntilDeadline();
  ASSERT_TRUE(remaining.has_value());
  EXPECT_GT(remaining->count(), 0);
  EXPECT_LE(remaining->count(), 10000);
}

TEST(MessageDeadline, PassedDeadlineReportsZeroRemaining) {
  auto msg = KeystoneMessage::create("s", "r", "cmd");
  // Set a deadline in the past.
  msg.deadline = std::chrono::system_clock::now() - 1s;

  EXPECT_TRUE(msg.hasDeadlinePassed());

  auto remaining = msg.getTimeUntilDeadline();
  ASSERT_TRUE(remaining.has_value());
  EXPECT_EQ(remaining->count(), 0);
}

// ---------------------------------------------------------------------------
// Response factory helpers
// ---------------------------------------------------------------------------

TEST(ResponseFactory, CreateSuccessSwapsSenderReceiver) {
  auto original = KeystoneMessage::create("requester", "worker", "do-thing");
  auto resp = Response::createSuccess(original, "worker", "ok-result");

  EXPECT_EQ(resp.msg_id, original.msg_id);
  EXPECT_EQ(resp.sender_id, "worker");
  EXPECT_EQ(resp.receiver_id, "requester");
  EXPECT_EQ(resp.status, Response::Status::Success);
  EXPECT_EQ(resp.result, "ok-result");
}

TEST(ResponseFactory, CreateErrorCarriesErrorStatusAndMessage) {
  auto original = KeystoneMessage::create("requester", "worker", "do-thing");
  auto resp = Response::createError(original, "worker", "boom");

  EXPECT_EQ(resp.msg_id, original.msg_id);
  EXPECT_EQ(resp.sender_id, "worker");
  EXPECT_EQ(resp.receiver_id, "requester");
  EXPECT_EQ(resp.status, Response::Status::Error);
  EXPECT_EQ(resp.result, "boom");
}

// ---------------------------------------------------------------------------
// Enum-to-string helpers (header inline; exercise all branches)
// ---------------------------------------------------------------------------

TEST(MessageEnums, PriorityToStringAllValues) {
  EXPECT_EQ(priorityToString(Priority::HIGH), "HIGH");
  EXPECT_EQ(priorityToString(Priority::NORMAL), "NORMAL");
  EXPECT_EQ(priorityToString(Priority::LOW), "LOW");
}

TEST(MessageEnums, ActionTypeToStringAllValues) {
  EXPECT_EQ(actionTypeToString(ActionType::EXECUTE), "EXECUTE");
  EXPECT_EQ(actionTypeToString(ActionType::RETURN_RESULT), "RETURN_RESULT");
  EXPECT_EQ(actionTypeToString(ActionType::SHUTDOWN), "SHUTDOWN");
}

TEST(MessageEnums, ContentTypeToStringAllValues) {
  EXPECT_EQ(contentTypeToString(ContentType::TEXT_PLAIN), "TEXT_PLAIN");
  EXPECT_EQ(contentTypeToString(ContentType::BINARY_CISTA), "BINARY_CISTA");
}
