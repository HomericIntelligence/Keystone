/**
 * @file test_subject_validator.cpp
 * @brief Unit tests for subject token validation (Issue #113, #280)
 *
 * Tests that validateSubjectToken() rejects path traversal and injection
 * characters in NATS subject tokens / agent IDs before they reach routing
 * or API call construction.
 *
 * Also tests validateNatsSubjectToken() and validateNatsSubject() which
 * accept NATS wildcard tokens (* and >) for subject filter construction
 * (Issue #280).
 */

#include "core/message.hpp"
#include "core/message_bus.hpp"
#include "core/message_sink.hpp"
#include "core/subject_validator.hpp"

#include <memory>

#include <gtest/gtest.h>

namespace {

// Minimal non-agent message sink used purely as a registration fixture for the
// MessageBus integration tests below. The transport core depends only on
// core::IMessageSink (the agent layer was extracted to ProjectAgamemnon per
// ADR-015), so these tests no longer need a concrete agent type.
struct StubSink : public keystone::core::IMessageSink {
  void receiveMessage(const keystone::core::KeystoneMessage& /*msg*/) override {}
};

}  // namespace

// =============================================================================
// Valid identifiers -- must pass without throwing
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
  EXPECT_NO_THROW(keystone::core::validateSubjectToken(
      "550e8400-e29b-41d4-a716-446655440000", "team_id"));
}

TEST(SubjectValidatorTest, ReturnsValueUnchanged) {
  const std::string id = "valid-id_123";
  EXPECT_EQ(keystone::core::validateSubjectToken(id, "id"), id);
}

// =============================================================================
// Path traversal attacks -- must throw std::invalid_argument
// =============================================================================

TEST(SubjectValidatorTest, RejectsPathTraversalDotDot) {
  EXPECT_THROW(keystone::core::validateSubjectToken("../../admin", "team_id"),
               std::invalid_argument);
}

TEST(SubjectValidatorTest, RejectsSlash) {
  EXPECT_THROW(keystone::core::validateSubjectToken("foo/bar", "team_id"),
               std::invalid_argument);
}

TEST(SubjectValidatorTest, RejectsLeadingSlash) {
  EXPECT_THROW(keystone::core::validateSubjectToken("/etc/passwd", "task_id"),
               std::invalid_argument);
}

// =============================================================================
// Other injection characters
// =============================================================================

TEST(SubjectValidatorTest, RejectsSpace) {
  EXPECT_THROW(keystone::core::validateSubjectToken("team id", "id"),
               std::invalid_argument);
}

TEST(SubjectValidatorTest, RejectsNewline) {
  EXPECT_THROW(keystone::core::validateSubjectToken("team\nid", "id"),
               std::invalid_argument);
}

TEST(SubjectValidatorTest, RejectsSemicolon) {
  EXPECT_THROW(keystone::core::validateSubjectToken("team;id", "id"),
               std::invalid_argument);
}

TEST(SubjectValidatorTest, RejectsDot) {
  EXPECT_THROW(keystone::core::validateSubjectToken("team.id", "id"),
               std::invalid_argument);
}

// =============================================================================
// Empty / boundary cases
// =============================================================================

TEST(SubjectValidatorTest, RejectsEmptyString) {
  EXPECT_THROW(keystone::core::validateSubjectToken("", "id"),
               std::invalid_argument);
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
  auto sink = std::make_shared<StubSink>();
  EXPECT_THROW(bus.registerAgent("../../evil", sink), std::invalid_argument);
}

TEST(SubjectValidatorTest, MessageBusRejectsSlashInAgentId) {
  keystone::core::MessageBus bus;
  auto sink = std::make_shared<StubSink>();
  EXPECT_THROW(bus.registerAgent("foo/bar", sink), std::invalid_argument);
}

TEST(SubjectValidatorTest, MessageBusAcceptsValidAgentId) {
  keystone::core::MessageBus bus;
  auto sink = std::make_shared<StubSink>();
  EXPECT_NO_THROW(bus.registerAgent("valid-agent_1", sink));
  EXPECT_TRUE(bus.hasAgent("valid-agent_1"));
}

// =============================================================================
// validateNatsSubjectToken -- Issue #280: NATS wildcard-aware token validation
// =============================================================================

TEST(NatsSubjectTokenTest, AcceptsAlphanumericToken) {
  EXPECT_NO_THROW(keystone::core::validateNatsSubjectToken("foo", "tok"));
  EXPECT_NO_THROW(keystone::core::validateNatsSubjectToken("abc123", "tok"));
  EXPECT_NO_THROW(
      keystone::core::validateNatsSubjectToken("agent-core_7", "tok"));
}

TEST(NatsSubjectTokenTest, AcceptsSingleStarWildcard) {
  EXPECT_NO_THROW(keystone::core::validateNatsSubjectToken("*", "tok"));
}

TEST(NatsSubjectTokenTest, AcceptsGreaterThanWildcard) {
  EXPECT_NO_THROW(keystone::core::validateNatsSubjectToken(">", "tok"));
}

TEST(NatsSubjectTokenTest, RejectsDotInSingleToken) {
  // Dots are subject separators and must not appear inside a single token.
  EXPECT_THROW(keystone::core::validateNatsSubjectToken("foo.bar", "tok"),
               std::invalid_argument);
}

TEST(NatsSubjectTokenTest, RejectsEmptyToken) {
  EXPECT_THROW(keystone::core::validateNatsSubjectToken("", "tok"),
               std::invalid_argument);
}

TEST(NatsSubjectTokenTest, RejectsSlashInToken) {
  EXPECT_THROW(keystone::core::validateNatsSubjectToken("foo/bar", "tok"),
               std::invalid_argument);
}

TEST(NatsSubjectTokenTest, RejectsSpaceInToken) {
  EXPECT_THROW(keystone::core::validateNatsSubjectToken("foo bar", "tok"),
               std::invalid_argument);
}

TEST(NatsSubjectTokenTest, RejectsDoubleWildcard) {
  // "**" is not a valid NATS token.
  EXPECT_THROW(keystone::core::validateNatsSubjectToken("**", "tok"),
               std::invalid_argument);
}

TEST(NatsSubjectTokenTest, ReturnsValueUnchanged) {
  const std::string tok = "*";
  EXPECT_EQ(keystone::core::validateNatsSubjectToken(tok, "tok"), tok);
}

TEST(NatsSubjectTokenTest, ErrorMessageContainsLabel) {
  try {
    keystone::core::validateNatsSubjectToken("bad token", "my_label");
    FAIL() << "Expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    EXPECT_NE(std::string(e.what()).find("my_label"), std::string::npos);
  }
}

// =============================================================================
// validateNatsSubject -- full dot-separated subject validation
// =============================================================================

TEST(NatsSubjectTest, AcceptsSimpleSubject) {
  EXPECT_NO_THROW(
      keystone::core::validateNatsSubject("hi.agents.task-1", "subj"));
}

TEST(NatsSubjectTest, AcceptsSingleToken) {
  EXPECT_NO_THROW(keystone::core::validateNatsSubject("agents", "subj"));
}

TEST(NatsSubjectTest, AcceptsStarWildcardInMiddle) {
  EXPECT_NO_THROW(
      keystone::core::validateNatsSubject("hi.myrmidon.*.status", "subj"));
}

TEST(NatsSubjectTest, AcceptsGtWildcardAtEnd) {
  EXPECT_NO_THROW(keystone::core::validateNatsSubject("hi.research.>", "subj"));
}

TEST(NatsSubjectTest, AcceptsGtAloneAsSubject) {
  EXPECT_NO_THROW(keystone::core::validateNatsSubject(">", "subj"));
}

TEST(NatsSubjectTest, RejectsGtNotAtEnd) {
  EXPECT_THROW(keystone::core::validateNatsSubject("hi.>.extra", "subj"),
               std::invalid_argument);
}

TEST(NatsSubjectTest, RejectsEmptySubject) {
  EXPECT_THROW(keystone::core::validateNatsSubject("", "subj"),
               std::invalid_argument);
}

TEST(NatsSubjectTest, RejectsEmptyTokenBetweenDots) {
  EXPECT_THROW(keystone::core::validateNatsSubject("hi..agents", "subj"),
               std::invalid_argument);
}

TEST(NatsSubjectTest, RejectsSpaceInToken) {
  EXPECT_THROW(keystone::core::validateNatsSubject("hi.bad token.end", "subj"),
               std::invalid_argument);
}

TEST(NatsSubjectTest, ReturnsSubjectUnchanged) {
  const std::string subj = "hi.agents.>";
  EXPECT_EQ(keystone::core::validateNatsSubject(subj, "subj"), subj);
}
