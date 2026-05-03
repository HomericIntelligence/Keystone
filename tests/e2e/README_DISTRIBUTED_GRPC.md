# Distributed gRPC E2E Tests

## Quick Start

### Build and Run Tests

```bash
# In Docker (recommended)
docker-compose up test

# Or build manually in Docker dev container
docker-compose up -d dev
docker-compose exec dev bash

# Inside container:
cd build
cmake -DENABLE_GRPC=ON -G Ninja ..
ninja distributed_grpc_tests
./distributed_grpc_tests
```

### Run Specific Tests

```bash
# Run all distributed gRPC tests
./distributed_grpc_tests

# Run only YAML tests
./distributed_grpc_tests --gtest_filter=*Yaml*

# Run only aggregation tests
./distributed_grpc_tests --gtest_filter=*Aggregation*

# Run integration test
./distributed_grpc_tests --gtest_filter=*Integration*

# Run with verbose output
./distributed_grpc_tests --gtest_filter=*HeartbeatMonitoring --gtest_print_time=1
```

## Test Categories

### 1. YAML Tests (Fast)

```bash
./distributed_grpc_tests --gtest_filter=*Yaml*
```

- YamlTaskSpecParsing
- YamlTaskSpecWithSubtasks
- YamlDurationParsing

### 2. ServiceRegistry Tests (Fast)

```bash
./distributed_grpc_tests --gtest_filter=*ServiceRegistry*
```

- ServiceRegistryBasicRegistration
- ServiceRegistryCapabilityQuery
- ServiceRegistryLevelFiltering

### 3. Heartbeat Tests (Slow - uses sleep)

```bash
./distributed_grpc_tests --gtest_filter=*Heartbeat*
```

- HeartbeatMonitoring (takes ~3.5s)
- HeartbeatWithMetrics

### 4. Load Balancing Tests (Fast)

```bash
./distributed_grpc_tests --gtest_filter=*LoadBalancing*
```

- LoadBalancingLeastLoaded
- LoadBalancingRoundRobin
- LoadBalancingNoAvailableAgents

### 5. Result Aggregation Tests (Moderate)

```bash
./distributed_grpc_tests --gtest_filter=*ResultAggregation*
```

- ResultAggregationWaitAll
- ResultAggregationFirstSuccess
- ResultAggregationMajority
- ResultAggregationTimeout (takes ~150ms)

### 6. Task Routing Tests (Fast)

```bash
./distributed_grpc_tests --gtest_filter=*TaskRouting*
```

- TaskRoutingFromYamlSpec
- TaskRoutingMissingCapability

### 7. Integration Tests (Fast)

```bash
./distributed_grpc_tests --gtest_filter=*Integration*
```

- IntegrationFullTaskFlowSimulated

## Test Output Examples

### Successful Run

```
[==========] Running 26 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 26 tests from DistributedGrpcTest
[ RUN      ] DistributedGrpcTest.YamlTaskSpecParsing
[       OK ] DistributedGrpcTest.YamlTaskSpecParsing (1 ms)
[ RUN      ] DistributedGrpcTest.YamlTaskSpecWithSubtasks
[       OK ] DistributedGrpcTest.YamlTaskSpecWithSubtasks (0 ms)
...
[----------] 26 tests from DistributedGrpcTest (5234 ms total)
[==========] 26 tests from 1 test suite ran. (5234 ms total)
[  PASSED  ] 26 tests.
```

### Failed Test Example

```
[ RUN      ] DistributedGrpcTest.ServiceRegistryCapabilityQuery
tests/e2e/distributed_grpc_test.cpp:234: Failure
Expected equality of these values:
  results.size()
    Which is: 3
  2
[  FAILED  ] DistributedGrpcTest.ServiceRegistryCapabilityQuery (1 ms)
```

## Troubleshooting

### Test Hangs

If a test hangs, it's likely waiting for a timeout. Check:

- HeartbeatMonitoring (waits 3.5s)
- ResultAggregationTimeout (waits 150ms)

### Test Fails Intermittently

- Check for timing assumptions
- Ensure no shared state between tests
- Verify TearDown() cleans up properly

### Build Fails

Ensure gRPC is enabled:

```bash
cmake -DENABLE_GRPC=ON ..
```

Check dependencies:

- gRPC library installed
- Protobuf compiler available
- yaml-cpp library available

### Link Errors

Verify CMakeLists.txt includes:

```cmake
target_link_libraries(
  distributed_grpc_tests
  keystone_network
  keystone_core
  GTest::gtest_main)
```

## Test Structure

### Fixture: DistributedGrpcTest

All tests use this fixture which provides:

- `registry_` - ServiceRegistry instance
- `router_` - TaskRouter instance
- `coordinator_` - HMASCoordinatorServiceImpl instance

Setup/TearDown handles creation and cleanup.

### Helper: YamlSpecBuilder

Fluent API for building task specs:

```cpp
auto spec = YamlSpecBuilder()
    .setGoal("Build project")
    .setTargetLevel(3)
    .setTargetAgentType("TaskAgent")
    .addRequiredCapability("cpp20")
    .build();
```

### Helper: waitFor

Timeout-based waiting:

```cpp
bool complete = waitFor([&]() {
    return aggregator.isComplete();
}, 5000ms);
```

## Continuous Integration

These tests run automatically on:

- Every commit
- Pull requests
- Merge to main

CI configuration in `.github/workflows/test.yml`:

```yaml
- name: Run distributed gRPC tests
  run: |
    cd build
    ./distributed_grpc_tests --gtest_output=xml:test-results/
```

## Code Coverage

Generate coverage report:

```bash
# Build with coverage
cmake -DENABLE_GRPC=ON -DENABLE_COVERAGE=ON ..
ninja distributed_grpc_tests

# Run tests
./distributed_grpc_tests

# Generate coverage report
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_html
```

View coverage:

```bash
open coverage_html/index.html
```

## Performance Benchmarks

Expected execution times:

- **Fast tests** (YAML, Registry, Routing): < 10ms each
- **Moderate tests** (Aggregation): 10-200ms each
- **Slow tests** (Heartbeat): 3-4 seconds each

**Total suite runtime**: ~5 seconds

## Related Documentation

- [PHASE_8_E2E_TESTS.md](../../docs/PHASE_8_E2E_TESTS.md) - Detailed test documentation
- [PHASE_8_COMPLETE.md](../../docs/PHASE_8_COMPLETE.md) - Phase 8 implementation
- [YAML_SPECIFICATION.md](../../docs/YAML_SPECIFICATION.md) - YAML spec format
- [NETWORK_PROTOCOL.md](../../docs/NETWORK_PROTOCOL.md) - gRPC protocol details

## Contributing

### Adding New Tests

1. Add test case to `distributed_grpc_test.cpp`
2. Follow naming convention: `TEST_F(DistributedGrpcTest, MyNewTest)`
3. Use helper classes (YamlSpecBuilder, waitFor)
4. Document in PHASE_8_E2E_TESTS.md
5. Verify CI passes

### Test Guidelines

- ✅ Use real implementations (no mocks)
- ✅ Create simple, concrete test data
- ✅ Each test should be independent
- ✅ Clean up in TearDown()
- ✅ Use EXPECT_* for non-fatal assertions
- ✅ Use ASSERT_* when test can't continue
- ❌ Don't use sleep() (use waitFor instead)
- ❌ Don't share state between tests
- ❌ Don't depend on test execution order

---

**Test Suite**: distributed_grpc_tests
**Test Count**: 26 tests
**Execution Time**: ~5 seconds
**Status**: ✅ Complete
