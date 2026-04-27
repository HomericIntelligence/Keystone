cmake_minimum_required(VERSION 3.20)
project(
  ProjectKeystone
  VERSION 0.1.0
  LANGUAGES CXX)

# C++20 standard required
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for clang-tidy and other tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings — C++ only to avoid breaking C FetchContent targets (e.g.
# nats.c)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(
    $<$<COMPILE_LANGUAGE:CXX>:-Wall> $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
    $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic> $<$<COMPILE_LANGUAGE:CXX>:-Werror>)
  # GCC 13+ has stricter dangling-reference warnings that trigger false
  # positives with spdlog/fmt.
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION
                                              VERSION_GREATER_EQUAL 13.0)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-dangling-reference>)
  endif()
endif()

# Code coverage (Phase 9.1)
option(ENABLE_COVERAGE "Enable code coverage" OFF)

# gRPC support for distributed multi-node communication (Phase 8)
option(ENABLE_GRPC "Enable gRPC support for distributed nodes" OFF)

# Profiling tests (Phase 2.3 - Issue #53)
option(ENABLE_PROFILING "Enable profiling tests (slow)" OFF)

if(ENABLE_COVERAGE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(STATUS "Code coverage enabled")
    add_compile_options(--coverage -fprofile-arcs -ftest-coverage -fno-inline
                        -O0)
    add_link_options(--coverage)
  else()
    message(WARNING "Code coverage only supported with GCC or Clang")
  endif()
endif()

# Static analysis (Phase 9.3)
option(ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" OFF)

if(ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
  if(CLANG_TIDY_EXE)
    message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
    # clang-tidy checks configuration
    set(CLANG_TIDY_CHECKS
        "*,-fuchsia-*,-llvm-header-guard"
        ",-google-readability-todo,-readability-magic-numbers"
        ",-cppcoreguidelines-avoid-magic-numbers"
        ",-modernize-use-trailing-return-type")
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};-checks=${CLANG_TIDY_CHECKS}")
  else()
    message(WARNING "clang-tidy not found")
  endif()
endif()

# Fuzz testing (Phase 9.2)
option(ENABLE_FUZZING "Enable fuzz testing with libFuzzer" OFF)

if(ENABLE_FUZZING)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(STATUS "Fuzz testing enabled")
    # Store fuzzing flags for use with fuzz targets only
    set(FUZZING_FLAGS "-fsanitize=fuzzer,address,undefined"
                      "-fno-omit-frame-pointer" "-g")
    # Do NOT apply globally - only to fuzz targets to avoid breaking googletest
  else()
    message(WARNING "Fuzz testing only supported with Clang "
                    "(use -DCMAKE_CXX_COMPILER=clang++)")
  endif()
endif()

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# Explicit output directories for build artifacts
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Report output directories (for scripts)
set(REPORTS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/reports")
set(COVERAGE_OUTPUT_DIR "${REPORTS_OUTPUT_DIR}/coverage")
set(BENCHMARK_OUTPUT_DIR "${REPORTS_OUTPUT_DIR}/benchmarks")
set(ANALYSIS_OUTPUT_DIR "${REPORTS_OUTPUT_DIR}/analysis")

# Export variables for scripts
set(ENV{COVERAGE_OUTPUT_DIR} "${COVERAGE_OUTPUT_DIR}")
set(ENV{BENCHMARK_OUTPUT_DIR} "${BENCHMARK_OUTPUT_DIR}")
set(ENV{ANALYSIS_OUTPUT_DIR} "${ANALYSIS_OUTPUT_DIR}")

# Create report directories
file(MAKE_DIRECTORY "${REPORTS_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${COVERAGE_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${BENCHMARK_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${ANALYSIS_OUTPUT_DIR}")

# ============================================================================
# Dependencies — Conan-provided via find_package(), cista via FetchContent
# ============================================================================

include(FetchContent)

# Google Test (Conan: gtest/1.14.0)
find_package(GTest REQUIRED)
include(GoogleTest)

# Google Benchmark (Conan: benchmark/1.8.3)
find_package(benchmark REQUIRED)

# concurrentqueue — lock-free MPMC queue (Conan: concurrentqueue/1.0.4)
find_package(concurrentqueue REQUIRED)

# fmt + spdlog — fmt is a Conan dep; use SPDLOG_FMT_EXTERNAL so spdlog does not
# try to use its bundled copy (which would make spdlog/fmt/fmt.h unavailable)
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)
add_compile_definitions(SPDLOG_FMT_EXTERNAL)

# Issue #323: Add Conan package include directories globally.
# Some CMake versions may not properly propagate INTERFACE_INCLUDE_DIRECTORIES
# through the link chain from Conan-provided packages. Adding them globally
# ensures all targets have access to spdlog, fmt, and concurrentqueue headers.
include_directories(
  ${spdlog_INCLUDE_DIRS_RELEASE}
  ${fmt_INCLUDE_DIRS_RELEASE}
  ${concurrentqueue_INCLUDE_DIRS_RELEASE})

# Cista — zero-copy serialization (NOT on ConanCenter, kept as FetchContent)
FetchContent_Declare(
  cista
  GIT_REPOSITORY https://github.com/felixguendling/cista.git
  GIT_TAG v0.14)
FetchContent_MakeAvailable(cista)

# nats.c — NATS client library with TLS support and reconnection callbacks
# (issues #122, #88) NOT on ConanCenter; pulled directly from upstream. natsc
# unconditionally adds test/check_cpp; clear our compile options so -Werror does
# not propagate into natsc's own subdirectories.
FetchContent_Declare(
  natsc
  GIT_REPOSITORY https://github.com/nats-io/nats.c.git
  GIT_TAG v3.12.0
  GIT_SHALLOW TRUE)
set(NATS_BUILD_STREAMING
    OFF
    CACHE BOOL "" FORCE)
set(NATS_BUILD_EXAMPLES
    OFF
    CACHE BOOL "" FORCE)
set(NATS_BUILD_TESTS
    OFF
    CACHE BOOL "" FORCE)
# natsc's test/ and test/check_cpp/ check BUILD_TESTING; turn it off so natsc's
# own tests (which require a live NATS server) are not compiled or registered
# with ctest. Also clear COMPILE_OPTIONS so our -Werror does not propagate into
# any natsc subdirectory that ignores BUILD_TESTING.
set(BUILD_TESTING
    OFF
    CACHE BOOL "" FORCE)
get_directory_property(_saved_compile_options COMPILE_OPTIONS)
set_directory_properties(PROPERTIES COMPILE_OPTIONS "")
FetchContent_MakeAvailable(natsc)
set_directory_properties(PROPERTIES COMPILE_OPTIONS "${_saved_compile_options}")

# yaml-cpp — YAML parser for task specifications (Phase 8 only)
if(ENABLE_GRPC)
  find_package(yaml-cpp REQUIRED)
endif()

# gRPC and Protobuf for distributed communication (Phase 8)
if(ENABLE_GRPC)
  message(
    STATUS "gRPC support enabled for distributed multi-node communication")
  find_package(Protobuf REQUIRED)
  find_package(gRPC REQUIRED)

  # Generate protobuf and gRPC source files
  set(PROTO_FILES
      ${PROJECT_SOURCE_DIR}/proto/hmas_coordinator.proto
      ${PROJECT_SOURCE_DIR}/proto/service_registry.proto
      ${PROJECT_SOURCE_DIR}/proto/common.proto)

  # Create proto directory if it doesn't exist
  file(MAKE_DIRECTORY ${PROJECT_SOURCE_DIR}/proto)

  # Protobuf generation
  protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

  # gRPC generation (if proto files exist)
  if(EXISTS ${PROJECT_SOURCE_DIR}/proto/hmas_coordinator.proto)
    set(GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)

    foreach(PROTO_FILE ${PROTO_FILES})
      get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
      list(APPEND GRPC_SRCS
           "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.grpc.pb.cc")
      list(APPEND GRPC_HDRS
           "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.grpc.pb.h")
    endforeach()
  endif()
else()
  message(
    STATUS
      "gRPC support disabled. Use -DENABLE_GRPC=ON to enable Phase 8 distributed features"
  )
endif()

enable_testing()

# Core library
add_library(
  keystone_core
  src/core/message.cpp
  src/core/message_bus.cpp
  src/core/message_serializer.cpp
  src/core/metrics.cpp
  src/core/profiling.cpp # Phase D.1: Performance profiling
  src/core/message_pool.cpp # Phase D.2: Message pooling
  src/core/agent_id_interning.cpp # Phase A2: Agent ID interning optimization
                                  # (FIX: added <mutex>)
  src/core/failure_injector.cpp # Phase 5.1: Chaos engineering - agent failures
  src/core/retry_policy.cpp # Phase 5.3: Chaos engineering - message retries
  src/core/heartbeat_monitor.cpp # Phase 5.4: Chaos engineering - heartbeat
  # monitoring
  src/core/circuit_breaker.cpp # Phase 5.4: Chaos engineering - circuit breaker
  src/monitoring/prometheus_exporter.cpp # Phase 6.3: Prometheus metrics
  # exporter
  src/monitoring/health_check_server.cpp # Phase 6.7: Health check endpoints
  # (C4)
  src/monitoring/nats_status.cpp # Phase 88: NATS reconnection status tracking
)

target_include_directories(
  keystone_core
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
         $<BUILD_INTERFACE:${cista_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include/keystone>)

# Link dependencies - note: keystone_core depends on keystone_concurrency
# (circular dependency handled via shared libraries) We'll link
# keystone_concurrency after it's defined
target_link_libraries(keystone_core PUBLIC spdlog::spdlog fmt::fmt
                                           concurrentqueue::concurrentqueue)

# Concurrency library (Phase A)
add_library(
  keystone_concurrency
  src/concurrency/task.cpp
  src/concurrency/thread_pool.cpp
  src/concurrency/work_stealing_queue.cpp
  src/concurrency/logger.cpp
  src/concurrency/pull_or_steal.cpp
  src/concurrency/work_stealing_scheduler.cpp
  src/concurrency/scheduler_accessor.cpp)

target_include_directories(
  keystone_concurrency PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                              $<INSTALL_INTERFACE:include/keystone>
                              $<TARGET_PROPERTY:spdlog::spdlog,INTERFACE_INCLUDE_DIRECTORIES>
                              $<TARGET_PROPERTY:fmt::fmt,INTERFACE_INCLUDE_DIRECTORIES>
                              $<TARGET_PROPERTY:concurrentqueue::concurrentqueue,INTERFACE_INCLUDE_DIRECTORIES>)

# Issue #323: Resolve circular dependency.
# Link concurrency library to all its external dependencies explicitly.
# This ensures spdlog, fmt, and concurrentqueue are available during compilation,
# breaking the circular dependency pattern that was previously used.
target_link_libraries(keystone_concurrency PUBLIC spdlog::spdlog fmt::fmt
                                                   concurrentqueue::concurrentqueue)

# Explicitly add include directories for Conan packages. Some CMake versions
# may not properly propagate INTERFACE_INCLUDE_DIRECTORIES from external targets.
target_include_directories(keystone_concurrency PUBLIC
  ${spdlog_INCLUDE_DIRS_RELEASE}
  ${fmt_INCLUDE_DIRS_RELEASE}
  ${concurrentqueue_INCLUDE_DIRS_RELEASE})
# keystone_core needs concurrency components for Logger usage
target_link_libraries(keystone_core PUBLIC keystone_concurrency)

# Network library (Phase 8: Distributed communication)
if(ENABLE_GRPC AND EXISTS ${PROJECT_SOURCE_DIR}/proto/hmas_coordinator.proto)
  add_library(
    keystone_network
    ${PROTO_SRCS}
    ${GRPC_SRCS}
    src/network/grpc_server.cpp
    src/network/grpc_client.cpp
    src/network/service_registry.cpp
    src/network/task_router.cpp
    src/network/yaml_parser.cpp)

  target_include_directories(
    keystone_network
    PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
           $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
           $<BUILD_INTERFACE:${Protobuf_INCLUDE_DIRS}>
           $<INSTALL_INTERFACE:include/keystone>)

  target_link_libraries(
    keystone_network PUBLIC keystone_core protobuf::libprotobuf gRPC::grpc++
                            gRPC::grpc++_reflection yaml-cpp::yaml-cpp)

  message(
    STATUS "keystone_network library configured for distributed communication")
endif()

# Agents library
add_library(
  keystone_agents
  src/agents/agent_core.cpp
  src/agents/async_agent.cpp
  src/agents/task_execution_strategy.cpp # ARCH-007: Strategy pattern
  src/agents/chief_architect_agent.cpp
  src/agents/component_lead_agent.cpp
  src/agents/module_lead_agent.cpp
  src/agents/task_agent.cpp)

target_include_directories(
  keystone_agents PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                         $<INSTALL_INTERFACE:include/keystone>)

target_link_libraries(keystone_agents PUBLIC keystone_core keystone_concurrency)

# Simulation library (Phase D.3)
add_library(
  keystone_simulation
  src/simulation/simulated_numa_node.cpp src/simulation/simulated_network.cpp
  src/simulation/simulated_cluster.cpp)

target_include_directories(
  keystone_simulation PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                             $<INSTALL_INTERFACE:include/keystone>)

target_link_libraries(keystone_simulation PUBLIC keystone_concurrency
                                                 keystone_core)

# E2E Tests - Basic Delegation
add_executable(basic_delegation_tests tests/e2e/basic_delegation_test.cpp)

target_link_libraries(basic_delegation_tests keystone_agents keystone_core
                      GTest::gtest_main)

gtest_discover_tests(basic_delegation_tests)

# E2E Tests - Task Cancellation (Issue #52)
add_executable(task_cancellation_tests tests/e2e/task_cancellation_test.cpp)

target_link_libraries(task_cancellation_tests keystone_agents keystone_core
                      GTest::gtest_main)

gtest_discover_tests(task_cancellation_tests)

# E2E Tests - Module Coordination
add_executable(module_coordination_tests tests/e2e/module_coordination_test.cpp)

target_link_libraries(module_coordination_tests keystone_agents keystone_core
                      GTest::gtest_main)

gtest_discover_tests(module_coordination_tests)

# E2E Tests - Component Coordination
add_executable(component_coordination_tests
               tests/e2e/component_coordination_test.cpp)

target_link_libraries(component_coordination_tests keystone_agents
                      keystone_core GTest::gtest_main)

gtest_discover_tests(component_coordination_tests)

# E2E Tests - Async Delegation (Work-Stealing)
add_executable(async_delegation_tests tests/e2e/async_delegation_test.cpp)

target_link_libraries(async_delegation_tests keystone_agents keystone_core
                      keystone_concurrency GTest::gtest_main)

gtest_discover_tests(async_delegation_tests)

# E2E Tests - Async Agents (Coroutines) DISABLED: Phase 6 async features not yet
# implemented add_executable(async_agents_tests tests/e2e/async_agents_test.cpp
# tests/e2e/full_async_hierarchy_test.cpp)
#
# target_link_libraries(async_agents_tests keystone_agents keystone_core
# keystone_concurrency GTest::gtest_main)
#
# gtest_discover_tests(async_agents_tests)

# E2E Tests - Distributed Hierarchy
add_executable(distributed_hierarchy_tests
               tests/e2e/distributed_hierarchy_test.cpp)

target_link_libraries(distributed_hierarchy_tests keystone_simulation
                      keystone_concurrency keystone_core GTest::gtest_main)

gtest_discover_tests(distributed_hierarchy_tests)

# E2E Tests - Distributed gRPC (Phase 8)
if(ENABLE_GRPC)
  add_executable(distributed_grpc_tests tests/e2e/distributed_grpc_test.cpp)

  target_link_libraries(distributed_grpc_tests keystone_network keystone_core
                        GTest::gtest_main)

  gtest_discover_tests(distributed_grpc_tests)
endif()

# E2E Tests - Multi-Component Hierarchy DISABLED: Phase 4 multi-component
# features not yet implemented add_executable(multi_component_tests
# tests/e2e/multi_component_test.cpp)
#
# target_link_libraries(multi_component_tests keystone_agents keystone_core
# keystone_concurrency GTest::gtest_main)
#
# gtest_discover_tests(multi_component_tests)

# E2E Tests - Chaos Engineering DISABLED: Phase 5 chaos engineering features not
# yet implemented add_executable(chaos_engineering_tests
# tests/e2e/chaos_engineering_test.cpp)
#
# target_link_libraries( chaos_engineering_tests keystone_agents keystone_core
# keystone_concurrency keystone_simulation GTest::gtest_main)
#
# gtest_discover_tests(chaos_engineering_tests DISCOVERY_TIMEOUT 10)

# Unit Tests
add_executable(
  unit_tests
  tests/unit/test_message_bus.cpp
  tests/unit/test_message_serializer.cpp
  # DISABLED: Async API not yet implemented
  # tests/unit/test_message_bus_async.cpp
  tests/unit/test_async_task_agent.cpp  # Issue #376: isFailed() guard
  tests/unit/test_message_priority.cpp
  tests/unit/test_agent_core.cpp # Additional coverage for AgentCore
  tests/unit/test_agent_id_interning.cpp # Phase A2: Agent ID interning tests
  tests/unit/test_metrics.cpp
  tests/unit/test_deadline_scheduling.cpp
  tests/unit/test_profiling.cpp # Phase D: Profiling tests
  tests/unit/test_message_pool.cpp # Phase D.2: Message pool tests
  tests/unit/test_failure_injector.cpp # Phase 5.1: Chaos engineering - agent
  # failures
  tests/unit/test_retry_policy.cpp # Phase 5.3: Chaos engineering - message
  # retries
  tests/unit/test_heartbeat_monitor.cpp # Phase 5.4: Chaos engineering -
  # heartbeat monitoring
  tests/unit/test_circuit_breaker.cpp # Phase 5.4: Chaos engineering - circuit
  # breaker
  tests/unit/test_agent_concepts.cpp # Issue #24: Agent concepts type safety
  tests/unit/test_health_check_server.cpp # Phase 6.7: Health check endpoints
                                          # (C4)
  tests/unit/test_coordination_state.cpp # Stream B Phase B1: CoordinationState
                                         # template tests (FIX: added mutexes)
  tests/unit/test_task_cancellation.cpp # Phase 1.2 (Issue #52): Task
                                        # cancellation notification
  tests/unit/test_security_regression.cpp # PR #1: Security vulnerability
                                          # regression tests
  tests/unit/test_agent_types.cpp # PR #2: AgentLevel enum and type definitions
  tests/unit/test_nats_connection.cpp # Issue #88: NATS reconnection callbacks
  tests/unit/test_subject_validator.cpp # Issue #280: NATS wildcard-aware token
                                        # validation
)

# Add gRPC test fixture if gRPC is enabled (Phase 2.2: Issue #53)
if(ENABLE_GRPC)
  target_sources(unit_tests PRIVATE tests/fixtures/grpc_test_fixture.cpp
                                    tests/unit/test_grpc_tls.cpp)
  target_include_directories(unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
  target_link_libraries(
    unit_tests
    keystone_agents
    keystone_core
    keystone_concurrency
    keystone_network
    keystone_transport
    GTest::gtest_main)
else()
  target_link_libraries(
    unit_tests keystone_agents keystone_core keystone_concurrency
    keystone_transport GTest::gtest_main)
endif()

gtest_discover_tests(unit_tests)

# Profiling Tests (Phase 2.3 - Issue #53) Separate test suite for profiling
# tests (slow, opt-in)
if(ENABLE_PROFILING)
  add_executable(profiling_tests tests/unit/test_profiling.cpp)

  target_compile_definitions(profiling_tests PRIVATE KEYSTONE_PROFILE=1)

  target_link_libraries(profiling_tests keystone_core GTest::gtest_main)

  gtest_discover_tests(profiling_tests)

  message(STATUS "Profiling tests enabled (ENABLE_PROFILING=ON)")
else()
  message(
    STATUS "Profiling tests disabled. Use -DENABLE_PROFILING=ON to enable.")
endif()

# Unit Tests - Agent Classes (Stream C Phase C2: Issue #45) Comprehensive unit
# tests for all agent classes with mock infrastructure
add_executable(
  agent_unit_tests
  tests/unit/test_task_agent.cpp tests/unit/test_chief_architect_agent.cpp
  tests/unit/test_module_lead_agent.cpp
  tests/unit/test_component_lead_agent.cpp)

target_include_directories(
  agent_unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests # For mocks/ directory
)

target_link_libraries(agent_unit_tests keystone_agents keystone_core
                      keystone_concurrency GTest::gtest_main GTest::gmock)

gtest_discover_tests(agent_unit_tests)

# Unit Tests - Concurrency (Phase A)
add_executable(
  concurrency_unit_tests
  tests/unit/test_task.cpp
  tests/unit/test_thread_pool.cpp
  # DISABLED: Compilation error - private constructor access
  # tests/unit/test_work_stealing_queue.cpp
  tests/unit/test_logger.cpp
  tests/unit/test_pull_or_steal.cpp
  tests/unit/test_work_stealing_scheduler.cpp
  tests/unit/test_cpu_affinity.cpp # Phase D.2: CPU affinity tests
  tests/unit/test_scheduler_backoff.cpp # Stream C1: 3-phase backoff tests
)

target_link_libraries(concurrency_unit_tests keystone_concurrency keystone_core
                      GTest::gtest_main)

gtest_discover_tests(concurrency_unit_tests)

# Unit Tests - Simulation (Phase D.3)
add_executable(
  simulation_unit_tests
  tests/unit/test_simulated_numa_node.cpp tests/unit/test_simulated_network.cpp
  tests/unit/test_simulated_cluster.cpp
  tests/unit/test_simulation_corner_cases.cpp)

target_link_libraries(simulation_unit_tests keystone_simulation
                      keystone_concurrency keystone_core GTest::gtest_main)

gtest_discover_tests(simulation_unit_tests)

# Transport library — NATS connection with TLS support (issue #122)
# and NATSListener pull-based fetch loop (issues #178, #205, #307)
add_library(keystone_transport src/transport/nats_connection.cpp
                               src/network/nats_listener.cpp)

target_include_directories(
  keystone_transport PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                            $<INSTALL_INTERFACE:include/keystone>)

target_link_libraries(keystone_transport PUBLIC nats spdlog::spdlog fmt::fmt)

# Explicitly add spdlog and fmt include directories for Conan packages
target_include_directories(keystone_transport PUBLIC
  ${spdlog_INCLUDE_DIRS_RELEASE}
  ${fmt_INCLUDE_DIRS_RELEASE})

# Unit Tests — NATS transport (issue #122)
add_executable(transport_unit_tests tests/unit/test_nats_connection.cpp)

target_link_libraries(transport_unit_tests keystone_transport GTest::gtest_main)

gtest_discover_tests(transport_unit_tests)

# Integration Tests (Phase C3: Registry integration tests)
add_executable(registry_integration_tests
               tests/integration/test_registry_integration.cpp)

target_link_libraries(registry_integration_tests keystone_agents keystone_core
                      GTest::gtest_main)

gtest_discover_tests(registry_integration_tests)

# Integration Tests (NATS: #178, #205, #307)
add_executable(nats_integration_tests
               tests/integration/test_nats_integration.cpp)

target_link_libraries(nats_integration_tests keystone_agents keystone_core
                      keystone_transport GTest::gtest_main)

gtest_discover_tests(nats_integration_tests)

# TLS Handshake Integration Tests (Issue #275)
# Opt-in: requires nats-server and openssl on PATH at test runtime.
# Disabled by default so CI environments without nats-server are not broken.
option(ENABLE_TLS_INTEGRATION_TESTS
       "Enable TLS handshake integration tests (requires nats-server and openssl)" OFF)

if(ENABLE_TLS_INTEGRATION_TESTS)
  add_executable(tls_integration_tests
                 tests/integration/test_tls_integration.cpp)

  target_link_libraries(tls_integration_tests keystone_transport
                        GTest::gtest_main)

  gtest_discover_tests(tls_integration_tests)

  message(STATUS "TLS integration tests enabled (ENABLE_TLS_INTEGRATION_TESTS=ON)")
else()
  message(
    STATUS
    "TLS integration tests disabled. Use -DENABLE_TLS_INTEGRATION_TESTS=ON to enable (requires nats-server + openssl)."
  )
endif()

# Add test target
add_custom_target(
  run_tests
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  DEPENDS basic_delegation_tests
          module_coordination_tests
          component_coordination_tests
          async_delegation_tests
          # DISABLED: async_agents_tests (Phase 6)
          distributed_hierarchy_tests
          # DISABLED: multi_component_tests (Phase 4) DISABLED:
          # chaos_engineering_tests (Phase 5)
          unit_tests
          concurrency_unit_tests
          simulation_unit_tests
          transport_unit_tests
          nats_integration_tests
          # DISABLED: registry_integration_tests (ADR-006) DISABLED:
  COMMENT "Running all ProjectKeystone tests with failure output")

# Benchmarks Phase 3.1: Re-enabled hierarchy benchmarks with unified async API
add_executable(hierarchy_benchmarks benchmarks/hierarchy_performance.cpp)

target_link_libraries(hierarchy_benchmarks keystone_agents keystone_core
                      keystone_concurrency benchmark::benchmark)

# Phase D.2: Message pool performance benchmarks
add_executable(message_pool_benchmarks benchmarks/message_pool_performance.cpp)

target_link_libraries(message_pool_benchmarks keystone_core
                      benchmark::benchmark)

# Phase D.3: Distributed work-stealing benchmarks
add_executable(distributed_benchmarks benchmarks/distributed_work_stealing.cpp)

target_link_libraries(distributed_benchmarks keystone_simulation
                      keystone_concurrency keystone_core benchmark::benchmark)

# P2-06: String allocation profiling benchmarks
add_executable(string_allocation_benchmarks
               benchmarks/string_allocation_profiling.cpp)

target_link_libraries(string_allocation_benchmarks keystone_core
                      benchmark::benchmark)

# Phase A2: Registry performance benchmarks (agent ID interning optimization)
add_executable(registry_performance_benchmarks
               benchmarks/registry_performance.cpp)

target_link_libraries(registry_performance_benchmarks keystone_agents
                      keystone_core benchmark::benchmark)

# P2-06: Simple profiling harness for valgrind massif
add_executable(profile_allocations benchmarks/profile_allocations.cpp)

target_link_libraries(profile_allocations keystone_core)

# Stream C1: Scheduler backoff benchmarks
add_executable(scheduler_backoff_benchmarks
               benchmarks/scheduler_backoff_benchmark.cpp)

target_link_libraries(scheduler_backoff_benchmarks keystone_concurrency
                      keystone_core benchmark::benchmark)

# Phase 3.1: Re-enabled message bus performance benchmarks with unified async
# API
add_executable(message_bus_performance_benchmarks
               benchmarks/message_bus_performance.cpp)

target_link_libraries(message_bus_performance_benchmarks keystone_agents
                      keystone_core benchmark::benchmark)

# Phase 6.7 M1: Load testing for production readiness
add_executable(hmas_load_test tests/load/hmas_load_test.cpp)
target_link_libraries(hmas_load_test keystone_agents keystone_core
                      keystone_concurrency)

# Phase 9.2: Fuzz testing targets (requires -DENABLE_FUZZING=ON
# -DCMAKE_CXX_COMPILER=clang++)
if(ENABLE_FUZZING)
  # Fuzz test: Message serialization
  add_executable(fuzz_message_serialization fuzz/fuzz_message_serialization.cpp)
  target_compile_options(fuzz_message_serialization PRIVATE ${FUZZING_FLAGS})
  target_link_options(fuzz_message_serialization PRIVATE ${FUZZING_FLAGS})
  target_link_libraries(fuzz_message_serialization keystone_core)

  # Fuzz test: Message bus routing
  add_executable(fuzz_message_bus_routing fuzz/fuzz_message_bus_routing.cpp)
  target_compile_options(fuzz_message_bus_routing PRIVATE ${FUZZING_FLAGS})
  target_link_options(fuzz_message_bus_routing PRIVATE ${FUZZING_FLAGS})
  target_link_libraries(fuzz_message_bus_routing keystone_core keystone_agents)

  # Fuzz test: Work-stealing scheduler
  add_executable(fuzz_work_stealing fuzz/fuzz_work_stealing.cpp)
  target_compile_options(fuzz_work_stealing PRIVATE ${FUZZING_FLAGS})
  target_link_options(fuzz_work_stealing PRIVATE ${FUZZING_FLAGS})
  target_link_libraries(fuzz_work_stealing keystone_concurrency keystone_core)

  # Fuzz test: Retry policy
  add_executable(fuzz_retry_policy fuzz/fuzz_retry_policy.cpp)
  target_compile_options(fuzz_retry_policy PRIVATE ${FUZZING_FLAGS})
  target_link_options(fuzz_retry_policy PRIVATE ${FUZZING_FLAGS})
  target_link_libraries(fuzz_retry_policy keystone_core)

  # Fuzz test: Subject validator
  add_executable(fuzz_subject_validator fuzz/fuzz_subject_validator.cpp)
  target_compile_options(fuzz_subject_validator PRIVATE ${FUZZING_FLAGS})
  target_link_options(fuzz_subject_validator PRIVATE ${FUZZING_FLAGS})
  target_link_libraries(fuzz_subject_validator keystone_core)

  message(STATUS "Fuzz testing targets enabled:")
  message(STATUS "  - fuzz_message_serialization")
  message(STATUS "  - fuzz_message_bus_routing")
  message(STATUS "  - fuzz_work_stealing")
  message(STATUS "  - fuzz_retry_policy")
  message(STATUS "  - fuzz_subject_validator")
  message(STATUS "Run with: ./<fuzz_target> -max_len=4096 -runs=1000000")
endif()

# ============================================================================
# Installation Rules
# ============================================================================

include(GNUInstallDirs)

# Install runtime libraries (keystone package)
install(
  TARGETS keystone_core keystone_concurrency keystone_agents keystone_simulation
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT keystone
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT keystone-dev
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT keystone)

# Install network library if gRPC is enabled
if(ENABLE_GRPC)
  install(
    TARGETS keystone_network
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT keystone
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT keystone-dev
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT keystone)
endif()

# ============================================================================
# Version Information (needed early for CMake package config)
# ============================================================================

set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")

# Install public headers (keystone-dev package)
install(
  DIRECTORY ${PROJECT_SOURCE_DIR}/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/keystone
  COMPONENT keystone-dev
  FILES_MATCHING
  PATTERN "*.hpp")

# Install generated proto headers if gRPC is enabled
if(ENABLE_GRPC AND EXISTS ${PROJECT_SOURCE_DIR}/proto/hmas_coordinator.proto)
  install(
    FILES ${GRPC_HDRS} ${PROTO_HDRS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/keystone/proto
    COMPONENT keystone-dev)
endif()

# Configure and install CMake package configuration files (keystone-dev package)
include(CMakePackageConfigHelpers)

configure_package_config_file(
  ${PROJECT_SOURCE_DIR}/cmake/KeystoneConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/KeystoneConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Keystone
  PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/KeystoneConfigVersion.cmake
  VERSION ${CPACK_PACKAGE_VERSION}
  COMPATIBILITY SameMajorVersion)

install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/KeystoneConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/KeystoneConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Keystone
  COMPONENT keystone-dev)

# Install documentation (keystone + keystone-doc packages) Core documentation
# goes to keystone package
install(
  FILES ${PROJECT_SOURCE_DIR}/README.md ${PROJECT_SOURCE_DIR}/CLAUDE.md
        ${PROJECT_SOURCE_DIR}/LICENSE
  DESTINATION ${CMAKE_INSTALL_DOCDIR}
  COMPONENT keystone
  OPTIONAL)

# Extended documentation goes to keystone-doc package
install(
  DIRECTORY ${PROJECT_SOURCE_DIR}/docs/
  DESTINATION ${CMAKE_INSTALL_DOCDIR}
  COMPONENT keystone-doc
  FILES_MATCHING
  PATTERN "*.md"
  PATTERN "*.txt"
  PATTERN "*.pdf"
  PATTERN "issues" EXCLUDE)

# Install test executables (keystone-test package)
install(
  TARGETS basic_delegation_tests
          module_coordination_tests
          component_coordination_tests
          async_delegation_tests
          distributed_hierarchy_tests
          unit_tests
          concurrency_unit_tests
          simulation_unit_tests
          registry_integration_tests
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/tests COMPONENT keystone-test)

# Install gRPC tests if enabled
if(ENABLE_GRPC)
  install(TARGETS distributed_grpc_tests
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/tests
                  COMPONENT keystone-test)
endif()

# Install benchmarks (keystone-misc package)
install(
  TARGETS message_pool_benchmarks distributed_benchmarks
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/benchmarks
          COMPONENT keystone-misc
          OPTIONAL)

# Install load test (keystone-misc package)
install(TARGETS hmas_load_test RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/tools
                                       COMPONENT keystone-misc)

# Install fuzz testing targets if enabled (keystone-misc package)
if(ENABLE_FUZZING)
  install(
    TARGETS fuzz_message_serialization fuzz_message_bus_routing
            fuzz_work_stealing fuzz_retry_policy fuzz_subject_validator
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/fuzz COMPONENT keystone-misc)
endif()

# Install build scripts and Docker files (keystone-dev package)
install(
  FILES ${PROJECT_SOURCE_DIR}/Dockerfile
        ${PROJECT_SOURCE_DIR}/docker-compose.yaml
  DESTINATION ${CMAKE_INSTALL_DOCDIR}/build
  COMPONENT keystone-dev
  OPTIONAL)

# Install proto files for development (keystone-dev package)
if(ENABLE_GRPC)
  install(
    DIRECTORY ${PROJECT_SOURCE_DIR}/proto/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/keystone/proto
    COMPONENT keystone-dev
    FILES_MATCHING
    PATTERN "*.proto")
endif()

# ============================================================================
# CPack Configuration
# ============================================================================

set(CPACK_PACKAGE_NAME "ProjectKeystone")
set(CPACK_PACKAGE_VENDOR "ProjectKeystone Development Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Hierarchical Multi-Agent System (HMAS) in C++20")
# Version already defined earlier for CMake package config
set(CPACK_PACKAGE_CONTACT "projectkeystone@example.com")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/projectkeystone/hmas")

# Resource files
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${PROJECT_SOURCE_DIR}/README.md")

# Component-based packaging
set(CPACK_COMPONENTS_ALL keystone keystone-dev keystone-doc keystone-test
                         keystone-misc)

# Component descriptions
set(CPACK_COMPONENT_KEYSTONE_DISPLAY_NAME "Keystone Runtime Libraries")
set(CPACK_COMPONENT_KEYSTONE_DESCRIPTION
    "Core runtime libraries for ProjectKeystone HMAS. Includes message bus, agents, concurrency, and simulation libraries."
)
set(CPACK_COMPONENT_KEYSTONE_REQUIRED ON)
set(CPACK_COMPONENT_KEYSTONE_GROUP "Runtime")

set(CPACK_COMPONENT_KEYSTONE-DEV_DISPLAY_NAME "Keystone Development Files")
set(CPACK_COMPONENT_KEYSTONE-DEV_DESCRIPTION
    "Development files for building applications with ProjectKeystone. Includes headers, static libraries, CMake targets, and build documentation."
)
set(CPACK_COMPONENT_KEYSTONE-DEV_GROUP "Development")
set(CPACK_COMPONENT_KEYSTONE-DEV_DEPENDS keystone)

set(CPACK_COMPONENT_KEYSTONE-DOC_DISPLAY_NAME "Keystone Documentation")
set(CPACK_COMPONENT_KEYSTONE-DOC_DESCRIPTION
    "Complete documentation for ProjectKeystone including architecture guides, API references, phase plans, and deployment guides."
)
set(CPACK_COMPONENT_KEYSTONE-DOC_GROUP "Documentation")

set(CPACK_COMPONENT_KEYSTONE-TEST_DISPLAY_NAME "Keystone Test Suite")
set(CPACK_COMPONENT_KEYSTONE-TEST_DESCRIPTION
    "Comprehensive test suite for QA validation. Includes unit tests, E2E tests, and integration tests for all HMAS components."
)
set(CPACK_COMPONENT_KEYSTONE-TEST_GROUP "Testing")
set(CPACK_COMPONENT_KEYSTONE-TEST_DEPENDS keystone)

set(CPACK_COMPONENT_KEYSTONE-MISC_DISPLAY_NAME "Keystone Miscellaneous Tools")
set(CPACK_COMPONENT_KEYSTONE-MISC_DESCRIPTION
    "Additional tools and utilities including benchmarks, load testing tools, and fuzz testing targets."
)
set(CPACK_COMPONENT_KEYSTONE-MISC_GROUP "Tools")
set(CPACK_COMPONENT_KEYSTONE-MISC_DEPENDS keystone)

# Component groups
set(CPACK_COMPONENT_GROUP_RUNTIME_DESCRIPTION
    "Runtime libraries and essential files")
set(CPACK_COMPONENT_GROUP_DEVELOPMENT_DESCRIPTION
    "Development tools and headers")
set(CPACK_COMPONENT_GROUP_DOCUMENTATION_DESCRIPTION "Documentation and guides")
set(CPACK_COMPONENT_GROUP_TESTING_DESCRIPTION "Test suites for QA")
set(CPACK_COMPONENT_GROUP_TOOLS_DESCRIPTION "Benchmarking and utility tools")

# DEB-specific settings
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_ENABLE_COMPONENT_DEPENDS ON)
set(CPACK_DEB_COMPONENT_INSTALL ON)

# DEB package dependencies
set(CPACK_DEBIAN_KEYSTONE_PACKAGE_NAME "libkeystone0")
set(CPACK_DEBIAN_KEYSTONE_PACKAGE_DEPENDS "libc6 (>= 2.34), libstdc++6 (>= 12)")

set(CPACK_DEBIAN_KEYSTONE-DEV_PACKAGE_NAME "libkeystone-dev")
set(CPACK_DEBIAN_KEYSTONE-DEV_PACKAGE_DEPENDS
    "libkeystone0 (= ${CPACK_PACKAGE_VERSION}), cmake (>= 3.20)")
set(CPACK_DEBIAN_KEYSTONE-DEV_PACKAGE_SUGGESTS "ninja-build, clang-format")

set(CPACK_DEBIAN_KEYSTONE-DOC_PACKAGE_NAME "keystone-doc")
set(CPACK_DEBIAN_KEYSTONE-DOC_PACKAGE_ARCHITECTURE "all")

set(CPACK_DEBIAN_KEYSTONE-TEST_PACKAGE_NAME "keystone-test")
set(CPACK_DEBIAN_KEYSTONE-TEST_PACKAGE_DEPENDS
    "libkeystone0 (= ${CPACK_PACKAGE_VERSION})")

set(CPACK_DEBIAN_KEYSTONE-MISC_PACKAGE_NAME "keystone-tools")
set(CPACK_DEBIAN_KEYSTONE-MISC_PACKAGE_DEPENDS
    "libkeystone0 (= ${CPACK_PACKAGE_VERSION})")

# RPM-specific settings
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Development/Libraries")
set(CPACK_RPM_ENABLE_COMPONENT_DEPENDS ON)
set(CPACK_RPM_COMPONENT_INSTALL ON)

# RPM package names
set(CPACK_RPM_KEYSTONE_PACKAGE_NAME "keystone")
set(CPACK_RPM_KEYSTONE-DEV_PACKAGE_NAME "keystone-devel")
set(CPACK_RPM_KEYSTONE-DOC_PACKAGE_NAME "keystone-doc")
set(CPACK_RPM_KEYSTONE-TEST_PACKAGE_NAME "keystone-test")
set(CPACK_RPM_KEYSTONE-MISC_PACKAGE_NAME "keystone-tools")

# RPM package dependencies
set(CPACK_RPM_KEYSTONE_PACKAGE_REQUIRES "glibc >= 2.34")
set(CPACK_RPM_KEYSTONE-DEV_PACKAGE_REQUIRES
    "keystone = ${CPACK_PACKAGE_VERSION}, cmake >= 3.20")
set(CPACK_RPM_KEYSTONE-TEST_PACKAGE_REQUIRES
    "keystone = ${CPACK_PACKAGE_VERSION}")
set(CPACK_RPM_KEYSTONE-MISC_PACKAGE_REQUIRES
    "keystone = ${CPACK_PACKAGE_VERSION}")

# Archive settings (TGZ, ZIP)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)

# Generators - enable multiple package formats
set(CPACK_GENERATOR "TGZ;ZIP")

# Optionally enable DEB/RPM on Linux
if(UNIX AND NOT APPLE)
  list(APPEND CPACK_GENERATOR "DEB" "RPM")
endif()

# Source package configuration
set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")
set(CPACK_SOURCE_IGNORE_FILES
    "/.git/"
    "/build/"
    "/.cache/"
    "/.vscode/"
    "/__pycache__/"
    "\\.pyc$"
    "\\.swp$"
    "\\.o$"
    "\\.a$"
    "\\.so$")

include(CPack)

# Workflow examples (optional)
option(BUILD_EXAMPLES "Build workflow examples" OFF)

if(BUILD_EXAMPLES)
  message(STATUS "Building workflow examples...")
  add_subdirectory(examples/workflow-examples)
else()
  message(
    STATUS "Workflow examples disabled. Use -DBUILD_EXAMPLES=ON to enable.")
endif()
