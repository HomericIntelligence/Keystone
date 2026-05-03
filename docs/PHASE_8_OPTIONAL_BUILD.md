# Phase 8 Optional Build Instructions

## Overview

Phase 8 (Distributed Multi-Node Communication) has been made **optional** to allow building and running ProjectKeystone
without requiring gRPC and Protobuf dependencies.

## Build Options

### Option 1: Build WITHOUT Phase 8 (Default)

This is the default build mode. No gRPC or Protobuf dependencies are required.

**Docker Build:**

```bash
docker-compose build production
docker run --rm projectkeystone:production
```

**Local Build:**

```bash
mkdir -p build && cd build
cmake -G Ninja ..
ninja
```

**What's Included:**

- ✅ All Phases 1-7 functionality
- ✅ 4-layer agent hierarchy (ChiefArchitect, ComponentLead, ModuleLead, TaskAgent)
- ✅ MessageBus-based local communication
- ✅ All E2E and unit tests (except distributed_grpc_tests)
- ✅ Async agents and coroutines
- ✅ Chaos engineering features
- ✅ Performance profiling and metrics

**What's Excluded:**

- ❌ gRPC-based distributed communication
- ❌ YAML task specifications
- ❌ ServiceRegistry for multi-node discovery
- ❌ distributed_grpc_tests

### Option 2: Build WITH Phase 8 (Distributed Features)

Enables all Phase 8 distributed communication features.

**Prerequisites:**

```bash
# Install gRPC and Protobuf
# Ubuntu/Debian:
sudo apt-get install -y \
    protobuf-compiler \
    libprotobuf-dev \
    libgrpc++-dev \
    libgrpc-dev \
    protobuf-compiler-grpc

# macOS (Homebrew):
brew install grpc protobuf

# Or build from source:
# See https://grpc.io/docs/languages/cpp/quickstart/
```

**Local Build:**

```bash
mkdir -p build && cd build
cmake -G Ninja -DENABLE_GRPC=ON ..
ninja

# Run distributed tests
./distributed_grpc_tests
```

**Docker Build with gRPC:**

```bash
# Currently requires manual Dockerfile modification to install gRPC
# See "Docker Support for Phase 8" section below
```

**What's Included (in addition to base system):**

- ✅ gRPC-based distributed communication
- ✅ Protocol Buffers message serialization
- ✅ YAML task specification parsing and generation
- ✅ ServiceRegistry for multi-node agent discovery
- ✅ HMASCoordinator service for task routing
- ✅ Load balancing across agent pools
- ✅ Result aggregation strategies
- ✅ Heartbeat monitoring for distributed agents
- ✅ distributed_grpc_tests (26 test cases)
- ✅ Multi-node Docker Compose deployment

## CMake Configuration

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `ENABLE_GRPC` | `OFF` | Enable Phase 8 distributed features |
| `ENABLE_COVERAGE` | `OFF` | Enable code coverage instrumentation |

### Example Configurations

**Basic build:**

```bash
cmake -G Ninja ..
```

**With gRPC:**

```bash
cmake -G Ninja -DENABLE_GRPC=ON ..
```

**With gRPC and coverage:**

```bash
cmake -G Ninja -DENABLE_GRPC=ON -DENABLE_COVERAGE=ON ..
```

**With sanitizers:**

```bash
cmake -G Ninja -DENABLE_GRPC=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined" ..
```

## Conditional Compilation

Phase 8 features are wrapped in `#ifdef ENABLE_GRPC` blocks throughout the codebase.

### Agent Headers

All agent headers (`include/agents/*.hpp`) have Phase 8 methods and members conditionally compiled:

```cpp
class TaskAgent : public BaseAgent {
public:
  // Always available (Phases 1-7)
  explicit TaskAgent(const std::string& agent_id);
  core::Response processMessage(const core::KeystoneMessage& msg) override;

#ifdef ENABLE_GRPC
  // Only available with -DENABLE_GRPC=ON
  void initializeGrpc(const std::string& coordinator_address,
                      const std::string& registry_address);
  void processYamlTask(const std::string& yaml_spec);
  void startHeartbeat();
  void shutdown();
#endif
};
```

### Files Affected

**Headers:**

- `include/agents/task_agent.hpp`
- `include/agents/module_lead_agent.hpp`
- `include/agents/component_lead_agent.hpp`
- `include/agents/chief_architect_agent.hpp`

**Source Files:**

- `src/agents/task_agent.cpp`
- `src/agents/module_lead_agent.cpp`
- `src/agents/component_lead_agent.cpp`
- `src/agents/chief_architect_agent.cpp`

**Network Components (only built with ENABLE_GRPC):**

- `include/network/grpc_server.hpp`
- `include/network/grpc_client.hpp`
- `include/network/service_registry.hpp`
- `include/network/hmas_coordinator_service.hpp`
- `include/network/task_router.hpp`
- `include/network/result_aggregator.hpp`
- `include/network/yaml_parser.hpp`
- All corresponding `.cpp` files in `src/network/`
- All `.proto` files in `proto/`

## Testing

### Without ENABLE_GRPC

```bash
# Run all Phases 1-7 tests
./basic_delegation_tests
./module_coordination_tests
./component_coordination_tests
./async_delegation_tests
./async_agents_tests
./distributed_hierarchy_tests
./multi_component_tests
./chaos_engineering_tests
./unit_tests
```

### With ENABLE_GRPC

```bash
# Run all tests including Phase 8
./distributed_grpc_tests     # 26 test cases

# Run specific test categories
./distributed_grpc_tests --gtest_filter="*YamlParsing*"
./distributed_grpc_tests --gtest_filter="*ServiceRegistry*"
./distributed_grpc_tests --gtest_filter="*LoadBalancing*"
```

## Docker Support for Phase 8

### Current Status

The default Dockerfile does NOT include gRPC dependencies. To build with Phase 8 support in Docker:

**Option A: Modify Dockerfile** (temporary)
Add gRPC installation to the builder stage:

```dockerfile
# In Dockerfile, after line 28 (install dependencies)
RUN apt-get update && apt-get install -y \
    # ... existing packages ...
    protobuf-compiler \
    libprotobuf-dev \
    libgrpc++-dev \
    libgrpc-dev \
    protobuf-compiler-grpc \
    && rm -rf /var/lib/apt/lists/*
```

Then build with:

```bash
docker build -t projectkeystone:grpc --build-arg ENABLE_GRPC=ON .
```

**Option B: Use docker-compose-distributed.yaml**

The distributed deployment configuration (Phase 8) requires gRPC support. A dedicated Dockerfile will be added in the
future.

### Future Work

- [ ] Create `Dockerfile.distributed` with gRPC pre-installed
- [ ] Add `docker-compose-grpc.yml` with build argument
- [ ] CI/CD pipeline for both build modes

## Migration Guide

### Existing Code Without Phase 8

If you have existing code that only uses Phases 1-7:

- ✅ **No changes required** - your code will compile and run as before
- ✅ All existing tests pass
- ✅ All existing functionality preserved

### Adding Phase 8 to Existing Code

To add distributed features to your agents:

```cpp
#ifdef ENABLE_GRPC
// Initialize gRPC clients
task_agent.initializeGrpc("localhost:50051", "localhost:50052");
task_agent.startHeartbeat();

// Process YAML tasks
task_agent.processYamlTask(yaml_spec);
#endif
```

## Troubleshooting

### Build Errors

**Error:** `fatal error: 'grpcpp/grpcpp.h' file not found`

- **Solution:** Either install gRPC/Protobuf OR disable Phase 8 by removing `-DENABLE_GRPC=ON`

**Error:** `undefined reference to 'grpc::...'`

- **Solution:** Ensure you're linking against gRPC libraries (this should be automatic with CMake)

**Error:** `Could NOT find Protobuf`

- **Solution:** Install protobuf development packages or disable Phase 8

### Runtime Errors

**Error:** Agent method not available

- **Cause:** Calling Phase 8 methods when built without `ENABLE_GRPC`
- **Solution:** Wrap calls in `#ifdef ENABLE_GRPC` or rebuild with `-DENABLE_GRPC=ON`

### Docker Issues

**Error:** `groupadd: GID '1000' already exists`

- **Solution:** This has been fixed in the latest Dockerfile (uses GID 1001)

## Performance Considerations

### Without ENABLE_GRPC

- **Binary Size:** Smaller (no gRPC/Protobuf dependencies)
- **Compile Time:** Faster (~30% reduction)
- **Runtime:** Minimal overhead (MessageBus-based communication)
- **Dependencies:** Fewer system dependencies required

### With ENABLE_GRPC

- **Binary Size:** Larger (~15MB additional for gRPC libs)
- **Compile Time:** Slower (proto generation + gRPC compilation)
- **Runtime:** Network latency for distributed communication (~300µs gRPC overhead)
- **Dependencies:** gRPC, Protobuf, yaml-cpp required

## References

- **Phase 8 Architecture:** [docs/PHASE_8_COMPLETE.md](PHASE_8_COMPLETE.md)
- **Network Protocol:** [docs/NETWORK_PROTOCOL.md](NETWORK_PROTOCOL.md)
- **YAML Specification:** [docs/YAML_SPECIFICATION.md](YAML_SPECIFICATION.md)
- **E2E Tests:** [docs/PHASE_8_E2E_TESTS.md](PHASE_8_E2E_TESTS.md)
- **gRPC Documentation:** <https://grpc.io/docs/languages/cpp/>
- **Protocol Buffers:** <https://developers.google.com/protocol-buffers/>

---

**Last Updated:** 2025-11-21
**Version:** 1.0
**Phase:** 8 (Optional)
