/**
 * @file test_subject_validator.cpp
 * @brief Unit tests for subject token validation (Issue #113)
 *
 * Tests that validateSubjectToken() rejects path traversal and injection
 * characters in NATS subject tokens / agent IDs before they reach routing
 * or API call construction.
 */

#include "core/subject_validator.hpp"
#include "agents/task_agent.hpp"
#include "core/message_bus.hpp"

#include <gtest/gtest.h>

// =============================================================================
// Valid identifiers — must pass without throwing
// =============================================================================

TEST(SubjectValidatorTest, AcceptsAlphanumeric) {
  EXPECT_NO_THROW(keystone::core::validateSubjectToken("abc123", "id"));
  EXPECT_NO_THROW(keystone::core::validateSubjectToken("ABC", "id"));
  EXPECT_NO_THROW(keystone::core::validateSubjectToken("a", "id"));
}

TEST(SubjectValidatorTest, AcceptsHyphens) {
  EXPECT_NO_THROW(keystone::core::validateSubjectToken("team-1", "id"));
  EXPECT_NO_THROW(keystone::core::validateSubjectToken("agent-core-7", "id"));
}

TEST(SubjectValidatorTest, AcceptsUnderscores) {
  EXPECT_NO_THROW(keystone::core::validateSubjectToken("task_42", "id"));
  EXPECT_NO_THROW(keystone::core::validateSubjectToken("abc_123", "id"));
}

TEST(SubjectValidatorTest, AcceptsUuid) {
  EXPECT_NO_THROW(
      keystone::core::validateSubjectToken("550e8400-e29b-41d4-a716-446655440000", "team_id"));
}

TEST(SubjectValidatorTest, ReturnsValueUnchanged) {
  const std::string id = "valid-id_123";
  EXPECT_EQ(keystone::core::validateSubjectToken(id, "id"), id);
}

// =============================================================================
// Path traversal attacks — must throw std::invalid_argument
// =============================================================================

TEST(SubjectValidatorTest, RejectsPathTraversalDotDot) {
  EXPECT_THROW(keystone::core::validateSubjectToken("../../admin", "team_id"),
               std::invalid_argument);
}

TEST(SubjectValidatorTest, RejectsSlash) {
  EXPECT_THROW(keystone::core::validateSubjectToken("foo/bar", "team_id"), std::invalid_argument);
}

TEST(SubjectValidatorTest, RejectsLeadingSlash) {
  EXPECT_THROW(keystone::core::validateSubjectToken("/etc/passwd", "task_id"),
               std::invalid_argument);
}

// =============================================================================
// Other injection characters
// =============================================================================

TEST(SubjectValidatorTest, RejectsSpace) {
  EXPECT_THROW(keystone::core::validateSubjectToken("team id", "id"), std::invalid_argument);
}

TEST(SubjectValidatorTest, RejectsNewline) {
  EXPECT_THROW(keystone::core::validateSubjectToken("team\nid", "id"), std::invalid_argument);
}

TEST(SubjectValidatorTest, RejectsSemicolon) {
  EXPECT_THROW(keystone::core::validateSubjectToken("team;id", "id"), std::invalid_argument);
}

TEST(SubjectValidatorTest, RejectsDot) {
  EXPECT_THROW(keystone::core::validateSubjectToken("team.id", "id"), std::invalid_argument);
}

// =============================================================================
// Empty / boundary cases
// =============================================================================

TEST(SubjectValidatorTest, RejectsEmptyString) {
  EXPECT_THROW(keystone::core::validateSubjectToken("", "id"), std::invalid_argument);
}

TEST(SubjectValidatorTest, ErrorMessageContainsLabel) {
  try {
    keystone::core::validateSubjectToken("../../evil", "team_id");
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_NE(std::string(e.what()).find("team_id"), std::string::npos);
  }
}

// =============================================================================
// MessageBus integration: registerAgent rejects unsafe IDs (Issue #113)
// =============================================================================

TEST(SubjectValidatorTest, MessageBusRejectsPathTraversalAgentId) {
  keystone::core::MessageBus bus;
  auto agent = std::make_shared<keystone::agents::TaskAgent>("../../evil");
  EXPECT_THROW(bus.registerAgent("../../evil", agent), std::invalid_argument);
}

TEST(SubjectValidatorTest, MessageBusRejectsSlashInAgentId) {
  keystone::core::MessageBus bus;
  auto agent = std::make_shared<keystone::agents::TaskAgent>("foo/bar");
  EXPECT_THROW(bus.registerAgent("foo/bar", agent), std::invalid_argument);
}

TEST(SubjectValidatorTest, MessageBusAcceptsValidAgentId) {
  keystone::core::MessageBus bus;
  auto agent = std::make_shared<keystone::agents::TaskAgent>("valid-agent_1");
  EXPECT_NO_THROW(bus.registerAgent("valid-agent_1", agent));
  EXPECT_TRUE(bus.hasAgent("valid-agent_1"));
}
