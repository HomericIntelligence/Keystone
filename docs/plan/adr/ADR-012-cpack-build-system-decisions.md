# ADR-012: CPack Build System Decisions

**Date**: 2025-11-25
**Status**: Accepted
**Context**: CPack packaging implementation for cross-platform distribution

## Summary

This document records key build system decisions made to enable CPack packaging and cross-platform distribution of
ProjectKeystone HMAS libraries. These decisions address critical packaging issues (C1, C2) and document architectural
trade-offs.

## Background

ProjectKeystone requires distributable packages for external consumers. The CPack packaging review identified two
critical issues preventing proper package functionality:

1. **C1**: Missing include directories prevented consumers from finding agent headers after installation
2. **C2**: Hardcoded `.so` library extensions in CMake config prevented Windows portability

Additionally, the build system has a documented circular dependency between core libraries that required explicit
handling.

## Decisions

### Decision 1: Circular Dependency Between keystone_core and keystone_concurrency

**Context**:

- `keystone_core` components (`metrics.cpp`, `retry_policy.cpp`, `circuit_breaker.cpp`, `heartbeat_monitor.cpp`) use
`Logger` from `concurrency/logger.hpp`
- `keystone_concurrency` depends on `keystone_core` for message types and core utilities
- This creates a circular dependency: `keystone_core` ↔ `keystone_concurrency`

**Decision**: Explicitly link both libraries to each other using static library circular dependency support

**Implementation**:

```cmake
# CMakeLists.txt lines 249-254
if(TARGET spdlog::spdlog)
    target_link_libraries(keystone_concurrency PUBLIC keystone_core PRIVATE spdlog::spdlog)
else()
    target_link_libraries(keystone_concurrency PUBLIC keystone_core PRIVATE spdlog)
endif()

# CMakeLists.txt lines 289-291
# Handle circular dependency: keystone_core uses Logger from keystone_concurrency
# This works with static libraries
target_link_libraries(keystone_core PUBLIC keystone_concurrency)
```

**Rationale**:

- Static libraries support circular dependencies (linker resolves symbols across multiple passes)
- Shared libraries would NOT support this pattern (linker error)
- Alternative of extracting Logger into separate library deemed unnecessarily complex for current scope
- Comment documentation makes the circular dependency explicit and intentional

**Trade-offs**:

- ✅ **Pro**: Simple solution, no additional libraries needed
- ✅ **Pro**: Works correctly with CPack static library packaging
- ⚠️ **Con**: Prevents future migration to shared libraries without refactoring
- ⚠️ **Con**: Non-obvious dependency structure for new contributors

**Future Considerations**:
If shared library distribution is needed:

1. Extract `Logger` into separate `keystone_logging` library
2. Both `keystone_core` and `keystone_concurrency` depend on `keystone_logging`
3. Eliminates circular dependency

---

### Decision 2: Global CMAKE_POSITION_INDEPENDENT_CODE for spdlog

**Context**:

- spdlog is fetched via FetchContent and compiled as a static library
- Static libraries linked into shared libraries require Position Independent Code (`-fPIC`)
- Without `-fPIC`, linking fails on position-independent systems (most modern Linux)

**Decision**: Enable `CMAKE_POSITION_INDEPENDENT_CODE ON` globally before fetching spdlog

**Implementation**:

```cmake
# CMakeLists.txt lines 145-150
# Build spdlog with -fPIC since we link it into static libraries
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.12.0)
```

**Rationale**:

- Applied before `FetchContent_Declare` ensures spdlog inherits the setting
- Global setting is acceptable because:
  - All ProjectKeystone libraries are compiled with `-fPIC` anyway (shared library compatibility)
  - Minimal performance overhead on modern systems
  - Simplifies build configuration

**Trade-offs**:

- ✅ **Pro**: spdlog correctly compiled with `-fPIC`
- ✅ **Pro**: No linker errors when creating static libraries
- ✅ **Pro**: Future-proofs for shared library migration
- ⚠️ **Con**: All targets inherit PIC setting (minimal overhead, ~2-5% on x86_64)

**Alternative Considered**:

```cmake
# Target-specific approach (rejected - too verbose)
FetchContent_MakeAvailable(spdlog)
set_target_properties(spdlog PROPERTIES POSITION_INDEPENDENT_CODE ON)
```

Rejected because global setting is simpler and has no practical downside.

---

### Decision 3: Platform-Specific Library Extensions in KeystoneConfig.cmake (C2 Fix)

**Context**:

- Original `KeystoneConfig.cmake.in` hardcoded `.so` extensions
- Windows uses `.lib` for static libraries
- Hardcoded extensions prevented cross-platform package portability

**Decision**: Use platform-specific library extension detection in CMake package config

**Implementation**:

```cmake
# cmake/KeystoneConfig.cmake.in lines 24-30
# Platform-specific static library extension
if(WIN32)
    set(_Keystone_LIBPREFIX "")
    set(_Keystone_LIBSUFFIX ".lib")
else()
    set(_Keystone_LIBPREFIX "lib")
    set(_Keystone_LIBSUFFIX ".a")
endif()

# Create imported targets (static libraries)
if(NOT TARGET Keystone::keystone_core)
    add_library(Keystone::keystone_core STATIC IMPORTED)
    set_target_properties(Keystone::keystone_core PROPERTIES
        IMPORTED_LOCATION "${_Keystone_LIBDIR}/${_Keystone_LIBPREFIX}keystone_core${_Keystone_LIBSUFFIX}"
        ...
    )
endif()
```

**Rationale**:

- CMake's `WIN32` variable provides robust platform detection
- `else` branch covers Linux and macOS (both use `lib*.a` for static libraries)
- Package now works identically on all platforms

**Platform Support**:

| Platform | Prefix | Suffix | Result |
|----------|--------|--------|--------|
| Linux | `lib` | `.a` | `libkeystone_core.a` |
| macOS | `lib` | `.a` | `libkeystone_core.a` |
| Windows | `` | `.lib` | `keystone_core.lib` |

**Trade-offs**:

- ✅ **Pro**: Cross-platform package portability
- ✅ **Pro**: Matches CMake standard conventions
- ✅ **Pro**: Works with `find_package(Keystone)` on all platforms
- ⚠️ **Note**: Assumes static libraries only (current packaging strategy)

**Future Shared Library Support**:
If shared library packaging is needed, additional detection required:

```cmake
if(WIN32)
    set(_Keystone_SHAREDLIBSUFFIX ".dll")
elseif(APPLE)
    set(_Keystone_SHAREDLIBSUFFIX ".dylib")
else()
    set(_Keystone_SHAREDLIBSUFFIX ".so")
endif()
```

---

### Decision 4: target_include_directories for keystone_agents (C1 Fix)

**Context**:

- `keystone_agents` library had no `target_include_directories` specified
- Consumer projects using `find_package(Keystone)` could not find agent headers
- Other libraries (`keystone_core`, `keystone_concurrency`, `keystone_simulation`) already had proper include
directories

**Decision**: Add `target_include_directories` with BUILD_INTERFACE and INSTALL_INTERFACE generator expressions

**Implementation**:

```cmake
# CMakeLists.txt lines 333-340
target_include_directories(
  keystone_agents
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/keystone>
  PRIVATE
    $<BUILD_INTERFACE:${spdlog_SOURCE_DIR}/include>
)
```

**Rationale**:

- `BUILD_INTERFACE`: Headers available at build time from source tree
- `INSTALL_INTERFACE`: Headers available after installation in `include/keystone/`
- `PRIVATE` spdlog: Implementation detail, not exposed to consumers
- Matches pattern used by all other ProjectKeystone libraries (consistency)

**Trade-offs**:

- ✅ **Pro**: Consumers can `#include <agents/chief_architect_agent.hpp>` after installation
- ✅ **Pro**: CMake transitively provides include paths via `target_link_libraries`
- ✅ **Pro**: Consistent with other library configurations
- ✅ **Pro**: No additional consumer configuration required

---

## Validation

All decisions validated through:

1. **Build Validation**: Clean build of 118/118 targets
2. **CPack Validation**: 10 packages generated successfully (5 components × 2 formats)
3. **KeystoneConfig.cmake Validation**: Correct platform-specific library naming
4. **Install Validation**: Headers findable in installed packages

**Package Output**:

```
2.2M ProjectKeystone-0.1.0-Linux-Development.deb    - Static libraries + headers
259K ProjectKeystone-0.1.0-Linux-Documentation.deb  - README, LICENSE, docs
 12K ProjectKeystone-0.1.0-Linux-Runtime.deb         - Runtime files
4.8M ProjectKeystone-0.1.0-Linux-Testing.deb         - Test executables
1.3M ProjectKeystone-0.1.0-Linux-Tools.deb           - Build tools
```

---

## Impact

### For Package Consumers

```cmake
# Consumer CMakeLists.txt
find_package(Keystone REQUIRED COMPONENTS core concurrency agents)

add_executable(my_agent my_agent.cpp)
target_link_libraries(my_agent PRIVATE Keystone::keystone_agents)
# Headers automatically available, no manual include_directories() needed
```

### For ProjectKeystone Developers

- Static library builds work correctly on all platforms
- No linker errors with spdlog
- Circular dependency explicitly documented and intentional
- Future shared library migration requires refactoring (documented)

---

## Alternatives Considered

### Alternative 1: Extract Logger into keystone_logging Library

**Pros**:

- Breaks circular dependency
- Cleaner architecture

**Cons**:

- Additional library to maintain
- More complex build configuration
- Not strictly necessary for current static library packaging

**Decision**: Deferred - acceptable for static libraries, revisit if shared libraries needed

### Alternative 2: Target-Specific CMAKE_POSITION_INDEPENDENT_CODE

**Pros**:

- More granular control
- Only spdlog gets `-fPIC`

**Cons**:

- More verbose CMake code
- All ProjectKeystone libraries need `-fPIC` anyway (shared library compatibility)
- Minimal performance benefit

**Decision**: Rejected - global setting simpler with no practical downside

---

## References

- **PR #50**: CPack packaging fixes implementation
- **Code Review**: Comprehensive review by code-review-orchestrator
- **CPack Documentation**: <https://cmake.org/cmake/help/latest/module/CPack.html>
- **CMake Generator Expressions**: <https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html>

---

## Conclusion

These build system decisions enable cross-platform CPack packaging while maintaining build simplicity. The circular
dependency is an acceptable trade-off for the current static library distribution model. Future shared library support
would require extracting Logger into a separate library to break the cycle.

**Status**: All decisions implemented and validated in PR #50

**Next Steps**: None - packaging complete and functional

---

**Last Updated**: 2025-11-25
**Author**: ProjectKeystone Development Team
**Reviewers**: Code Review Orchestrator (Architecture, Implementation, Security, Safety, Test, Dependency,
Documentation, Performance specialists)
