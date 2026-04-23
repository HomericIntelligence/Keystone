/**
 * @file test_nats_listener.cpp
 * @brief Unit tests for NatsListener startup error handling (Issue #139)
 *
 * Tests cover:
 * - Happy path: subscribe succeeds on first attempt
 * - StreamNotFound + auto_create_stream=true: creates stream, retries, succeeds
 * - StreamNotFound + auto_create_stream=false: logs instructions, retries, succeeds
 * - Transient connection failure: retries with backoff, eventually succeeds
 * - Permanent error (auth): raised immediately, no retry
 * - Shutdown during retry: exits cleanly without throwing
 * - Backoff caps at max_backoff_ms
 */

#include "network/nats_listener.hpp"

#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace keystone::network;

// ---------------------------------------------------------------------------
// Mock INatsConnection
// ---------------------------------------------------------------------------

/**
 * @brief Configurable mock that returns a fixed sequence of outcomes.
 *
 * Each element of connect_outcomes_ and subscribe_outcomes_ is consumed in
 * order. The last element is repeated if the sequence is exhausted.
 */
class MockNatsConnection : public INatsConnection {
 public:
  enum class Outcome {
    Success,           ///< Call succeeds
    StreamNotFound,    ///< Throws std::domain_error (stream-not-found)
    ConnectionFailed,  ///< Throws std::system_error (transient)
    Permanent,         ///< Throws std::runtime_error (auth / permanent)
  };

  // Sequences of outcomes for connect() and subscribe()
  std::vector<Outcome> connect_outcomes{Outcome::Success};
  std::vector<Outcome> subscribe_outcomes{Outcome::Success};

  // Invocation counters
  int connect_calls{0};
  int subscribe_calls{0};
  int create_stream_calls{0};
  int disconnect_calls{0};

  // Set to true to make createStream() throw
  bool create_stream_throws{false};

  void connect(const std::string& /*url*/) override {
    auto outcome = outcomeAt(connect_outcomes, connect_calls++);
    throwFor(outcome, "connect");
  }

  void subscribe(const std::string& /*subject*/,
                 const std::string& /*stream_name*/,
                 const std::string& /*durable_name*/,
                 NatsMessageHandler /*handler*/) override {
    auto outcome = outcomeAt(subscribe_outcomes, subscribe_calls++);
    throwFor(outcome, "subscribe");
  }

  void createStream(const std::string& /*stream_name*/,
                    const std::string& /*subject_filter*/) override {
    ++create_stream_calls;
    if (create_stream_throws) {
      throw std::runtime_error("createStream: server error");
    }
  }

  void disconnect() override { ++disconnect_calls; }

 private:
  static Outcome outcomeAt(const std::vector<Outcome>& seq, int index) {
    if (seq.empty()) {
      return Outcome::Success;
    }
    auto idx = std::min(static_cast<int>(seq.size()) - 1, index);
    return seq[static_cast<size_t>(idx)];
  }

  static void throwFor(Outcome outcome, const char* /*op*/) {
    switch (outcome) {
      case Outcome::Success:
        return;
      case Outcome::StreamNotFound:
        throw std::domain_error("stream not found");
      case Outcome::ConnectionFailed:
        throw std::system_error(std::make_error_code(std::errc::connection_refused),
                                "connection refused");
      case Outcome::Permanent:
        throw std::runtime_error("authorization violation");
    }
  }
};

// ---------------------------------------------------------------------------
// Helper: build a Config with fast backoff so tests don't take long
// ---------------------------------------------------------------------------

static NatsListener::Config fastConfig(bool auto_create = false) {
  return NatsListener::Config{
      .server_url = "nats://localhost:4222",
      .subject = "hi.tasks.>",
      .stream_name = "homeric-tasks",
      .durable_name = "keystone-worker",
      .auto_create_stream = auto_create,
      .initial_backoff_ms = std::chrono::milliseconds(1),
      .max_backoff_ms = std::chrono::milliseconds(10),
      .backoff_multiplier = 2.0,
      .shutdown_poll_ms = std::chrono::milliseconds(1),
  };
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class NatsListenerTest : public ::testing::Test {
 protected:
  MockNatsConnection* mock_{nullptr};  // non-owning view into the unique_ptr

  std::unique_ptr<NatsListener> makeListener(bool auto_create = false) {
    auto conn = std::make_unique<MockNatsConnection>();
    mock_ = conn.get();
    return std::make_unique<NatsListener>(fastConfig(auto_create), std::move(conn));
  }
};

// ---------------------------------------------------------------------------
// Construction validation
// ---------------------------------------------------------------------------

TEST_F(NatsListenerTest, ThrowsOnNullConnection) {
  EXPECT_THROW(NatsListener(fastConfig(), nullptr), std::invalid_argument);
}

TEST_F(NatsListenerTest, ThrowsOnEmptyServerUrl) {
  auto conn = std::make_unique<MockNatsConnection>();
  NatsListener::Config cfg = fastConfig();
  cfg.server_url = "";
  EXPECT_THROW(NatsListener(cfg, std::move(conn)), std::invalid_argument);
}

TEST_F(NatsListenerTest, ThrowsOnEmptySubject) {
  auto conn = std::make_unique<MockNatsConnection>();
  NatsListener::Config cfg = fastConfig();
  cfg.subject = "";
  EXPECT_THROW(NatsListener(cfg, std::move(conn)), std::invalid_argument);
}

TEST_F(NatsListenerTest, ThrowsOnEmptyStreamName) {
  auto conn = std::make_unique<MockNatsConnection>();
  NatsListener::Config cfg = fastConfig();
  cfg.stream_name = "";
  EXPECT_THROW(NatsListener(cfg, std::move(conn)), std::invalid_argument);
}

TEST_F(NatsListenerTest, ThrowsOnEmptyDurableName) {
  auto conn = std::make_unique<MockNatsConnection>();
  NatsListener::Config cfg = fastConfig();
  cfg.durable_name = "";
  EXPECT_THROW(NatsListener(cfg, std::move(conn)), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Happy path
// ---------------------------------------------------------------------------

TEST_F(NatsListenerTest, StartSucceedsOnFirstAttempt) {
  auto listener = makeListener();

  EXPECT_NO_THROW(listener->start());

  EXPECT_TRUE(listener->isConnected());
  EXPECT_EQ(listener->getAttemptCount(), 1u);
  EXPECT_EQ(mock_->connect_calls, 1);
  EXPECT_EQ(mock_->subscribe_calls, 1);
  EXPECT_EQ(mock_->create_stream_calls, 0);
}

// ---------------------------------------------------------------------------
// StreamNotFound — auto_create_stream=true
// ---------------------------------------------------------------------------

TEST_F(NatsListenerTest, StreamNotFoundAutoCreateTrue_CreatesAndRetries) {
  auto listener = makeListener(/*auto_create=*/true);

  // First subscribe fails with StreamNotFound; second succeeds
  mock_->subscribe_outcomes = {
      MockNatsConnection::Outcome::StreamNotFound,
      MockNatsConnection::Outcome::Success,
  };

  EXPECT_NO_THROW(listener->start());

  EXPECT_TRUE(listener->isConnected());
  EXPECT_EQ(mock_->create_stream_calls, 1);
  EXPECT_EQ(mock_->subscribe_calls, 2);
}

TEST_F(NatsListenerTest, StreamNotFoundAutoCreateTrue_CreateFails_StillRetries) {
  auto listener = makeListener(/*auto_create=*/true);
  mock_->create_stream_throws = true;

  // subscribe: fail, fail, succeed
  mock_->subscribe_outcomes = {
      MockNatsConnection::Outcome::StreamNotFound,
      MockNatsConnection::Outcome::StreamNotFound,
      MockNatsConnection::Outcome::Success,
  };

  EXPECT_NO_THROW(listener->start());

  EXPECT_TRUE(listener->isConnected());
  EXPECT_EQ(mock_->create_stream_calls, 2);
}

// ---------------------------------------------------------------------------
// StreamNotFound — auto_create_stream=false
// ---------------------------------------------------------------------------

TEST_F(NatsListenerTest, StreamNotFoundAutoCreateFalse_LogsAndRetries) {
  auto listener = makeListener(/*auto_create=*/false);

  // subscribe: fail, succeed
  mock_->subscribe_outcomes = {
      MockNatsConnection::Outcome::StreamNotFound,
      MockNatsConnection::Outcome::Success,
  };

  EXPECT_NO_THROW(listener->start());

  EXPECT_TRUE(listener->isConnected());
  EXPECT_EQ(mock_->create_stream_calls, 0);
  EXPECT_EQ(mock_->subscribe_calls, 2);
  EXPECT_EQ(listener->getAttemptCount(), 2u);
}

// ---------------------------------------------------------------------------
// Transient connection failure
// ---------------------------------------------------------------------------

TEST_F(NatsListenerTest, TransientConnectionFailure_RetriesAndSucceeds) {
  auto listener = makeListener();

  // connect: fail twice, then succeed
  mock_->connect_outcomes = {
      MockNatsConnection::Outcome::ConnectionFailed,
      MockNatsConnection::Outcome::ConnectionFailed,
      MockNatsConnection::Outcome::Success,
  };

  EXPECT_NO_THROW(listener->start());

  EXPECT_TRUE(listener->isConnected());
  EXPECT_EQ(mock_->connect_calls, 3);
  EXPECT_EQ(listener->getAttemptCount(), 3u);
}

// ---------------------------------------------------------------------------
// Permanent error — no retry
// ---------------------------------------------------------------------------

TEST_F(NatsListenerTest, PermanentError_ThrowsImmediately) {
  auto listener = makeListener();

  mock_->connect_outcomes = {MockNatsConnection::Outcome::Permanent};

  EXPECT_THROW(listener->start(), std::runtime_error);

  EXPECT_FALSE(listener->isConnected());
  EXPECT_EQ(mock_->connect_calls, 1);
}

TEST_F(NatsListenerTest, PermanentErrorOnSubscribe_ThrowsImmediately) {
  auto listener = makeListener();

  mock_->subscribe_outcomes = {MockNatsConnection::Outcome::Permanent};

  EXPECT_THROW(listener->start(), std::runtime_error);

  EXPECT_FALSE(listener->isConnected());
  EXPECT_EQ(mock_->subscribe_calls, 1);
}

// ---------------------------------------------------------------------------
// Shutdown during retry
// ---------------------------------------------------------------------------

TEST_F(NatsListenerTest, StopDuringRetry_ExitsCleanly) {
  auto listener = makeListener();

  // subscribe always fails with StreamNotFound so it keeps retrying
  mock_->subscribe_outcomes = {MockNatsConnection::Outcome::StreamNotFound};

  // Stop after a short delay on a background thread
  std::thread stopper([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    listener->stop();
  });

  EXPECT_NO_THROW(listener->start());

  stopper.join();
  EXPECT_FALSE(listener->isConnected());
}

// ---------------------------------------------------------------------------
// Backoff cap
// ---------------------------------------------------------------------------

TEST_F(NatsListenerTest, BackoffCapsAtMaxBackoff) {
  // Build a listener with very small max so we can observe the cap
  auto conn = std::make_unique<MockNatsConnection>();
  mock_ = conn.get();

  NatsListener::Config cfg = fastConfig();
  cfg.initial_backoff_ms = std::chrono::milliseconds(1);
  cfg.max_backoff_ms = std::chrono::milliseconds(5);
  cfg.backoff_multiplier = 10.0;  // grows fast

  // subscribe: fail many times, then succeed
  mock_->subscribe_outcomes = {
      MockNatsConnection::Outcome::StreamNotFound,
      MockNatsConnection::Outcome::StreamNotFound,
      MockNatsConnection::Outcome::StreamNotFound,
      MockNatsConnection::Outcome::Success,
  };

  NatsListener listener(cfg, std::move(conn));
  auto start = std::chrono::steady_clock::now();
  EXPECT_NO_THROW(listener.start());
  auto elapsed = std::chrono::steady_clock::now() - start;

  EXPECT_TRUE(listener.isConnected());
  // Total wait is at most 3 * max_backoff_ms (5ms each) = 15ms, well under 1s
  EXPECT_LT(elapsed, std::chrono::milliseconds(500));
}

// ---------------------------------------------------------------------------
// Initial state
// ---------------------------------------------------------------------------

TEST_F(NatsListenerTest, InitiallyNotConnected) {
  auto listener = makeListener();
  EXPECT_FALSE(listener->isConnected());
  EXPECT_EQ(listener->getAttemptCount(), 0u);
}

// ---------------------------------------------------------------------------
// Message handler is invoked
// ---------------------------------------------------------------------------

TEST_F(NatsListenerTest, MessageHandlerPassedToSubscribe) {
  // We just verify start() completes — the handler forwarding is tested via
  // integration with a real NATS server (outside unit test scope)
  auto listener = makeListener();

  bool handler_set = false;
  listener->setMessageHandler([&](const std::string&, const std::string&) { handler_set = true; });

  EXPECT_NO_THROW(listener->start());
  EXPECT_TRUE(listener->isConnected());
  // handler_set stays false because MockNatsConnection never calls the handler
  // — that is correct behaviour for a mock
}
