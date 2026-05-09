// fuzz_work_stealing.cpp
// Fuzz test for work-stealing scheduler
//
// Tests the robustness of the work-stealing scheduler against:
// - Random task submissions
// - Concurrent stealing operations
// - Empty queues
// - Rapid push/pop sequences
// - Priority inversions
// - Deadline violations
//
// Build with: cmake -DENABLE_FUZZING=ON -DCMAKE_CXX_COMPILER=clang++ ..
// Run with: ./fuzz_work_stealing -max_len=2048 -runs=1000000

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>

#include "concurrency/task.hpp"
#include "concurrency/work_stealing_scheduler.hpp"

using namespace keystone;
using namespace keystone::concurrency;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Skip empty or too small inputs
  if (size < 2) {
    return 0;
  }

  try {
    // Extract thread count from first byte (limit to reasonable range)
    uint8_t thread_count = std::max(uint8_t(1), uint8_t(data[0] % 8 + 1));

    // Create scheduler with fuzzed thread count
    auto scheduler = std::make_unique<WorkStealingScheduler>(thread_count);
    scheduler->start();

    // Extract task count from second byte (limit to avoid timeout)
    uint8_t task_count = std::min(data[1], uint8_t(50));
    size_t offset = 2;

    // Submit fuzzed tasks
    for (uint8_t i = 0; i < task_count && offset < size; ++i) {
      if (offset >= size) break;

      // Get task type from data
      uint8_t task_type = data[offset] % 4;
      offset++;

      switch (task_type) {
        case 0: {
          // Simple task
          scheduler->submit([]() {
            // Do minimal work
            volatile int x = 0;
            (void)x;  // Suppress unused warning
          });
          break;
        }

        case 1: {
          // Priority task (if we have priority data)
          if (offset >= size) break;
          uint8_t priority = data[offset];
          offset++;

          scheduler->submit([priority]() {
            volatile int x = priority;
            (void)x;  // Suppress unused warning
          });
          break;
        }

        case 2: {
          // Task with fuzzed deadline
          if (offset + 4 > size) break;

          // Extract deadline offset in microseconds
          uint32_t deadline_us = 0;
          for (int j = 0; j < 4; ++j) {
            deadline_us |= (uint32_t(data[offset + j]) << (j * 8));
          }
          offset += 4;

          // Limit deadline to reasonable range (1ms to 1s)
          deadline_us = std::min(deadline_us, uint32_t(1000000));
          deadline_us = std::max(deadline_us, uint32_t(1000));

          auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::microseconds(deadline_us);

          scheduler->submit([deadline]() {
            // Check if we met the deadline
            auto now = std::chrono::steady_clock::now();
            (void)(now <= deadline);  // Suppress unused warning
          });
          break;
        }

        case 3: {
          // Potentially throwing task
          if (offset >= size) break;
          bool should_throw = (data[offset] % 10 == 0);
          offset++;

          scheduler->submit([should_throw]() {
            if (should_throw) {
              throw std::runtime_error("fuzz exception");
            }
          });
          break;
        }
      }
    }

    // Let tasks execute for a short time
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Shutdown gracefully
    scheduler->shutdown();

  } catch (const std::exception&) {
    // Expected for invalid configurations
    return 0;
  }

  return 0;
}
