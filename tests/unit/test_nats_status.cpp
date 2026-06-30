#include "monitoring/nats_status.hpp"

#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using keystone::monitoring::NatsConnectionState;
using keystone::monitoring::NatsStatusTracker;

TEST(NatsStatusTrackerTest, InitialStateIsDisconnected) {
  NatsStatusTracker tracker;
  EXPECT_EQ(tracker.state(), NatsConnectionState::kDisconnected);
}

TEST(NatsStatusTrackerTest, InitialLastSuccessEpochMsIsZero) {
  NatsStatusTracker tracker;
  EXPECT_EQ(tracker.lastSuccessEpochMs(), 0);
}

TEST(NatsStatusTrackerTest, SetConnectedUpdatesState) {
  NatsStatusTracker tracker;
  tracker.setConnected();
  EXPECT_EQ(tracker.state(), NatsConnectionState::kConnected);
}

TEST(NatsStatusTrackerTest, SetConnectedUpdatesTimestamp) {
  NatsStatusTracker tracker;
  tracker.setConnected();
  EXPECT_GT(tracker.lastSuccessEpochMs(), 0);
}

TEST(NatsStatusTrackerTest, SetDisconnectedUpdatesState) {
  NatsStatusTracker tracker;
  tracker.setConnected();
  tracker.setDisconnected();
  EXPECT_EQ(tracker.state(), NatsConnectionState::kDisconnected);
}

TEST(NatsStatusTrackerTest, SetDisconnectedDoesNotResetTimestamp) {
  NatsStatusTracker tracker;
  tracker.setConnected();
  int64_t ts = tracker.lastSuccessEpochMs();
  ASSERT_GT(ts, 0);
  tracker.setDisconnected();
  EXPECT_EQ(tracker.lastSuccessEpochMs(), ts);
}

TEST(NatsStatusTrackerTest, SetReconnectingUpdatesState) {
  NatsStatusTracker tracker;
  tracker.setConnected();
  tracker.setReconnecting();
  EXPECT_EQ(tracker.state(), NatsConnectionState::kReconnecting);
}

TEST(NatsStatusTrackerTest, SetReconnectingDoesNotResetTimestamp) {
  NatsStatusTracker tracker;
  tracker.setConnected();
  int64_t ts = tracker.lastSuccessEpochMs();
  ASSERT_GT(ts, 0);
  tracker.setReconnecting();
  EXPECT_EQ(tracker.lastSuccessEpochMs(), ts);
}

TEST(NatsStatusTrackerTest, MultipleSetConnectedUpdatesTimestamp) {
  NatsStatusTracker tracker;
  tracker.setConnected();
  int64_t ts1 = tracker.lastSuccessEpochMs();
  // Small sleep to ensure clock advances
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  tracker.setConnected();
  int64_t ts2 = tracker.lastSuccessEpochMs();
  EXPECT_GE(ts2, ts1);
}

TEST(NatsStatusTrackerTest, ConcurrentStateUpdatesAreSafe) {
  NatsStatusTracker tracker;
  std::atomic<bool> start{false};
  constexpr int32_t kThreads = 8;
  constexpr int32_t kIters = 200;

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int32_t i = 0; i < kThreads; ++i) {
    threads.emplace_back([&tracker, &start, i]() {
      while (!start.load()) {}
      for (int32_t j = 0; j < kIters; ++j) {
        switch ((i + j) % 3) {
          case 0:
            tracker.setConnected();
            break;
          case 1:
            tracker.setDisconnected();
            break;
          case 2:
            tracker.setReconnecting();
            break;
        }
        (void)tracker.state();
        (void)tracker.lastSuccessEpochMs();
      }
    });
  }

  start.store(true);
  for (auto& t : threads) {
    t.join();
  }
  // No crash == pass; state must be one of the valid enum values
  NatsConnectionState st = tracker.state();
  EXPECT_TRUE(st == NatsConnectionState::kConnected || st == NatsConnectionState::kDisconnected ||
              st == NatsConnectionState::kReconnecting);
}
