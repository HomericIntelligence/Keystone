# Phase 9 Coverage Baseline

**Date**: 2025-11-19
**Branch**: `claude/phase-9-enhanced-testing-012SnSku72Wm999NSXdouz9Y`

## Overall Coverage (Baseline)

```
Line Coverage:     86.2% (2059 of 2390 lines)
Function Coverage: 88.5% (423 of 478 functions)
Branch Coverage:   N/A (not tracked)
```

**Target**: 95% line coverage
**Gap**: 8.8% (331 lines need coverage)

---

## Files with Lowest Coverage

Priority files needing additional tests:

| File | Line Coverage | Lines Uncovered | Priority |
|------|--------------|----------------|----------|
| `src/agents/agent_base.cpp` | 13.6% | 38 | **CRITICAL** |
| `src/agents/async_component_lead_agent.cpp` | 12.7% | 62 | **CRITICAL** |
| `include/core/message.hpp` | 16.7% | 5 | HIGH |
| `src/agents/async_chief_architect_agent.cpp` | 20.4% | 43 | HIGH |
| `src/agents/async_base_agent.cpp` | 22.6% | 41 | HIGH |
| `src/core/profiling.cpp` | 32.8% | 78 | MEDIUM |
| `include/concurrency/work_stealing_queue.hpp` | 33.3% | 14 | MEDIUM |
| `include/agents/async_component_lead_agent.hpp` | 33.3% | 4 | MEDIUM |
| `include/agents/async_module_lead_agent.hpp` | 33.3% | 4 | MEDIUM |
| `include/concurrency/thread_pool.hpp` | 38.5% | 8 | MEDIUM |

---

## Test Status

**Tests Run**: 329 total

- **Passed**: 325 (98.8%)
- **Failed**: 4 (1.2%)
  - `Phase5MessageLossTest.CombinedPartitionAndLoss`
  - `ThreadPoolTest.SubmitCoroutineHandle` (SEGFAULT)
  - `WorkStealingSchedulerTest.SubmitCoroutine`
  - `SimulatedNetworkTest.SendAndReceive`
- **Skipped**: 5 (ProfilingTest suite - intentionally disabled)

**Note**: Failed tests do not contribute to coverage. Fixing these tests may increase coverage.

---

## Action Plan

### Phase 9.1 Continued: Increase Coverage to 95%

**Priority 1: Agent Base Classes** (~185 lines)

- [ ] Add tests for `agent_base.cpp` error paths
- [ ] Add tests for `async_base_agent.cpp` lifecycle methods
- [ ] Add tests for `async_chief_architect_agent.cpp` orchestration logic
- [ ] Add tests for `async_component_lead_agent.cpp` module coordination

**Priority 2: Core Infrastructure** (~95 lines)

- [ ] Add tests for `message.hpp` utility methods
- [ ] Add tests for `profiling.cpp` profiling logic (currently skipped)
- [ ] Add tests for error handling in core components

**Priority 3: Concurrency** (~35 lines)

- [ ] Add tests for `work_stealing_queue.hpp` edge cases
- [ ] Add tests for `thread_pool.hpp` shutdown scenarios

**Priority 4: Fix Failing Tests** (may add coverage)

- [ ] Fix `ThreadPoolTest.SubmitCoroutineHandle` segfault
- [ ] Fix `WorkStealingSchedulerTest.SubmitCoroutine`
- [ ] Fix `Phase5MessageLossTest.CombinedPartitionAndLoss`
- [ ] Fix `SimulatedNetworkTest.SendAndReceive`

---

## Coverage Report Generation

### Command

```bash
./scripts/generate_coverage.sh
```

### Output Location

```
build/coverage/html/index.html
```

### Manual Coverage Check

```bash
# Capture coverage
lcov --capture --directory build --output-file coverage.info --ignore-errors negative,mismatch

# Filter third-party
lcov --remove coverage.info '/usr/*' '*/third_party/*' '*/tests/*' '*/_deps/*' --output-file coverage_filtered.info

# Generate HTML
genhtml coverage_filtered.info --output-directory coverage_html

# View summary
lcov --summary coverage_filtered.info
```

---

## Notes

1. **Multi-threaded Coverage**: We use `--ignore-errors negative,mismatch` due to race
   conditions in gcov with multithreaded tests. This is expected and does not affect
   accuracy.

2. **Skipped Tests**: ProfilingTest suite is intentionally skipped (5 tests). These
   could be re-enabled to potentially increase coverage.

3. **Function Coverage**: At 88.5%, function coverage is higher than line coverage,
   suggesting that tested functions have some untested code paths (edge cases, error
   handling).

4. **Branch Coverage**: Not currently tracked. Consider enabling with
   `--rc lcov_branch_coverage=1` for more detailed coverage.

---

**Next Step**: Write additional tests for Priority 1 files to reach 95% line coverage.
