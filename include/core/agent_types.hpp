#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace keystone {
namespace core {

/**
 * @brief Agent hierarchy levels in the 4-layer HMAS architecture
 *
 * ProjectKeystone implements a 4-layer hierarchy:
 * - L0 (Chief Architect): Strategic decisions, system-wide coordination
 * - L1 (Component Lead): Component architecture, module coordination
 * - L2 (Module Lead): Module design, task decomposition
 * - L3 (Task Agent): Concrete execution, code implementation
 *
 * Using uint8_t ensures minimal memory footprint (4 possible values: 0-3)
 * while providing type safety and preventing invalid levels at compile time.
 */
enum class AgentLevel : uint8_t {
  L0 = 0,  ///< Chief Architect (Level 0)
  L1 = 1,  ///< Component Lead (Level 1)
  L2 = 2,  ///< Module Lead (Level 2)
  L3 = 3   ///< Task Agent (Level 3)
};

/**
 * @brief Convert AgentLevel enum to string representation
 *
 * @param level Agent level enum value
 * @return String representation ("L0", "L1", "L2", "L3")
 *
 * Example:
 * @code
 * std::string name = agentLevelToString(AgentLevel::L0);  // "L0"
 * @endcode
 */
inline std::string agentLevelToString(AgentLevel level) {
  switch (level) {
    case AgentLevel::L0:
      return "L0";
    case AgentLevel::L1:
      return "L1";
    case AgentLevel::L2:
      return "L2";
    case AgentLevel::L3:
      return "L3";
    default:
      return "UNKNOWN";
  }
}

/**
 * @brief Convert string to AgentLevel enum
 *
 * @param str String representation ("L0", "L1", "L2", "L3")
 * @return AgentLevel if valid, nullopt if invalid
 *
 * Example:
 * @code
 * auto level = stringToAgentLevel("L2");
 * if (level.has_value()) {
 *   // Use level.value()
 * }
 * @endcode
 */
inline std::optional<AgentLevel> stringToAgentLevel(std::string_view str) {
  if (str == "L0")
    return AgentLevel::L0;
  if (str == "L1")
    return AgentLevel::L1;
  if (str == "L2")
    return AgentLevel::L2;
  if (str == "L3")
    return AgentLevel::L3;
  return std::nullopt;
}

/**
 * @brief Get numeric value of AgentLevel
 *
 * @param level Agent level enum value
 * @return Underlying uint8_t value (0, 1, 2, or 3)
 *
 * Example:
 * @code
 * uint8_t value = agentLevelValue(AgentLevel::L2);  // 2
 * @endcode
 */
inline uint8_t agentLevelValue(AgentLevel level) {
  return static_cast<uint8_t>(level);
}

/**
 * @brief Check if level is valid (L0-L3 range)
 *
 * @param value Numeric value to check
 * @return true if value is in range [0, 3], false otherwise
 */
inline bool isValidAgentLevel(uint8_t value) {
  return value <= 3;
}

/**
 * @brief Convert numeric value to AgentLevel
 *
 * @param value Numeric value (0-3)
 * @return AgentLevel if valid, nullopt if out of range
 *
 * Example:
 * @code
 * auto level = valueToAgentLevel(2);  // AgentLevel::L2
 * @endcode
 */
inline std::optional<AgentLevel> valueToAgentLevel(uint8_t value) {
  if (!isValidAgentLevel(value)) {
    return std::nullopt;
  }
  return static_cast<AgentLevel>(value);
}

}  // namespace core
}  // namespace keystone
