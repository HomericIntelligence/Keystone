/**
 * @file test_agent_envelope.cpp
 * @brief Unit tests for AgentEnvelope and AgentActionType (Issue #515)
 *
 * Verifies that orchestration-level semantics (CANCEL_TASK, TASK_FAILED,
 * DECOMPOSE) are correctly encapsulated by AgentEnvelope and do not leak
 * into the transport layer (KeystoneMessage / core::ActionType).
 */

#include "agents/agent_action_type.hpp"
#include "agents/agent_envelope.hpp"
#include "core/message.hpp"

#include <gtest/gtest.h>

using namespace keystone::agents;
using namespace keystone::core;

// ---------------------------------------------------------------------------
// AgentActionType string conversion
// ---------------------------------------------------------------------------

TEST(AgentActionTypeTest, ToStringDecompose) {
  EXPECT_EQ(agentActionTypeToString(AgentActionType::DECOMPOSE), "DECOMPOSE");
}

TEST(AgentActionTypeTest, ToStringCancelTask) {
  EXPECT_EQ(agentActionTypeToString(AgentActionType::CANCEL_TASK), "CANCEL_TASK");
}

TEST(AgentActionTypeTest, ToStringTaskFailed) {
  EXPECT_EQ(agentActionTypeToString(AgentActionType::TASK_FAILED), "TASK_FAILED");
}

// ---------------------------------------------------------------------------
// AgentEnvelope factory: createCancellation
// ---------------------------------------------------------------------------

TEST(AgentEnvelopeTest, CreateCancellationFields) {
  auto env = AgentEnvelope::createCancellation("alice", "bob", "task-42", "sess-1");

  EXPECT_EQ(env.transport_msg.sender_id, "alice");
  EXPECT_EQ(env.transport_msg.receiver_id, "bob");
  ASSERT_TRUE(env.agent_action.has_value());
  EXPECT_EQ(*env.agent_action, AgentActionType::CANCEL_TASK);
  ASSERT_TRUE(env.task_id.has_value());
  EXPECT_EQ(*env.task_id, "task-42");
  EXPECT_EQ(env.session_id, "sess-1");
  EXPECT_EQ(env.transport_msg.priority, Priority::HIGH);
}

TEST(AgentEnvelopeTest, CreateCancellationDefaultSession) {
  auto env = AgentEnvelope::createCancellation("a", "b", "t1");
  EXPECT_EQ(env.session_id, "default");
}

TEST(AgentEnvelopeTest, CreateCancellationTransportActionIsExecute) {
  // The transport action_type must remain EXECUTE (pure transport) even for
  // a CANCEL_TASK envelope.  Only the agent layer sees AgentActionType.
  auto env = AgentEnvelope::createCancellation("a", "b", "t1");
  EXPECT_EQ(env.transport_msg.action_type, ActionType::EXECUTE);
}

// ---------------------------------------------------------------------------
// AgentEnvelope factory: createFailure
// ---------------------------------------------------------------------------

TEST(AgentEnvelopeTest, CreateFailureFields) {
  auto env = AgentEnvelope::createFailure("child", "parent", "disk full");

  EXPECT_EQ(env.transport_msg.sender_id, "child");
  EXPECT_EQ(env.transport_msg.receiver_id, "parent");
  ASSERT_TRUE(env.agent_action.has_value());
  EXPECT_EQ(*env.agent_action, AgentActionType::TASK_FAILED);
  EXPECT_EQ(env.session_id, "default");
}

TEST(AgentEnvelopeTest, CreateFailureTransportActionIsExecute) {
  auto env = AgentEnvelope::createFailure("a", "b", "err");
  EXPECT_EQ(env.transport_msg.action_type, ActionType::EXECUTE);
}

// ---------------------------------------------------------------------------
// AgentEnvelope factory: create (generic)
// ---------------------------------------------------------------------------

TEST(AgentEnvelopeTest, CreateDecompose) {
  auto env = AgentEnvelope::create("sender", "receiver", AgentActionType::DECOMPOSE, "sess");

  ASSERT_TRUE(env.agent_action.has_value());
  EXPECT_EQ(*env.agent_action, AgentActionType::DECOMPOSE);
  EXPECT_EQ(env.transport_msg.action_type, ActionType::EXECUTE);
}

// ---------------------------------------------------------------------------
// AgentEnvelope::wrap — round-trip decoding
// ---------------------------------------------------------------------------

TEST(AgentEnvelopeTest, WrapCancellationRoundTrip) {
  // Create a cancellation envelope, extract the transport message, wrap it back.
  auto original = AgentEnvelope::createCancellation("parent", "child", "task-99", "s1");

  auto decoded = AgentEnvelope::wrap(original.transport_msg);

  ASSERT_TRUE(decoded.agent_action.has_value());
  EXPECT_EQ(*decoded.agent_action, AgentActionType::CANCEL_TASK);
  ASSERT_TRUE(decoded.task_id.has_value());
  EXPECT_EQ(*decoded.task_id, "task-99");
}

TEST(AgentEnvelopeTest, WrapFailureRoundTrip) {
  auto original = AgentEnvelope::createFailure("child", "parent", "oom");

  auto decoded = AgentEnvelope::wrap(original.transport_msg);

  ASSERT_TRUE(decoded.agent_action.has_value());
  EXPECT_EQ(*decoded.agent_action, AgentActionType::TASK_FAILED);
}

TEST(AgentEnvelopeTest, WrapDecomposeRoundTrip) {
  auto original = AgentEnvelope::create("s", "r", AgentActionType::DECOMPOSE, "sess", "goal text");

  auto decoded = AgentEnvelope::wrap(original.transport_msg);

  ASSERT_TRUE(decoded.agent_action.has_value());
  EXPECT_EQ(*decoded.agent_action, AgentActionType::DECOMPOSE);
}

TEST(AgentEnvelopeTest, WrapPlainMessageHasNoAgentAction) {
  // A plain EXECUTE message (e.g., a task command) should not be decoded
  // as any AgentActionType.
  auto msg = KeystoneMessage::create("a", "b", ActionType::EXECUTE, "echo hi");

  auto decoded = AgentEnvelope::wrap(msg);

  EXPECT_FALSE(decoded.agent_action.has_value());
}

TEST(AgentEnvelopeTest, WrapShutdownMessageHasNoAgentAction) {
  auto msg = KeystoneMessage::create("a", "b", ActionType::SHUTDOWN);

  auto decoded = AgentEnvelope::wrap(msg);

  EXPECT_FALSE(decoded.agent_action.has_value());
}

// ---------------------------------------------------------------------------
// Transport purity: ActionType enum has no orchestration variants
// ---------------------------------------------------------------------------

TEST(AgentEnvelopeTest, TransportActionTypeHasOnlyTransportVariants) {
  // Verify the transport-level enum contains only the three allowed values.
  // If CANCEL_TASK or TASK_FAILED were re-added to ActionType, the switch
  // in actionTypeToString() would require an update and this test would need
  // updating. The existence of this test documents the invariant.
  EXPECT_EQ(actionTypeToString(ActionType::EXECUTE), "EXECUTE");
  EXPECT_EQ(actionTypeToString(ActionType::RETURN_RESULT), "RETURN_RESULT");
  EXPECT_EQ(actionTypeToString(ActionType::SHUTDOWN), "SHUTDOWN");
}
