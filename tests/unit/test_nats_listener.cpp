/**
 * @file test_nats_listener.cpp
 * @brief Unit tests for NATSListener subject parsing and ack/nak routing.
 *
 * These tests exercise the pure classify_subject() logic without requiring a
 * real NATS server.  Every code path that determines whether a message is
 * acked, naked, or triggers a DAG callback is covered here (issue #86).
 */

#include "network/nats_listener.hpp"

#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

using keystone::network::NATSListener;
using keystone::network::NATSListenerConfig;
using keystone::network::SubjectVerdict;

// ---------------------------------------------------------------------------
// Subject parsing — kMalformed
// ---------------------------------------------------------------------------

TEST(NATSListenerClassify, MalformedSubject_TooFewParts) {
  auto cls = NATSListener::classify_subject("hi.tasks.team1.task1");
  EXPECT_EQ(cls.verdict, SubjectVerdict::kMalformed);
}

TEST(NATSListenerClassify, MalformedSubject_EmptyString) {
  auto cls = NATSListener::classify_subject("");
  EXPECT_EQ(cls.verdict, SubjectVerdict::kMalformed);
}

TEST(NATSListenerClassify, MalformedSubject_NoParts) {
  auto cls = NATSListener::classify_subject("notdotted");
  EXPECT_EQ(cls.verdict, SubjectVerdict::kMalformed);
}

// ---------------------------------------------------------------------------
// Subject parsing — kUnsafeToken (path traversal / special chars)
// ---------------------------------------------------------------------------

TEST(NATSListenerClassify, UnsafeTeamId_PathTraversal) {
  auto cls = NATSListener::classify_subject("hi.tasks.../../etc/passwd.task1.completed");
  EXPECT_EQ(cls.verdict, SubjectVerdict::kUnsafeToken);
}

TEST(NATSListenerClassify, UnsafeTaskId_Slash) {
  auto cls = NATSListener::classify_subject("hi.tasks.team1.tas/k1.completed");
  EXPECT_EQ(cls.verdict, SubjectVerdict::kUnsafeToken);
}

TEST(NATSListenerClassify, UnsafeTeamId_EmptyToken) {
  // Empty token between dots produces an empty part
  auto cls = NATSListener::classify_subject("hi.tasks..task1.completed");
  EXPECT_EQ(cls.verdict, SubjectVerdict::kUnsafeToken);
}

TEST(NATSListenerClassify, UnsafeTaskId_Dot) {
  auto cls = NATSListener::classify_subject("hi.tasks.team1.ta.sk.completed");
  // parts[3] == "sk" which is safe; but this now has 6 parts and verb=completed
  // which is terminal.  The point: verify that dots within a token are caught
  // by the splitter correctly – there is no dot-in-token issue.
  // Actually "hi.tasks.team1.ta.sk.completed" splits into 6 parts:
  //   [0]=hi [1]=tasks [2]=team1 [3]=ta [4]=sk [5]=completed
  // team_id=team1, task_id=ta, verb=sk — verb "sk" is unknown → kUnknownVerb.
  EXPECT_EQ(cls.verdict, SubjectVerdict::kUnknownVerb);
}

// ---------------------------------------------------------------------------
// Subject parsing — kUnknownVerb
// ---------------------------------------------------------------------------

TEST(NATSListenerClassify, UnknownVerb) {
  auto cls = NATSListener::classify_subject("hi.tasks.team1.task1.purged");
  EXPECT_EQ(cls.verdict, SubjectVerdict::kUnknownVerb);
}

// ---------------------------------------------------------------------------
// Subject parsing — kNonTerminalVerb
// ---------------------------------------------------------------------------

TEST(NATSListenerClassify, NonTerminalVerb_Updated) {
  auto cls = NATSListener::classify_subject("hi.tasks.team1.task1.updated");
  EXPECT_EQ(cls.verdict, SubjectVerdict::kNonTerminalVerb);
}

TEST(NATSListenerClassify, NonTerminalVerb_Created) {
  auto cls = NATSListener::classify_subject("hi.tasks.team1.task1.created");
  EXPECT_EQ(cls.verdict, SubjectVerdict::kNonTerminalVerb);
}

TEST(NATSListenerClassify, NonTerminalVerb_Assigned) {
  auto cls = NATSListener::classify_subject("hi.tasks.team1.task1.assigned");
  EXPECT_EQ(cls.verdict, SubjectVerdict::kNonTerminalVerb);
}

TEST(NATSListenerClassify, NonTerminalVerb_Started) {
  auto cls = NATSListener::classify_subject("hi.tasks.team1.task1.started");
  EXPECT_EQ(cls.verdict, SubjectVerdict::kNonTerminalVerb);
}

// ---------------------------------------------------------------------------
// Subject parsing — kTerminal
// ---------------------------------------------------------------------------

TEST(NATSListenerClassify, TerminalVerb_Completed) {
  auto cls = NATSListener::classify_subject("hi.tasks.team1.task1.completed");
  EXPECT_EQ(cls.verdict, SubjectVerdict::kTerminal);
  EXPECT_EQ(cls.team_id, "team1");
  EXPECT_EQ(cls.task_id, "task1");
  EXPECT_EQ(cls.verb, "completed");
}

TEST(NATSListenerClassify, TerminalVerb_Failed) {
  auto cls = NATSListener::classify_subject("hi.tasks.team-a.task_99.failed");
  EXPECT_EQ(cls.verdict, SubjectVerdict::kTerminal);
  EXPECT_EQ(cls.team_id, "team-a");
  EXPECT_EQ(cls.task_id, "task_99");
  EXPECT_EQ(cls.verb, "failed");
}

TEST(NATSListenerClassify, TerminalVerb_ValidAlphanumericIds) {
  auto cls = NATSListener::classify_subject("hi.tasks.ABC123.XYZ789.completed");
  EXPECT_EQ(cls.verdict, SubjectVerdict::kTerminal);
  EXPECT_EQ(cls.team_id, "ABC123");
  EXPECT_EQ(cls.task_id, "XYZ789");
}

// ---------------------------------------------------------------------------
// Constructor validation
// ---------------------------------------------------------------------------

TEST(NATSListenerConstruct, NullCallbackThrows) {
  NATSListenerConfig cfg;
  cfg.subject = "hi.tasks.>";
  cfg.durable_name = "test-consumer";
  EXPECT_THROW(NATSListener(cfg, nullptr), std::invalid_argument);
}

TEST(NATSListenerConstruct, ValidConstruct) {
  NATSListenerConfig cfg;
  cfg.subject = "hi.tasks.>";
  cfg.durable_name = "test-consumer";
  bool called = false;
  EXPECT_NO_THROW(NATSListener(cfg, [&](std::string_view, std::string_view) { called = true; }));
}
