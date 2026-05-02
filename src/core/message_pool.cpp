#include "core/message_pool.hpp"

#include <algorithm>

namespace keystone {
namespace core {

MessagePool::ThreadLocalData& MessagePool::getThreadLocal() {
  thread_local ThreadLocalData data;
  return data;
}

KeystoneMessage MessagePool::acquire() {
  auto& tld = getThreadLocal();
  tld.stats.total_acquires++;

  if (!tld.pool.empty()) {
    // Pool hit: Reuse existing message
    tld.stats.pool_hits++;

    KeystoneMessage msg = std::move(tld.pool.back());
    tld.pool.pop_back();

    tld.stats.current_size = tld.pool.size();
    return msg;
  }

  // Pool miss: Create new message with NORMAL priority (default)
  tld.stats.pool_misses++;
  KeystoneMessage msg{};
  msg.priority = Priority::NORMAL;
  return msg;
}

void MessagePool::release(KeystoneMessage&& msg) {
  auto& tld = getThreadLocal();
  tld.stats.total_releases++;

  if (tld.pool.size() >= MAX_POOL_SIZE) {
    // Pool full - let message be destroyed (RAII)
    return;
  }

  // Reset message to default state before returning to pool
  msg.msg_id.clear();
  msg.sender_id.clear();
  msg.receiver_id.clear();
  msg.command.clear();
  msg.payload.reset();
  msg.priority = Priority::NORMAL;
  msg.deadline.reset();
  // timestamp will be overwritten on next use

  tld.pool.push_back(std::move(msg));

  tld.stats.current_size = tld.pool.size();
  if (tld.pool.size() > tld.stats.max_size_reached) {
    tld.stats.max_size_reached = tld.pool.size();
  }
}

size_t MessagePool::getPoolSize() { return getThreadLocal().pool.size(); }

void MessagePool::clear() {
  auto& tld = getThreadLocal();
  tld.pool.clear();
  tld.stats.current_size = 0;
}

MessagePool::PoolStats MessagePool::getStats() {
  return getThreadLocal().stats;
}

void MessagePool::resetStats() {
  auto& tld = getThreadLocal();
  tld.stats = PoolStats{};
  tld.stats.current_size = tld.pool.size();
}

}  // namespace core
}  // namespace keystone
