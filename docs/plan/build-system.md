# Keystone Build System

## Overview

Keystone uses **CMake 3.20+** with **Conan 2** for dependency management and **CMakePresets.json** (v8) for
preset-based builds. Traditional C++ headers are used (not C++20 modules). This document outlines the build
configuration, toolchain requirements, and development workflows.

See also: `/AGENTS.md` for project scope and architecture.

---

## Toolchain Requirements

### Compiler Support

| Compiler | Minimum Version | Recommended |
|----------|----------------|-------------|
| **GCC** | 11.0 | 13.0+ |
| **Clang** | 15.0 | 17.0+ |
| **MSVC** | 17.0 (VS 2022) | 17.8+ |

All compilers must support C++20 (e.g., `std::latch`, `std::barrier`, `std::atomic<T>`).

### Build Tools

- **CMake**: 3.20 or later (see CMakePresets section below)
- **Ninja**: 1.11+ (recommended build system)
- **Make**: 4.3+ (alternative to Ninja)
- **Just** (optional): Unified build commands via `justfile`

### Package Manager: Conan 2

Keystone uses **Conan 2** for C++ dependency management:

```bash
# Install Conan 2
pip install conan>=2.0

# Configure profile (one-time)
conan profile detect --force

# Install dependencies
conan install . --build=missing
```

**Dependencies** (see `conanfile.py`):

- **nats.c** v3.12.0 — NATS JetStream C client
- **concurrentqueue** — Lock-free queue (moodycamel)
- **spdlog** — Structured logging
- **fmt** — Format library (as spdlog's text formatting backend)
- **GoogleTest** — Unit testing (dev dependency)

---

## Project Structure

```
Keystone/
├── CMakeLists.txt              # Root CMake configuration
├── CMakePresets.json           # Build presets (v8 schema)
├── conanfile.py                # Conan 2 dependency manifest
├── conanfile.lock              # Locked dependency versions
├── pixi.toml                   # Pixi environment (optional)
├── justfile                    # Build command shortcuts (optional)
├── .clang-format               # Code formatting rules
├── .clang-tidy                 # Static analysis rules
├── cmake/
│   └── FindNATS.cmake          # NATS.c library finder
├── src/
│   ├── core/                   # Message routing, KIM protocol
│   ├── concurrency/            # Thread pools, synchronization
│   ├── network/                # NATS JetStream integration
│   ├── transport/              # Transport abstraction
│   ├── monitoring/             # Logging, metrics, health
│   ├── simulation/             # Test simulation framework
│   ├── agents/                 # Minimal agent stubs (tests/examples)
│   ├── daemon/                 # Keystone daemon process
│   └── keystone/               # Main library interface
├── tests/
│   ├── CMakeLists.txt
│   ├── unit/                   # Unit tests per component
│   ├── integration/            # Cross-component integration tests
│   ├── e2e/                    # End-to-end distributed tests
│   ├── load/                   # Load and throughput tests
│   ├── fixtures/               # Shared test fixtures
│   └── mocks/                  # Mock implementations
├── docs/
│   ├── plan/                   # Planning and design docs
│   ├── api/                    # API documentation
│   └── architecture/           # Architecture decisions
├── examples/
│   └── *.cpp                   # Example programs
└── CHANGELOG.md                # Release notes
```

---

## Root CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)

project(Keystone
    VERSION 0.1.0
    DESCRIPTION "Pure Invisible Transport Layer for HomericIntelligence"
    LANGUAGES CXX
)

# C++20 Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for clang-tidy, clang-format, etc.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings (C++ only, to avoid breaking nats.c FetchContent)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(
    $<$<COMPILE_LANGUAGE:CXX>:-Wall>
    $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
    $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>
    $<$<COMPILE_LANGUAGE:CXX>:-Werror>
  )
endif()

# Build options
option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)
option(ENABLE_GRPC "Enable gRPC support for distributed nodes" OFF)
option(ENABLE_PROFILING "Enable profiling tests (slow)" OFF)
option(ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" OFF)

# Code coverage setup
if(ENABLE_COVERAGE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(--coverage -fprofile-arcs -ftest-coverage -O0)
    add_link_options(--coverage)
  else()
    message(WARNING "Code coverage only supported with GCC or Clang")
  endif()
endif()

# Find dependencies
find_package(NATS REQUIRED)
find_package(concurrentqueue REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)

# Optional gRPC support
if(ENABLE_GRPC)
  find_package(gRPC REQUIRED)
  find_package(Protobuf REQUIRED)
endif()

# Testing
enable_testing()
find_package(GTest REQUIRED)

# Add subdirectories
add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(examples)

# Installation
include(GNUInstallDirs)
install(TARGETS keystone_core
    EXPORT KeystoneTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY src/keystone/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT KeystoneTargets
    FILE KeystoneTargets.cmake
    NAMESPACE Keystone::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Keystone
)
```

---

## CMake Presets (CMakePresets.json)

Keystone uses **CMakePresets.json v8** for preset-based builds with parallel build output directories:

```json
{
    "version": 8,
    "cmakeMinimumRequired": {
        "major": 3,
        "minor": 20
    },
    "configurePresets": [
        {
            "name": "default",
            "hidden": true,
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build/${presetName}",
            "cacheVariables": {
                "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
            }
        },
        {
            "name": "debug",
            "inherits": "default",
            "displayName": "Debug Build",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Debug"
            }
        },
        {
            "name": "release",
            "inherits": "default",
            "displayName": "Release Build",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Release"
            }
        },
        {
            "name": "asan",
            "inherits": "default",
            "displayName": "Debug + AddressSanitizer",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Debug",
                "CMAKE_CXX_FLAGS": "-fsanitize=address -fno-omit-frame-pointer"
            }
        },
        {
            "name": "tsan",
            "inherits": "default",
            "displayName": "Debug + ThreadSanitizer",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Debug",
                "CMAKE_CXX_FLAGS": "-fsanitize=thread -fno-omit-frame-pointer"
            }
        },
        {
            "name": "ubsan",
            "inherits": "default",
            "displayName": "Debug + UBSanitizer",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Debug",
                "CMAKE_CXX_FLAGS": "-fsanitize=undefined -fno-omit-frame-pointer"
            }
        }
    ],
    "buildPresets": [
        {
            "name": "debug",
            "configurePreset": "debug"
        },
        {
            "name": "release",
            "configurePreset": "release"
        },
        {
            "name": "asan",
            "configurePreset": "asan"
        },
        {
            "name": "tsan",
            "configurePreset": "tsan"
        }
    ],
    "testPresets": [
        {
            "name": "default",
            "configurePreset": "debug",
            "output": {
                "outputOnFailure": true
            }
        },
        {
            "name": "asan",
            "configurePreset": "asan",
            "output": {
                "outputOnFailure": true
            }
        },
        {
            "name": "tsan",
            "configurePreset": "tsan",
            "output": {
                "outputOnFailure": true
            }
        }
    ]
}
```

**Build Output Structure**:

```
build/
├── debug/      # Debug build
├── release/    # Release build
├── asan/       # AddressSanitizer + UBSanitizer
├── tsan/       # ThreadSanitizer
└── ubsan/      # UndefinedBehaviorSanitizer
```

---

## Build Workflows

### Using Justfile (Recommended)

Keystone uses `just` for unified build commands:

```bash
# Show all available commands
just help

# Build with AddressSanitizer
just build debug.asan

# Build release mode
just build release

# Build debug mode
just build debug

# Build with ThreadSanitizer
just build debug.tsan

# Run all tests
just test debug.asan

# Run specific test suites
just test basic
just test module
just test unit

# Run linters
just lint
just format
```

### Manual CMake Workflow (without justfile)

#### Local Development Build

```bash
# Install dependencies
conan install . --build=missing

# Configure with debug preset
cmake --preset debug

# Build
cmake --build build/debug --parallel

# Run tests
ctest --preset default
```

#### Release Build

```bash
# Configure
cmake --preset release

# Build with optimizations
cmake --build build/release --parallel

# Install
cmake --install build/release --prefix /usr/local
```

#### With AddressSanitizer

```bash
# Configure
cmake --preset asan

# Build
cmake --build build/asan --parallel

# Run tests (will detect memory errors)
cd build/asan
ctest --output-on-failure
```

#### With ThreadSanitizer

```bash
# Configure
cmake --preset tsan

# Build
cmake --build build/tsan --parallel

# Run tests (will detect data races)
cd build/tsan
ctest --output-on-failure
```

---

## Code Quality

### clang-format

All C++ code must pass `clang-format` with the project `.clang-format` config:

```bash
just format        # Format all source files in-place
just format-check  # Check formatting without modifying (CI gate)
```

### clang-tidy

Static analysis is enforced via `clang-tidy`:

```bash
just lint          # Run clang-tidy on all targets
```

### Sanitizers

| Sanitizer | Purpose | Preset |
|-----------|---------|--------|
| **ASan** | Use-after-free, buffer overflows, heap corruption | `asan` |
| **UBSan** | Undefined behavior (signed overflow, null deref, etc.) | `ubsan` |
| **TSan** | Data races, lock-order inversions | `tsan` |

Run sanitizer builds before every PR merge:

```bash
cmake --preset asan && cmake --build --preset asan && ctest --preset asan
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan
```

---

## CI/CD Integration

GitHub Actions workflows run:

1. **Build** with GCC 13 and Clang 17 on Linux
2. **Test** all tests under sanitizers
3. **Code coverage** on Linux builds
4. **Format check** (`just format-check`)
5. **Static analysis** (`just lint`)

All must pass before PR merge.

---

## Platform-Specific Considerations

### Linux (Ubuntu/Debian)

```bash
# Install dependencies
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    gcc-13 \
    g++-13 \
    git \
    python3-pip

# Install Conan
pip3 install conan

# Set default compiler
export CC=gcc-13
export CXX=g++-13

# Build
conan install . --build=missing
cmake --preset debug
cmake --build build/debug
```

### macOS

```bash
# Install via Homebrew
brew install cmake ninja llvm@17 python3

# Install Conan
pip3 install conan

# Use LLVM toolchain
export CC=/usr/local/opt/llvm@17/bin/clang
export CXX=/usr/local/opt/llvm@17/bin/clang++

# Build
conan install . --build=missing
cmake --preset debug
cmake --build build/debug
```

### Windows

```powershell
# Install Visual Studio 2022 with C++ workload
# Install CMake and Ninja via Visual Studio installer or winget
# Install Python from python.org

# Install Conan
pip install conan

# Use Developer Command Prompt
conan install . --build=missing
cmake --preset debug
cmake --build build/debug
```

---

## Troubleshooting

### CMake Not Found

**Problem**: `cmake: command not found`

**Solution**: Install CMake 3.20+ or use package manager (apt, brew, choco)

### Conan Dependencies Not Found

**Problem**: `find_package(nats.c) not found`

**Solution**: Run `conan install . --build=missing` before cmake

### Ninja Build Failures

**Problem**: Build errors with Ninja

**Solution**: Try Make instead: `cmake --preset debug -G "Unix Makefiles"`

### Compiler Not Supporting C++20

**Problem**: "C++20 not supported" error

**Solution**: Upgrade compiler to minimum versions (GCC 11, Clang 15, MSVC 17)

---

**Document Version**: 2.0 (Pure Transport — Conan 2 + CMakePresets)
**Last Updated**: 2026-04-25
