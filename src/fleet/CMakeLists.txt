cmake_minimum_required(VERSION 3.20)

# Also supported as a focused build: cmake -S src/fleet -B build/fleet.
set(_fleet_tests_default OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(_fleet_tests_default ON)
  project(KeystoneFleetGateway LANGUAGES C CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  include(FetchContent)
  set(NATS_BUILD_STREAMING
      OFF
      CACHE BOOL "" FORCE)
  set(NATS_BUILD_EXAMPLES
      OFF
      CACHE BOOL "" FORCE)
  set(NATS_BUILD_TESTS
      OFF
      CACHE BOOL "" FORCE)
  set(BUILD_TESTING
      OFF
      CACHE BOOL "" FORCE)
  FetchContent_Declare(
    natsc
    GIT_REPOSITORY https://github.com/nats-io/nats.c.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(natsc)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(googletest)
  enable_testing()
  include(GoogleTest)
  include(GNUInstallDirs)
endif()
option(ENABLE_FLEET_INTEGRATION_TESTS
       "Run gateway tests against private local NATS servers"
       ${_fleet_tests_default})

include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(nlohmann_json)

add_executable(keystone_fleet_gateway main.cpp)
set_target_properties(keystone_fleet_gateway PROPERTIES OUTPUT_NAME
                                                        keystone-fleet-gateway)
target_link_libraries(keystone_fleet_gateway
                      PRIVATE nats nlohmann_json::nlohmann_json)
target_compile_options(keystone_fleet_gateway PRIVATE -Wall -Wextra -Wpedantic
                                                      -Werror)
file(RELATIVE_PATH _fleet_library_path "${CMAKE_INSTALL_FULL_BINDIR}"
     "${CMAKE_INSTALL_FULL_LIBDIR}")
if(APPLE)
  set(_fleet_origin "@loader_path")
else()
  set(_fleet_origin "$ORIGIN")
endif()
set_target_properties(
  keystone_fleet_gateway
  PROPERTIES INSTALL_RPATH "${_fleet_origin}/${_fleet_library_path}"
             INSTALL_RPATH_USE_LINK_PATH FALSE)
install(TARGETS keystone_fleet_gateway
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT keystone)
# The dependency's upstream install belongs to CPack's excluded Unspecified
# component. Keep its real library and SONAME links with our runtime component.
install(TARGETS nats LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                             COMPONENT keystone)

if(ENABLE_FLEET_INTEGRATION_TESTS)
  add_executable(fleet_gateway_tests
                 ../../tests/integration/test_fleet_gateway.cpp)
  target_link_libraries(
    fleet_gateway_tests PRIVATE nats nlohmann_json::nlohmann_json
                                GTest::gtest_main)
  target_compile_definitions(
    fleet_gateway_tests
    PRIVATE
      FLEET_GATEWAY_BINARY="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/keystone-fleet-gateway"
  )
  target_compile_options(fleet_gateway_tests PRIVATE -Wall -Wextra -Wpedantic
                                                     -Werror)
  add_dependencies(fleet_gateway_tests keystone_fleet_gateway)
  add_test(NAME fleet_gateway_integration COMMAND fleet_gateway_tests)
  set_tests_properties(fleet_gateway_integration PROPERTIES TIMEOUT 60 LABELS
                                                            integration)
  add_test(
    NAME fleet_gateway_install
    COMMAND
      "${CMAKE_COMMAND}" "-DFLEET_BUILD_DIR=${CMAKE_BINARY_DIR}"
      "-DFLEET_BINDIR=${CMAKE_INSTALL_BINDIR}"
      "-DFLEET_LIBDIR=${CMAKE_INSTALL_LIBDIR}" -P
      "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/integration/fleet_install.cmake")
  set_tests_properties(fleet_gateway_install PROPERTIES TIMEOUT 60 LABELS
                                                        integration)
endif()
