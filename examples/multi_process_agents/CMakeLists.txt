cmake_minimum_required(VERSION 3.20)
project(MultiProcessAgents CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(include)

# Try to find nlohmann_json, if not found download it
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
  message(STATUS "nlohmann_json not found, will use header-only include")
  # Create directory for header
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/nlohmann)
  # Download single-header version
  if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/include/nlohmann/json.hpp)
    message(STATUS "Downloading nlohmann/json.hpp...")
    file(
      DOWNLOAD
      https://github.com/nlohmann/json/releases/download/v3.11.2/json.hpp
      ${CMAKE_CURRENT_BINARY_DIR}/include/nlohmann/json.hpp
      STATUS download_status)
  endif()
  include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)
  # Create an interface library
  add_library(nlohmann_json INTERFACE)
  target_include_directories(nlohmann_json
                             INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/include)
  add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
endif()

# Process 1: ChiefArchitect
add_executable(process1_chief src/process1_chief.cpp src/queue_manager.cpp)
target_link_libraries(process1_chief nlohmann_json::nlohmann_json)

# Process 2: ComponentLead
add_executable(process2_component_lead src/process2_component_lead.cpp
                                       src/queue_manager.cpp)
target_link_libraries(process2_component_lead nlohmann_json::nlohmann_json)

# Process 3: ModuleLead
add_executable(process3_module_lead src/process3_module_lead.cpp
                                    src/queue_manager.cpp)
target_link_libraries(process3_module_lead nlohmann_json::nlohmann_json)

# Process 4: TaskAgent
add_executable(process4_task_agent src/process4_task_agent.cpp
                                   src/queue_manager.cpp)
target_link_libraries(process4_task_agent nlohmann_json::nlohmann_json)
