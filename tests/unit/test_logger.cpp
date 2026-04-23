/**
 * @file test_logger.cpp
 * @brief Unit tests for Logger and LogContext
 */

#include "concurrency/logger.hpp"

#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace keystone::concurrency;

// Test: LogContext set and get
TEST(LogContextTest, SetAndGet) {
  LogContext::set("agent_1", 5, "session_abc");

  EXPECT_EQ(LogContext::getAgentId(), "agent_1");
  EXPECT_EQ(LogContext::getWorkerId(), int32_t{5});
  EXPECT_EQ(LogContext::getSessionId(), "session_abc");
}

// Test: LogContext clear
TEST(LogContextTest, Clear) {
  LogContext::set("agent_1", 5, "session_abc");
  LogContext::clear();

  EXPECT_EQ(LogContext::getAgentId(), "");
  EXPECT_EQ(LogContext::getWorkerId(), int32_t{-1});
  EXPECT_EQ(LogContext::getSessionId(), "");
}

// Test: LogContext formatting
TEST(LogContextTest, ContextString) {
  LogContext::set("chief", 0, "sess_1");
  EXPECT_EQ(LogContext::getContextString(), "[chief:0:sess_1]");
}

// Test: LogContext with no context set
TEST(LogContextTest, NoContext) {
  LogContext::clear();
  EXPECT_EQ(LogContext::getContextString(), "[no-context]");
}

// Test: LogContext is thread-local
TEST(LogContextTest, ThreadLocal) {
  LogContext::clear();
  LogContext::set("main_thread", 0, "main_session");

  std::string other_thread_context;

  std::thread t([&other_thread_context]() {
    // In new thread, context should be empty
    other_thread_context = LogContext::getContextString();

    // Set different context in this thread
    LogContext::set("worker_thread", 1, "worker_session");
    EXPECT_EQ(LogContext::getAgentId(), "worker_thread");
  });

  t.join();

  // Main thread context should be unchanged
  EXPECT_EQ(LogContext::getAgentId(), "main_thread");
  EXPECT_EQ(other_thread_context, "[no-context]");
}

// Test: Logger initialization
TEST(LoggerTest, Initialization) {
  Logger::init(spdlog::level::info);
  // If we get here without crash, initialization succeeded
  SUCCEED();
  Logger::shutdown();
}

// Test: Logger basic logging (no crash test)
TEST(LoggerTest, BasicLogging) {
  Logger::init(spdlog::level::info);
  LogContext::set("test_agent", 0, "test_session");

  // These should not crash
  Logger::info("Test message");
  Logger::info("Test with arg: {}", 42);
  Logger::warn("Warning message");
  Logger::error("Error message");

  SUCCEED();
  Logger::shutdown();
}

// Test: Logger with different levels
TEST(LoggerTest, LogLevels) {
  Logger::init(spdlog::level::trace);
  LogContext::set("test_agent", 0, "test_session");

  // All levels should work
  Logger::trace("Trace message");
  Logger::debug("Debug message");
  Logger::info("Info message");
  Logger::warn("Warn message");
  Logger::error("Error message");
  Logger::critical("Critical message");

  SUCCEED();
  Logger::shutdown();
}

// Test: Logger set level
TEST(LoggerTest, SetLevel) {
  Logger::init(spdlog::level::info);

  // Change level
  Logger::setLevel(spdlog::level::debug);
  Logger::debug("Debug message after level change");

  SUCCEED();
  Logger::shutdown();
}

// Test: Logger multiple initializations
TEST(LoggerTest, MultipleInitializations) {
  Logger::init(spdlog::level::info);
  Logger::init(spdlog::level::debug);  // Should not crash

  Logger::info("After multiple inits");

  SUCCEED();
  Logger::shutdown();
}

// Test: Logger thread safety
TEST(LoggerTest, ThreadSafety) {
  Logger::init(spdlog::level::info);

  std::vector<std::thread> threads;
  for (int32_t i = 0; i < 10; ++i) {
    threads.emplace_back([i]() {
      LogContext::set("worker_" + std::to_string(i), i, "session_1");

      for (int32_t j = 0; j < 100; ++j) {
        Logger::info("Thread {} message {}", i, j);
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  SUCCEED();
  Logger::shutdown();
}

// Test: Logger without initialization (auto-init)
TEST(LoggerTest, AutoInitialization) {
  // Logger should auto-initialize on first use
  Logger::info("Auto-initialized message");

  SUCCEED();
  Logger::shutdown();
}

// Test: LogContext with empty strings
TEST(LogContextTest, EmptyStrings) {
  LogContext::set("", 0, "");
  // Empty agent_id is treated as no context (better behavior)
  EXPECT_EQ(LogContext::getContextString(), "[no-context]");
}

// Test: LogContext with negative worker ID
TEST(LogContextTest, NegativeWorkerId) {
  LogContext::set("agent", -5, "session");
  EXPECT_EQ(LogContext::getWorkerId(), int32_t{-5});
  EXPECT_EQ(LogContext::getContextString(), "[agent:-5:session]");
}

// ---- Correlation ID tests ----

// Test: generateCorrelationId produces UUID4-format strings
TEST(CorrelationIdTest, FormatIsUUID4) {
  std::string id = generateCorrelationId();
  ASSERT_EQ(id.size(), 36u);
  EXPECT_EQ(id[8], '-');
  EXPECT_EQ(id[13], '-');
  EXPECT_EQ(id[18], '-');
  EXPECT_EQ(id[23], '-');
  // Version nibble must be '4'
  EXPECT_EQ(id[14], '4');
  // Variant nibble must be '8', '9', 'a', or 'b'
  char variant = id[19];
  EXPECT_TRUE(variant == '8' || variant == '9' || variant == 'a' || variant == 'b')
      << "variant nibble '" << variant << "' is not in {8,9,a,b}";
}

// Test: generateCorrelationId produces unique IDs
TEST(CorrelationIdTest, Uniqueness) {
  std::string id1 = generateCorrelationId();
  std::string id2 = generateCorrelationId();
  EXPECT_NE(id1, id2);
}

// Test: LogContext setCorrelationId / getCorrelationId
TEST(LogContextTest, SetAndGetCorrelationId) {
  LogContext::set("agent", 0, "sess");
  LogContext::setCorrelationId("test-corr-id");
  EXPECT_EQ(LogContext::getCorrelationId(), "test-corr-id");
}

// Test: correlation ID appears in context string
TEST(LogContextTest, ContextStringIncludesCorrelationId) {
  LogContext::set("agent", 0, "sess");
  LogContext::setCorrelationId("abc-123");
  EXPECT_EQ(LogContext::getContextString(), "[agent:0:sess:corr=abc-123]");
}

// Test: context string without correlation ID is unchanged
TEST(LogContextTest, ContextStringNoCorrelationId) {
  LogContext::set("agent", 0, "sess");
  LogContext::clearCorrelationId();
  EXPECT_EQ(LogContext::getContextString(), "[agent:0:sess]");
}

// Test: clear() also clears the correlation ID
TEST(LogContextTest, ClearAlsoClearsCorrelationId) {
  LogContext::set("agent", 0, "sess");
  LogContext::setCorrelationId("some-id");
  LogContext::clear();
  EXPECT_EQ(LogContext::getCorrelationId(), "");
}

// Test: clearCorrelationId leaves other fields intact
TEST(LogContextTest, ClearCorrelationIdLeavesContextIntact) {
  LogContext::set("agent", 2, "session");
  LogContext::setCorrelationId("keep-me-not");
  LogContext::clearCorrelationId();
  EXPECT_EQ(LogContext::getAgentId(), "agent");
  EXPECT_EQ(LogContext::getWorkerId(), int32_t{2});
  EXPECT_EQ(LogContext::getSessionId(), "session");
  EXPECT_EQ(LogContext::getCorrelationId(), "");
}

// Test: CorrelationScope sets a fresh correlation ID
TEST(CorrelationScopeTest, SetsCorrelationId) {
  LogContext::set("agent", 0, "sess");
  LogContext::clearCorrelationId();
  {
    CorrelationScope scope;
    EXPECT_FALSE(scope.id().empty());
    EXPECT_EQ(LogContext::getCorrelationId(), scope.id());
  }
}

// Test: CorrelationScope restores previous correlation ID on destruction
TEST(CorrelationScopeTest, RestoresPreviousId) {
  LogContext::set("agent", 0, "sess");
  LogContext::setCorrelationId("outer-id");
  {
    CorrelationScope scope;
    EXPECT_NE(LogContext::getCorrelationId(), "outer-id");
  }
  EXPECT_EQ(LogContext::getCorrelationId(), "outer-id");
}

// Test: CorrelationScope accepts a caller-supplied ID
TEST(CorrelationScopeTest, AcceptsSuppliedId) {
  LogContext::set("agent", 0, "sess");
  {
    CorrelationScope scope("my-fixed-id");
    EXPECT_EQ(scope.id(), "my-fixed-id");
    EXPECT_EQ(LogContext::getCorrelationId(), "my-fixed-id");
  }
}

// Test: nested CorrelationScopes restore correctly
TEST(CorrelationScopeTest, NestedScopesRestoreCorrectly) {
  LogContext::set("agent", 0, "sess");
  LogContext::setCorrelationId("root");
  {
    CorrelationScope outer("outer");
    EXPECT_EQ(LogContext::getCorrelationId(), "outer");
    {
      CorrelationScope inner("inner");
      EXPECT_EQ(LogContext::getCorrelationId(), "inner");
    }
    EXPECT_EQ(LogContext::getCorrelationId(), "outer");
  }
  EXPECT_EQ(LogContext::getCorrelationId(), "root");
}

// Test: correlation ID is thread-local (each thread has its own)
TEST(CorrelationScopeTest, ThreadLocalIsolation) {
  LogContext::set("main", 0, "main-sess");
  std::string main_id = generateCorrelationId();
  LogContext::setCorrelationId(main_id);

  std::string other_thread_id;
  std::thread t([&other_thread_id]() {
    // New thread starts with empty correlation ID
    other_thread_id = LogContext::getCorrelationId();
    LogContext::set("worker", 1, "worker-sess");
    CorrelationScope scope;
    // Doesn't affect main thread
  });
  t.join();

  EXPECT_EQ(other_thread_id, "");
  EXPECT_EQ(LogContext::getCorrelationId(), main_id);
}

// Test: all log lines in a scope share the same correlation ID
TEST(CorrelationScopeTest, LogLinesShareId) {
  Logger::init(spdlog::level::info);
  LogContext::set("agent", 0, "sess");
  {
    CorrelationScope scope;
    std::string active_id = LogContext::getCorrelationId();
    Logger::info("First log line, corr={}", active_id);
    Logger::info("Second log line, corr={}", LogContext::getCorrelationId());
    EXPECT_EQ(LogContext::getCorrelationId(), active_id);
  }
  SUCCEED();
  Logger::shutdown();
}
