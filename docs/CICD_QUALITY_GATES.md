# CI/CD Quality Gates for Keystone

This document describes the automated quality gates enforced in Keystone's CI/CD pipeline.

## Overview

Keystone implements **5 quality gates** that must pass before code can be merged:

1. **Code Coverage** - ≥95% line coverage required
2. **Static Analysis** - Zero critical errors from clang-tidy and cppcheck
3. **Fuzz Testing** - 1 hour of fuzz testing without crashes
4. **Performance Benchmarks** - No regressions >10%
5. **Unit & E2E Tests** - All 329+ tests must pass

## Quality Gates

### 1. Code Coverage (≥95%)

**Goal**: Ensure comprehensive test coverage

**Workflow Job**: `coverage`

**Process**:

1. Build with coverage instrumentation (`-DENABLE_COVERAGE=ON`)
2. Run all 329+ tests
3. Generate coverage report with `lcov`
4. Filter out system headers and third-party code
5. Extract line coverage percentage
6. **Gate**: FAIL if coverage < 95%

**Commands**:

```bash
./scripts/generate_coverage.sh
```

**Success Criteria**:

- Line coverage ≥ 95%
- Coverage report uploaded to artifacts

**Artifacts**:

- `coverage-report/` - HTML coverage report

**Viewing Results**:

```bash
# Download artifact from GitHub Actions
unzip coverage-report.zip
open coverage-report/html/index.html
```

### 2. Static Analysis

**Goal**: Catch code quality issues and potential bugs

**Workflow Job**: `static-analysis`

**Tools**:

- **clang-tidy**: C++ linting and best practices
- **cppcheck**: Static analysis for C/C++

**Process**:

1. Build with `ENABLE_CLANG_TIDY=ON`
2. Run `clang-tidy` on all source files
3. Run `cppcheck` with all checks enabled
4. Generate reports (txt and xml)
5. Count errors and warnings
6. **Gate**: FAIL if critical errors found

**Commands**:

```bash
./scripts/run_static_analysis.sh
```

**Success Criteria**:

- Zero critical errors
- Warnings are reported but don't block

**Artifacts**:

- `static-analysis-reports/clang-tidy-report.txt`
- `static-analysis-reports/clang-tidy-report.xml`
- `static-analysis-reports/cppcheck-report.txt`
- `static-analysis-reports/cppcheck-report.xml`

**Checks Performed**:

- Memory safety (leaks, use-after-free)
- Undefined behavior
- Performance anti-patterns
- Code style violations
- Modern C++ best practices

### 3. Fuzz Testing (1 hour)

**Goal**: Discover crashes and undefined behavior

**Workflow Job**: `fuzz-testing`

**Fuzz Targets** (15 minutes each):

1. `fuzz_message_serialization` - Message parsing
2. `fuzz_message_bus_routing` - MessageBus routing
3. `fuzz_work_stealing` - Work-stealing scheduler
4. `fuzz_retry_policy` - Retry policy calculations

**Process**:

1. Build with `ENABLE_FUZZING=ON` (requires Clang)
2. Run each fuzz target for 15 minutes
3. Check for crash artifacts (`crash-*` files)
4. **Gate**: FAIL if any crashes found

**Commands**:

```bash
# Build
cmake -DENABLE_FUZZING=ON -DCMAKE_CXX_COMPILER=clang++ ..
ninja

# Run (example)
./fuzz_message_serialization -max_len=4096 -max_total_time=900
```

**Success Criteria**:

- No crash artifacts generated
- All fuzz targets complete 15 minutes without crashes

**Artifacts** (on failure):

- `fuzz-crash-artifacts/crash-*` - Crashing inputs

**Reproducing Crashes**:

```bash
# Download crash artifact
./fuzz_message_serialization crash-abc123def456

# Run with debugger
gdb --args ./fuzz_message_serialization crash-abc123def456
```

### 4. Performance Benchmarks

**Goal**: Prevent performance regressions

**Workflow Job**: `benchmarks`

**Benchmark Suites** (45+ tests):

- `hierarchy_benchmarks` (5 tests)
- `message_pool_benchmarks` (10 tests)
- `distributed_benchmarks` (8 tests)
- `message_bus_benchmarks` (11 tests)
- `resilience_benchmarks` (20 tests)

**Process**:

1. Build in Release mode (`CMAKE_BUILD_TYPE=Release`)
2. Download baseline (from previous runs)
3. Run all 45+ benchmarks
4. Compare against baseline
5. Detect regressions (>10% slowdown)
6. **Gate**: FAIL if regressions detected

**Commands**:

```bash
./scripts/run_benchmarks.sh --compare benchmarks/results/baseline.json
```

**Success Criteria**:

- No benchmarks >10% slower than baseline
- Baseline created if first run

**Artifacts**:

- `benchmark-baseline/baseline.json` - Baseline metrics
- `benchmark-results/` - Current run results

**Regression Threshold**:

- **Regression**: >10% slower (ratio > 1.10)
- **Improvement**: >10% faster (ratio < 0.90)
- **Unchanged**: Within ±10%

**Example Output**:

```
✓ Passing: 40
↑ Improvements: 3
↓ Regressions: 2

=== REGRESSIONS (>10% slower) ===
  BM_MessageRouting_FanOut/512
    Baseline: 245.23 us
    Current:  285.67 us
    Change:   +16.5% (x1.16)
```

### 5. Unit & E2E Tests

**Goal**: Verify functionality and integration

**Workflow Job**: `tests`

**Test Suites** (329+ tests):

- Unit tests (various components)
- E2E tests (Phase 1-5)
- Concurrency tests
- Simulation tests

**Process**:

1. Build in Debug mode
2. Run all tests with `ctest`
3. **Gate**: FAIL if any test fails

**Commands**:

```bash
cd build
cmake -DCMAKE_BUILD_TYPE=Debug -G Ninja ..
ninja
ctest --output-on-failure
```

**Success Criteria**:

- All tests pass
- No segfaults or timeouts

## Workflow Triggers

The quality gate workflow runs on:

1. **Push to main/develop**

   ```
   git push origin main
   ```

2. **Push to claude/** branches

   ```
   git push origin claude/feature-name-xyz
   ```

3. **Pull requests to main/develop**

   ```
   gh pr create --base main
   ```

4. **Manual dispatch**
   - Via GitHub Actions UI
   - "Run workflow" button

## Concurrency Control

Only one workflow runs at a time per branch:

```yaml
concurrency:
  group: ${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true
```

This prevents duplicate runs and saves CI resources.

## Quality Summary Job

The `quality-summary` job runs after all quality gates complete:

**Purpose**:

- Aggregate results from all jobs
- Provide single pass/fail status
- Used for branch protection rules

**Status**:

- ✅ **PASS**: All 5 quality gates passed
- ❌ **FAIL**: At least one quality gate failed

## Branch Protection Rules

The live `main` rulesets require these existing GitHub Actions contexts. Merge
queue rollout must preserve all of them:

- `lint`, `unit-tests`, `integration-tests`, `test`, `build`, and `package`
- `security/dependency-scan` and `security/secrets-scan`
- `schema-validation` and `deps/version-sync`
- `coverage` and `release`

The rulesets also preserve required signatures, linear history, deletion and
non-fast-forward protection, zero required approvals, review-thread resolution,
and non-strict status checks. Queue activation does not weaken or replace those
rules.

## Merge Queue Activation

The workflows are prepared for a staged merge-queue rollout by handling
`merge_group/checks_requested`. This repository has no canonical, repo-owned
ruleset configuration file, so an administrator must perform activation only
after this readiness change merges and the repository smoke check is ready.

Add one `merge_queue` rule to the existing active `homeric-main-baseline`
ruleset while preserving every existing condition, bypass actor, rule, required
context, and enforcement field. The rule must use this exact payload:

```json
{
  "type": "merge_queue",
  "parameters": {
    "check_response_timeout_minutes": 60,
    "grouping_strategy": "ALLGREEN",
    "max_entries_to_build": 10,
    "max_entries_to_merge": 5,
    "merge_method": "SQUASH",
    "min_entries_to_merge": 1,
    "min_entries_to_merge_wait_minutes": 5
  }
}
```

Activation is a full-replacement ruleset write and therefore requires this
operator sequence:

1. Fetch and retain the complete live ruleset as the rollback snapshot.
2. Verify the snapshot includes all required contexts listed above and every
   current non-queue rule.
3. Append only the queue rule, update the ruleset, and read it back.
4. Compare the read-back with the snapshot, allowing only the new queue rule.
   If any unrelated field changed, immediately restore the snapshot.
5. Enqueue one representative pull request and record the
   `merge_group/checks_requested` workflow run, all required check conclusions,
   and the queued squash merge result on
   [Keystone #610](https://github.com/HomericIntelligence/Keystone/issues/610).

This activation belongs to the post-merge rollout tracked by
[Odysseus #386](https://github.com/HomericIntelligence/Odysseus/issues/386); it
must not be performed from the implementation PR.

## Local Validation

Run quality gates locally before pushing:

```bash
# 1. Coverage
./scripts/generate_coverage.sh
# Expect: ≥95% coverage

# 2. Static analysis
./scripts/run_static_analysis.sh
# Expect: 0 errors

# 3. Fuzz testing (quick check)
cd build
./fuzz_message_serialization -max_total_time=60
# Expect: No crashes

# 4. Benchmarks
./scripts/run_benchmarks.sh
# Expect: No regressions

# 5. Tests
cd build
ctest --output-on-failure
# Expect: All pass
```

## Viewing Results

### GitHub Actions UI

1. Navigate to **Actions** tab
2. Click on latest **Quality Gates** workflow run
3. View each job:
   - Green checkmark ✅ = Passed
   - Red X ❌ = Failed
4. Click job to view logs and artifacts

### Artifacts

Download artifacts from workflow run:

- **coverage-report**: HTML coverage report
- **static-analysis-reports**: Detailed linting reports
- **fuzz-crash-artifacts**: Crashing inputs (if any)
- **benchmark-baseline**: Baseline performance metrics
- **benchmark-results**: Current benchmark results

### PR Comments

For pull requests, the coverage job automatically comments:

```markdown
## Code Coverage Report

| Metric | Value | Status |
|--------|-------|--------|
| **Line Coverage** | 87.3% | ❌ FAIL |
| **Threshold** | 95% | - |

❌ Coverage below quality gate - please add more tests

[View detailed report](...)
```

## Troubleshooting

### Coverage Below Threshold

**Problem**: Coverage is 87%, need 95%

**Solution**:

1. Check `PHASE_9_COVERAGE_BASELINE.md` for priority files
2. Write tests for uncovered code
3. Re-run coverage locally
4. Push when ≥95%

### Static Analysis Errors

**Problem**: clang-tidy reports errors

**Solution**:

1. Download `static-analysis-reports` artifact
2. Read `clang-tidy-report.txt`
3. Fix reported issues
4. Re-run locally: `./scripts/run_static_analysis.sh`
5. Push fixes

### Fuzz Testing Crashes

**Problem**: Fuzz target found crash

**Solution**:

1. Download `fuzz-crash-artifacts`
2. Reproduce locally: `./fuzz_target crash-abc123`
3. Debug with gdb: `gdb --args ./fuzz_target crash-abc123`
4. Fix bug
5. Verify fix with fuzz test
6. Push fix

### Performance Regression

**Problem**: Benchmark >10% slower

**Solution**:

1. Download `benchmark-results` artifact
2. Identify which benchmark regressed
3. Profile locally: `perf record ./benchmark_exe`
4. Optimize code
5. Re-run benchmarks locally
6. Push optimization

### Test Failures

**Problem**: Unit tests fail in CI

**Solution**:

1. View test logs in GitHub Actions
2. Reproduce locally: `ctest --output-on-failure`
3. Fix failing tests
4. Verify all pass locally
5. Push fix

## Performance Considerations

**Workflow Duration** (approximate):

| Job | Duration | Parallelizable |
|-----|----------|----------------|
| coverage | 10-15 min | No |
| static-analysis | 10-15 min | No |
| fuzz-testing | 60-65 min | Yes (per target) |
| benchmarks | 10-15 min | No |
| tests | 5-10 min | No |

**Total**: ~60-75 minutes (sequential)

**Optimization Strategies**:

1. Use GitHub Actions caching for dependencies
2. Run fuzz targets in parallel (future)
3. Incremental coverage (only changed files)
4. Benchmark comparison only on `main` branch

## Cost Estimation

**GitHub Actions Minutes** (per workflow run):

- **Public repos**: Free unlimited
- **Private repos**: Depends on plan

**Approximate usage**:

- 75 minutes per workflow run
- ~10 runs per day (active development)
- **Total**: ~750 minutes/day

## Future Enhancements

Planned improvements to quality gates:

1. **Branch Coverage**: Track branch coverage (not just line)
2. **Mutation Testing**: Verify test quality with mutation testing
3. **Incremental Coverage**: Only check coverage on changed code
4. **Performance Trends**: Track benchmark trends over time
5. **Security Scanning**: Add SAST (CodeQL, Semgrep)
6. **Dependency Scanning**: Add Dependabot for CVEs
7. **Container Scanning**: Scan Docker images for vulnerabilities

## References

- [GitHub Actions Documentation](https://docs.github.com/en/actions)
- [Branch Protection Rules](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/defining-the-mergeability-of-pull-requests/about-protected-branches)
- [Google Benchmark](https://github.com/google/benchmark)
- [libFuzzer](https://llvm.org/docs/LibFuzzer.html)
- [clang-tidy](https://clang.llvm.org/extra/clang-tidy/)
- [cppcheck](http://cppcheck.net/)
- [lcov](http://ltp.sourceforge.net/coverage/lcov.php)

---

**Last Updated**: 2025-11-19
**Version**: 1.0
**Phase**: 9.5 - CI/CD Quality Gates
