#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "core/profiling.hpp"

using namespace keystone::core;

class ProfilingTest : public ::testing::Test {
 protected:
  void SetUp() override { ProfilingSession::reset(); }
};

TEST_F(ProfilingTest, DisabledByDefault) {
  // Without KEYSTONE_PROFILE=1, profiling should be disabled
  // Note: This test may fail if KEYSTONE_PROFILE=1 is set in environment
  auto session = ProfilingSession::start("test_section");
  session.end();

  // Even if disabled, API should be safe to use
  auto stats = ProfilingSession::getStats("test_section");
  (void)stats;  // Suppress unused warning - just testing API safety
  // Stats may or may not exist depending on env variable
}

TEST_F(ProfilingTest, RAIIAutoEnd) {
  // Session should end automatically when destroyed
  {
    auto session = ProfilingSession::start("raii_test");
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    // session.end() not called - destructor will call it
  }

  // If profiling enabled, should have recorded the duration
  auto stats = ProfilingSession::getStats("raii_test");
  if (ProfilingSession::isEnabled()) {
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->sample_count, 1);
    EXPECT_GT(stats->min_us, 90.0);  // At least 90µs (slept for 100µs)
  }
}

TEST_F(ProfilingTest, MultipleSamples) {
  if (!ProfilingSession::isEnabled()) {
    GTEST_SKIP() << "Profiling disabled (KEYSTONE_PROFILE not set)";
  }

  // Record multiple samples
  for (int32_t i = 0; i < 10; ++i) {
    auto session = ProfilingSession::start("multi_sample");
    std::this_thread::sleep_for(std::chrono::microseconds(10 * (i + 1)));
    session.end();
  }

  auto stats = ProfilingSession::getStats("multi_sample");
  ASSERT_TRUE(stats.has_value());
  EXPECT_EQ(stats->sample_count, 10);
  EXPECT_LT(stats->min_us, stats->max_us);
  EXPECT_GT(stats->mean_us, 0.0);
  EXPECT_GE(stats->p50_us, stats->min_us);
  EXPECT_LE(stats->p95_us, stats->max_us);
}

TEST_F(ProfilingTest, PercentileCalculation) {
  if (!ProfilingSession::isEnabled()) {
    GTEST_SKIP() << "Profiling disabled (KEYSTONE_PROFILE not set)";
  }

  // Create known distribution with longer sleeps (10x) to minimize overhead
  // impact Using 10-1000µs range so profiling overhead (~1000µs) becomes
  // negligible percentage
  for (int32_t i = 1; i <= 100; ++i) {
    auto session = ProfilingSession::start("percentiles");
    std::this_thread::sleep_for(std::chrono::microseconds(i * 10));
    session.end();
  }

  auto stats = ProfilingSession::getStats("percentiles");
  ASSERT_TRUE(stats.has_value());
  EXPECT_EQ(stats->sample_count, 100);

  // P50 should be around middle (500µs ± tolerance for overhead)
  EXPECT_NEAR(stats->p50_us, 500.0, 1200.0);

  // P95 should be near end (950µs ± tolerance for overhead)
  //
  // TSan-instrumented builds (integration-tests runs test.debug.tsan) inflate
  // the tail of the timing distribution: occasional multi-ms stalls push p95
  // past the 950±1200µs window even though the median stays well inside it
  // (observed p95_us ≈ 2680µs on a loaded runner, diff 1730µs). The wall-clock
  // assertions are therefore only meaningful without a race detector; the
  // ordering + range checks below hold under every build configuration and are
  // what actually validate the percentile calculation.
#ifdef __has_feature
#define KEYSTONE_TEST_HAS_FEATURE(feature) __has_feature(feature)
#else
#define KEYSTONE_TEST_HAS_FEATURE(feature) 0
#endif
#if !defined(__SANITIZE_THREAD__) && \
    !KEYSTONE_TEST_HAS_FEATURE(thread_sanitizer)
  EXPECT_NEAR(stats->p95_us, 950.0, 1200.0);
#endif
#undef KEYSTONE_TEST_HAS_FEATURE

  // Ordering (this should always hold regardless of overhead)
  EXPECT_LE(stats->p50_us, stats->p95_us);
  EXPECT_LE(stats->p95_us, stats->p99_us);

  // Bounds (percentiles must lie within the observed sample range)
  EXPECT_GE(stats->p50_us, stats->min_us);
  EXPECT_LE(stats->p95_us, stats->max_us);
  EXPECT_LE(stats->p99_us, stats->max_us);
}

TEST_F(ProfilingTest, GenerateReport) {
  if (!ProfilingSession::isEnabled()) {
    GTEST_SKIP() << "Profiling disabled (KEYSTONE_PROFILE not set)";
  }

  // Add some profiling data
  {
    auto s1 = ProfilingSession::start("section1");
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  {
    auto s2 = ProfilingSession::start("section2");
    std::this_thread::sleep_for(std::chrono::microseconds(200));
  }

  std::string report = ProfilingSession::generateReport();

  // Report should contain section names
  EXPECT_NE(report.find("section1"), std::string::npos);
  EXPECT_NE(report.find("section2"), std::string::npos);
  EXPECT_NE(report.find("Performance Profiling Report"), std::string::npos);
}

TEST_F(ProfilingTest, ThreadSafety) {
  if (!ProfilingSession::isEnabled()) {
    GTEST_SKIP() << "Profiling disabled (KEYSTONE_PROFILE not set)";
  }

  // Multiple threads profiling same section
  auto worker = []() {
    for (int32_t i = 0; i < 100; ++i) {
      auto session = ProfilingSession::start("thread_safe");
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
  };

  std::thread t1(worker);
  std::thread t2(worker);
  std::thread t3(worker);

  t1.join();
  t2.join();
  t3.join();

  auto stats = ProfilingSession::getStats("thread_safe");
  ASSERT_TRUE(stats.has_value());
  EXPECT_EQ(stats->sample_count, 300);  // 3 threads × 100 samples
}

TEST_F(ProfilingTest, MoveSemantics) {
  if (!ProfilingSession::isEnabled()) {
    GTEST_SKIP() << "Profiling disabled (KEYSTONE_PROFILE not set)";
  }

  auto session1 = ProfilingSession::start("move_test");
  std::this_thread::sleep_for(std::chrono::microseconds(50));

  // Move construct
  auto session2 = std::move(session1);
  std::this_thread::sleep_for(std::chrono::microseconds(50));

  // session2 destructor will end
  session2.end();

  auto stats = ProfilingSession::getStats("move_test");
  ASSERT_TRUE(stats.has_value());
  EXPECT_EQ(stats->sample_count, 1);
  EXPECT_GT(stats->mean_us, 90.0);  // Should be ~100µs total
}
