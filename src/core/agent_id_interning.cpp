#include "core/agent_id_interning.hpp"

#include <limits>
#include <shared_mutex>
#include <stdexcept>

namespace keystone {
namespace core {

uint32_t AgentIdInterning::intern(const std::string& agent_id) {
  // First, try read-only lookup (shared lock - multiple readers allowed)
  {
    std::shared_lock<std::shared_mutex> read_lock(mutex_);
    auto it = string_to_id_.find(agent_id);
    if (it != string_to_id_.end()) {
      return it->second;  // Found existing ID
    }
  }  // Release read lock before acquiring write lock

  // Not found, need to create new ID (exclusive lock - single writer)
  std::unique_lock<std::shared_mutex> write_lock(mutex_);

  // Double-check: another thread might have created it while we waited for
  // write lock
  auto it = string_to_id_.find(agent_id);
  if (it != string_to_id_.end()) {
    return it->second;  // Another thread beat us to it
  }

  // SECURITY FIX: Check for ID space exhaustion before incrementing
  // uint32_t wraps to 0 after 4,294,967,295, causing ID collisions
  if (next_id_ == std::numeric_limits<uint32_t>::max()) {
    throw std::overflow_error("Agent ID space exhausted: Cannot register more than " +
                              std::to_string(std::numeric_limits<uint32_t>::max()) + " agents");
  }

  // Create new ID
  uint32_t new_id = next_id_++;
  string_to_id_[agent_id] = new_id;
  id_to_string_[new_id] = agent_id;

  return new_id;
}

std::optional<uint32_t> AgentIdInterning::tryGetId(const std::string& agent_id) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = string_to_id_.find(agent_id);
  if (it != string_to_id_.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::optional<std::string> AgentIdInterning::tryGetString(uint32_t id) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = id_to_string_.find(id);
  if (it != id_to_string_.end()) {
    return it->second;
  }
  return std::nullopt;
}

void AgentIdInterning::clear() {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  string_to_id_.clear();
  id_to_string_.clear();
  next_id_ = 0;
}

size_t AgentIdInterning::size() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return string_to_id_.size();
}

}  // namespace core
}  // namespace keystone
