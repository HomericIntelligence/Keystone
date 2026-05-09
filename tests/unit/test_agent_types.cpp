/**
 * @file test_agent_types.cpp
 * @brief Unit tests for agent type definitions (AgentLevel enum)
 */

#include <gtest/gtest.h>

#include "core/agent_types.hpp"

namespace keystone {
namespace core {
namespace {

// =============================================================================
// AgentLevel Enum Tests
// =============================================================================

TEST(AgentTypesTest, AgentLevelEnumValues) {
  // Verify enum underlying values are correct (0-3)
  EXPECT_EQ(static_cast<uint8_t>(AgentLevel::L0), 0u);
  EXPECT_EQ(static_cast<uint8_t>(AgentLevel::L1), 1u);
  EXPECT_EQ(static_cast<uint8_t>(AgentLevel::L2), 2u);
  EXPECT_EQ(static_cast<uint8_t>(AgentLevel::L3), 3u);
}

TEST(AgentTypesTest, AgentLevelToString) {
  EXPECT_EQ(agentLevelToString(AgentLevel::L0), "L0");
  EXPECT_EQ(agentLevelToString(AgentLevel::L1), "L1");
  EXPECT_EQ(agentLevelToString(AgentLevel::L2), "L2");
  EXPECT_EQ(agentLevelToString(AgentLevel::L3), "L3");
}

TEST(AgentTypesTest, StringToAgentLevel) {
  // Valid conversions
  auto l0 = stringToAgentLevel("L0");
  ASSERT_TRUE(l0.has_value());
  EXPECT_EQ(l0.value(), AgentLevel::L0);

  auto l1 = stringToAgentLevel("L1");
  ASSERT_TRUE(l1.has_value());
  EXPECT_EQ(l1.value(), AgentLevel::L1);

  auto l2 = stringToAgentLevel("L2");
  ASSERT_TRUE(l2.has_value());
  EXPECT_EQ(l2.value(), AgentLevel::L2);

  auto l3 = stringToAgentLevel("L3");
  ASSERT_TRUE(l3.has_value());
  EXPECT_EQ(l3.value(), AgentLevel::L3);

  // Invalid conversions
  EXPECT_FALSE(stringToAgentLevel("L4").has_value());
  EXPECT_FALSE(stringToAgentLevel("").has_value());
  EXPECT_FALSE(stringToAgentLevel("l0").has_value());  // case-sensitive
  EXPECT_FALSE(stringToAgentLevel("Level0").has_value());
  EXPECT_FALSE(stringToAgentLevel("INVALID").has_value());
}

TEST(AgentTypesTest, AgentLevelValue) {
  EXPECT_EQ(agentLevelValue(AgentLevel::L0), 0u);
  EXPECT_EQ(agentLevelValue(AgentLevel::L1), 1u);
  EXPECT_EQ(agentLevelValue(AgentLevel::L2), 2u);
  EXPECT_EQ(agentLevelValue(AgentLevel::L3), 3u);
}

TEST(AgentTypesTest, IsValidAgentLevel) {
  // Valid levels (0-3)
  EXPECT_TRUE(isValidAgentLevel(0));
  EXPECT_TRUE(isValidAgentLevel(1));
  EXPECT_TRUE(isValidAgentLevel(2));
  EXPECT_TRUE(isValidAgentLevel(3));

  // Invalid levels (4+)
  EXPECT_FALSE(isValidAgentLevel(4));
  EXPECT_FALSE(isValidAgentLevel(5));
  EXPECT_FALSE(isValidAgentLevel(10));
  EXPECT_FALSE(isValidAgentLevel(255));
}

TEST(AgentTypesTest, ValueToAgentLevel) {
  // Valid values
  auto l0 = valueToAgentLevel(0);
  ASSERT_TRUE(l0.has_value());
  EXPECT_EQ(l0.value(), AgentLevel::L0);

  auto l1 = valueToAgentLevel(1);
  ASSERT_TRUE(l1.has_value());
  EXPECT_EQ(l1.value(), AgentLevel::L1);

  auto l2 = valueToAgentLevel(2);
  ASSERT_TRUE(l2.has_value());
  EXPECT_EQ(l2.value(), AgentLevel::L2);

  auto l3 = valueToAgentLevel(3);
  ASSERT_TRUE(l3.has_value());
  EXPECT_EQ(l3.value(), AgentLevel::L3);

  // Invalid values
  EXPECT_FALSE(valueToAgentLevel(4).has_value());
  EXPECT_FALSE(valueToAgentLevel(5).has_value());
  EXPECT_FALSE(valueToAgentLevel(255).has_value());
}

TEST(AgentTypesTest, RoundTripConversion) {
  // Test: enum → string → enum
  for (uint8_t i = 0; i <= 3; ++i) {
    auto level = valueToAgentLevel(i);
    ASSERT_TRUE(level.has_value());

    std::string str = agentLevelToString(level.value());
    auto converted = stringToAgentLevel(str);
    ASSERT_TRUE(converted.has_value());
    EXPECT_EQ(converted.value(), level.value());
  }

  // Test: enum → value → enum
  AgentLevel levels[] = {AgentLevel::L0, AgentLevel::L1, AgentLevel::L2,
                         AgentLevel::L3};
  for (auto level : levels) {
    uint8_t value = agentLevelValue(level);
    auto converted = valueToAgentLevel(value);
    ASSERT_TRUE(converted.has_value());
    EXPECT_EQ(converted.value(), level);
  }
}

TEST(AgentTypesTest, EnumSize) {
  // Verify enum uses uint8_t storage (1 byte)
  EXPECT_EQ(sizeof(AgentLevel), sizeof(uint8_t));
  EXPECT_EQ(sizeof(AgentLevel), 1u);
}

}  // namespace
}  // namespace core
}  // namespace keystone
