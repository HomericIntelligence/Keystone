# CMakeLists.txt for ProjectKeystone Workflow Examples
#
# This file builds all workflow examples demonstrating the HMAS across three
# deployment models: - Thread-based (shared memory within process) - IPC-based
# (multiple processes with shared memory) - Network-based (distributed
# multi-machine with gRPC)

cmake_minimum_required(VERSION 3.20)

# ═══════════════════════════════════════════════════════════════
# Thread-Based Examples (Always Built)
# ═══════════════════════════════════════════════════════════════

message(STATUS "Building Thread-Based Examples...")

# 2-Layer: Chief → TaskAgent
add_executable(thread_2layer 01-thread-based/2layer_example.cpp)
target_link_libraries(thread_2layer PRIVATE agents core concurrency
                                            Threads::Threads)
target_include_directories(thread_2layer PRIVATE ${PROJECT_SOURCE_DIR}/include)

# 3-Layer: Chief → ModuleLead → TaskAgents
add_executable(thread_3layer 01-thread-based/3layer_example.cpp)
target_link_libraries(thread_3layer PRIVATE agents core concurrency
                                            Threads::Threads)
target_include_directories(thread_3layer PRIVATE ${PROJECT_SOURCE_DIR}/include)

# 4-Layer: Full Hierarchy
add_executable(thread_4layer 01-thread-based/4layer_example.cpp)
target_link_libraries(thread_4layer PRIVATE agents core concurrency
                                            Threads::Threads)
target_include_directories(thread_4layer PRIVATE ${PROJECT_SOURCE_DIR}/include)

message(
  STATUS "Thread-based examples: thread_2layer, thread_3layer, thread_4layer")

# ═══════════════════════════════════════════════════════════════
# IPC-Based Examples (Requires Boost.Interprocess)
# ═══════════════════════════════════════════════════════════════

option(ENABLE_IPC "Enable IPC-based examples (requires Boost.Interprocess)" OFF)

if(ENABLE_IPC)
  message(STATUS "Building IPC-Based Examples...")

  # Find Boost.Interprocess
  find_package(Boost REQUIRED COMPONENTS system)

  if(Boost_FOUND)
    # 2-Layer: Chief Process ← → Task Process
    add_executable(ipc_2layer 02-ipc-based/2layer_example.cpp)
    target_link_libraries(ipc_2layer PRIVATE agents core Boost::system
                                             Threads::Threads)
    target_include_directories(ipc_2layer PRIVATE ${PROJECT_SOURCE_DIR}/include
                                                  ${Boost_INCLUDE_DIRS})

    # 3-Layer: Documentation template
    add_executable(ipc_3layer 02-ipc-based/3layer_example.cpp)
    target_link_libraries(ipc_3layer PRIVATE Threads::Threads)

    # 4-Layer: Documentation template
    add_executable(ipc_4layer 02-ipc-based/4layer_example.cpp)
    target_link_libraries(ipc_4layer PRIVATE Threads::Threads)

    message(STATUS "IPC-based examples: ipc_2layer, ipc_3layer, ipc_4layer")
  else()
    message(WARNING "Boost not found. IPC examples will not be built.")
  endif()
else()
  message(STATUS "IPC-based examples disabled. Use -DENABLE_IPC=ON to enable.")
endif()

# ═══════════════════════════════════════════════════════════════
# Network-Based Examples (Requires gRPC)
# ═══════════════════════════════════════════════════════════════

if(ENABLE_GRPC)
  message(STATUS "Building Network-Based Examples...")

  # Find gRPC and Protobuf
  find_package(gRPC CONFIG)
  find_package(Protobuf REQUIRED)

  if(gRPC_FOUND AND Protobuf_FOUND)
    # 2-Layer: Chief (Machine A) → TaskAgent (Machine B)
    add_executable(network_2layer 03-network-based/2layer_example.cpp)
    target_link_libraries(
      network_2layer PRIVATE agents core network gRPC::grpc++
                             protobuf::libprotobuf Threads::Threads)
    target_include_directories(network_2layer
                               PRIVATE ${PROJECT_SOURCE_DIR}/include)
    target_compile_definitions(network_2layer PRIVATE ENABLE_GRPC)

    # 3-Layer: Documentation template
    add_executable(network_3layer 03-network-based/3layer_example.cpp)
    target_link_libraries(network_3layer PRIVATE Threads::Threads)
    target_compile_definitions(network_3layer PRIVATE ENABLE_GRPC)

    # 4-Layer: Documentation template
    add_executable(network_4layer 03-network-based/4layer_example.cpp)
    target_link_libraries(network_4layer PRIVATE Threads::Threads)
    target_compile_definitions(network_4layer PRIVATE ENABLE_GRPC)

    message(
      STATUS
        "Network-based examples: network_2layer, network_3layer, network_4layer"
    )
  else()
    message(
      WARNING "gRPC or Protobuf not found. Network examples will not be built.")
  endif()
else()
  message(
    STATUS "Network-based examples disabled. Use -DENABLE_GRPC=ON to enable.")
endif()

# ═══════════════════════════════════════════════════════════════
# Installation
# ═══════════════════════════════════════════════════════════════

# Install all built examples
install(TARGETS thread_2layer thread_3layer thread_4layer
        DESTINATION bin/examples)

if(ENABLE_IPC AND Boost_FOUND)
  install(TARGETS ipc_2layer ipc_3layer ipc_4layer DESTINATION bin/examples)
endif()

if(ENABLE_GRPC
   AND gRPC_FOUND
   AND Protobuf_FOUND)
  install(TARGETS network_2layer network_3layer network_4layer
          DESTINATION bin/examples)
endif()

# Install README files
install(FILES README.md 01-thread-based/README.md 02-ipc-based/README.md
              03-network-based/README.md
        DESTINATION share/doc/projectkeystone/examples)

# ═══════════════════════════════════════════════════════════════
# Custom Targets
# ═══════════════════════════════════════════════════════════════

# Build all thread-based examples
add_custom_target(thread-examples DEPENDS thread_2layer thread_3layer
                                          thread_4layer)

# Build all IPC-based examples (if enabled)
if(ENABLE_IPC AND Boost_FOUND)
  add_custom_target(ipc-examples DEPENDS ipc_2layer ipc_3layer ipc_4layer)
endif()

# Build all network-based examples (if enabled)
if(ENABLE_GRPC
   AND gRPC_FOUND
   AND Protobuf_FOUND)
  add_custom_target(network-examples DEPENDS network_2layer network_3layer
                                             network_4layer)
endif()

# Build all examples
add_custom_target(all-examples DEPENDS thread-examples)

if(ENABLE_IPC AND Boost_FOUND)
  add_dependencies(all-examples ipc-examples)
endif()

if(ENABLE_GRPC
   AND gRPC_FOUND
   AND Protobuf_FOUND)
  add_dependencies(all-examples network-examples)
endif()

# ═══════════════════════════════════════════════════════════════
# Testing
# ═══════════════════════════════════════════════════════════════

if(BUILD_TESTING)
  # Test that all thread-based examples compile
  add_test(NAME test_thread_2layer_builds
           COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target
                   thread_2layer)

  add_test(NAME test_thread_3layer_builds
           COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target
                   thread_3layer)

  add_test(NAME test_thread_4layer_builds
           COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target
                   thread_4layer)

  # Run thread-based examples (should execute without errors)
  add_test(NAME run_thread_2layer COMMAND thread_2layer)

  add_test(NAME run_thread_3layer COMMAND thread_3layer)

  add_test(NAME run_thread_4layer COMMAND thread_4layer)
endif()

# ═══════════════════════════════════════════════════════════════
# Summary
# ═══════════════════════════════════════════════════════════════

message(STATUS "")
message(STATUS "═══════════════════════════════════════════════════════")
message(STATUS "  Workflow Examples Configuration Summary")
message(STATUS "═══════════════════════════════════════════════════════")
message(STATUS "Thread-based examples: ENABLED")
message(STATUS "  - thread_2layer, thread_3layer, thread_4layer")
message(STATUS "")

if(ENABLE_IPC AND Boost_FOUND)
  message(STATUS "IPC-based examples: ENABLED")
  message(STATUS "  - ipc_2layer, ipc_3layer, ipc_4layer")
else()
  message(STATUS "IPC-based examples: DISABLED")
  message(STATUS "  Enable with: -DENABLE_IPC=ON")
endif()
message(STATUS "")

if(ENABLE_GRPC
   AND gRPC_FOUND
   AND Protobuf_FOUND)
  message(STATUS "Network-based examples: ENABLED")
  message(STATUS "  - network_2layer, network_3layer, network_4layer")
else()
  message(STATUS "Network-based examples: DISABLED")
  message(STATUS "  Enable with: -DENABLE_GRPC=ON")
endif()
message(STATUS "")
message(STATUS "Build all examples with: ninja all-examples")
message(STATUS "═══════════════════════════════════════════════════════")
message(STATUS "")
