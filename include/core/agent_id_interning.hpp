#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace keystone {
namespace core {

/**
 * @brief Bidirectional string ↔ integer ID mapping for agent IDs
 *
 * Optimization (Phase A2): Convert O(log n) string lookups to O(1) integer
 * lookups.
 *
 * Performance Benefits:
 * - String comparison (strcmp): O(n) where n = string length
 * - Integer comparison: O(1)
 * - Hash map lookup with integers is faster than strings
 * - Expected 20-30% performance improvement in registry operations
 *
 * Thread Safety:
 * - Uses std::shared_mutex for reader-writer lock
 * - Multiple concurrent readers allowed
 * - Single writer at a time
 * - Optimized for read-heavy workloads (typical agent lookup pattern)
 *
 * Usage:
 * @code
 * AgentIdInterning interning;
 * uint32_t id = interning.intern("agent_123");  // Get or create ID
 * auto str = interning.tryGetString(id);        // Reverse lookup
 * @endcode
 */
class AgentIdInterning {
 public:
  AgentIdInterning() = default;
  ~AgentIdInterning() = default;

  // Non-copyable, non-movable (contains mutex)
  AgentIdInterning(const AgentIdInterning&) = delete;
  AgentIdInterning& operator=(const AgentIdInterning&) = delete;
  AgentIdInterning(AgentIdInterning&&) = delete;
  AgentIdInterning& operator=(AgentIdInterning&&) = delete;

  /**
   * @brief Get or create integer ID for agent string
   *
   * If the string has been interned before, returns existing ID.
   * Otherwise, creates a new ID and stores the mapping.
   *
   * Thread-safe with exclusive lock (writer).
   *
   * @param agent_id Agent string identifier
   * @return uint32_t Unique integer ID (0, 1, 2, ...)
   */
  uint32_t intern(const std::string& agent_id);

  /**
   * @brief Lookup integer ID for existing agent string
   *
   * Does NOT create a new ID if string is not found.
   *
   * Thread-safe with shared lock (reader).
   *
   * @param agent_id Agent string identifier
   * @return std::optional<uint32_t> Integer ID if found, nullopt otherwise
   */
  std::optional<uint32_t> tryGetId(const std::string& agent_id) const;

  /**
   * @brief Lookup agent string for existing integer ID
   *
   * Reverse lookup: integer → string.
   *
   * Thread-safe with shared lock (reader).
   *
   * @param id Integer ID
   * @return std::optional<std::string> Agent string if found, nullopt otherwise
   */
  std::optional<std::string> tryGetString(uint32_t id) const;

  /**
   * @brief Clear all mappings and reset ID counter
   *
   * Thread-safe with exclusive lock (writer).
   */
  void clear();

  /**
   * @brief Get number of interned IDs
   *
   * Thread-safe with shared lock (reader).
   *
   * @return size_t Number of agent IDs interned
   */
  size_t size() const;

 private:
  // Bidirectional mapping: string ↔ uint32_t
  std::unordered_map<std::string, uint32_t> string_to_id_;
  std::unordered_map<uint32_t, std::string> id_to_string_;

  // Next available ID (increments sequentially: 0, 1, 2, ...)
  uint32_t next_id_{0};

  // Reader-writer lock: multiple readers OR single writer
  mutable std::shared_mutex mutex_;
};

}  // namespace core
}  // namespace keystone
