#pragma once

#include <regex>
#include <stdexcept>
#include <string>

namespace keystone {
namespace core {

// Matches UUIDs, alphanumeric slugs, hyphens, and underscores.
// Rejects path traversal characters (/, .., spaces, newlines, semicolons, etc.).
// Applied at all external input boundaries where IDs are extracted from
// NATS subject tokens or network messages before use in routing/API calls.
inline const std::regex& safeIdPattern() {
  static const std::regex kPattern{R"(^[a-zA-Z0-9_-]+$)"};
  return kPattern;
}

/**
 * @brief Validate that a subject token is safe to use as an identifier.
 *
 * Rejects empty strings and any value containing path traversal characters,
 * slashes, spaces, or other characters outside [a-zA-Z0-9_-].
 *
 * @param value The token value to validate (e.g. agent_id, task_id).
 * @param label Human-readable label used in the exception message.
 * @return The validated value (unchanged).
 * @throws std::invalid_argument if value is empty or contains unsafe characters.
 */
inline const std::string& validateSubjectToken(const std::string& value,
                                               const std::string& label) {
  if (value.empty() || !std::regex_match(value, safeIdPattern())) {
    throw std::invalid_argument("Invalid " + label + ": unsafe characters in '" + value + "'");
  }
  return value;
}

}  // namespace core
}  // namespace keystone
