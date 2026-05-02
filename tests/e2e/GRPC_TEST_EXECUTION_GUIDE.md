# gRPC Test Execution Guide

## Quick Test Commands

### Run All Tests

```bash
./distributed_grpc_tests
```

### Run by Category

#### YAML Tests (3 tests, ~5ms)

```bash
./distributed_grpc_tests --gtest_filter="*Yaml*"
```

- YamlTaskSpecParsing
- YamlTaskSpecWithSubtasks
- YamlDurationParsing

#### ServiceRegistry Tests (3 tests, ~10ms)

```bash
./distributed_grpc_tests --gtest_filter="*ServiceRegistry*"
```

- ServiceRegistryBasicRegistration
- ServiceRegistryCapabilityQuery
- ServiceRegistryLevelFiltering

#### Heartbeat Tests (2 tests, ~4s - includes sleep)

```bash
./distributed_grpc_tests --gtest_filter="*Heartbeat*"
```

- HeartbeatMonitoring (⚠️ takes 3.5s)
- HeartbeatWithMetrics

#### Load Balancing Tests (3 tests, ~5ms)

```bash
./distributed_grpc_tests --gtest_filter="*LoadBalancing*"
```

- LoadBalancingLeastLoaded
- LoadBalancingRoundRobin
- LoadBalancingNoAvailableAgents

#### Result Aggregation Tests (4 tests, ~200ms)

```bash
./distributed_grpc_tests --gtest_filter="*ResultAggregation*"
```

- ResultAggregationWaitAll
- ResultAggregationFirstSuccess
- ResultAggregationMajority
- ResultAggregationTimeout (⚠️ takes 150ms)

#### Task Routing Tests (2 tests, ~5ms)

```bash
./distributed_grpc_tests --gtest_filter="*TaskRouting*"
```

- TaskRoutingFromYamlSpec
- TaskRoutingMissingCapability

#### Task State Tests (2 tests, ~5ms)

```bash
./distributed_grpc_tests --gtest_filter="*TaskState*"
```

- TaskStateTracking
- MultipleTaskStateTracking

#### Integration Tests (1 test, ~10ms)

```bash
./distributed_grpc_tests --gtest_filter="*Integration*"
```

- IntegrationFullTaskFlowSimulated

#### Edge Case Tests (5 tests, ~10ms)

```bash
./distributed_grpc_tests --gtest_filter="*Invalid*" --gtest_filter="*Duplicate*" --gtest_filter="*Unregister*"
--gtest_filter="*Reset*" --gtest_filter="*Cleanup*"
```

- InvalidYamlParsing
- DuplicateAgentRegistration
- UnregisterNonexistentAgent
- AggregatorResetFunctionality
- TaskCleanupOldTasks

### Run Specific Test

```bash
./distributed_grpc_tests --gtest_filter="DistributedGrpcTest.IntegrationFullTaskFlowSimulated"
```

## Test Timing Guide

### Fast Tests (run frequently during development)

Run these after every code change:

```bash
./distributed_grpc_tests
--gtest_filter="*Yaml*:*ServiceRegistry*:*LoadBalancing*:*TaskRouting*:*TaskState*:*Integration*:*Invalid*:*Duplicate*:*
Unregister*:*Reset*:*Cleanup*:*Strategy*"
```

**Expected time**: ~100ms

### All Tests (run before commit)

```bash
./distributed_grpc_tests
```

**Expected time**: ~5s (includes sleep-based tests)

### Skip Slow Tests

If you want to skip the heartbeat timeout tests:

```bash
./distributed_grpc_tests --gtest_filter="-*Heartbeat*:*ResultAggregationTimeout*"
```

**Expected time**: ~500ms

## Test Output Examples

### Successful Test

```
[ RUN      ] DistributedGrpcTest.YamlTaskSpecParsing
[       OK ] DistributedGrpcTest.YamlTaskSpecParsing (1 ms)
```

### Failed Test

```
[ RUN      ] DistributedGrpcTest.ServiceRegistryCapabilityQuery
tests/e2e/distributed_grpc_test.cpp:234: Failure
Expected equality of these values:
  results.size()
    Which is: 3
  2
[  FAILED  ] DistributedGrpcTest.ServiceRegistryCapabilityQuery (1 ms)
```

### All Tests Summary

```
[==========] Running 26 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 26 tests from DistributedGrpcTest
...
[----------] 26 tests from DistributedGrpcTest (5234 ms total)

[----------] Global test environment tear-down
[==========] 26 tests from 1 test suite ran. (5234 ms total)
[  PASSED  ] 26 tests.
```

## Debugging Tests

### Run with Verbose Output

```bash
./distributed_grpc_tests --gtest_filter="*Integration*" --gtest_print_time=1
```

### Run Single Test with Debug Info

```bash
gdb --args ./distributed_grpc_tests --gtest_filter="DistributedGrpcTest.IntegrationFullTaskFlowSimulated"
```

### Check for Memory Leaks (with Valgrind)

```bash
valgrind --leak-check=full ./distributed_grpc_tests
```

### Check for Data Races (with TSan)

Build with Thread Sanitizer:

```bash
cmake -DENABLE_GRPC=ON -DCMAKE_CXX_FLAGS="-fsanitize=thread" ..
ninja distributed_grpc_tests
./distributed_grpc_tests
```

## Continuous Integration

### GitHub Actions Workflow

```yaml
- name: Run distributed gRPC tests
  run: |
    cd build
    ./distributed_grpc_tests --gtest_output=xml:test-results/distributed-grpc.xml
```

### Expected CI Behavior

- ✅ All 26 tests pass
- ✅ Total time < 10 seconds
- ✅ No memory leaks
- ✅ No data races
- ✅ Test results published

## Coverage Analysis

### Generate Coverage Report

```bash
# Build with coverage
cmake -DENABLE_GRPC=ON -DENABLE_COVERAGE=ON ..
ninja distributed_grpc_tests

# Run tests
./distributed_grpc_tests

# Generate coverage
lcov --capture --directory . --output-file coverage.info
lcov --remove coverage.info '/usr/*' '*/third_party/*' '*/test/*' --output-file coverage_filtered.info
genhtml coverage_filtered.info --output-directory coverage_html

# View
open coverage_html/index.html
```

### Expected Coverage

- **YAML Parser**: >90%
- **ServiceRegistry**: >95%
- **TaskRouter**: >90%
- **ResultAggregator**: >95%
- **HMASCoordinatorService**: >80%

## Performance Benchmarking

### Measure Test Execution Time

```bash
time ./distributed_grpc_tests --gtest_filter="*Fast*"
```

### Profile Hot Paths

```bash
perf record ./distributed_grpc_tests
perf report
```

### Memory Usage

```bash
/usr/bin/time -v ./distributed_grpc_tests
```

## Troubleshooting

### Test Hangs

**Symptom**: Test never completes

**Likely Cause**: Waiting for timeout

- HeartbeatMonitoring waits 3.5s
- ResultAggregationTimeout waits 150ms

**Solution**: Be patient or skip with `--gtest_filter="-*Timeout*"`

### Test Fails Intermittently

**Symptom**: Test passes sometimes, fails sometimes

**Likely Causes**:

1. Timing assumptions (use `waitFor()` instead of `sleep()`)
2. Shared state between tests (verify `TearDown()` cleans up)
3. Thread synchronization issues (check for data races)

**Solution**: Run with TSan:

```bash
cmake -DCMAKE_CXX_FLAGS="-fsanitize=thread" ..
```

### Build Fails

**Symptom**: Linker errors or missing symbols

**Likely Causes**:

1. gRPC not enabled: `cmake -DENABLE_GRPC=ON ..`
2. Missing dependencies: Install gRPC, protobuf, yaml-cpp
3. Wrong library linking in CMakeLists.txt

**Solution**: Verify CMakeLists.txt has:

```cmake
target_link_libraries(
  distributed_grpc_tests
  keystone_network
  keystone_core
  GTest::gtest_main)
```

### Test Crashes

**Symptom**: Segmentation fault

**Likely Causes**:

1. Null pointer dereference
2. Use-after-free
3. Buffer overflow

**Solution**: Run with ASan:

```bash
cmake -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" ..
ninja distributed_grpc_tests
./distributed_grpc_tests
```

## Test Development Workflow

### Adding New Test

1. **Write test case**:

   ```cpp
   TEST_F(DistributedGrpcTest, MyNewTest) {
       // Setup
       auto spec = YamlSpecBuilder().setGoal("...").build();

       // Execute
       auto result = router_->routeTask(spec);

       // Verify
       EXPECT_TRUE(result.success);
   }
   ```

2. **Run test**:

   ```bash
   ./distributed_grpc_tests --gtest_filter="*MyNewTest*"
   ```

3. **Verify passes**:

   ```
   [ RUN      ] DistributedGrpcTest.MyNewTest
   [       OK ] DistributedGrpcTest.MyNewTest (1 ms)
   ```

4. **Document in PHASE_8_E2E_TESTS.md**

5. **Commit**:

   ```bash
   git add tests/e2e/distributed_grpc_test.cpp
   git commit -m "test: Add MyNewTest for feature X"
   ```

## Complete Test List

All 26 test cases in execution order:

1. YamlTaskSpecParsing
2. YamlTaskSpecWithSubtasks
3. YamlDurationParsing
4. ServiceRegistryBasicRegistration
5. ServiceRegistryCapabilityQuery
6. ServiceRegistryLevelFiltering
7. HeartbeatMonitoring ⚠️ (3.5s)
8. HeartbeatWithMetrics
9. LoadBalancingLeastLoaded
10. LoadBalancingRoundRobin
11. LoadBalancingNoAvailableAgents
12. ResultAggregationWaitAll
13. ResultAggregationFirstSuccess
14. ResultAggregationMajority
15. ResultAggregationTimeout ⚠️ (150ms)
16. TaskRoutingFromYamlSpec
17. TaskRoutingMissingCapability
18. TaskStateTracking
19. MultipleTaskStateTracking
20. AggregationStrategyConversion
21. IntegrationFullTaskFlowSimulated
22. InvalidYamlParsing
23. DuplicateAgentRegistration
24. UnregisterNonexistentAgent
25. AggregatorResetFunctionality
26. TaskCleanupOldTasks

**Total**: 26 tests
**Execution Time**: ~5 seconds
**Pass Rate**: 100% expected

---

**Quick Reference**: See [README_DISTRIBUTED_GRPC.md](README_DISTRIBUTED_GRPC.md)
**Full Documentation**: See [../../docs/PHASE_8_E2E_TESTS.md](../../docs/PHASE_8_E2E_TESTS.md)
**Summary**: See [../../DISTRIBUTED_GRPC_TESTS_SUMMARY.md](../../DISTRIBUTED_GRPC_TESTS_SUMMARY.md)
