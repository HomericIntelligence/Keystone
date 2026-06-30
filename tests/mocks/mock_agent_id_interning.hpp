#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <gmock/gmock.h>

namespace keystone::test {

/**
 * @brief Mock implementation of AgentIdInterning for testing
 *
 * Enables unit tests to verify agent ID interning behavior
 * independently of the real implementation.
 *
 * Usage in tests:
 * @code
 * MockAgentIdInterning mock_interning;
 * EXPECT_CALL(mock_interning, intern("agent_1")).WillOnce(Return(0));
 * uint32_t id = mock_interning.intern("agent_1");
 * @endcode
 */
class MockAgentIdInterning {
 public:
  MockAgentIdInterning() = default;
  ~MockAgentIdInterning() = default;

  /**
   * @brief Mock for intern() method
   *
   * Get or create integer ID for agent string
   */
  MOCK_METHOD(uint32_t, intern, (const std::string& agent_id), ());

  /**
   * @brief Mock for tryGetId() method
   *
   * Lookup integer ID for existing agent string
   */
  MOCK_METHOD(std::optional<uint32_t>, tryGetId, (const std::string& agent_id), (const));

  /**
   * @brief Mock for tryGetString() method
   *
   * Lookup agent string for existing integer ID
   */
  MOCK_METHOD(std::optional<std::string>, tryGetString, (uint32_t id), (const));

  /**
   * @brief Mock for clear() method
   *
   * Clear all mappings and reset ID counter
   */
  MOCK_METHOD(void, clear, (), ());

  /**
   * @brief Mock for size() method
   *
   * Get number of interned IDs
   */
  MOCK_METHOD(size_t, size, (), (const));
};

}  // namespace keystone::test
